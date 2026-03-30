/*
 * echograph.c
 *
 *  Created on: Feb. 5, 2025
 *      Author: slocke
 */
#include <echogram.h>
#include "FreeRTOS.h"
#include "stream_buffer.h"
#include "peripherals.h"
#include "pin_mux.h"
#include "fsl_gpio.h"
#include "task.h"
#include "queue.h"
#include "math.h"
#include "env.h"
#include <stdbool.h>

/*
 * The DMA engine is only capable of doing transfers up to 1024 items.
 * We anticipate transfers up to 25000, and want it to be configurable. This means we have
 * to chain multiple transfers together to get our required size.
 */
#define ADC_MAX_XFER 20000
/*
 * The first descriptor is stored as a "head" descriptor by the peripheral. For linked list
 * transfers, we only need to allocate the _additional_ list items.
 */
#define MAX_LIST_ITEMS ((ADC_MAX_XFER/DMA_MAX_TRANSFER_COUNT))
DMA_ALLOCATE_LINK_DESCRIPTORS(dma_list,MAX_LIST_ITEMS);
//set up a ping-pong buffer so we can (hopefully) eliminate a copy
#define MAX_BUFFERS 2
echogram_header_t header[MAX_BUFFERS];
uint16_t adc_data[MAX_BUFFERS][ADC_MAX_XFER] __attribute__((aligned(4)));
uint32_t wr_buff_index=0;
TaskHandle_t echogram_usb_task;
TaskHandle_t echogram_ser_task;
TaskHandle_t echogram_ser_tx_task;
TaskHandle_t depth_calc_task;

QueueHandle_t sensor_queue;

struct echotask_config {
	uint32_t bw_limit;
	StreamBufferHandle_t tx_stream;
};

static struct echotask_config usb_task_config;
static struct echotask_config ser_task_config;

void DepthCalcTask(void * params){
	uint16_t * data;
	echogram_header_t * hdr;
	uint32_t i;
	uint32_t sum, start, max_sum, max_start,max_count;
	bool in_pulse;
	uint64_t min_pulse_len_counts;
	float a,y,y_1;
	sensor_sample_t sample;
	int32_t diff;


	while(1){
		ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
		data = adc_data[0];
		hdr = &header[0];
		if(!wr_buff_index){
			data = adc_data[1];
			hdr = &header[1];
		}
		/* We want a pulse that is at least 60% as long as our source ping */
		min_pulse_len_counts = hdr->ping_len_us;
		min_pulse_len_counts *= hdr->f_sample;
		min_pulse_len_counts *= 60;
		min_pulse_len_counts /= 1000000;
		min_pulse_len_counts /= 100;

		/*
		 * We low-pass filter the echogram to get a "base" for the threshold. We then add a fixed number to that base threshold
		 * (to account for noise). The peak-to-peak noise (observed in the office) of "quiet" is about 300 counts (centered around 6375 counts).
		 * This threshold "adder" number may need to be tweaked in the real world (hence it is stored in the environment.
		 * The parameters for the low-pass filter are determined at run-time based on the ping length. The theory is that (for a first order filter),
		 * the time constant (tau) represents the time in which the filter will reach ~66% of the final value. For a ping length of, say, 100us, a first
		 * order filter with tau=100us should then always be ~34% less than the peak value, giving us a predictable gap between the threshold and
		 * received ping.  It is also quite easy to design a first order IIR filter with a known tau:
		 * y[n] = bx[n] + ay[n-1]
		 * for b = (1-a) (ie, no gain), 'a' is the decay factor, so: a = e^(-1/T) (where T = tau)
		 * We must convert tau from seconds (microseconds) to samples first:
		 * tau = ping_len_us*sample_rate/1000000
		 */
		a = 1.0;
		a = (float)hdr->ping_len_us;
		a *= 1e-6;
		a *= (float)hdr->f_sample;
		a *= env.threshold_filter_scale;
		a = pow(M_E,-1.0/a);
		y_1 = data[0];
		/* create the base threshold and find a pulse that is higher than our filtered response */
		sum = 0;
		start = 0;
		max_sum = 0;
		max_start = 0;
		in_pulse = false;
		max_count = 0;
		for(i=1;i<hdr->burst_len;i++,y_1 = y){
			y = (1-a)*data[i] + a*y_1;
			diff = data[i] - y;

			if(in_pulse){
				if(diff > 0){
					sum += diff;
				}else{
					in_pulse = false;
					if(sum > max_sum){
						max_sum = sum;
						max_start = start;
						max_count = i-start;
					}
				}
			}else{
				if(diff > 0){
					sum = diff;
					in_pulse = true;
					start = i;
				}
			}
		}

		//TODO: add SOS profile to depth calculation?  Would have to be loaded in as a piecewise sound velocity profile
		sample.reading = max_start;
		sample.reading /= hdr->f_sample;	//convert to time
		sample.reading += (double)hdr->ping_len_us/1000000.0;	//add in the ping duration

		sample.reading *= env.speed_of_sound/2;

		/* Some sanity checks on the resulint depth before reporting it */
		if(max_start<hdr->burst_len && sample.reading > 0.4 && max_count > min_pulse_len_counts){
			sample.type = TYPE_DEPTH;
		}else{
			sample.type = TYPE_DEPTH_EMPTY;
		}
		xQueueSendToBack(sensor_queue,&sample,pdMS_TO_TICKS(5));
	}
}

