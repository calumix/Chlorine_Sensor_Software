/*
 * echograph.h
 *
 *  Created on: Feb. 5, 2025
 *      Author: slocke
 */

#ifndef ECHOGRAM_H_
#define ECHOGRAM_H_

#include "FreeRTOS.h"
#include "stream_buffer.h"
#include "queue.h"

typedef struct _echogram_header_t{
	uint32_t frame_sync[3];
	uint32_t burst_len;
	uint32_t ping_len_us;
	uint32_t ping_period_us;
	uint32_t f_sample;
} __attribute__((packed)) echogram_header_t;

typedef enum {
	TYPE_CPU_TEMP,
	TYPE_THERMISTOR,
	TYPE_DEPTH,
	TYPE_DEPTH_EMPTY,
} sensor_type_t;
typedef struct _sensor_sample_t {
	sensor_type_t type;
	float reading;
} sensor_sample_t;

void ADCEnableBurst(uint32_t len,uint32_t ping_period_us,uint32_t ping_length_us);
void ADCDisableBurst(void);
void ADCDMAInit(int32_t baud, StreamBufferHandle_t usb_stream, QueueHandle_t* sensor_messages);

#endif /* ECHOGRAM_H_ */
