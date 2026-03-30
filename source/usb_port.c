/*
 * usb.c
 *
 *  Created on: Feb. 7, 2025
 *      Author: slocke
 */
#include <stdbool.h>
#include "usb_device_config.h"
#include "usb.h"
#include "usb_device.h"
#include "usb_device_class.h"
#include "usb_device_ch9.h"
#include "usb_device_descriptor.h"
#include "usb_device_cdc_acm.h"

//#include "virtual_com.h"
#include "FreeRTOS.h"
#include "task.h"
#include "stream_buffer.h"
#include "pin_mux.h"
#include "clock_config.h"
#include "board.h"
#include "fsl_power.h"
#include "usb_phy.h"

extern usb_device_endpoint_struct_t g_UsbDeviceCdcVcomDicEndpoints[];
extern usb_device_class_struct_t g_UsbDeviceCdcVcomConfig;

/* State information for the USB connection */
typedef struct _usb_cdc_vcom_struct
{
    usb_device_handle deviceHandle; /* USB device handle. */
    class_handle_t cdcAcmHandle; /* USB CDC ACM class handle.                                                         */
    volatile uint8_t attach;     /* A flag to indicate whether a usb device is attached. 1: attached, 0: not attached */
    TaskHandle_t txTaskHandle;      /* USB tx task handle. */
    StreamBufferHandle_t txStream;	/* Stream buffer for data to transmit */
    StreamBufferHandle_t rxStream;	/* Stream buffer for received data */
    uint8_t speed; /* Speed of USB device. USB_SPEED_FULL/USB_SPEED_LOW/USB_SPEED_HIGH.                 */
    volatile uint8_t
        startTransactions; /* A flag to indicate whether a CDC device is ready to transmit and receive data.    */
    uint8_t currentConfiguration;                                           /* Current configuration value. */
    uint8_t currentInterfaceAlternateSetting[USB_CDC_VCOM_INTERFACE_COUNT]; /* Current alternate setting value for each
                                                                               interface. */
} usb_cdc_vcom_struct_t;

usb_status_t USB_DeviceCdcVcomCallback(class_handle_t handle, uint32_t event, void *param);
usb_status_t USB_DeviceCallback(usb_device_handle handle, uint32_t event, void *param);

/* USB device class information */
static usb_device_class_config_struct_t s_cdcAcmConfig[1] = {{
    USB_DeviceCdcVcomCallback,
    0,
    &g_UsbDeviceCdcVcomConfig,
}};

/* USB device class configuration information */
static usb_device_class_config_list_struct_t s_cdcAcmConfigList = {
    s_cdcAcmConfig,
    USB_DeviceCallback,
    1,
};

usb_cdc_vcom_struct_t s_cdcVcom;
extern usb_device_endpoint_struct_t g_UsbDeviceCdcVcomDicEndpoints[];
extern usb_device_class_struct_t g_UsbDeviceCdcVcomConfig;

/* We don't actual set line coding. This is a hard-coded rate that we can return when queried */
#define LINE_CODING_SIZE (0x07)
#define LINE_CODING_DTERATE (115200)
#define LINE_CODING_CHARFORMAT (0x00)
#define LINE_CODING_PARITYTYPE (0x00)
#define LINE_CODING_DATABITS (0x08)
static uint8_t s_lineCoding[LINE_CODING_SIZE] = {
    /* E.g. 0x00,0xC2,0x01,0x00 : 0x0001C200 is 115200 bits per second */
    (LINE_CODING_DTERATE >> 0U) & 0x000000FFU,
    (LINE_CODING_DTERATE >> 8U) & 0x000000FFU,
    (LINE_CODING_DTERATE >> 16U) & 0x000000FFU,
    (LINE_CODING_DTERATE >> 24U) & 0x000000FFU,
    LINE_CODING_CHARFORMAT,
    LINE_CODING_PARITYTYPE,
    LINE_CODING_DATABITS};

/* Data buffer for receiving and sending*/
USB_DMA_NONINIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE) static uint8_t s_currRecvBuf[HS_CDC_VCOM_BULK_OUT_PACKET_SIZE];
USB_DMA_NONINIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE) static uint8_t s_currSendBuf[HS_CDC_VCOM_BULK_OUT_PACKET_SIZE];


