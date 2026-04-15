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
#include "pin_mux.h"
#include "fsl_gpio.h"
#include "mcp3551.h"
#include <math.h>

#define IO_EXP_LED_Gn		(1<<0)
#define IO_EXP_LED_Rn		(1<<1)
#define IO_EXP_RESETn		(1<<2)
#define IO_EXP_SETn			(1<<3)
#define IO_EXP_POLARITY		(1<<4)
#define IO_EXP_TIA_GAIN_1	(1<<5)
#define IO_EXP_TIA_GAIN_0	(1<<6)

struct range_map ranges[NUM_RANGES]={
		{CH_RANGE_1UA,1,1000000.0},
		{CH_RANGE_10UA,10,100000.0},
		{CH_RANGE_100UA,100,10000.0},
		{CH_RANGE_1MA,1000,1000.0},
};

static const uint8_t dig_pot_addr[NUM_CHANNELS] = {0x18, 0x4c, 0x1a, 0x4e};
static const uint8_t io_exp_addr_9554a[NUM_CHANNELS]  __attribute__((unused)) = {0x38,0x39, 0x3a, 0x3b};
static const uint8_t io_exp_addr_9554[NUM_CHANNELS]  __attribute__((unused)) = {0x20,0x21, 0x22, 0x23};
static struct mcp3551 spi_dev;
static struct adc meas_adc;
static struct adc rtd_adc;

static struct channel channels[NUM_CHANNELS];

#define R_TOP			270.0
#define AV				101.0		//nominal instr amp gain
#define R_AB			10000.0
#define RDAC_COUNTS		64
#define VOLT_SET_REF	1.65

uint16_t ChannelRangeNumToUa(enum channel_range r){
	int i;
	for(i=0;i<NUM_RANGES;i++){
		if(ranges[i].range == r)
			return ranges[i].range_ua;
	}
	return ranges[i].range_ua;
}

enum channel_range ChannelRangeUaToNum(uint16_t ua){
	int i;
	//assume largest range to start.
	uint16_t selected_index=NUM_RANGES-1;
	//loop from second largest (3) down to lowest (1).
	for(i=NUM_RANGES-1;i>0;i--){
		if(ranges[i-1].range_ua < ua){
			return ranges[selected_index].range;	//use currently selected index
		}
		else selected_index = i-1;
	}
	return ranges[selected_index].range;
}

float ChannelRangeNumToScale(enum channel_range r){
	int i;
	for(i=0;i<NUM_RANGES;i++){
		if(ranges[i].range == r)
			return ranges[i].scale;
	}
	return ranges[i].scale;
}

static void _setvoltage(struct channel * ch, float voltage){
	double calc;
	uint8_t rdac;

	/* voltage is +/-, and is set from ~0 to ~20mV. Actual maximum varies
	 * because of tolerance of pot
	 * Voltage is calculated as:
	 * Vset = (+/-1.65 * Rwb/(750k + Rab*(1+tol)))/Av
	 * where Rab is 10k, Rwb is (Rab*rdac/64)
	 *
	 * Vset = (+/-1.65 * (rdac/64) * (1/((750k/Rab) + (1+tol))))/Av
	 * So:
	 * D = (Vset*Av)*64 * (750k/Rab + (1+tol))/(+/- 1.65)
	 */
	calc = voltage * AV * RDAC_COUNTS;
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
	return calc/AV;
}

