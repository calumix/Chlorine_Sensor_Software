/*
 * adc.c
 *
 *  Created on: Mar. 31, 2026
 *      Author: slocke
 */

#include "adc.h"
#include "channel.h"
#include "message.h"

void MeasTask(void * params){
	QueueHandle_t q = (QueueHandle_t)params;
	struct Message msg;
	uint8_t chan=0;
	float r,v,c;
	uint64_t time_ms;

	MessageInit(&msg,q);

	while(1){
		if(!ChannelGetState(chan,NULL)){
			r = ChannelReadResistance(chan,&v,&c);
			time_ms = ( ( uint64_t ) ( xTaskGetTickCount() ) * ( uint64_t ) 1000U ) / ( uint64_t ) configTICK_RATE_HZ;;
			//send message
			MessageSendFormat(&msg, "MEAS,%u,%llu,%e,%e,%e",
					chan+1,
					time_ms,
					r,v,c);
		}else{
			taskYIELD();
		}

		//update to a new channel/group
		if(++chan > NUM_CHANNELS) chan = 0;
	}
}

void RTDTask(void * params){
	QueueHandle_t q = (QueueHandle_t)params;
	struct Message msg;
	uint8_t chan=0;
	float t;
	uint64_t time_ms;

	MessageInit(&msg,q);

	while(1){
		if(!ChannelGetState(chan,NULL)){
			t = ChannelReadRTD(chan);
			time_ms = ( ( uint64_t ) ( xTaskGetTickCount() ) * ( uint64_t ) 1000U ) / ( uint64_t ) configTICK_RATE_HZ;;
			//send message
			MessageSendFormat(&msg, "RTD,%u,%llu,%e",
					chan+1,
					time_ms,
					t);
		}else{
			taskYIELD();
		}

		//update to a new channel/group
		if(++chan > NUM_CHANNELS) chan = 0;
	}
}
