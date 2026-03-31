/*
 * channel.h
 *
 *  Created on: Mar. 31, 2026
 *      Author: slocke
 */

#ifndef CHANNEL_H_
#define CHANNEL_H_

#include "tca9554.h"
#include "ad5258.h"

struct channel {
	struct tca9554 io_exp;
	struct ad5258 pot;
	bool isolate;
};

enum channel_range {
	CH_RANGE_1MA,
	CH_RANGE_100UA,
	CH_RANGE_10UA,
	CH_RANGE_1UA
};

struct channel_state {
	float voltage;
	enum channel_range range;
};

int ChannelInit(struct channel * ch, uint8_t dig_pot_addr, uint8_t io_exp_addr);
int ChannelSetState(struct channel * ch, const struct channel_state *state,bool isolate);
int ChannelGetState(struct channel * ch, struct channel_state *state);

#endif /* CHANNEL_H_ */
