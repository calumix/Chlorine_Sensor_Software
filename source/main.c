/*
 * Copyright 2016-2025 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file    LPC5528_Project.c
 * @brief   Application entry point.
 */
#include <stdio.h>
#include <usb_port.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "stream_buffer.h"
#include "board.h"
#include "peripherals.h"
#include "pin_mux.h"
#include "clock_config.h"
#include "fsl_debug_console.h"
#include "fsl_ostimer.h"
#include "commands.h"
#include "message.h"
#include "usb_port.h"
#include "env.h"
#include "adc.h"
/*
 * Allocating this here (instead of letting the driver allocate it) is a workaround
 * for a bug in the driver/code generator. If the driver allocats this, it puts it in the normal SRAM bank
 * Since the hardware requriement is for the buffer to be aligned on a 16-bit (64kByte) boundary, it forces
 * the linker to put this at a memory location that is actually outside of RAM (.data starts at 0x20000000,
 * then .bss where this variable is located, which the linker puts at 0x20010000 which is actually past the
 * end of SRAM -- and all other bss variables get tagged in behind it).
 * The other method to "fix" this is to initialize the variable so that it gets put at the front of the .data
 * segment...
 */
uint32_t CAN0_RAM_BASE_ADDRESS[CAN0_MESSAGE_RAM_SIZE] __attribute__((section(".bss.$SRAMX"), aligned(CAN0_BASE_ADDRESS_ALIGN_SIZE)));

//This prevents linker from removing symbol -- this symbol is necessary for gdb to show individual freertos task call stacks when debugging
extern const uint8_t FreeRTOSDebugConfig[];

/*
 * The Rx task just watches a stream buffer, accumulates a message, and dispatches it to the message queue
 */
struct RxTaskParams {
	QueueHandle_t command;
	StreamBufferHandle_t rx_stream;
	struct Message * msg;
};

void CommandRxTask(void * params){
	struct RxTaskParams * rx = (struct RxTaskParams *)params;
	uint8_t bytes;
	uint8_t data;

	while(1){
		bytes = xStreamBufferReceive(rx->rx_stream,&data,sizeof(data),portMAX_DELAY);
		while(bytes){
			 bytes -= MessageProcess(rx->msg, &data, bytes);
			 if(MessageCheckReady(rx->msg)){
				 xQueueSend(rx->command,(void*)rx->msg,portMAX_DELAY);
				 MessageClear(rx->msg);
			 }
		}
	}
}
/*
 * The Tx task monitors the queue and converts any messages to the USB
 * data stream. This one way around the USB subsystem using streams (as a stream
 * can only have one writer unless extra protection is used).
 */
struct TxTaskParams{
	QueueHandle_t msg_queue;
	StreamBufferHandle_t usb_tx_stream;
};

void DataTxTask(void * params){
	struct TxTaskParams * tx = (struct TxTaskParams *)params;
	uint8_t buffer[MESSAGE_RESP_BUFFER_LEN];

	while(1){
		xQueueReceive(tx->msg_queue,&buffer,portMAX_DELAY);
		xStreamBufferSend(tx->usb_tx_stream,buffer,strlen((char*)buffer),portMAX_DELAY);
	}
}

struct RxTaskParams rx_task_config;
struct TxTaskParams tx_task_config;
struct Message usb_port_msg;
struct Message data_msg;
struct Message rtd_msg;
struct mcp3551 spi_dev;
struct adc_task_params meas_task_params = {
		.cs_port = BOARD_INITPINS_MEASURE_CSn_PORT,
		.cs_pin = BOARD_INITPINS_MEASURE_CSn_PIN,
		.ch_sel0_port = BOARD_INITPINS_MEASURE_CH_SEL0_PORT,
		.ch_sel0_pin =  BOARD_INITPINS_MEASURE_CH_SEL0_PIN,
		.ch_sel1_port = BOARD_INITPINS_MEASURE_CH_SEL1_PORT,
		.ch_sel1_pin =  BOARD_INITPINS_MEASURE_CH_SEL1_PIN,
		.group_port = BOARD_INITPINS_VOLT_CURRn_PORT,
		.group_pin = BOARD_INITPINS_VOLT_CURRn_PIN,
		.group_a_name = "VOLT",
		.group_a_scale = 1.0,
		.group_a_offset = 0.0,
		.group_b_name = "CURR",
		.group_b_scale = 1.0,
		.group_b_offset = 0.0,
		.dev = &spi_dev,
		.msg = &data_msg,
	};
