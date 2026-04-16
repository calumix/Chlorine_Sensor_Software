/*
 * mcp3551.c
 *
 *  Created on: Mar. 31, 2026
 *      Author: slocke
 *
 * The NXP SPI driver does not support using a GPIO as a chip select. To
 * properly work with FreeRTOS, we have to check if the device is busy before
 * asserting a CS and starting a transfer, so we have to roll our own SPI driver.
 * The peripheral config can still be done with the tools (sets up clock speeds/modes/etc)
 * but the actual transactions need to be managed by us.
 */
#include "mcp3551.h"
#include "pin_mux.h"
#include "fsl_gpio.h"
#include "env.h"

SemaphoreHandle_t sync;

/* FLEXCOMM3_IRQn interrupt handler */
void FLEXCOMM3_IRQHandler(void) {
  uint32_t intStatus;
  BaseType_t task_woken=pdFALSE;

  /* Reading all interrupt flags of status register */
  intStatus = FLEXCOMM_SPI_PERIPHERAL->INTSTAT;

  if(intStatus & SPI_STAT_MSTIDLE(1)){
	  FLEXCOMM_SPI_PERIPHERAL->INTENCLR = SPI_STAT_MSTIDLE(1);	//disable further interrupts
	  xSemaphoreGiveFromISR(sync,&task_woken);
  }

  portYIELD_FROM_ISR(task_woken);
}

int mcp3551_init(struct mcp3551 * dev){
	FLEXCOMM_SPI_PERIPHERAL->INTENCLR = 0x1F;	//clear all interrupt sources
	dev->lock = xSemaphoreCreateMutex();
	sync = xSemaphoreCreateBinary();
	return 1;
}

int mcp3551_start(struct mcp3551 * dev,uint32_t gpio_port, uint32_t gpio_pin){
	/*
	 * The ADC doesn't automatically start a new conversion when one finishes to start a conversion
	 * we have to be sure that the device is in sleep mode (by starting a conversion, waiting for
	 * completion, and then reading the data). Once that synchronization has been completed (ie, call
	 * mcp3551_read once, which will force a conversion), then you can re-start
	 * another conversion by simply toggling CS low then high
	 */
	xSemaphoreTake(dev->lock,portMAX_DELAY);
	GPIO_PinWrite(GPIO,gpio_port,gpio_pin,0);
	vTaskDelay(1);
	GPIO_PinWrite(GPIO,gpio_port,gpio_pin,1);
	xSemaphoreGive(dev->lock);
	return 1;
}

int mcp3551_read(struct mcp3551 * dev,uint32_t gpio_port, uint32_t gpio_pin, int32_t * value){
	uint8_t t;
	union{
		uint8_t bytes[4];
		int32_t total;
	} val;
	uint32_t rd_val;

	/* wait until the ADC is ready */
	do{
		xSemaphoreTake(dev->lock,portMAX_DELAY);

		//assert CS and check if the ADC is done
		GPIO_PinWrite(GPIO,gpio_port,gpio_pin,0);
		t = GPIO_PinRead(GPIO,BOARD_INITPINS_SPI_MISO_PORT,BOARD_INITPINS_SPI_MISO_PIN);

		if(!t){
			//MCP is ready
			break;
		}
		//deassert CS so that someone else can use the bus while we wait
		GPIO_PinWrite(GPIO,gpio_port,gpio_pin,1);

		xSemaphoreGive(dev->lock);
		//typical conversion takes 73ms.
		vTaskDelay(DIV_ROUND_UP(10,portTICK_PERIOD_MS)+1);
	}while(1);

	//ADC is now ready, we still hold the mutex, and CS is still low

	//start SPI read of 3 bytes
	FLEXCOMM_SPI_PERIPHERAL->FIFOWR = SPI_FIFOWR_LEN(8-1);
	FLEXCOMM_SPI_PERIPHERAL->FIFOWR = SPI_FIFOWR_LEN(8-1);
	FLEXCOMM_SPI_PERIPHERAL->FIFOWR = SPI_FIFOWR_LEN(8-1) | SPI_FIFOWR_EOT(1);

	//enable our idle interrupt
	FLEXCOMM_SPI_PERIPHERAL->INTENSET = SPI_STAT_MSTIDLE(1);

	xSemaphoreTake(sync,portMAX_DELAY);
	GPIO_PinWrite(GPIO,gpio_port,gpio_pin,1);

	//read complete, get data
	val.bytes[3] = 0;
	rd_val = FLEXCOMM_SPI_PERIPHERAL->FIFORD;
	val.bytes[2] = rd_val & 0x00FF;
	rd_val = FLEXCOMM_SPI_PERIPHERAL->FIFORD;
	val.bytes[1] = rd_val & 0x00FF;
	rd_val = FLEXCOMM_SPI_PERIPHERAL->FIFORD;
	val.bytes[0] = rd_val & 0x00FF;

	xSemaphoreGive(dev->lock);

	val.bytes[2] &= 0x3F;	//clear overflow bits
	if(val.bytes[2] & 0x20){	//sign extend
		val.bytes[2] |= 0xC0;
		val.bytes[3] = 0xFF;
	}
	*value = val.total;

	return 1;
}
