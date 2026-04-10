/*
 * ad5258.c
 *
 *  Created on: Mar. 31, 2026
 *      Author: slocke
 */
#include "ad5258.h"
#include "peripherals.h"
#include "FreeRTOS.h"

#define CMD_RDAC				(0x00<<5)
#define CMD_EEPROM				(0x01<<5)
#define CMD_WP					(0x02<<5)
#define CMD_NOP					(0x04<<5)
#define CMD_RESTORE				(0x05<<5)
#define CMD_SAVE				(0x06<<5)
#define EEPROM_ADDR_RDAC		0x00
#define EEPROM_ADDR_TOL_INT		0x1E
#define EEPROM_ADDR_TOL_FRAC	0x1F

int ad5258_set_wp(struct ad5258 * dev, uint8_t wp){
	int status;
	i2c_master_transfer_t masterXfer;

    masterXfer.slaveAddress   = dev->address;
    masterXfer.direction      = kI2C_Write;
    masterXfer.subaddress     = CMD_WP;
    masterXfer.subaddressSize = 1;
    masterXfer.data           = &wp;
    masterXfer.dataSize       = 1;
    masterXfer.flags          = kI2C_TransferDefaultFlag;

    status = I2C_RTOS_Transfer(dev->i2c_dev, &masterXfer);
    if (status != kStatus_Success){
        return 0;
    }
    return 1;
}


int ad5258_write_rdac(struct ad5258 * dev, uint8_t val){
	int status;
	i2c_master_transfer_t masterXfer;

    masterXfer.slaveAddress   = dev->address;
    masterXfer.direction      = kI2C_Write;
    masterXfer.subaddress     = CMD_RDAC;
    masterXfer.subaddressSize = 1;
    masterXfer.data           = &val;
    masterXfer.dataSize       = 1;
    masterXfer.flags          = kI2C_TransferDefaultFlag;

    status = I2C_RTOS_Transfer(dev->i2c_dev, &masterXfer);
    if (status != kStatus_Success){
        return 0;
    }
    return 1;
}

int ad5258_read_rdac(struct ad5258 * dev, uint8_t * val){
	int status;
	i2c_master_transfer_t masterXfer;

    masterXfer.slaveAddress   = dev->address;
    masterXfer.direction      = kI2C_Read;
    masterXfer.subaddress     = CMD_RDAC;
    masterXfer.subaddressSize = 1;
    masterXfer.data           = val;
    masterXfer.dataSize       = 1;
    masterXfer.flags          = kI2C_TransferDefaultFlag;

    status = I2C_RTOS_Transfer(dev->i2c_dev, &masterXfer);
    if (status != kStatus_Success){
        return 0;
    }
    return 1;
}

int ad5258_load_tolerance(struct ad5258 * dev){
	int status;
	uint8_t regs[2];
	i2c_master_transfer_t masterXfer;

    masterXfer.slaveAddress   = dev->address;
    masterXfer.direction      = kI2C_Read;
    masterXfer.subaddress     = CMD_EEPROM | EEPROM_ADDR_TOL_INT;
    masterXfer.subaddressSize = 1;
    masterXfer.data           = regs;
    masterXfer.dataSize       = 2;
    masterXfer.flags          = kI2C_TransferDefaultFlag;

    status = I2C_RTOS_Transfer(dev->i2c_dev, &masterXfer);
    if (status != kStatus_Success){
        return 0;
    }

    dev->tolerance = regs[0] & 0x7F;
    if(regs[0] & 0x80) dev->tolerance *= -1;
    dev->tolerance += regs[1]*3.90625e-3;
    dev->tolerance /= 100;	//convert from percentage
    return 1;
}
