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
#include "nmea.h"
#include "tuss4470.h"
#include "commands.h"
#include "message.h"
#include "usb_port.h"
#include "echogram.h"
#include "env.h"

//This prevents linker from removing symbol -- this symbol is necessary for gdb to show individual freertos task call stacks when debugging
extern const uint8_t FreeRTOSDebugConfig[];

/*
 * The Rx task just watches a stream buffer, accumulates a message, and dispatches it to the message queue
 */
struct RxTaskParams {
	QueueHandle_t command;
	struct Message * msg;
};

void CommandRxTask(void * params){
	struct RxTaskParams * rx = (struct RxTaskParams *)params;
	uint8_t bytes;
	uint8_t data;

	while(1){
		bytes = xStreamBufferReceive(rx->msg->rx_stream,&data,sizeof(data),portMAX_DELAY);
		while(bytes){
			 bytes -= MessageProcess(rx->msg, &data, bytes);
			 if(MessageCheckReady(rx->msg)){
				 xQueueSend(rx->command,(void*)rx->msg,portMAX_DELAY);
				 MessageClear(rx->msg);
			 }
		}
	}
}


struct RxTaskParams rx_task_config;
struct Message nmea_port_msg;
QueueHandle_t cmd_queue;
QueueHandle_t ping_cmds;
StreamBufferHandle_t usb_tx;
StreamBufferHandle_t usb_rx;
QueueHandle_t sensor_reading;


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
	TUSS4470Init(&ping_cmds);
    CommandParserInit(&cmd_queue,ping_cmds);
    MessageInit(&nmea_port_msg);
    USBInit(&usb_tx, &usb_rx);
    ADCDMAInit(env.echo_baud, usb_tx, &sensor_reading);
    NMEAInit(env.nmea_baud,nmea_port_msg.rx_stream,nmea_port_msg.tx_queue,sensor_reading);
    rx_task_config.msg = &nmea_port_msg;
    rx_task_config.command = cmd_queue;

    xTaskCreate(CommandRxTask,"NMEA Parse",256,(void*)&rx_task_config,0,NULL);

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
