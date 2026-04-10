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

	MessageInit(&msg,q);

	while(1){
		if(!ChannelGetState(chan,NULL)){
			r = ChannelReadResistance(chan,&v,&c);

			//send message
			MessageSendFormat(&msg, "MEAS,%u,%e,%e,%e",
					chan,
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

	MessageInit(&msg,q);

	while(1){
		if(!ChannelGetState(chan,NULL)){
			t = ChannelReadRTD(chan);

			//send message
			MessageSendFormat(&msg, "RTD,%u,%e",
					chan,
					t);
		}else{
			taskYIELD();
		}

		//update to a new channel/group
		if(++chan > NUM_CHANNELS) chan = 0;
	}
}
