/*
 * message.h
 *
 *  Created on: Feb. 3, 2025
 *      Author: slocke
 */

#ifndef MESSAGE_H_
#define MESSAGE_H_

#include "FreeRTOS.h"
#include "queue.h"
#include "stream_buffer.h"

#ifndef ARRAY_SIZE
	#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif

#define MAX_MESSAGE_LEN 128
#define MAX_HEADER_FOOTER_LEN (4+2+6+1)	//includes length of talker ID, $, commas, CS and CR/LF
//FIXME: should this be a different TX and RX talker ID?? SD (depth sounder) for TX, and PST (proprietary sensor-tech for RX)?
#define TALKER_ID 	"SD"
enum Message_State {
	MSG_STATE_IDLE,
	MSG_STATE_FOUND_START,
	MSG_STATE_READY,
};

/*
 * Accumulates a messaged from one of the communications interfaces which is then passed
 * to the parser.
 */

struct Message{
	int wr_index;
	enum Message_State state;
	StreamBufferHandle_t rx_stream;
	QueueHandle_t tx_queue;
	char message[MAX_MESSAGE_LEN+1];	//+1 for null termination
	char response[MAX_MESSAGE_LEN+MAX_HEADER_FOOTER_LEN+1];
};

int MessageSendFormat(struct Message * msg, const char * fmt,...);
void MessageInit(struct Message * msg);
int MessageProcess(struct Message * msg, unsigned char * data, int len);
void MessageClear(struct Message * msg);
int MessageCheckReady(struct Message * msg);


#endif /* MESSAGE_H_ */
