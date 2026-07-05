#include "FFT_processing.h"
#include "KY037_sound_sensor.h"
#include "WS2815_led_strip.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

typedef enum {
    STATIC,
    FREQ_AS_POSITION,
    FREQ_AS_COLOR,
} STYLE;

void app_main(void) {
    // xTaskCreate(capture_sound, "capture_sound", 2048, NULL, 10, NULL);
    xTaskCreate(update_led_strip, "update_LED_strip", 2048, NULL, 10, NULL);
    STYLE style;
    switch (style) {
        case STATIC:
            break;
        case FREQ_AS_POSITION:
            frequency_to_LED_position();
            break;
        case FREQ_AS_COLOR:
            frequency_to_LED_color();
            break;
    }
}
