/*
 * channel.c
 *
 *  Created on: Mar. 31, 2026
 *      Author: slocke
 *
 *  This has all the required functions to control the state of a channel
 *
 */
#include "peripherals.h"
#include "FreeRTOS.h"
#include "channel.h"
#include "env.h"

#define IO_EXP_LED_Gn		(1<<0)
#define IO_EXP_LED_Rn		(1<<1)
#define IO_EXP_RESETn		(1<<2)
#define IO_EXP_SETn			(1<<3)
#define IO_EXP_POLARITY		(1<<4)
#define IO_EXP_TIA_GAIN_1	(1<<5)
#define IO_EXP_TIA_GAIN_0	(1<<6)

#define R_TOP			750000.0
#define R_AB			10000.0
#define RDAC_COUNTS		64
#define VOLT_SET_REF	1.65

static void _setvoltage(struct channel * ch, float voltage){
	double calc;
	uint8_t rdac;

	/* voltage is +/-, and is set from ~0 to ~20mV. Actual maximum varies
	 * because of tolerance of pot
	 * Voltage is calculated as:
	 * Vset = +/-1.65 * Rwb/(750k + Rab*(1+tol))
	 * where Rab is 10k, Rwb is (Rab*rdac/64)
	 *
	 * Vset = +/-1.65 * (rdac/64) * (1/((750k/Rab) + (1+tol)))
	 * So:
	 * D = Vset*64 * (750k/Rab + (1+tol))/(+/- 1.65)
	 */
	calc = voltage * RDAC_COUNTS;
	calc *= R_TOP/R_AB + (1+ch->pot.tolerance);
	calc /= VOLT_SET_REF;

	if(calc < 0){
		calc *= -1;
		tca9554_set(&ch->io_exp,IO_EXP_POLARITY);
	}else{
		tca9554_clear(&ch->io_exp,IO_EXP_POLARITY);
	}
	rdac = calc + 0.5;
	if(rdac >= RDAC_COUNTS) rdac = RDAC_COUNTS-1;
	ad5258_write_rdac(&ch->pot,rdac);
}

static float _getvoltage(struct channel * ch){
	uint8_t rdac;
	double calc;

	tca9554_read(&ch->io_exp,&rdac);
	calc = VOLT_SET_REF;
	if(rdac & IO_EXP_POLARITY){
		calc *= -1;
	}

	ad5258_read_rdac(&ch->pot,&rdac);
	calc *= rdac;
	calc /= RDAC_COUNTS;
	calc /= R_TOP/R_AB + (1+ch->pot.tolerance);
	return calc;
}

static void _relay(struct channel * ch, bool isolate){
	uint8_t bit;
	bit = IO_EXP_SETn;
	ch->isolate = 1;
	if(isolate){
		bit = IO_EXP_RESETn;
		ch->isolate = 0;
	}

	tca9554_clear(&ch->io_exp,bit);
	vTaskDelay(DIV_ROUND_UP(20,portTICK_PERIOD_MS));
	tca9554_set(&ch->io_exp,bit);
}

static void _setrange(struct channel * ch, enum channel_range range){
	switch(range){
		case CH_RANGE_1MA:
			tca9554_clear(&ch->io_exp, IO_EXP_TIA_GAIN_1 | IO_EXP_TIA_GAIN_0);
			break;
		case CH_RANGE_100UA:
			tca9554_clear(&ch->io_exp, IO_EXP_TIA_GAIN_1);
			tca9554_set(&ch->io_exp, IO_EXP_TIA_GAIN_0);
			break;
		case CH_RANGE_10UA:
			tca9554_set(&ch->io_exp, IO_EXP_TIA_GAIN_1);
			tca9554_clear(&ch->io_exp, IO_EXP_TIA_GAIN_0);
			break;
		case CH_RANGE_1UA:
			tca9554_set(&ch->io_exp, IO_EXP_TIA_GAIN_1 | IO_EXP_TIA_GAIN_0);
			break;
	}
}

static enum channel_range _getrange(struct channel * ch){
	uint8_t io;
	tca9554_read(&ch->io_exp,&io);

	if((io & IO_EXP_TIA_GAIN_0) && (io & IO_EXP_TIA_GAIN_1))
		return CH_RANGE_1UA;
	else if((io & IO_EXP_TIA_GAIN_1))
		return CH_RANGE_10UA;
	else if((io & IO_EXP_TIA_GAIN_0))
		return CH_RANGE_100UA;

	return CH_RANGE_1MA;

}

int ChannelGetState(struct channel * ch, struct channel_state *state){
	state->range = _getrange(ch);
	state->voltage = _getvoltage(ch);
	return 1;
}

int ChannelSetState(struct channel * ch, const struct channel_state *state,bool isolate){
	_setvoltage(ch,state->voltage);
	_setrange(ch,state->range);
	if(isolate != ch->isolate)
		_relay(ch,isolate);
	return 1;
}
int ChannelInit(struct channel * ch, uint8_t dig_pot_addr, uint8_t io_exp_addr){
	ch->io_exp.address = io_exp_addr;
	ch->io_exp.i2c_dev = &FLEXCOMM_I2C_rtosHandle;
	tca9554_write(&ch->io_exp,IO_EXP_LED_Gn | IO_EXP_LED_Rn | IO_EXP_RESETn | IO_EXP_SETn);
	tca9554_direction(&ch->io_exp,0x80);

	_relay(ch,1);

	ch->pot.address = io_exp_addr;
	ch->pot.i2c_dev = &FLEXCOMM_I2C_rtosHandle;
	ad5258_load_tolerance(&ch->pot);
	_setvoltage(ch,0.0);



	return 1;

}


