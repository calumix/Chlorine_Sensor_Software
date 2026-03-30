/*
 * tuss4470.h
 *
 *  Created on: Jan. 29, 2025
 *      Author: slocke
 */

#ifndef TUSS4470_H_
#define TUSS4470_H_


#define BPF1_HPF_FREQ(x) 		((x)&0x3F)
#define BPF1_ENABLE_HP_ONLY		(1<<6)
#define BPF1_FC_TRIM_OVRD		(1<<7)

#define BPF2_FC_TRIM(x)		((x)&0x0F)
#define BPF2_Q_SEL_2		(0x2<<4)
#define BPF2_Q_SEL_3		(0x3<<4)
#define BPF2_Q_SEL_4		(0x0<<4)
#define BPF2_Q_SEL_5		(0x1<<4)

#define DEV1_LOGAMP_INT_ADJ(x)	((x)&0x0F)
#define DEV1_LOGAMP_SLOPE_ADJ(x) (((x)&0x7)<<4)
#define DEV1_LOGAMP_OVRD	(1<<7)

#define DEV2_LNA_GAIN_12_5	(0x03)
#define DEV2_LNA_GAIN_20	(0x02)
#define DEV2_LNA_GAIN_10	(0x01)
#define DEV2_LNA_GAIN_15	(0x00)
#define DEV2_VOUT_SCALE_SEL_3_3	(0x0)
#define DEV2_VOUT_SCALE_SEL_5_0 (0x1)
#define DEV2_LOGAMP_DIS_FIRST	(1<<7)
#define DEV2_LOGAMP_DIS_LAST	(1<<6)

#define DEV3_IO_MODE(x)		((x)&0x03)
#define DEV3_DRV_PLS_FLT_DT_DIS	(0x07<<2)
#define DEV3_DRV_PLS_FLT_DT_4	(0x06<<2)
#define DEV3_DRV_PLS_FLT_DT_8	(0x05<<2)
#define DEV3_DRV_PLS_FLT_DT_16	(0x04<<2)
#define DEV3_DRV_PLS_FLT_DT_24	(0x03<<2)
#define DEV3_DRV_PLS_FLT_DT_32	(0x02<<2)
#define DEV3_DRV_PLS_FLT_DT_48	(0x01<<2)
#define DEV3_DRV_PLS_FLT_DT_64	(0x00<<2)

#define VDRV_VOLTAGE_LEVEL_VOLTS(x) VDRV_VOLTAGE_LEVEL((x)-5)
#define VDRV_VOLTAGE_LEVEL(x)	((x)&0x0F)
#define VDRV_CURRENT_LEVEL_10	(0<<4)
#define VDRV_CURRENT_LEVEL_20	(1<<4)
#define VDRV_HI_Z				(1<<5)
#define VDRV_DIS_REG_LSTN		(1<<6)

#define ECHO_INT_THR_SEL(x)		((x)&0x0F)
#define ECHO_INT_CMP_EN			(1<<4)

#define ZC_CMP_HYST_30			(0)
#define ZC_CMP_HYST_80			(1)
#define ZC_CMP_HYST_130			(2)
#define ZC_CMP_HYST_180			(3)
#define ZC_CMP_HYST_230			(4)
#define ZC_CMP_HYST_280			(5)
#define ZC_CMP_HYST_330			(6)
#define ZC_CMP_HYST_380			(7)
#define ZC_CMP_STG_SEL(x)		(((x)&0x3)<<3)
#define ZC_CMP_IN_SEL_VCM		(0<<5)
#define ZC_CMP_IN_SEL_INN		(1<<5)
#define ZC_EN_ECHO_INT			(1<<6)
#define ZC_CMP_EN				(1<<7)

#define BRST_PULSE(x)			((x)&0x1F)
#define BRST_PRE_DRIVER_MODE	(1<<6)
#define BRST_HALF_BRG_MODE		(1<<7)

#define TOF_CMD_TRIG			(1<<0)
#define TOF_VDRV_TRIG			(1<<1)
#define TOF_STDBY_MODE_EN		(1<<6)
#define TOF_SLEEP_MODE_EN		(1<<7)

#define DEV_STAT_EE_CRC_FLT		(1<<0)
#define DEV_STAT_DRV_PULSE_FLT	(1<<1)
#define DEV_STAT_PULSE_NUM_FLT	(1<<2)
#define DEV_VDRV_REAY			(1<<3)


typedef enum {
	bpf_config_1 = 0x10,
	bpf_config_2 = 0x11,
	dev_ctrl_1 = 0x12,
	dev_ctrl_2 = 0x13,
	dev_ctrl_3 = 0x14,
	vdrv_ctrl = 0x16,
	echo_int_config = 0x17,
	zc_config = 0x18,
	burst_pulse = 0x1a,
	tof_config = 0x1b,
	dev_stat = 0x1c,
	device_id = 0x1d,
	rev_id = 0x1e
} tuss4470_regs_t;

/* commands can be sent to the pinger thread to change settings */
typedef enum {
	/* period of ping interval in microseconds */
	PING_SET_PERIOD,

	/* length of burst in microseconds */
	PING_SET_LEN,

	/* high or low power */
	PING_SET_POWER,

	/* Set the low noise amp gain. One of DEV2_LNA_GAIN_xxx above */
	PING_SET_LNA,

	/* Set the Q of the RX filter. One of BPF2_Q_SEL_x from above */
	PING_SET_Q,

	PING_STOP,
	PING_START,

	/* code to mark this command as invalid */
	PING_NOP,
} ping_cfg_cmd_t;

typedef struct ping_cur_cfg{
	uint32_t period;
	uint32_t len;
	uint8_t lna;
	uint8_t q;
	uint8_t power;
	uint8_t running;
} ping_cur_cfg_t;

typedef struct ping_cfg {
	ping_cfg_cmd_t cmd;
	uint32_t params[2];
} ping_cfg_t;

void TUSS4470Init(QueueHandle_t * cmd_queue);
void TUSS4470GetConfig(ping_cur_cfg_t * cur_cfg);
void tuss4470_read(tuss4470_regs_t reg, uint8_t * data, uint8_t * rx_status);
void tuss4470_write(tuss4470_regs_t reg, uint8_t data, uint8_t * rx_status);
#endif /* TUSS4470_H_ */
