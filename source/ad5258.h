/*
 * ad5258.h
 *
 *  Created on: Mar. 31, 2026
 *      Author: slocke
 */

#ifndef AD5258_H_
#define AD5258_H_
#include <stdint.h>
#include "peripherals.h"

struct ad5258 {
	uint8_t address;
	i2c_rtos_handle_t * i2c_dev;
	float tolerance;
};

int ad5258_load_tolerance(struct ad5258 * dev);
int ad5258_read_rdac(struct ad5258 * dev, uint8_t * val);
int ad5258_write_rdac(struct ad5258 * dev, uint8_t val);
int ad5258_set_wp(struct ad5258 * dev, uint8_t wp);

#endif /* AD5258_H_ */
