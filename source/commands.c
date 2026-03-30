/*
 * commands.c
 *
 *  Created on: Feb. 3, 2025
 *      Author: slocke
 */
//#include <string.h>
#include <stdlib.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "commands.h"
#include "env.h"
#include "message.h"
#include "peripherals.h"
#include <math.h>


/*
 * Echos the first argument back (for testing)
 */
int EchoCommand(struct Command * cmd,struct Message * msg, int argc, char * argv[]){
	if(argc >= 2) MessageSendFormat(msg, "%s,%s",cmd->str, argv[1]);
	else return -1;
	return 0;
}

/*
 * ENV,SAVE
 * ENV,DEFAULT
 * ENV,NMEA_BAUD,<baud>
 * ENV,ECHO_BAUD,<baud>
 * ENV,THERMISTOR,<r25>,<a>,<b>,<c>,<d>
 */

int EnvCommand(struct Command * cmd,struct Message * msg, int argc, char * argv[]){
	if(strcmp(argv[1],"SAVE")==0){
		EnvWrite();
		MessageSendFormat(msg, "%s,%s,OK",cmd->str,argv[1]);
		return 0;
	}

	if(strcmp(argv[1],"DEFAULT")==0){
		EnvSetDefaults();
		MessageSendFormat(msg, "%s,%s,OK",cmd->str,argv[1]);
		return 0;
	}

	if(strcmp(argv[1],"LOAD")==0){
		EnvInit();
		MessageSendFormat(msg, "%s,%s,%s",cmd->str,argv[1],env.flags&ENV_FLAGS_DEFAULT ? "DEF" : "OK");
		return 0;
	}

	if(strcmp(argv[1],"NMEA_BAUD")==0 && (argc == 3 || argv[2][0] == '?' || argc == 2)){
		if(argc == 3){
			env.nmea_baud = strtoul(argv[2],NULL,10);
		}
		MessageSendFormat(msg, "%s,%s,%i",cmd->str,argv[1],
					env.nmea_baud);
		return 0;
	}
	if(strcmp(argv[1],"ECHO_BAUD")==0 && (argc == 3 || argv[2][0] == '?' || argc == 2)){
		if(argc == 3){
			env.echo_baud = strtoul(argv[2],NULL,10);
		}
		MessageSendFormat(msg, "%s,%s,%i",cmd->str,argv[1],
					env.echo_baud);
		return 0;
	}

	if(strcmp(argv[1],"THERMISTOR")==0 && (argc == 7 || argv[2][0] == '?' || argc == 2)){
		if(argc == 7){
			env.thermistor_r25 = strtof(argv[2],NULL);
			env.thermistor_a = strtof(argv[3],NULL);
			env.thermistor_b = strtof(argv[4],NULL);
			env.thermistor_c = strtof(argv[5],NULL);
			env.thermistor_d = strtof(argv[6],NULL);
		}
		MessageSendFormat(msg, "%s,%s,%f,%f,%f,%f,%f",cmd->str,argv[1],
					env.thermistor_r25,
					env.thermistor_a,
					env.thermistor_b,
					env.thermistor_c,
					env.thermistor_d);
		return 0;
	}

	if(strcmp(argv[1],"SOS")==0 && (argc == 3 || argv[2][0] == '?' || argc == 2)){
		if(argc == 3){
			env.speed_of_sound = strtof(argv[2],NULL);
		}
		MessageSendFormat(msg, "%s,%s,%f",cmd->str,argv[1],
					env.speed_of_sound);
		return 0;
	}

	if(strcmp(argv[1],"TOFFSET")==0 && (argc == 3 || argv[2][0] == '?' || argc == 2)){
		if(argc == 3){
			env.transducer_offset = strtof(argv[2],NULL);
		}
		MessageSendFormat(msg, "%s,%s,%f",cmd->str,argv[1],
					env.transducer_offset);
		return 0;
	}

	if(strcmp(argv[1],"THRESH_FILT_SCALE")==0 && (argc == 3 || argv[2][0] == '?' || argc == 2)){
		if(argc == 3){
			env.threshold_filter_scale = strtof(argv[2],NULL);
		}
		MessageSendFormat(msg, "%s,%s,%f",cmd->str,argv[1],
					env.threshold_filter_scale);
		return 0;
	}

	MessageSendFormat(msg, "%s,?",cmd->str);

	return 0;
}



#define xstr(a) str(a)
#define str(a) #a
int VerCommand(struct Command * cmd,struct Message * msg,int argc, char * argv[]){
	//MessageSendFormat(reply_stream, "%s," xstr(FW_VERSION),cmd->str);
	MessageSendFormat(msg, "%s," "TEST",cmd->str);
	return 0;
}


/*
 * Set parameters of the ping
 * PING,start
 * Ping,stop
 * ping,len,<microseconds>
 * ping,per,<microseconds>
 * ping,lna,[10,12.5,15,20]
 * ping,power,[0|1]
 * ping,q,[2-5]
 */
const float lna_values[] = {
		15, 10, 20, 12.5
};
const uint8_t q_values[] = {
		4,5,2,3
};

/*
 * To write SN:
 * SN,W,ABC123,<serial_number>
 * To read SN:
 * SN
 */
/*
int SNCommand(struct Command * cmd,StreamBufferHandle_t reply_stream,int argc, char * argv[]){
	if(argc == 4 && argv[1][0] == 'W' && strcmp(argv[2],"ABC123")==0){
		strncpy(env.sn,argv[3],ENV_MAX_SN_LEN-1);
	}

	MessageSendFormat(reply_stream, "%s,%s",cmd->str,env.sn);

	return 0;
}
*/

/*
 * This table has our message commands and the function to execute for each
 */
static struct Command commands[] = {

		{"ECHO",EchoCommand},
		{"VER",VerCommand},
		{"ENV",EnvCommand},
		//{"SN",SNCommand},

};

static void HandleMessage(struct Message * msg){
	char * argv[COMMAND_MAX_ARGS];
	char * id;
	int argc;
	int i;

	//The first argument is the Talker ID of the board.  Does it match ours?
	id = strtok(msg->message,COMMAND_DELIM);
	if(id == NULL || *id == '\0') return;	//no ID!

	if(strcmp(id,TALKER_ID) != 0) return;	//message not for us

	argc=0;
	do{
		argv[argc] = strtok(NULL,COMMAND_DELIM);
	}while(argv[argc++] != NULL && argc < COMMAND_MAX_ARGS);
	argc--;
	for(i=0;i<ARRAY_SIZE(commands);i++){
		if(strncmp(argv[0],commands[i].str,strlen(commands[i].str)) == 0){
			//match
			if(commands[i].Func != NULL){
				if(commands[i].Func(&commands[i],msg, argc, argv)){
					MessageSendFormat(msg,"ERROR,COMMAND PARSE ERROR");
				}
			}
			break;//out of FOR loop
		}
	}
	if(i==ARRAY_SIZE(commands)) MessageSendFormat(msg,"ERROR,INVALID COMMAND,'%s'",argv[0]);
}


void CommandParserTask(void*params){
	QueueHandle_t * msg_queue = (QueueHandle_t*)params;
	struct Message msg;

	while(1){
		xQueueReceive(*msg_queue,&msg,portMAX_DELAY);
		HandleMessage(&msg);
	}
}

void CommandParserInit(QueueHandle_t * message_rx_queue){
	*message_rx_queue = xQueueCreate(3,sizeof(struct Message));
	vQueueAddToRegistry(*message_rx_queue,"CMD");
	xTaskCreate(CommandParserTask,"CMD",384,message_rx_queue,1,NULL);

}


