/*
 * mcp3551.h
 *
 *  Created on: Mar. 31, 2026
 *      Author: slocke
 */

#ifndef MCP3551_H_
#define MCP3551_H_
#include <stdint.h>
#include "peripherals.h"

struct mcp3551 {
	SemaphoreHandle_t lock;
};

int mcp3551_read(struct mcp3551 * dev,uint32_t gpio_port, uint32_t gpio_pin, int32_t * value);
int mcp3551_start(struct mcp3551 * dev,uint32_t gpio_port, uint32_t gpio_pin);
int mcp3551_init(struct mcp3551 * dev);
#endif /* MCP3551_H_ */