void EchogramTask(void* params){
	uint32_t bytes;
	uint32_t written;
	uint16_t * data;
	uint8_t * p;
	echogram_header_t * hdr;
	echogram_header_t downsampled_header;
	uint16_t * downsampled_data=NULL;
	uint64_t max_burst=0;
	uint32_t i,j,step;
	struct echotask_config * cfg = (struct echotask_config*)params;

	if(cfg->bw_limit != 0){
		downsampled_data = pvPortMalloc(sizeof(uint16_t)*ADC_MAX_XFER);
	}

	while(1){
		ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
		data = adc_data[0];
		hdr = &header[0];
		if(!wr_buff_index){
			data = adc_data[1];
			hdr = &header[1];
		}

		/*
		 * If we have a bandwidth limit, down-sample the ADC data to fit. This
		 * is applicable to the serialport output.
		 * bw_limit is the maximum number of bytes per second that we can
		 * sustainably transmit (usually around 90%*baud/10). We attempt
		 * to fit our depth echogram in that limit
		 */
		if(cfg->bw_limit != 0){
			/* How many bytes per frame can we allow */
			max_burst = ((uint64_t)cfg->bw_limit*hdr->ping_period_us)/1000000;
			max_burst -= sizeof(echogram_header_t);	//need to reserve the length of the header
			//TODO: what's better, more samples @ 8-bit, or more resolution at fewer samples?
			max_burst /= sizeof(uint16_t);	//this is the most number of samples we can transmit

			/* Ideally our downsampled size is big enough, but if not we have to resample */
			if(max_burst < hdr->burst_len){
				/*
				 * For now, we only do integer downsampling factors by sub-sampling the
				 * original data.
				 */
				step = hdr->burst_len/max_burst;
				for(i=0,j=0;i<max_burst;i++,j+=step) downsampled_data[i] = data[j];

				memcpy(&downsampled_header, hdr,sizeof(echogram_header_t));
				downsampled_header.burst_len = max_burst;
				downsampled_header.f_sample /= step;
				data = downsampled_data;
				hdr = &downsampled_header;
			}
		}

		xStreamBufferSend(cfg->tx_stream,hdr,sizeof(echogram_header_t),portMAX_DELAY);

		bytes = hdr->burst_len*sizeof(uint16_t);
		p = (uint8_t*)data;

		while(bytes){
			written = xStreamBufferSend(cfg->tx_stream,p,bytes,portMAX_DELAY);
			bytes -= written;
			p += written;
		}
	}
}

void FLEXCOMM2_IRQHandler(void){
	BaseType_t higher_task_woken=pdFALSE;
	uint8_t __attribute__((unused)) byte;

	if(FLEXCOMM_USART_PERIPHERAL->FIFOINTSTAT & USART_FIFOINTSTAT_TXLVL(1)){
		//space in tx register -- notify tx task
		vTaskNotifyGiveFromISR(echogram_ser_tx_task,&higher_task_woken);
		portYIELD_FROM_ISR(higher_task_woken);
		FLEXCOMM_USART_PERIPHERAL->FIFOINTENCLR = USART_FIFOINTENCLR_TXLVL(1);
	}
	if(FLEXCOMM_USART_PERIPHERAL->FIFOINTSTAT & USART_FIFOINTSTAT_RXLVL(1)){
		//RX is not enabled. Should never get here
		byte = FLEXCOMM_USART_PERIPHERAL->FIFORD;
	}

}

