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
#include "tuss4470.h"
#include "peripherals.h"
#include <math.h>
static QueueHandle_t ping_queue;


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
	/*
	 * TODO: Add support for 8-bit echogram data
	if(strcmp(argv[1],"EG_8BIT")==0 && (argc == 3 || argv[2][0] == '?' || argc == 2)){
		if(argc == 3){
			if(argv[2][0] == '1') env.flags |= ENV_FLAGS_8BIT_ECHO;
			else env.flags &= ~ENV_FLAGS_8BIT_ECHO;
		}
		MessageSendFormat(msg, "%s,%s,%s",cmd->str,argv[1],
					(env.flags & ENV_FLAGS_8BIT_ECHO) ? "1":"0");
		return 0;
	}
	*/

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

int RegCommand(struct Command * cmd,struct Message * msg,int argc, char * argv[]){
	uint8_t reg=0xFF;
	uint8_t data,status;

	if(argc >= 2){
		//read
		reg = strtoul(argv[1],NULL,0);
		if(argc == 3){
			//write
			data = strtoul(argv[2],NULL,0);
			tuss4470_write(reg,data,NULL);
		}
	}

	if(reg != 0xFF){
		//perform read
		tuss4470_read(reg,&data,&status);
	}
	MessageSendFormat(msg,"%s,0x%x,0x%x,0x%x",cmd->str,reg,data,status);

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
int PingCommand(struct Command * cmd,struct Message * msg,int argc, char * argv[]){
	ping_cur_cfg_t cfg;
	ping_cfg_t ping_cmd;
	float f,e;
	int i;

	ping_cmd.cmd = PING_NOP;

	if(argc >= 2){
		if(strcmp(argv[1],"START")==0){
			ping_cmd.cmd = PING_START;
		}else if(strcmp(argv[1],"STOP")==0){
			ping_cmd.cmd = PING_STOP;
		}else if(strcmp(argv[1],"LEN")==0 && argc == 3){
			ping_cmd.params[0]=strtoul(argv[2],NULL,10);
			ping_cmd.cmd = PING_SET_LEN;
		}else if(strcmp(argv[1],"PER")==0 && argc == 3){
			ping_cmd.params[0]=strtoul(argv[2],NULL,10);
			ping_cmd.cmd = PING_SET_PERIOD;
		}else if(strcmp(argv[1],"LNA")==0 && argc == 3){
			f = strtof(argv[2],NULL);
			//find closest LNA value
			ping_cmd.params[0] = 0;
			e = 100000000;
			for(i=0;i<ARRAY_SIZE(lna_values);i++){
				if(fabs(lna_values[i] - f) < e){
					e = fabs(lna_values[i] - f);
					ping_cmd.params[0] = i;
				}
			}
			ping_cmd.cmd = PING_SET_LNA;
		}else if(strcmp(argv[1],"POWER")==0 && argc == 3){
			ping_cmd.params[0] = strtoul(argv[2],NULL,10);
			ping_cmd.cmd = PING_SET_POWER;
		}else if(strcmp(argv[1],"Q")==0 && argc == 3){
			f = strtof(argv[2],NULL);
			//find closest LNA value
			ping_cmd.params[0] = 0;
			e = 100000000;
			for(i=0;i<ARRAY_SIZE(q_values);i++){
				if(fabs(q_values[i] - f) < e){
					e = fabs(q_values[i] - f);
					ping_cmd.params[0] = i;
				}
			}
			ping_cmd.params[0] <<= 4;
			ping_cmd.cmd = PING_SET_Q;
		}

		if(ping_cmd.cmd != PING_NOP){
			xQueueSend(ping_queue,(void*)&ping_cmd,portMAX_DELAY);
		}
	}
	/* Return current config */
	TUSS4470GetConfig(&cfg);
	MessageSendFormat(msg,"%s,%s,%uus,%uus,%0.1f,%s,%u",
			cmd->str,
			cfg.running ? "running" : "stopped",
					cfg.len,
					cfg.period,
					lna_values[cfg.lna],
					cfg.power ? "hp" : "lp",
					q_values[cfg.q>>4]);

	return 0;
}

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
		{"PING",PingCommand},
		{"REG",RegCommand},
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

void CommandParserInit(QueueHandle_t * message_rx_queue, QueueHandle_t ping_cfg_queue){
	*message_rx_queue = xQueueCreate(3,sizeof(struct Message));
	ping_queue = ping_cfg_queue;
	vQueueAddToRegistry(*message_rx_queue,"CMD");
	xTaskCreate(CommandParserTask,"CMD",384,message_rx_queue,1,NULL);

}


