/*
 * adc.h
 *
 *  Created on: Mar. 31, 2026
 *      Author: slocke
 */

#ifndef ADC_H_
#define ADC_H_

#include "peripherals.h"
#include "message.h"
#include "mcp3551.h"

#define CH_NAME_LEN	8

struct adc_task_params {
	struct Message * msg;	//handle to a message api that will format our responses.
	uint32_t cs_port;
	uint32_t cs_pin;
	uint32_t ch_sel0_port;
	uint32_t ch_sel0_pin;
	uint32_t ch_sel1_port;
	uint32_t ch_sel1_pin;
	struct mcp3551 *dev;
	uint32_t group_port;
	uint32_t group_pin;

	/* TODO: instead of this, add a callback function so we can combine readings in RTD case */
	char group_a_name[CH_NAME_LEN];
	float group_a_scale;
	float group_a_offset;

	char group_b_name[CH_NAME_LEN];
	float group_b_scale;
	float group_b_offset;
};

void ADCTask(void * params);

#endif /* ADC_H_ */
