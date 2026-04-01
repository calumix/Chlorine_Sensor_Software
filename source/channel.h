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
};

extern struct range_map ranges[NUM_RANGES];

struct channel_state {
	float voltage;
	enum channel_range range;
};

int ChannelInit(struct channel * ch, uint8_t dig_pot_addr, uint8_t io_exp_addr);
int ChannelSetState(struct channel * ch, const struct channel_state *state,bool isolate);
int ChannelGetState(struct channel * ch, struct channel_state *state);
enum channel_range ChannelRangeUaToNum(uint16_t ua);
uint16_t ChannelRangeNumToUa(enum channel_range r);
#endif /* CHANNEL_H_ */
