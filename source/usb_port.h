/*
 * usb.h
 *
 *  Created on: Feb. 7, 2025
 *      Author: slocke
 */

#ifndef USB_PORT_H_
#define USB_PORT_H_

#include "FreeRTOS.h"
#include "stream_buffer.h"

void USBInit(StreamBufferHandle_t * usb_tx_stream, StreamBufferHandle_t * usb_rx_stream);


#endif /* USB_PORT_H_ */
