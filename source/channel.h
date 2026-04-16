/*
 * channel.h
 *
 *  Created on: Mar. 31, 2026
 *      Author: slocke
 */

#ifndef CHANNEL_H_
#define CHANNEL_H_
#include <stdint.h>
#include "tca9554.h"
#include "ad5258.h"

/*
 * This wraps the mcp3551 + multiplexor options, and prevents multiple access
 */

enum {
	MEAS_ADC_VOLT_GROUP = 1,
	MEAS_ADC_CURR_GROUP = 0,
};

enum {
	RTD_ADC_RTD_GROUP = 1,
	RTD_ADC_REF_GROUP = 0,
};


struct adc {
	struct mcp3551 * dev;

	SemaphoreHandle_t lock;	//protects objects below

	uint32_t cs_port;
	uint32_t cs_pin;

	uint32_t ch_sel0_port;
	uint32_t ch_sel0_pin;
	uint32_t ch_sel1_port;
	uint32_t ch_sel1_pin;

	uint32_t group_port;
	uint32_t group_pin;
};


struct channel {
	struct tca9554 io_exp;
	struct ad5258 pot;
	bool isolate;
};

#define NUM_CHANNELS 4
#define NUM_RANGES	4

enum channel_range {
	CH_RANGE_1UA=0,
	CH_RANGE_10UA=1,
	CH_RANGE_100UA=2,
	CH_RANGE_1MA=3,
};

struct range_map {
	enum channel_range range;
	uint16_t range_ua;
	float scale;
};

extern struct range_map ranges[NUM_RANGES];

struct channel_state {
	float voltage;
	enum channel_range range;
};

void ChannelInit(void);
void ChannelSetState(uint8_t channel_num, const struct channel_state *state,bool isolate);
int ChannelGetState(uint8_t channel_num, struct channel_state *state);
enum channel_range ChannelRangeUaToNum(uint16_t ua);
uint16_t ChannelRangeNumToUa(enum channel_range r);
float ChannelRangeNumToScale(enum channel_range r);
float ChannelReadVoltage(uint8_t channel_num);
float ChannelReadCurrent(uint8_t channel_num);
float ChannelReadResistance(uint8_t channel_num, float * voltage, float * current);
float ChannelReadRTD(uint8_t channel_num);
void ChannelAutoRange(uint8_t channel_num, float max_voltage, float max_resistance);

#endif /* CHANNEL_H_ */
