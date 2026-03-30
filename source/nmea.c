/*
 * nmea.c
 *
 *  Created on: Jan. 28, 2025
 *      Author: slocke
 */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "stream_buffer.h"
#include "peripherals.h"
#include "message.h"
#include "echogram.h"
#include "env.h"
#include <stdio.h>

static StreamBufferHandle_t rx_stream;
static TaskHandle_t uart_tx_task;
static QueueHandle_t tx_queue;
static QueueHandle_t sensor_readings_q;

void FLEXCOMM0_IRQHandler(void){
	BaseType_t higher_task_woken=pdFALSE;
	uint8_t byte;

	if(FLEXCOMM_NMEA_PERIPHERAL->FIFOINTSTAT & USART_FIFOINTSTAT_TXLVL(1)){
		//space in tx register -- notify tx task
		vTaskNotifyGiveFromISR(uart_tx_task,&higher_task_woken);
		portYIELD_FROM_ISR(higher_task_woken);
		FLEXCOMM_NMEA_PERIPHERAL->FIFOINTENCLR = USART_FIFOINTENCLR_TXLVL(1);
	}
	if(FLEXCOMM_NMEA_PERIPHERAL->FIFOINTSTAT & USART_FIFOINTSTAT_RXLVL(1)){
		byte = FLEXCOMM_NMEA_PERIPHERAL->FIFORD;
		xStreamBufferSendFromISR(rx_stream,&byte,1,&higher_task_woken);
		portYIELD_FROM_ISR(higher_task_woken);
	}

}
/*appends *XX\r\n to the end of the nmea string (that is assumed to start with $) */
void nmea_append_checksum(char * str){
	int checksum,len;
	int i;
	len = strlen(str);

	for(i=1,checksum=0;i<len;i++) checksum ^= str[i];

	//put in the checksum and final delimiters
	sprintf(&str[len], "*%02X\r\n",checksum);
}

void NMEA_Build_Sentences_Task(void * params){
	char msg[MAX_MESSAGE_LEN+MAX_HEADER_FOOTER_LEN+1];
	sensor_sample_t sensor_data;

	while(1){
		xQueueReceive(sensor_readings_q,&sensor_data,portMAX_DELAY);
		switch(sensor_data.type){
			case TYPE_DEPTH:
				/* Construct and send DBT (depth below transducer) and DPT (depth, using transducer offset) */
				sprintf(msg,"$SDDBT,%0.2f,f,%0.2f,M,%0.2f,F",
						sensor_data.reading*3.281, //depth in feet
						sensor_data.reading, //depth in meter
						sensor_data.reading/1.829 //depth in fathoms
						);
				nmea_append_checksum(msg);
				xQueueSend(tx_queue,(void*)msg,portMAX_DELAY);

				sprintf(msg,"$SDDPT,%0.2f,%0.2f,200",
								sensor_data.reading, //depth relative to transducer
								env.transducer_offset //offset from transducer (+ means transducer to waterline, - means distance from transducer to keel)
								);
				nmea_append_checksum(msg);
				xQueueSend(tx_queue,(void*)msg,portMAX_DELAY);
				break;
			case TYPE_DEPTH_EMPTY:
				/* Send empty message */
				sprintf(msg,"$SDDBT,,f,,M,,F");
				nmea_append_checksum(msg);
				xQueueSend(tx_queue,(void*)msg,portMAX_DELAY);
				sprintf(msg,"$SDDPT,,,200");
				nmea_append_checksum(msg);
				xQueueSend(tx_queue,(void*)msg,portMAX_DELAY);
				break;
			case TYPE_THERMISTOR:
				sprintf(msg,"$SDMTW,%0.2f,C",
								sensor_data.reading //Temperature, degrees C
								);
				nmea_append_checksum(msg);
				xQueueSend(tx_queue,(void*)msg,portMAX_DELAY);
				break;
			case TYPE_CPU_TEMP:
				sprintf(msg,"$PCPUT,%0.1f,C",
								sensor_data.reading //Temperature, degrees C
								);
				nmea_append_checksum(msg);
				xQueueSend(tx_queue,(void*)msg,portMAX_DELAY);
				break;
		}


	}
}

void NMEA_Tx_Task(void * params){
	char * msg;
	uint32_t bytes_sent;
	uint32_t msg_len;

	msg = pvPortMalloc(256);

	while(1){
		xQueueReceive(tx_queue,msg,portMAX_DELAY);
		msg_len = strlen(msg);
		bytes_sent = 0;
		while(bytes_sent < msg_len){
			if(FLEXCOMM_NMEA_PERIPHERAL->FIFOSTAT & USART_FIFOSTAT_TXNOTFULL(1)){
				FLEXCOMM_NMEA_PERIPHERAL->FIFOWR = msg[bytes_sent++];
			}else{
				//we filled up the tx buffer -- enable the interrupt and wait
				FLEXCOMM_NMEA_PERIPHERAL->FIFOINTENSET = USART_FIFOINTENSET_TXLVL(1);
				ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(50));
				//try again.
			}
		}
	}
}


void NMEAInit(uint32_t baud, StreamBufferHandle_t rs, QueueHandle_t tq, QueueHandle_t sensor_q){
	// Setup generic UART parameters
	FLEXCOMM_NMEA_PERIPHERAL->FIFOCFG = USART_FIFOCFG_ENABLETX(1) |
										USART_FIFOCFG_ENABLERX(1) |
										USART_FIFOCFG_EMPTYTX(1) |
										USART_FIFOCFG_EMPTYRX(1);
	FLEXCOMM_NMEA_PERIPHERAL->FIFOTRIG = USART_FIFOTRIG_TXLVLENA(1) |
										USART_FIFOTRIG_RXLVLENA(1) |
										USART_FIFOTRIG_TXLVL(8) |
										USART_FIFOTRIG_RXLVL(0);
	FLEXCOMM_NMEA_PERIPHERAL->FIFOINTENSET = USART_FIFOINTENSET_RXLVL(1);

	USART_SetBaudRate(FLEXCOMM_NMEA_PERIPHERAL,baud,FLEXCOMM_NMEA_CLOCK_SOURCE);

	rx_stream = rs;
	tx_queue = tq;

    xTaskCreate(NMEA_Tx_Task, "NMEA Tx",
                                 256,
                                 NULL,
                                 0,
                                 &uart_tx_task
                               );

    sensor_readings_q = sensor_q;
    xTaskCreate(NMEA_Build_Sentences_Task, "NMEA sntc",512,NULL,0,NULL);

    NVIC_EnableIRQ(FLEXCOMM0_IRQn);

}
