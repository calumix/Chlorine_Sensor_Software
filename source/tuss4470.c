/*
 * tuss4470.c
 *
 *  Created on: Jan. 29, 2025
 *      Author: slocke
 */
#include <echogram.h>
#include "peripherals.h"
#include "pin_mux.h"
#include "fsl_gpio.h"
#include "FreeRTOS.h"
#include "task.h"
#include "tuss4470.h"
volatile uint32_t pulse_count;

#define LOGAMP_ITCPT_ADJ_COUNT	16
const float logamp_int_adjust[LOGAMP_ITCPT_ADJ_COUNT] = {
	1,
	1.155,
	1.334,
	1.54,
	1.778,
	2.054,
	2.371,
	2.738,
	1,
	0.931,
	0.866,
	0.806,
	0.75,
	0.698,
	0.649,
	0.604,
};

volatile uint32_t ping_length_us;
volatile uint32_t ping_period_us;


/*
 * Returns 0 if data has even parity, or 1 if it has odd parity.
 */
static uint8_t calc_parity(uint16_t data){
	data ^= data >> 1;
	data ^= data >> 2;
	data ^= data >> 4;
	data ^= data >> 8;
	return data & 1;
}

void tuss4470_read(tuss4470_regs_t reg, uint8_t * data, uint8_t * rx_status){
	spi_transfer_t xfer;
	uint16_t cmd, resp;

	cmd = ((0x3F&reg)<<9) | (1<<15);
	if(!calc_parity(cmd)) cmd |= 1<<8;	//if not already odd, make it odd

	xfer.configFlags = kSPI_FrameAssert;
	xfer.dataSize = 2;
	xfer.txData = (uint8_t*)&cmd;
	xfer.rxData = (uint8_t*)&resp;

	SPI_RTOS_Transfer(&FLEXCOMM_SPI_rtosHandle, &xfer);

	//the result should also have odd parity
	configASSERT(calc_parity(resp));

	if(data != NULL) *data = resp & 0x00FF;

	if(rx_status != NULL) *rx_status = (resp>>9)&0x3F;
}

void tuss4470_write(tuss4470_regs_t reg, uint8_t data, uint8_t * rx_status){
	spi_transfer_t xfer;
	uint16_t cmd, resp;

	cmd = ((0x3F&reg)<<9) | data;
	if(!calc_parity(cmd)) cmd |= 1<<8;	//if not already odd, make it odd

	xfer.configFlags = kSPI_FrameAssert;
	xfer.dataSize = 2;
	xfer.txData = (uint8_t*)&cmd;
	xfer.rxData = (uint8_t*)&resp;

	SPI_RTOS_Transfer(&FLEXCOMM_SPI_rtosHandle, &xfer);

	//the result should also have odd parity
	configASSERT(calc_parity(resp));

	//the result should have r/w bit and register address as we've sent
	configASSERT((cmd>>9) == ((resp&0x00FF)>>1));

	if(rx_status != NULL) *rx_status = (resp>>9)&0x3F;
}

/*
 * Configure the SCT to perform bursting and ADC sampling
 */