void USB1_IRQHandler(void)
{
    USB_DeviceLpcIp3511IsrFunction(s_cdcVcom.deviceHandle);
}
void USB_DeviceClockInit(void)
{

    usb_phy_config_struct_t phyConfig = {
        0x05, //BOARD_USB_PHY_D_CAL,
        0x0A, //BOARD_USB_PHY_TXCAL45DP,
        0x0A, //BOARD_USB_PHY_TXCAL45DM,
    };

    /* enable USB IP clock */
    CLOCK_EnableUsbhs0PhyPllClock(kCLOCK_UsbPhySrcExt, BOARD_XTAL0_CLK_HZ);
    CLOCK_EnableUsbhs0DeviceClock(kCLOCK_UsbSrcUnused, 0U);
    USB_EhciPhyInit(kUSB_ControllerLpcIp3511Hs0, BOARD_XTAL0_CLK_HZ, &phyConfig);
    //USB_EhciPhyInit(kUSB_ControllerLpcIp3511Hs0, BOARD_XTAL0_CLK_HZ, NULL);

    /* Clear the USB RAM */
    for (int i = 0; i < FSL_FEATURE_USBHSD_USB_RAM; i++)
    {
        ((uint8_t *)FSL_FEATURE_USBHSD_USB_RAM_BASE_ADDRESS)[i] = 0x00U;
    }

}
void USB_DeviceIsrEnable(void)
{
    /* Install isr, set priority, and enable IRQ. */
    NVIC_SetPriority(USB1_IRQn, 1);
    NVIC_EnableIRQ(USB1_IRQn);
}

void USBTxTask(void * params){
	uint32_t bytes;
	usb_status_t err;

	USB_DeviceClockInit();

    s_cdcVcom.speed        = USB_SPEED_FULL;
    s_cdcVcom.attach       = 0;
    s_cdcVcom.cdcAcmHandle = (class_handle_t)NULL;
    s_cdcVcom.deviceHandle = NULL;

	configASSERT(kStatus_USB_Success == USB_DeviceClassInit(kUSB_ControllerLpcIp3511Hs0,&s_cdcAcmConfigList, &s_cdcVcom.deviceHandle));
	s_cdcVcom.cdcAcmHandle = s_cdcAcmConfigList.config->classHandle;
	USB_DeviceIsrEnable();
	vTaskDelay(pdMS_TO_TICKS(5)+2);
	xTaskNotifyGive(s_cdcVcom.txTaskHandle);	//prime the task notification as "available"
	USB_DeviceRun(s_cdcVcom.deviceHandle);

	while(1){
		/*
		 * Wait for data to send
		 */
		bytes = xStreamBufferReceive(s_cdcVcom.txStream,(void*)s_currSendBuf,g_UsbDeviceCdcVcomDicEndpoints[1].maxPacketSize,portMAX_DELAY);
		if(bytes > 0 && s_cdcVcom.startTransactions){
			/*
			 * FIXME: there may be an issue here. If we fail to send for some reason, the next time through
			 * the loop we'll wait for a task notification, but since nothing was queued to send, the ISR
			 * will never give the notification. Not sure if this can actually happen -- would require
			 * a failure to send that was NOT due to the endpoint being busy.
			 */
			ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
			err = USB_DeviceCdcAcmSend(s_cdcVcom.cdcAcmHandle, USB_CDC_VCOM_BULK_IN_ENDPOINT,s_currSendBuf,bytes);
			configASSERT(err == kStatus_USB_Success);
		}

	}
}

/*!
 * @brief USB device callback function.
 *
 * This function handles the usb device specific requests.
 *
 * @param handle          The USB device handle.
 * @param event           The USB device event type.
 * @param param           The parameter of the device specific request.
 *
 * @return A USB error code or kStatus_USB_Success.
 */
