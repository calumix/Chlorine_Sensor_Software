/*
 * tca9554.h
 *
 *  Created on: Mar. 31, 2026
 *      Author: slocke
 */

#ifndef TCA9554_H_
#define TCA9554_H_
#include <stdint.h>
#include "peripherals.h"

struct tca9554 {
	uint8_t address;
	i2c_rtos_handle_t * i2c_dev;
};

int tca9554_read(struct tca9554 * dev, uint8_t * io);
int tca9554_write(struct tca9554 * dev, uint8_t io);
int tca9554_direction(struct tca9554 * dev, uint8_t direction);
int tca9554_set(struct tca9554 * dev, uint8_t mask);
int tca9554_clear(struct tca9554 * dev, uint8_t mask);

#endif /* TCA9554_H_ */
