/*
 * env.h
 *
 *  Created on: Feb. 18, 2025
 *      Author: slocke
 */

#ifndef ENV_H_
#define ENV_H_

enum {
	ENV_FLAGS_8BIT_ECHO = (1<<0),	//if set, serial echogram data is sent as 8-bit values, otherwise 16-bit
	ENV_FLAGS_DEFAULT = (1<<31),	//set if the environment is default (crc failed)
};

#include <stdint.h>

/*
 * make sure structure elements are aligned to their native length!
 */
typedef struct _environment {
	uint32_t uart_baud;
	uint32_t flags;
	uint16_t crc_ccit;
}__attribute__((packed)) environment_t;

#ifndef EXCLUDE_ENV
extern environment_t env;
#endif

void EnvInit(void);
void EnvSetDefaults(void);
void EnvWrite(void);

#endif /* ENV_H_ */