void EchoPort_Tx_Task(void * params){
	char * msg;
	struct echotask_config * cfg = (struct echotask_config*)params;
	uint32_t bytes_sent;
	uint32_t bytes_avail;

	msg = pvPortMalloc(256);

	while(1){
		bytes_avail = xStreamBufferReceive(cfg->tx_stream,msg,256,portMAX_DELAY);
		bytes_sent = 0;
		while(bytes_sent < bytes_avail){
			if(FLEXCOMM_USART_PERIPHERAL->FIFOSTAT & USART_FIFOSTAT_TXNOTFULL(1)){
				FLEXCOMM_USART_PERIPHERAL->FIFOWR = msg[bytes_sent++];
			}else{
				//we filled up the tx buffer -- enable the interrupt and wait
				FLEXCOMM_USART_PERIPHERAL->FIFOINTENSET = USART_FIFOINTENSET_TXLVL(1);
				ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(50));
				//try again.
			}
		}
	}
}

void ADCDMACallback(struct _dma_handle *handle, void *userData, bool transferDone, uint32_t intmode){
	BaseType_t wake;
	wr_buff_index++;
	if(wr_buff_index >= MAX_BUFFERS) wr_buff_index = 0;

	GPIO_PortClear(GPIO,BOARD_INITPINS_DBG_PORT,BOARD_INITPINS_DBG_PIN_MASK);	//debug
	ADCDisableBurst();
	vTaskNotifyGiveFromISR(echogram_usb_task,&wake);
	portYIELD_FROM_ISR(wake);
	vTaskNotifyGiveFromISR(echogram_ser_task,&wake);
	portYIELD_FROM_ISR(wake);
	vTaskNotifyGiveFromISR(depth_calc_task,&wake);
	portYIELD_FROM_ISR(wake);

	/*
	 * Start acquisition of the temperature ADC channels. Each of these conversions will take about 1ms
	 */
	//clear fifo
	ADC0_PERIPHERAL->CTRL |= ADC_CTRL_RSTFIFO1_MASK;
	ADC0_PERIPHERAL->CTRL &= ~ADC_CTRL_RSTFIFO1_MASK;
	LPADC_DoSoftwareTrigger(ADC0_PERIPHERAL,1<<ADC0_THERM_TRIGGER);
}

void ADC0_IRQHandler(void){
	BaseType_t wake;
	uint32_t res;
	sensor_sample_t sample;
	float ln;

	ADC0_PERIPHERAL->STAT |= ADC_STAT_RDY1_MASK;
	while(ADC0->FCTRL[1] & ADC_FCTRL_FCOUNT_MASK){
		res = ADC0_PERIPHERAL->RESFIFO[1];
		switch((res&ADC_RESFIFO_CMDSRC_MASK) >> ADC_RESFIFO_CMDSRC_SHIFT){
			case ADC0_THERMISTOR:
				sample.type = TYPE_THERMISTOR;
				sample.reading = (res&ADC_RESFIFO_D_MASK) >> ADC_RESFIFO_D_SHIFT;

				//convert to proportional resistance (compared to our reference 10k pullup):
				sample.reading = 10000*sample.reading/(32768-sample.reading);

				//convert to proprtional ratio to R25 resistance:
				sample.reading /= env.thermistor_r25;

				//use steinhart-hart equation to calcualte temperature proportional to resistance
				ln = logf(sample.reading);
				sample.reading = env.thermistor_a + env.thermistor_b*ln;
				ln *= ln;	//square
				sample.reading += env.thermistor_c*ln;
				ln *= ln;	//cube
				sample.reading += env.thermistor_d*ln;

				sample.reading = 1/sample.reading;
				sample.reading -= 273.15;

				xQueueSendToBackFromISR(sensor_queue,&sample,&wake);
				portYIELD_FROM_ISR(wake);
				break;
			case ADC0_CPU_TEMP:
				sample.type = TYPE_CPU_TEMP;
				sample.reading = (res&ADC_RESFIFO_D_MASK) >> ADC_RESFIFO_D_SHIFT;
				xQueueSendToBackFromISR(sensor_queue,&sample,&wake);
				portYIELD_FROM_ISR(wake);
				break;
		}
	}

}

