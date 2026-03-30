/*
 * message.c
 *
 *  Created on: Feb. 3, 2025
 *      Author: slocke
 */
#include <stdarg.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include "FreeRTOS.h"
#include "message.h"
#include "task.h"

/*
 * A message is the following:
 * $<data>/r/n
 *
 */
void MessageClear(struct Message * msg){
	msg->wr_index = 0;
	msg->state = MSG_STATE_IDLE;
}

/*
 * Not a whole lot to do here.
 */
void MessageInit(struct Message * msg, StreamBufferHandle_t tx_stream, StreamBufferHandle_t rx_stream){
	if(tx_stream != NULL)
		msg->tx_stream = tx_stream;
	else
		msg->tx_stream = xStreamBufferCreate(MAX_MESSAGE_LEN,1);

	if(rx_stream != NULL)
		msg->rx_stream = rx_stream;
	else
		msg->rx_stream = xStreamBufferCreate(MAX_MESSAGE_LEN,1);

	MessageClear(msg);
}

int MessageSendFormat(struct Message * msg, const char * fmt,...){
	va_list args;
	int ret;
	int i,checksum;

	sprintf(msg->response,"$" TALKER_ID ",");
	va_start(args,fmt);
	ret = vsnprintf(&msg->response[strlen(msg->response)],MAX_MESSAGE_LEN,fmt,args);
	va_end(args);
	ret = strlen(msg->response);
	//now we have to calculate the checksum of everything after the '$'
	for(i=1,checksum=0;i<ret;i++) checksum ^= msg->response[i];
	//put in the checksum and final delimiters
	sprintf(&msg->response[ret], "*%02X\r\n",checksum);
	ret = strlen(msg->response);
	xStreamBufferSend(msg->tx_stream,(void*)msg->response,strlen(msg->response),portMAX_DELAY);
	return ret;
}

int MessageCheckReady(struct Message * msg){
	if(msg->state == MSG_STATE_READY) return 1;
	return 0;
}

/*
 * add 'len' bytes of 'data' into our message.  The function returns when either 'len' bytes have been
 * parsed, or when a full message is ready.  The function returns the number of bytes actually processed.
 * If the number of bytes is less than 'len', then a message is ready to be used. f this function is called
 * again after a message is ready, it will return zero. To clear the completed state and parse more data,
 * call MessageClear() before calling this function again.
 */
int MessageProcess(struct Message * msg, unsigned char * data, int len){
	int i;

	for(i=0;i<len;i++){
		switch(msg->state){
			case MSG_STATE_IDLE:
				if(data[i] == '$') msg->state = MSG_STATE_FOUND_START;
				break;
			case MSG_STATE_FOUND_START:
				if(data[i] == '$'){
					MessageClear(msg);
					i--;	//re-do this character
					continue;
				}

				if(data[i] == '\r' || data[i] == '\n'){
					msg->state = MSG_STATE_READY;
					msg->message[msg->wr_index] = '\0';	//null terminate
				}else if(data[i] == '\b'){
					if(msg->wr_index != 0) msg->wr_index--;
				}else if(msg->wr_index < MAX_MESSAGE_LEN){
					msg->message[msg->wr_index++]=toupper(data[i]);
				}

				break;
			case MSG_STATE_READY:
				return i;
				break;
		}
	}
	return i;
}


