#ifndef SOUND_SENSOR_H
#define SOUND_SENSOR_H

#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_adc/adc_continuous.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SAMPLE_FREQ_HZ 20000  //  sampling rate
#define SAMPLE_COUNT 4096     // Must be power of 2 for FFT
#define SAMPLE_BUFFER_SIZE (SAMPLE_COUNT * SOC_ADC_DIGI_DATA_BYTES_PER_CONV)

float get_silence_offset(uint8_t* buf);
uint8_t* init_KY037_sound_sensor(float* silence_offset);
uint32_t capture_sound(uint8_t* samples);

#endif  // SOUND_SENSOR_H
