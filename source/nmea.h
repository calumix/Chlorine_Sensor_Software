/*
 * nmea.h
 *
 *  Created on: Jan. 28, 2025
 *      Author: slocke
 */

#ifndef NMEA_H_
#define NMEA_H_

#include "FreeRTOS.h"
#include "stream_buffer.h"
#include "queue.h"

void NMEAInit(uint32_t baud, StreamBufferHandle_t rs, QueueHandle_t tq, QueueHandle_t sensor_q);

#endif /* NMEA_H_ */
