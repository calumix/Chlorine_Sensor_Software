/*
 * adc.c
 *
 *  Created on: Mar. 31, 2026
 *      Author: slocke
 */

#include "adc.h"
#include "pin_mux.h"
#include "fsl_gpio.h"

static void _adc_chan(struct adc_task_params * cfg,uint8_t chan){
	switch(chan){
	case 0:
		GPIO_PinWrite(GPIO,cfg->ch_sel0_port,cfg->ch_sel0_pin,0);
		GPIO_PinWrite(GPIO,cfg->ch_sel1_port,cfg->ch_sel1_pin,0);
		break;
	case 1:
		GPIO_PinWrite(GPIO,cfg->ch_sel0_port,cfg->ch_sel0_pin,1);
		GPIO_PinWrite(GPIO,cfg->ch_sel1_port,cfg->ch_sel1_pin,0);
		break;
	case 2:
		GPIO_PinWrite(GPIO,cfg->ch_sel0_port,cfg->ch_sel0_pin,0);
		GPIO_PinWrite(GPIO,cfg->ch_sel1_port,cfg->ch_sel1_pin,1);
		break;
	default:
		GPIO_PinWrite(GPIO,cfg->ch_sel0_port,cfg->ch_sel0_pin,1);
		GPIO_PinWrite(GPIO,cfg->ch_sel1_port,cfg->ch_sel1_pin,1);
		break;
	}

}

/*
 * This task will constantly rotate through all channels in the ADC reading them and
 * generating data messages
 */
void ADCTask(void * params){
	struct adc_task_params * cfg = (struct adc_task_params*)params;
	uint32_t val;
	uint8_t chan=0;
	uint8_t grp=1;
	float f_val;

	/* The first reading will be invalid, becasue we don't know what channel was
	 * selected. Set channel here, and perform a dummy conversion
	 */
	GPIO_PinWrite(GPIO,cfg->group_port,cfg->group_pin,grp);	//set to group A by default
	_adc_chan(cfg,chan);
	mcp3551_read(cfg->dev,cfg->cs_port,cfg->cs_pin,&val);

	/* device should now be set to ch0, and in sleep mode.
	 * We'll need to trigger a conversion start */
	while(1){
		mcp3551_start(cfg->dev,cfg->cs_port,cfg->cs_pin);
		/* We know it takes at least 73 ms for the conversion, so delay at least that long */
		vTaskDelay(pdMS_TO_TICKS(75));
		mcp3551_read(cfg->dev,cfg->cs_port,cfg->cs_pin,&val);

		//convert value
		val &= 0x3FFFFF;	//drop over-range bits. Remainder is signed integer
		f_val = (signed int)(val << 10);
		if(grp){
			f_val *= cfg->group_a_scale;
			f_val += cfg->group_a_offset;
		}else{
			f_val *= cfg->group_b_scale;
			f_val += cfg->group_b_offset;
		}

		//send message
		MessageSendFormat(cfg->msg, "%s,%u,%f",
				grp ? cfg->group_a_name : cfg->group_b_name,
				chan,
				f_val);

		//update to a new channel/group
		if(grp){
			grp = 0;
			if(++chan >= 4)
				chan = 0;
		}else{
			grp = 1;
		}
		GPIO_PinWrite(GPIO,cfg->group_port,cfg->group_pin,grp);
		_adc_chan(cfg,chan);
	}
}