usb_status_t USB_DeviceCallback(usb_device_handle handle, uint32_t event, void *param)
{
    usb_status_t error = kStatus_USB_InvalidRequest;
    uint16_t *temp16   = (uint16_t *)param;
    uint8_t *temp8     = (uint8_t *)param;

    switch (event)
    {
        case kUSB_DeviceEventBusReset:
        {
            s_cdcVcom.attach               = 0;
            s_cdcVcom.currentConfiguration = 0U;
            error                          = kStatus_USB_Success;

#if (defined(USB_DEVICE_CONFIG_LPCIP3511HS) && (USB_DEVICE_CONFIG_LPCIP3511HS > 0U))
#if !((defined FSL_FEATURE_SOC_USBPHY_COUNT) && (FSL_FEATURE_SOC_USBPHY_COUNT > 0U))
            /* The work-around is used to fix the HS device Chirping issue.
             * Please refer to the implementation for the detail information.
             */
            USB_DeviceHsPhyChirpIssueWorkaround();
#endif
#endif

            /* Get USB speed to configure the device, including max packet size and interval of the endpoints. */
            if (kStatus_USB_Success == USB_DeviceClassGetSpeed(kUSB_ControllerLpcIp3511Hs0, &s_cdcVcom.speed))
            {
                USB_DeviceSetSpeed(handle, s_cdcVcom.speed);
            }
        }
        break;
#if (defined(USB_DEVICE_CONFIG_DETACH_ENABLE) && (USB_DEVICE_CONFIG_DETACH_ENABLE > 0U))
        case kUSB_DeviceEventDetach:
        {
#if (defined(USB_DEVICE_CONFIG_LPCIP3511HS) && (USB_DEVICE_CONFIG_LPCIP3511HS > 0U))
#if !((defined FSL_FEATURE_SOC_USBPHY_COUNT) && (FSL_FEATURE_SOC_USBPHY_COUNT > 0U))
            USB_DeviceDisconnected();
#endif
#endif
            error = kStatus_USB_Success;
        }
        break;
#endif
        case kUSB_DeviceEventSetConfiguration:
            if (0U == (*temp8))
            {
                s_cdcVcom.attach               = 0;
                s_cdcVcom.currentConfiguration = 0U;
                error                          = kStatus_USB_Success;
            }
            else if (USB_CDC_VCOM_CONFIGURE_INDEX == (*temp8))
            {
                s_cdcVcom.attach               = 1;
                s_cdcVcom.currentConfiguration = *temp8;
                error                          = kStatus_USB_Success;
                /* Schedule buffer for receive */
                USB_DeviceCdcAcmRecv(s_cdcVcom.cdcAcmHandle, USB_CDC_VCOM_BULK_OUT_ENDPOINT, s_currRecvBuf,
                                     g_UsbDeviceCdcVcomDicEndpoints[1].maxPacketSize);
            }
            else
            {
                /* no action, return kStatus_USB_InvalidRequest */
            }
            break;
        case kUSB_DeviceEventSetInterface:
            if (0U != s_cdcVcom.attach)
            {
                uint8_t interface        = (uint8_t)((*temp16 & 0xFF00U) >> 0x08U);
                uint8_t alternateSetting = (uint8_t)(*temp16 & 0x00FFU);
                if (interface == USB_CDC_VCOM_COMM_INTERFACE_INDEX)
                {
                    if (alternateSetting < USB_CDC_VCOM_COMM_INTERFACE_ALTERNATE_COUNT)
                    {
                        s_cdcVcom.currentInterfaceAlternateSetting[interface] = alternateSetting;
                        error                                                 = kStatus_USB_Success;
                    }
                }
                else if (interface == USB_CDC_VCOM_DATA_INTERFACE_INDEX)
                {
                    if (alternateSetting < USB_CDC_VCOM_DATA_INTERFACE_ALTERNATE_COUNT)
                    {
                        s_cdcVcom.currentInterfaceAlternateSetting[interface] = alternateSetting;
                        error                                                 = kStatus_USB_Success;
                    }
                }
                else
                {
                    /* no action, return kStatus_USB_InvalidRequest */
                }
            }
            break;
        case kUSB_DeviceEventGetConfiguration:
            if (NULL != param)
            {
                /* Get current configuration request */
                *temp8 = s_cdcVcom.currentConfiguration;
                error  = kStatus_USB_Success;
            }
            break;
        case kUSB_DeviceEventGetInterface:
            if (NULL != param)
            {
                /* Get current alternate setting of the interface request */
                uint8_t interface = (uint8_t)((*temp16 & 0xFF00U) >> 0x08U);
                if (interface < USB_CDC_VCOM_INTERFACE_COUNT)
                {
                    *temp16 = (*temp16 & 0xFF00U) | s_cdcVcom.currentInterfaceAlternateSetting[interface];
                    error   = kStatus_USB_Success;
                }
            }
            break;
        case kUSB_DeviceEventGetDeviceDescriptor:
            if (NULL != param)
            {
                error = USB_DeviceGetDeviceDescriptor(handle, (usb_device_get_device_descriptor_struct_t *)param);
            }
            break;
        case kUSB_DeviceEventGetConfigurationDescriptor:
            if (NULL != param)
            {
                error = USB_DeviceGetConfigurationDescriptor(handle,
                                                             (usb_device_get_configuration_descriptor_struct_t *)param);
            }
            break;
        case kUSB_DeviceEventGetStringDescriptor:
            if (NULL != param)
            {
                /* Get device string descriptor request */
                error = USB_DeviceGetStringDescriptor(handle, (usb_device_get_string_descriptor_struct_t *)param);
            }
            break;
        default:
            /* no action, return kStatus_USB_InvalidRequest */
            break;
    }

    return error;
}