static void _relay(struct channel * ch, bool isolate){
	uint8_t bit;
	bit = IO_EXP_SETn;
	ch->isolate = 0;
	if(isolate){
		bit = IO_EXP_RESETn;
		ch->isolate = 1;
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

static int32_t _adc_read_chan(struct adc * a, uint8_t grp, uint8_t chan){
	int32_t val;
	xSemaphoreTake(a->lock,portMAX_DELAY);
	//select the correct group
	GPIO_PinWrite(GPIO,a->group_port,a->group_pin,grp);
	//select the correct channel
	switch(chan){
		case 0:
			GPIO_PinWrite(GPIO,a->ch_sel0_port,a->ch_sel0_pin,0);
			GPIO_PinWrite(GPIO,a->ch_sel1_port,a->ch_sel1_pin,0);
			break;
		case 1:
			GPIO_PinWrite(GPIO,a->ch_sel0_port,a->ch_sel0_pin,1);
			GPIO_PinWrite(GPIO,a->ch_sel1_port,a->ch_sel1_pin,0);
			break;
		case 2:
			GPIO_PinWrite(GPIO,a->ch_sel0_port,a->ch_sel0_pin,0);
			GPIO_PinWrite(GPIO,a->ch_sel1_port,a->ch_sel1_pin,1);
			break;
		default:
			GPIO_PinWrite(GPIO,a->ch_sel0_port,a->ch_sel0_pin,1);
			GPIO_PinWrite(GPIO,a->ch_sel1_port,a->ch_sel1_pin,1);
			break;
	}
	//give a short stabilization time
	vTaskDelay(1);

	mcp3551_start(a->dev,a->cs_port,a->cs_pin);
	/* We know it takes at least 73 ms for the conversion, so delay at least that long */
	vTaskDelay(pdMS_TO_TICKS(75));
	mcp3551_read(a->dev,a->cs_port,a->cs_pin,&val);
	xSemaphoreGive(a->lock);

	return val;

}


float ChannelReadVoltage(uint8_t channel_num){
	float f;

	if(channels[channel_num].isolate){
		return 0;
	}

	f = _adc_read_chan(&meas_adc,MEAS_ADC_VOLT_GROUP,channel_num);

	//convert value
	f *= 3.3/2097151.0/AV;

	return f;
}

float ChannelReadCurrent(uint8_t channel_num){
	float f;

	if(channels[channel_num].isolate){
		return 0;
	}

	f = _adc_read_chan(&meas_adc,MEAS_ADC_CURR_GROUP,channel_num);

	//convert value
	f *= -3.3/2097151.0;	//Due to TIA inversion, current is always negative of what is real
	f /= ChannelRangeNumToScale(_getrange(&channels[channel_num]));

	return f;
}

float ChannelReadResistance(uint8_t channel_num, float * voltage, float * current){
	float v,c;
	if(channels[channel_num].isolate){
		return NAN;
	}

	v = ChannelReadVoltage(channel_num);
	c = ChannelReadCurrent(channel_num);
	if(voltage != NULL) *voltage = v;
	if(current != NULL) *current = c;

	return v/c;
}

float ChannelReadRTD(uint8_t channel_num){
	float v_rtd;
	float v_ref;
	v_rtd = _adc_read_chan(&rtd_adc,RTD_ADC_RTD_GROUP,channel_num);
	v_ref = _adc_read_chan(&rtd_adc,RTD_ADC_REF_GROUP,channel_num);

	return ((v_rtd * 3000.0)/(v_ref * 100.0 * (1+100/3.4))-1)/0.00385;
}


int ChannelGetState(uint8_t channel_num, struct channel_state *state){
	if(state != NULL){
		state->range = _getrange(&channels[channel_num]);
		state->voltage = _getvoltage(&channels[channel_num]);
	}
	return channels[channel_num].isolate;
}

void ChannelSetState(uint8_t channel_num, const struct channel_state *state,bool isolate){
	_setvoltage(&channels[channel_num],state->voltage);
	_setrange(&channels[channel_num],state->range);
	if(isolate != channels[channel_num].isolate)
		_relay(&channels[channel_num],isolate);
}

void ChannelAutoRange(uint8_t channel_num, float max_voltage, float max_resistance){
	struct channel_state s;

	s.voltage = max_voltage;
	s.range = ChannelRangeUaToNum(max_voltage * 1000000.0 * 1.05 / max_resistance);
	ChannelSetState(channel_num,&s,0);

}

void ChannelInit(void){
	int i;
	int32_t val;

	/* We share the SPI device between both the measurement and RTD adc, so only one init */
	mcp3551_init(&spi_dev);

	meas_adc.dev = &spi_dev;
	meas_adc.cs_port = BOARD_INITPINS_MEASURE_CSn_PORT;
	meas_adc.cs_pin = BOARD_INITPINS_MEASURE_CSn_PIN;
	meas_adc.ch_sel0_port = BOARD_INITPINS_MEASURE_CH_SEL0_PORT;
	meas_adc.ch_sel0_pin =  BOARD_INITPINS_MEASURE_CH_SEL0_PIN;
	meas_adc.ch_sel1_port = BOARD_INITPINS_MEASURE_CH_SEL1_PORT;
	meas_adc.ch_sel1_pin =  BOARD_INITPINS_MEASURE_CH_SEL1_PIN;
	meas_adc.group_port = BOARD_INITPINS_VOLT_CURRn_PORT;
	meas_adc.group_pin = BOARD_INITPINS_VOLT_CURRn_PIN;
	meas_adc.lock = xSemaphoreCreateMutex();

	rtd_adc.dev = &spi_dev;
	rtd_adc.cs_port = BOARD_INITPINS_RTD_CSn_PORT;
	rtd_adc.cs_pin = BOARD_INITPINS_RTD_CSn_PIN;
	rtd_adc.ch_sel0_port = BOARD_INITPINS_RTD_CH_SEL0_PORT;
	rtd_adc.ch_sel0_pin =  BOARD_INITPINS_RTD_CH_SEL0_PIN;
	rtd_adc.ch_sel1_port = BOARD_INITPINS_RTD_CH_SEL1_PORT;
	rtd_adc.ch_sel1_pin =  BOARD_INITPINS_RTD_CH_SEL1_PIN;
	rtd_adc.group_port = BOARD_INITPINS_RTD_REFn_PORT;
	rtd_adc.group_pin = BOARD_INITPINS_RTD_REFn_PIN;
	rtd_adc.lock = xSemaphoreCreateMutex();

	//perform one read to get the ADC into the sleep state.
	mcp3551_read(&spi_dev,meas_adc.cs_port,meas_adc.cs_pin,&val);
	mcp3551_read(&spi_dev,rtd_adc.cs_port,rtd_adc.cs_pin,&val);

	//TODO: auto-detect if we're using the 9554 or 9554a port expander
	for(i=0;i<NUM_CHANNELS;i++){
		channels[i].io_exp.address = io_exp_addr_9554[i];
		channels[i].io_exp.i2c_dev = &FLEXCOMM_I2C_rtosHandle;
		channels[i].pot.address = dig_pot_addr[i];
		channels[i].pot.i2c_dev =  &FLEXCOMM_I2C_rtosHandle;

		tca9554_write(&channels[i].io_exp,IO_EXP_LED_Gn | IO_EXP_LED_Rn | IO_EXP_RESETn | IO_EXP_SETn);
		tca9554_direction(&channels[i].io_exp,0x80);
		_relay(&channels[i],1);

		ad5258_load_tolerance(&channels[i].pot);
		ad5258_set_wp(&channels[i].pot, 0);

		_setvoltage(&channels[i],0.0);

		tca9554_clear(&channels[i].io_exp, IO_EXP_LED_Gn);
	}

}