#define ENABLE_STATE(x) (1<<(x))
#define ENABLE_EVENT(x) (1<<(x))
static void sct_config(void){
	uint8_t deadband;
    /* Enable the SCTimer clock*/
    CLOCK_EnableClock(kCLOCK_Sct0);
    RESET_PeripheralReset(kSCT0_RST_SHIFT_RSTn);

	SCT0->CTRL = SCT_CTRL_HALT_L(1) | SCT_CTRL_CLRCTR_L(1);	//stop/clear timer (for now)
	SCT0->CONFIG = SCT_CONFIG_UNIFY(1) | SCT_CONFIG_CLKMODE(0);
	SCT0->OUTPUT = 0x03;	//start with both outputs high

	/*
	 * We use CT0-MAT0 and ARM_TXEV (SEV instruction) as input events. We route these to SCTIN0/1 (respectively)
	 */
	CLOCK_EnableClock(kCLOCK_InputMux);
	INPUTMUX->SCT0_INMUX[0] = 8;	//select CT0-MAT0 as SCT_IN0
	INPUTMUX->SCT0_INMUX[1] = 0x16;	//select ARM_TXEV as SCT_IN1
	INPUTMUX->SCT0_INMUX[5] = 5;	//select SCT_GPI5 as SCT_IN5
	CLOCK_DisableClock(kCLOCK_InputMux);

	/*
	 * See Readme for description of events and states.
	 */
	SCT0->LIMIT = ENABLE_EVENT(0) | ENABLE_EVENT(5) | ENABLE_EVENT(6) | ENABLE_EVENT(9) | ENABLE_EVENT(10);
	SCT0->EV[0].CTRL = SCT_EV_CTRL_COMBMODE(1) | SCT_EV_CTRL_MATCHSEL(0);
	SCT0->EV[1].CTRL = SCT_EV_CTRL_COMBMODE(1) | SCT_EV_CTRL_MATCHSEL(1);
	SCT0->EV[2].CTRL = SCT_EV_CTRL_COMBMODE(1) | SCT_EV_CTRL_MATCHSEL(2);
	SCT0->EV[3].CTRL = SCT_EV_CTRL_COMBMODE(1) | SCT_EV_CTRL_MATCHSEL(3);
	SCT0->EV[4].CTRL = SCT_EV_CTRL_COMBMODE(1) | SCT_EV_CTRL_MATCHSEL(4);
	SCT0->EV[5].CTRL = SCT_EV_CTRL_COMBMODE(1) | SCT_EV_CTRL_MATCHSEL(5);
	SCT0->EV[6].CTRL = SCT_EV_CTRL_COMBMODE(1) | SCT_EV_CTRL_MATCHSEL(0)| SCT_EV_CTRL_STATELD(1) | SCT_EV_CTRL_STATEV(2); //match0 (transition to S2)
	SCT0->EV[7].CTRL = SCT_EV_CTRL_COMBMODE(2) | SCT_EV_CTRL_IOSEL(5) | SCT_EV_CTRL_STATELD(1) | SCT_EV_CTRL_STATEV(1) | SCT_EV_CTRL_IOCOND(1); //CT0-MAT3 RISE (transition to S1)
	SCT0->EV[8].CTRL = SCT_EV_CTRL_COMBMODE(2) | SCT_EV_CTRL_IOSEL(5) | SCT_EV_CTRL_STATELD(1) | SCT_EV_CTRL_STATEV(1) | SCT_EV_CTRL_IOCOND(2); //CT0-MAT3 FALL (transition to S1)
	//SCT0->EV[9].CTRL = SCT_EV_CTRL_COMBMODE(2) | SCT_EV_CTRL_IOSEL(1) | SCT_EV_CTRL_IOCOND(1)| SCT_EV_CTRL_STATELD(1) | SCT_EV_CTRL_STATEV(3); //DMA0 complete - SEV instruction (clear timer and transition to S3)
	SCT0->EV[10].CTRL = SCT_EV_CTRL_COMBMODE(2) | SCT_EV_CTRL_IOSEL(0) | SCT_EV_CTRL_IOCOND(1) | SCT_EV_CTRL_STATELD(1) | SCT_EV_CTRL_STATEV(0); //CT0-MAT0 (transition to S0)

	SCT0->EV[0].STATE = ENABLE_STATE(0);
	SCT0->EV[1].STATE = ENABLE_STATE(0) | ENABLE_STATE(1);
	SCT0->EV[2].STATE = ENABLE_STATE(0);
	SCT0->EV[3].STATE = ENABLE_STATE(0) | ENABLE_STATE(1);
	SCT0->EV[4].STATE = ENABLE_STATE(0);
	SCT0->EV[5].STATE = ENABLE_STATE(2);
	SCT0->EV[6].STATE = ENABLE_STATE(1);
	SCT0->EV[7].STATE = ENABLE_STATE(0);
	SCT0->EV[8].STATE = ENABLE_STATE(0);
	//SCT0->EV[9].STATE = ENABLE_STATE(2);
	SCT0->EV[10].STATE = ENABLE_STATE(2) | ENABLE_STATE(3);

	SCT0->OUT[1].SET = ENABLE_EVENT(1) | ENABLE_EVENT(6);
	SCT0->OUT[1].CLR = ENABLE_EVENT(2);
	SCT0->OUT[0].SET = ENABLE_EVENT(3) | ENABLE_EVENT(6);
	SCT0->OUT[0].CLR = ENABLE_EVENT(4);
	//Trigger to ADC
	SCT0->OUT[4].SET = ENABLE_EVENT(5);
	SCT0->OUT[4].CLR = ENABLE_EVENT(5) | ENABLE_EVENT(6);
	//We set and clear out4 at the same event, so have to provide a conflict resolution:
	SCT0->RES = SCT_RES_O4RES(3);	//toggle output

	deadband = CLOCK_GetSctClkFreq()/5e6;	//~200ns deadtime
 	SCT0->MATCH[0] = CLOCK_GetSctClkFreq()/200000;
	SCT0->MATCHREL[0] = SCT0->MATCH[0];

	SCT0->MATCH[1] = 0;
	SCT0->MATCHREL[1] = SCT0->MATCH[1];

	SCT0->MATCH[2] = SCT0->MATCH[0]/2-deadband;
	SCT0->MATCHREL[2] = SCT0->MATCH[2];

	SCT0->MATCH[3] = SCT0->MATCH[0]/2;
	SCT0->MATCHREL[3] = SCT0->MATCH[3];

	SCT0->MATCH[4] = SCT0->MATCH[0]-deadband;
	SCT0->MATCHREL[4] = SCT0->MATCH[4];

	SCT0->MATCH[5] = CLOCK_GetSctClkFreq()/200000;	//adc sample rate. Since we toggle the ADC trigger, our clock here has to be twice the desired sample rate
	SCT0->MATCHREL[5] = SCT0->MATCH[5];

	//enable event flags for debug
	SCT0->EVEN = 0x01;
	//NVIC_EnableIRQ(SCT0_IRQn);

	/*
	 * CT0 is used for period and burst length
	 */
    CLOCK_EnableClock(kCLOCK_Timer0);
    RESET_PeripheralReset(kCTIMER0_RST_SHIFT_RSTn);
    CTIMER0->TCR = CTIMER_TCR_CRST(1);
    CTIMER0->MR[0] = CLOCK_GetCTimerClkFreq(0)/5;	//200ms period
    ping_period_us = 200000;
    //add match reload register functionality so we can change period/burst length on the fly
    CTIMER0->MSR[0] = CTIMER0->MR[0];
    CTIMER0->MR[3] = CLOCK_GetCTimerClkFreq(0)/2500;	//400us burst of clocks
    ping_length_us = 400;
    CTIMER0->MSR[3] = CTIMER0->MR[3];
    CTIMER0->MCR = CTIMER_MCR_MR0R(1) | CTIMER_MCR_MR0I(1) |
    				CTIMER_MCR_MR0RL(1) | CTIMER_MCR_MR3RL(1) | CTIMER_MCR_MR3I(1);

    CTIMER0->EMR = CTIMER_EMR_EMC0(2) |		//set pin
    					CTIMER_EMR_EMC3(3);	//toggle match pin
    NVIC_EnableIRQ(CTIMER0_IRQn);

    SCT0->STATE = 3;	//Set to idle state by default
}
void CTIMER0_IRQHandler(void){
	uint64_t calc;
	if(CTIMER0->IR & CTIMER_IR_MR0INT(1)){
		//GPIO_PortSet(GPIO,BOARD_INITPINS_DBG_PORT,BOARD_INITPINS_DBG_PIN_MASK);
		CTIMER0->IR = CTIMER_IR_MR0INT(1);	//clear interrupt
		CTIMER0->EMR &= ~CTIMER_EMR_EM0(1);	//clear output

		/* re-calculate the ping period (in case it was changed on reload) */
		calc = CTIMER0->MSR[0];
		calc *= 1000000;
		calc /= CLOCK_GetCTimerClkFreq(0);
		ping_period_us = calc;

		calc = (ping_period_us - ping_length_us - 5000);	//maximum burst length, in microseconds
		calc = (calc * 100000)/1000000;

		//GPIO_PortClear(GPIO,BOARD_INITPINS_DBG_PORT,BOARD_INITPINS_DBG_PIN_MASK);
		ADCEnableBurst(calc, ping_period_us, ping_length_us);

		GPIO_PortSet(GPIO,BOARD_INITPINS_DBG_PORT,BOARD_INITPINS_DBG_PIN_MASK);
	}

	if(CTIMER0->IR & CTIMER_IR_MR3INT(1)){
		CTIMER0->IR = CTIMER_IR_MR3INT(1);	//clear interrupt
		calc = CTIMER0->MSR[3];
		calc *= 1000000;
		calc /= CLOCK_GetCTimerClkFreq(0);
		ping_length_us = calc;
	}

}
void SCT0_IRQHandler(void){
	SCT0->EVFLAG = 0x3FF;	//reset all flags
	switch(SCT0->STATE){
	case 0:
		break;
	case 1:
		break;
	case 2:
		break;
	case 3:
		break;
	}
}