/*!
 * @brief CDC class specific callback function.
 *
 * This function handles the CDC class specific requests. For the current case, device is waiting for data from host.
 * Once device receives data, kUSB_DeviceCdcEventRecvResponse event will be asserted. In kUSB_DeviceCdcEventRecvResponse
 * event, the received data lenght is saved to s_recvSize. If s_recvSize is 0, device will call USB_DeviceCdcAcmRecv to
 * schedule buffer for next receiving event directly and it means there is no data to echo to host. If s_recvSize is not
 * 0, USB_DeviceCdcVcomTask will copy the received data into s_currSendBuf then send back to host. Once data is echoed
 * back completely, USB_DeviceCdcAcmRecv is called in kUSB_DeviceCdcEventSendResponse event to be ready to receive the
 * next data from host. Instead, USB_DeviceCdcAcmRecv also can be called in kUSB_DeviceCdcEventRecvResponse event as
 * long as the received data is handled completely.
 *
 * @param handle          The CDC ACM class handle.
 * @param event           The CDC ACM class event type.
 * @param param           The parameter of the class specific request.
 *
 * @return A USB error code or kStatus_USB_Success.
 */
usb_status_t USB_DeviceCdcVcomCallback(class_handle_t handle, uint32_t event, void *param)
{
    usb_status_t error = kStatus_USB_InvalidRequest;
#if ((defined USB_DEVICE_CONFIG_CDC_CIC_EP_DISABLE) && (USB_DEVICE_CONFIG_CDC_CIC_EP_DISABLE > 0U))
#else
    uint32_t len;
#endif
    usb_device_cdc_acm_request_param_struct_t *acmReqParam;
    usb_device_endpoint_callback_message_struct_t *epCbParam;
    BaseType_t wake=pdFALSE;

    acmReqParam = (usb_device_cdc_acm_request_param_struct_t *)param;
    epCbParam   = (usb_device_endpoint_callback_message_struct_t *)param;
    switch (event)
    {
        case kUSB_DeviceCdcEventSendResponse:
        {
            if ((epCbParam->length != 0) &&
                (0U == (epCbParam->length % g_UsbDeviceCdcVcomDicEndpoints[0].maxPacketSize)))
            {
                /* If the last packet is the size of endpoint, then send also zero-ended packet,
                 ** meaning that we want to inform the host that we do not have any additional
                 ** data, so it can flush the output.
                 */
                error = USB_DeviceCdcAcmSend(handle, USB_CDC_VCOM_BULK_IN_ENDPOINT, NULL, 0);
            }
            else if ((1U == s_cdcVcom.attach) && (1U == s_cdcVcom.startTransactions))
            {
                if ((epCbParam->buffer != NULL) || ((epCbParam->buffer == NULL) && (epCbParam->length == 0)))
                {
                	/* Transmit is complete. Alert the Tx task */
                	vTaskNotifyGiveFromISR(s_cdcVcom.txTaskHandle,&wake);
                	portYIELD_FROM_ISR(wake);
                }
            }
            else
            {
            }
        }
        break;
        case kUSB_DeviceCdcEventRecvResponse:
        {
            if ((1U == s_cdcVcom.attach) && (1U == s_cdcVcom.startTransactions))
            {
                /* Save the received data length, the data will be handled in USB_DeviceCdcVcomTask. Certainly, the
                   received data also can be handled by any other user's application. Meanwhile, once complete handling
                   the received data then we can also call USB_DeviceCdcAcmRecv here to be ready to receive the next
                   data.  */
                len = epCbParam->length;
                error      = kStatus_USB_Success;

                /* this performs a copy of the data */
                xStreamBufferSendFromISR(s_cdcVcom.rxStream,s_currRecvBuf,len,&wake);
                portYIELD_FROM_ISR(wake);

                /* resubmit the receive buffer */
                error = USB_DeviceCdcAcmRecv(handle, USB_CDC_VCOM_BULK_OUT_ENDPOINT, s_currRecvBuf, g_UsbDeviceCdcVcomDicEndpoints[1].maxPacketSize);
            }
        }
        break;
        case kUSB_DeviceCdcEventSerialStateNotif:
            ((usb_device_cdc_acm_struct_t *)handle)->hasSentState = 0;
            error = kStatus_USB_Success;
            break;
        case kUSB_DeviceCdcEventSendEncapsulatedCommand:
            /* no action, return kStatus_USB_InvalidRequest */
            break;
        case kUSB_DeviceCdcEventGetEncapsulatedResponse:
            /* no action, return kStatus_USB_InvalidRequest */
            break;
        case kUSB_DeviceCdcEventSetCommFeature:
            /* no action, return kStatus_USB_InvalidRequest */
            break;
        case kUSB_DeviceCdcEventGetCommFeature:
            /* no action, return kStatus_USB_InvalidRequest */
            break;
        case kUSB_DeviceCdcEventClearCommFeature:
            /* no action, return kStatus_USB_InvalidRequest */
            break;
        case kUSB_DeviceCdcEventGetLineCoding:
            *(acmReqParam->buffer) = (uint8_t*)s_lineCoding;
            *(acmReqParam->length) = LINE_CODING_SIZE;
            error                  = kStatus_USB_Success;
            break;
        case kUSB_DeviceCdcEventSetLineCoding:
        	/* We don't actually set a line coding. Just ignore what they ask for and return hard-coded value */
            if (1U == acmReqParam->isSetup)
            {
                *(acmReqParam->buffer) = (uint8_t*)s_lineCoding;
                *(acmReqParam->length) = LINE_CODING_SIZE;
            }
            else
            {
                /* no action, data phase, s_lineCoding has been assigned */
            }
            error = kStatus_USB_Success;
            break;
        case kUSB_DeviceCdcEventSetControlLineState:
            error = kStatus_USB_Success;
            s_cdcVcom.startTransactions = 1;
            break;
        case kUSB_DeviceCdcEventSendBreak:
            break;
        default:
            break;
    }

    return error;
}