struct adc_task_params rtd_task_params = {
		.cs_port = BOARD_INITPINS_RTD_CSn_PORT,
		.cs_pin = BOARD_INITPINS_RTD_CSn_PIN,
		.ch_sel0_port = BOARD_INITPINS_RTD_CH_SEL0_PORT,
		.ch_sel0_pin =  BOARD_INITPINS_RTD_CH_SEL0_PIN,
		.ch_sel1_port = BOARD_INITPINS_RTD_CH_SEL1_PORT,
		.ch_sel1_pin =  BOARD_INITPINS_RTD_CH_SEL1_PIN,
		.group_port = BOARD_INITPINS_RTD_REFn_PORT,
		.group_pin = BOARD_INITPINS_RTD_REFn_PIN,
		.group_a_name = "RTD",
		.group_a_scale = 1.0,
		.group_a_offset = 0.0,
		.group_b_name = "REF",
		.group_b_scale = 1.0,
		.group_b_offset = 0.0,
		.dev = &spi_dev,
		.msg = &rtd_msg,
	};
QueueHandle_t cmd_queue;	//post messages to this to be parsed
QueueHandle_t tx_queue;		//post messages to this to be sent out USB port
StreamBufferHandle_t usb_tx;
StreamBufferHandle_t usb_rx;

static configRUN_TIME_COUNTER_TYPE first_count;
configRUN_TIME_COUNTER_TYPE FreeRTOSRuntimeCounterGet(void){
	return OSTIMER_GetCurrentTimerValue(OSTIMER) - first_count;
}
void FreeRTOSRuntimeCounterInit(void){
	OSTIMER_Init(OSTIMER);
	first_count = OSTIMER_GetCurrentTimerValue(OSTIMER);
}

/*
 * Some of the initialization requires the kernel to be running. This
 * task just does all the initialization, then deletes itself
 */
void InitTask(void*params){
	EnvInit();

	/* The command parser will create a queue (to post commands to be parsed to) and kick off it's thread */
	CommandParserInit(&cmd_queue);

	/* Set up USB. interaction after this point will be via the two stream buffers. Note that there can only
	 * be one writer/reader task for each of the stream buffers!
	 */
    USBInit(&usb_tx, &usb_rx);

    /*
     * Since data to the USB port will be sourced from both ADC data and command responses, we have to
     * multiplex them into a single stream. This queue will be posted to from both sources, and a task
     * will merge the queue data into the USB tx stream buffer
     */
	tx_queue = xQueueCreate(4,MESSAGE_RESP_BUFFER_LEN);
	vQueueAddToRegistry(tx_queue,"USB");

    MessageInit(&usb_port_msg,tx_queue);
    MessageInit(&data_msg,tx_queue);
    MessageInit(&rtd_msg,tx_queue);

    rx_task_config.command = cmd_queue;
    rx_task_config.msg = &usb_port_msg;
    rx_task_config.rx_stream = usb_rx;
    xTaskCreate(CommandRxTask,"CMD Parse",256,(void*)&rx_task_config,0,NULL);

    tx_task_config.msg_queue = tx_queue;
    tx_task_config.usb_tx_stream = usb_tx;
    xTaskCreate(DataTxTask,"TX Data",256,(void*)&tx_task_config,0,NULL);

    mcp3551_init(&spi_dev);
    xTaskCreate(ADCTask,"MEAS",256,(void*)&meas_task_params,0,NULL);
    xTaskCreate(ADCTask,"RTD",256,(void*)&rtd_task_params,0,NULL);

	vTaskDelete(NULL);
}

/*
 * @brief   Application entry point.
 */
int main(void) {

    /* Init board hardware. */
    BOARD_InitBootPins();
    BOARD_InitBootClocks();
    BOARD_InitBootPeripherals();
#ifndef BOARD_INIT_DEBUG_CONSOLE_PERIPHERAL
    /* Init FSL debug console. */
    BOARD_InitDebugConsole();
#endif

    xTaskCreate(InitTask,"Init",1024,NULL,0,NULL);

    vTaskStartScheduler();
    /* FreeRTOS schedule should never return */

    /*We touch this structure to prevent gcc from optimizing it out */
    while(1){
    	if(FreeRTOSDebugConfig[0] == 0) __NOP();
    }
    return 0 ;
}