static void ADCDMATransfer(uint32_t samples){
	dma_descriptor_t * p_list = dma_list;
	uint32_t xfer;
	dma_descriptor_t * p_next=NULL;
	uint16_t * p_data;;
	dma_channel_config_t dma_ch_config;
	uint32_t xfer_size = 0;

	header[wr_buff_index].burst_len = samples;
	p_data = adc_data[wr_buff_index];

	GPIO_PortSet(GPIO,BOARD_INITPINS_DBG_PORT,BOARD_INITPINS_DBG_PIN_MASK);

	/* Create a basic transfer config. This assumes we're linking transfers, and it doesn't have a length set
	 * (both are updated later)
	 */
	xfer = DMA_SetChannelXferConfig(
			1,	//reload
			0,	//clrtrig
			0,	//intA
			0,	//intB
			kDMA_Transfer16BitWidth,	//width
			0,	//srcinc
			1,	//dstinc
			xfer_size);	//number of bytes to transfer
	p_next = NULL;

	/* The first descriptor is the head descriptor (setup by the driver). */
	if(samples > DMA_MAX_TRANSFER_COUNT){
		//we'll need at least one of our linked list items to complete this transfer
		xfer_size = DMA_MAX_TRANSFER_COUNT;
		p_next = p_list;
	}else{
		//we can do the whole transfer with just the head descriptor
		xfer_size = samples;
		xfer &= ~DMA_CHANNEL_XFERCFG_RELOAD_MASK;	//clear reload
		xfer |= DMA_CHANNEL_XFERCFG_SETINTA_MASK | DMA_CHANNEL_XFERCFG_CLRTRIG_MASK;
	}
	samples -= xfer_size;
	//prepare the head transfer item
	xfer &= ~DMA_CHANNEL_XFERCFG_XFERCOUNT_MASK;
	xfer |= DMA_CHANNEL_XFERCFG_XFERCOUNT(xfer_size-1);

	DMA_PrepareChannelTransfer(&dma_ch_config,
				(void *)&ADC0_PERIPHERAL->RESFIFO[0],
				p_data,
				xfer,
				kDMA_PeripheralToMemory,
				kDMA_NoTrigger,p_next++);
	p_data += xfer_size;

	/* Fill the remainder of the link list */
	xfer |= DMA_CHANNEL_XFERCFG_SWTRIG_MASK;	//all remaining list items trigger immediately
	while(samples){
		if(samples > DMA_MAX_TRANSFER_COUNT){
			xfer_size = DMA_MAX_TRANSFER_COUNT;
		}else{
			xfer_size = samples;
			xfer &= ~DMA_CHANNEL_XFERCFG_RELOAD_MASK;	//clear reload
			p_next = NULL;
			xfer |= DMA_CHANNEL_XFERCFG_SETINTA_MASK | DMA_CHANNEL_XFERCFG_CLRTRIG_MASK;
		}
		xfer &= ~DMA_CHANNEL_XFERCFG_XFERCOUNT_MASK;
		xfer |= DMA_CHANNEL_XFERCFG_XFERCOUNT(xfer_size-1);

	    DMA_SetupDescriptor(p_list++, xfer, (void *)&ADC0_PERIPHERAL->RESFIFO[0], p_data, p_next++);
	    p_data += xfer_size;
	    samples -= xfer_size;
	}

    DMA_SubmitChannelTransfer(&DMA0_CH0_Handle, &dma_ch_config);
    DMA_StartTransfer(&DMA0_CH0_Handle);
    GPIO_PortClear(GPIO,BOARD_INITPINS_DBG_PORT,BOARD_INITPINS_DBG_PIN_MASK);
}


