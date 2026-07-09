#include <math.h>

#include "KY037_sound_sensor.h"
#include "WS2815_led_strip.h"
#include "dsps_fft2r.h"
#include "esp_adc/adc_continuous.h"
#include "esp_log.h"

void frequency_to_LED_position1(uint8_t* digi_sound_buf, uint8_t* led_color_buf,
                                float silence_offset);
void frequency_to_LED_position2(uint8_t* digi_sound_buf, uint8_t* led_color_buf,
                                float silence_offset);
