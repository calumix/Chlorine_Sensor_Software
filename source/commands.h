/*
 * commands.h
 *
 *  Created on: Feb. 3, 2025
 *      Author: slocke
 */

#ifndef COMMANDS_H_
#define COMMANDS_H_

#define MAX_SN_STRING_LEN	12

#define COMMAND_DELIM	","
#define COMMAND_MAX_ARGS	12
#include "FreeRTOS.h"
#include "message.h"
#include "queue.h"
#include "stream_buffer.h"

struct Command {
	const char * str;
	int (*Func)(struct Command * cmd, struct Message * msg, int argc, char * argv[]);
};

void CommandParserInit(QueueHandle_t * message_rx_queue);


#endif /* COMMANDS_H_ */
