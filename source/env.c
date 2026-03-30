/*
 * env.c
 *
 *  Created on: Feb. 18, 2025
 *      Author: slocke
 */

#define EXCLUDE_ENV
#include "env.h"
#include "FreeRTOS.h"
#include "task.h"
#include "peripherals.h"

environment_t env;
#define EEPROM_I2C_ADDR 	(0x50)
#define EEPROM_PAGE_SIZE	32
#define DIV_ROUND_UP(x,y)	(((x)+(y)-1)/(y))
//used internally to write a single page (or less).  That means that addr has to be aligned to EEPROM_PAGE_SIZE bytes for this to work correctly
static int EEPROMWritePage(uint16_t addr, uint8_t * buff, uint32_t bytes){
	int status;
	i2c_master_transfer_t masterXfer;

    masterXfer.slaveAddress   = EEPROM_I2C_ADDR;
    masterXfer.direction      = kI2C_Write;
    /* EEPROM takes high byte of address first.  This really doesn't matter to us, except if we do continuous reads across 8-bit boundary (which we current don't).
     * This must, however, match how the bootloader addresses EEPROM.  To keep it simple, we follow the datasheet recommended way (high byte of address first)
     * */
    masterXfer.subaddress     = addr;
    masterXfer.subaddressSize = 1;
    masterXfer.data           = buff;
    masterXfer.dataSize       = bytes;
    masterXfer.flags          = kI2C_TransferDefaultFlag;

    status = I2C_RTOS_Transfer(&FLEXCOMM_I2C_rtosHandle, &masterXfer);
    if (status != kStatus_Success){
        return 0;
    }
    vTaskDelay(DIV_ROUND_UP(5,portTICK_PERIOD_MS)+1);
    return bytes;

}

static int EEPROM_WriteArray(uint16_t addr, uint8_t * buff, uint32_t bytes){
	int write_len;
	int i=0;

	//Divvy up the buffer into EEPROM_PAGE_SIZE chunks for writing
	while(bytes){
		write_len = EEPROM_PAGE_SIZE - (addr%EEPROM_PAGE_SIZE);
		if(write_len > bytes) write_len = bytes;
		if(EEPROMWritePage(addr,&buff[i],write_len)==0) return 0;
		i+= write_len;
		bytes -= write_len;
		addr += write_len;
	}
	return bytes;
}

static int EEPROM_ReadArray(uint16_t addr, uint8_t * buff, uint32_t bytes){
	int status;
	i2c_master_transfer_t masterXfer;

    masterXfer.slaveAddress   = EEPROM_I2C_ADDR;
    masterXfer.direction      = kI2C_Read;
    masterXfer.subaddress     = addr;
    masterXfer.subaddressSize = 1;
    masterXfer.data           = buff;
    masterXfer.dataSize       = bytes;
    masterXfer.flags          = kI2C_TransferDefaultFlag;

    status = I2C_RTOS_Transfer(&FLEXCOMM_I2C_rtosHandle, &masterXfer);
    if (status != kStatus_Success){
        return 0;
    }

    return bytes;
}

static uint16_t EnvCRC(void){
	CRC_WriteSeed(CRC_ENGINE_PERIPHERAL,0x0000FFFF);
	CRC_WriteData(CRC_ENGINE_PERIPHERAL, (const uint8_t*)&env, sizeof(env)-sizeof(uint16_t));
	return CRC_Get16bitResult(CRC_ENGINE_PERIPHERAL);
}

void EnvWrite(void){
	env.flags &= ~ENV_FLAGS_DEFAULT;
	env.crc_ccit = EnvCRC();
	EEPROM_WriteArray(0,(uint8_t*)&env,sizeof(env));
}
void EnvSetDefaults(void){
	env.uart_baud = 115200;
	env.flags = ENV_FLAGS_DEFAULT;
}

void EnvInit(void){
	uint16_t calculated_crc;

	/*
	 * Read environment from EEPROM
	 */
	EEPROM_ReadArray(0,(uint8_t *)&env, sizeof(env));

	/*
	 * See if CRC validates
	 */
	calculated_crc = EnvCRC();
	if(calculated_crc != env.crc_ccit){
		/*
		 * Env is not good. Load defaults
		 */
		EnvSetDefaults();
		//only write back to EEPROM on command
	}


}