void USBInit(StreamBufferHandle_t * tx, StreamBufferHandle_t * rx){
    NVIC_ClearPendingIRQ(USB1_IRQn);
    NVIC_ClearPendingIRQ(USB1_NEEDCLK_IRQn);

    POWER_DisablePD(kPDRUNCFG_PD_USB1_PHY); /*< Turn on USB1 Phy */

    /* reset the IP to make sure it's in reset state. */
    RESET_PeripheralReset(kUSB1H_RST_SHIFT_RSTn);
    RESET_PeripheralReset(kUSB1D_RST_SHIFT_RSTn);
    RESET_PeripheralReset(kUSB1_RST_SHIFT_RSTn);
    RESET_PeripheralReset(kUSB1RAM_RST_SHIFT_RSTn);

    CLOCK_EnableClock(kCLOCK_Usbh1);
    /* Put PHY powerdown under software control */
    *((uint32_t *)(USBHSH_BASE + 0x50)) = USBHSH_PORTMODE_SW_PDCOM_MASK;
    /* According to reference mannual, device mode setting has to be set by access usb host register */
    *((uint32_t *)(USBHSH_BASE + 0x50)) |= USBHSH_PORTMODE_DEV_ENABLE_MASK;
    /* enable usb1 host clock */
    CLOCK_DisableClock(kCLOCK_Usbh1);

    s_cdcVcom.txStream = xStreamBufferCreate(1024,1);
    s_cdcVcom.rxStream = xStreamBufferCreate(1024,1);
    *rx = s_cdcVcom.rxStream;
    *tx = s_cdcVcom.txStream;
    xTaskCreate(USBTxTask,"USB Tx",256,NULL,0,&s_cdcVcom.txTaskHandle);

}