static void sct_start(void){
	//start timer
	SCT0->CTRL = SCT_CTRL_HALT_L(0);	//start SCT0
	CTIMER0->TCR = CTIMER_TCR_CEN(1);	//start CT0
}

static void sct_stop(void){
	//stop/reset timers
	SCT0->CTRL = SCT_CTRL_HALT_L(1) | SCT_CTRL_CLRCTR_L(1);	//stop/clear timer (for now)
	CTIMER0->TCR = CTIMER_TCR_CRST(1);
}

void TUSS4470GetConfig(ping_cur_cfg_t * cur_cfg){
	uint8_t reg_val;

	cur_cfg->period = ping_period_us;

	cur_cfg->len = ping_length_us;

	cur_cfg->running = CTIMER0->TCR & CTIMER_TCR_CEN(1) ? 1 : 0;

	cur_cfg->power = GPIO_PinRead(GPIO,BOARD_INITPINS_HIGH_POWER_PORT,BOARD_INITPINS_HIGH_POWER_PIN);

	tuss4470_read(bpf_config_2,&reg_val,NULL);
	cur_cfg->q = reg_val;

	tuss4470_read(dev_ctrl_2,&reg_val,NULL);
	cur_cfg->lna = reg_val;

}

void TUSS4470Task(void*p){
	QueueHandle_t cmd_queue = (QueueHandle_t)p;
	uint8_t data;
	ping_cfg_t cmd;
	uint64_t calc;
	/*
	 * Configure the device for our use
	 */
	tuss4470_read(device_id,&data,NULL);
	configASSERT(data == 0xB9)


	tuss4470_write(bpf_config_1,BPF1_HPF_FREQ(0x1D),NULL);//0x1D = 196.78 kHz, 0x1E = 206.05 kHz
	tuss4470_write(bpf_config_2,BPF2_Q_SEL_4,NULL);
	tuss4470_write(dev_ctrl_1,0,NULL);
	tuss4470_write(dev_ctrl_2,DEV2_LNA_GAIN_10,NULL);
	tuss4470_write(dev_ctrl_3,DEV3_IO_MODE(2)| DEV3_DRV_PLS_FLT_DT_DIS,NULL);
	tuss4470_write(vdrv_ctrl,VDRV_CURRENT_LEVEL_20 | VDRV_VOLTAGE_LEVEL_VOLTS(8) ,NULL);
	//tuss4470_write(echo_int_config,0,NULL);
	//tuss4470_write(zc_config,ZC_CMP_HYST_230|ZC_CMP_STG_SEL(2),NULL);
	tuss4470_write(burst_pulse,BRST_PRE_DRIVER_MODE | BRST_PULSE(0),NULL);
	//tuss4470_write(tof_config,0,NULL);

	sct_config();
	sct_start();
	while(1){

		/* Wait for commands to update our parameters */
		xQueueReceive(cmd_queue, &cmd,portMAX_DELAY);
		switch(cmd.cmd){
			case PING_SET_PERIOD:
				/* Limit period to a low end of 5ms */
				if(cmd.params[0] < 50000) cmd.params[0] = 50000;
				calc = CLOCK_GetCTimerClkFreq(0);
				calc *= cmd.params[0];
				calc /= 1000000;
				CTIMER0->MSR[0] = calc;
				break;
			case PING_SET_LEN:
				/* Limit ping lenght to between 80 and 1000 us */
				if(cmd.params[0] < 80) cmd.params[0] = 80;
				if(cmd.params[0] > 1000) cmd.params[0] = 1000;
				calc = CLOCK_GetCTimerClkFreq(0);
				calc *= cmd.params[0];
				calc /= 1000000;
				CTIMER0->MSR[3] = calc;
				break;
			case PING_SET_POWER:
				if(cmd.params[0] == 0){
					//low power
					GPIO_PortClear(GPIO,BOARD_INITPINS_HIGH_POWER_PORT,BOARD_INITPINS_HIGH_POWER_PIN_MASK);
				}else{
					//high power
					GPIO_PortSet(GPIO,BOARD_INITPINS_HIGH_POWER_PORT,BOARD_INITPINS_HIGH_POWER_PIN_MASK);
				}
				break;
			case PING_SET_LNA:
				tuss4470_write(dev_ctrl_2,cmd.params[0],NULL);
				break;
			case PING_SET_Q:
				tuss4470_write(bpf_config_2,cmd.params[0],NULL);
				break;
			case PING_STOP:
				sct_stop();
				break;
			case PING_START:
				sct_start();
				break;
			case PING_NOP:
				break;
		}

	}

}

void TUSS4470Init(QueueHandle_t * cmd_queue){
	QueueHandle_t ping_cmds;
	ping_cmds = xQueueCreate(3,sizeof(ping_cfg_t));
	*cmd_queue = ping_cmds;
    xTaskCreate(TUSS4470Task, "tuss4470",
                             256,
                             (void*)ping_cmds,
                             0,
                             NULL
                           );

}

