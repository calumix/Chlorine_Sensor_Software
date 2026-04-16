/*
** ###################################################################
**     Processors:          LPC5514JBD100
**                          LPC5514JBD64
**                          LPC5514JEV59
**
**     Compilers:           GNU C Compiler
**                          IAR ANSI C/C++ Compiler for ARM
**                          Keil ARM C/C++ Compiler
**                          MCUXpresso Compiler
**
**     Reference manual:    LPC55S1x/LPC551x User manual Rev.0.6  15 November 2019
**     Version:             rev. 2.0, 2024-10-29
**     Build:               b250520
**
**     Abstract:
**         CMSIS Peripheral Access Layer for LPC5514
**
**     Copyright 1997-2016 Freescale Semiconductor, Inc.
**     Copyright 2016-2025 NXP
**     SPDX-License-Identifier: BSD-3-Clause
**
**     http:                 www.nxp.com
**     mail:                 support@nxp.com
**
**     Revisions:
**     - rev. 1.0 (2018-08-22)
**         Initial version based on v0.2UM
**     - rev. 1.1 (2019-12-03)
**         Initial version based on v0.6UM
**     - rev. 2.0 (2024-10-29)
**         Change the device header file from single flat file to multiple files based on peripherals,
**         each peripheral with dedicated header file located in periphN folder.
**
** ###################################################################
*/

/*!
 * @file LPC5514.h
 * @version 2.0
 * @date 2024-10-29
 * @brief CMSIS Peripheral Access Layer for LPC5514
 *
 * CMSIS Peripheral Access Layer for LPC5514
 */

#if !defined(LPC5514_H_)  /* Check if memory map has not been already included */
#define LPC5514_H_

/* IP Header Files List */
#include <periph/PERI_ADC.h>
#include <periph/PERI_AHB_SECURE_CTRL.h>
#include <periph/PERI_ANACTRL.h>
#include <periph/PERI_CAN.h>
#include <periph/PERI_CDOG.h>
#include <periph/PERI_CRC.h>
#include <periph/PERI_CTIMER.h>
#include <periph/PERI_DBGMAILBOX.h>
#include <periph/PERI_DMA.h>
#include <periph/PERI_FLASH.h>
#include <periph/PERI_FLASH_CFPA.h>
#include <periph/PERI_FLASH_CMPA.h>
#include <periph/PERI_FLASH_KEY_STORE.h>
#include <periph/PERI_FLASH_NMPA.h>
#include <periph/PERI_FLASH_ROMPATCH.h>
#include <periph/PERI_FLEXCOMM.h>
#include <periph/PERI_GINT.h>
#include <periph/PERI_GPIO.h>
#include <periph/PERI_I2C.h>
#include <periph/PERI_I2S.h>
#include <periph/PERI_INPUTMUX.h>
#include <periph/PERI_IOCON.h>
#include <periph/PERI_MRT.h>
#include <periph/PERI_OSTIMER.h>
#include <periph/PERI_PINT.h>
#include <periph/PERI_PLU.h>
#include <periph/PERI_PMC.h>
#include <periph/PERI_RNG.h>
#include <periph/PERI_RTC.h>
#include <periph/PERI_SCT.h>
#include <periph/PERI_SPI.h>
#include <periph/PERI_SYSCON.h>
#include <periph/PERI_SYSCTL.h>
#include <periph/PERI_USART.h>
#include <periph/PERI_USB.h>
#include <periph/PERI_USBFSH.h>
#include <periph/PERI_USBHSD.h>
#include <periph/PERI_USBHSH.h>
#include <periph/PERI_USBPHY.h>
#include <periph/PERI_UTICK.h>
#include <periph/PERI_WWDT.h>

#endif  /* #if !defined(LPC5514_H_) */
