#ifndef SOUND_SENSOR_H
#define SOUND_SENSOR_H

#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_adc/adc_continuous.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "global.h"

float get_silence_offset(uint8_t* buf, size_t size);
uint8_t* init_KY037_sound_sensor(float* silence_offset, uint32_t* size);
uint32_t capture_sound(uint8_t* samples, uint32_t size);

#endif  // SOUND_SENSOR_H