/* Set up the hardware trigger so that when trigger events happen, we create ADC samples */
void ADCEnableBurst(uint32_t len,uint32_t ping_period_us,uint32_t ping_length_us){

	if(DMA_ChannelIsActive(DMA0_DMA_BASEADDR, DMA0_CH0_DMA_CHANNEL)){
		//stop ADC
		ADCDisableBurst();

		//kill any pending DMA transfers:
		DMA_DisableChannel(DMA0_DMA_BASEADDR, DMA0_CH0_DMA_CHANNEL);
		DMA0_DMA_BASEADDR->COMMON->ABORT = 1<<DMA0_CH0_DMA_CHANNEL;
	}


	//enable from ADC to our buffer of a given length
	if(len > ADC_MAX_XFER) len = ADC_MAX_XFER;

	//clear fifo
	ADC0_PERIPHERAL->CTRL |= ADC_CTRL_RSTFIFO0_MASK;
	ADC0_PERIPHERAL->CTRL &= ~ADC_CTRL_RSTFIFO0_MASK;

	ADCDMATransfer(len);

	header[wr_buff_index].f_sample = 100000;
	header[wr_buff_index].ping_len_us = ping_length_us;
	header[wr_buff_index].ping_period_us = ping_period_us;

	ADC0_PERIPHERAL->TCTRL[ADC0_ECHO_TRIGGER] |= ADC_TCTRL_HTEN_MASK; //enable hardware trigger
}

/* Disable HW trigger to stop conversion */
void ADCDisableBurst(void){
	//turn off trigger
	ADC0_PERIPHERAL->TCTRL[ADC0_ECHO_TRIGGER] &= ~ADC_TCTRL_HTEN_MASK;
}

void ADCDMAInit(int32_t baud, StreamBufferHandle_t usb_stream, QueueHandle_t * sensor_message){
	// Setup generic UART parameters
	FLEXCOMM_USART_PERIPHERAL->FIFOCFG = USART_FIFOCFG_ENABLETX(1) |
										USART_FIFOCFG_ENABLERX(1) |
										USART_FIFOCFG_EMPTYTX(1) |
										USART_FIFOCFG_EMPTYRX(1);
	FLEXCOMM_USART_PERIPHERAL->FIFOTRIG = USART_FIFOTRIG_TXLVLENA(1) |
										USART_FIFOTRIG_RXLVLENA(1) |
										USART_FIFOTRIG_TXLVL(8) |
										USART_FIFOTRIG_RXLVL(0);
	FLEXCOMM_USART_PERIPHERAL->FIFOINTENSET = USART_FIFOINTENSET_RXLVL(1);

	USART_SetBaudRate(FLEXCOMM_USART_PERIPHERAL,baud,FLEXCOMM_USART_CLOCK_SOURCE);

	sensor_queue = xQueueCreate(20,sizeof(sensor_sample_t));
	*sensor_message = sensor_queue;

	ADC0_PERIPHERAL->FCTRL[1] = ADC_FCTRL_FWMARK(0);
	ADC0_PERIPHERAL->IE |= ADC_IE_FWMIE1_MASK;
	ADC0_PERIPHERAL->CTRL |= ADC_CTRL_ADCEN_MASK;	//enable ADC
	NVIC_EnableIRQ(ADC0_IRQn);

    DMA_SetCallback(&DMA0_CH0_Handle,ADCDMACallback,NULL);
    NVIC_EnableIRQ(DMA0_IRQn);
    header[0].frame_sync[0] = 0xFFFFFFFF;
    header[0].frame_sync[1] = 0xFFFFFFFF;
    header[0].frame_sync[2] = 0x0000FFFF;
    header[1].frame_sync[0] = 0xFFFFFFFF;
    header[1].frame_sync[1] = 0xFFFFFFFF;
    header[1].frame_sync[2] = 0x0000FFFF;

	usb_task_config.bw_limit = 0;
	usb_task_config.tx_stream = usb_stream;
	xTaskCreate(EchogramTask, "EG-USB",256,&usb_task_config,1,&echogram_usb_task);

	ser_task_config.bw_limit = baud*95/(100*10);
    ser_task_config.tx_stream = xStreamBufferCreate(512,1);
	xTaskCreate(EchogramTask, "EG-SER",256,&ser_task_config,1,&echogram_ser_task);

    xTaskCreate(EchoPort_Tx_Task, "EG-Tx", 256, &ser_task_config, 2, &echogram_ser_tx_task);
	NVIC_EnableIRQ(FLEXCOMM2_IRQn);

	xTaskCreate(DepthCalcTask, "DPTH", 256, NULL,1,&depth_calc_task);


}
