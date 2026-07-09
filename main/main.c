#include <assert.h>

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

STYLE style = STATIC;

static float sample_silence_offset = 0;
static uint8_t* sound_samples = NULL;
static uint8_t* led_pixels = NULL;

static const char* APP_TAG = "application";

void capture_and_process_sound(void* args) {
    while (1) {
        uint32_t read_size = capture_sound(sound_samples);

        // while (read_size < SAMPLE_BUFFER_SIZE) {
        //     read_size = capture_sound(sound_samples);
        // }

        ESP_LOGI(APP_TAG, "capture sound successfully %.2d bytes", read_size);

        switch (style) {
            case STATIC:
                for (int i = 0; i < LED_COUNT; i += 3) {
                    led_pixels[i * 3 + 0] = 0;
                    led_pixels[i * 3 + 1] = 0;
                    led_pixels[i * 3 + 2] = 110;
                }
                break;
            case FREQ_AS_POSITION:
                frequency_to_LED_position1(sound_samples, led_pixels, sample_silence_offset);
                break;
            case FREQ_AS_COLOR:
                frequency_to_LED_position2(sound_samples, led_pixels, sample_silence_offset);
                break;
        }
    }
}

void display_led_strip(void* args) {
    while (1) {
        update_led_strip();
    }
}

void app_main(void) {
    sound_samples = init_KY037_sound_sensor(&sample_silence_offset);
    led_pixels = init_WS2815_LED_strip();

    xTaskCreate(capture_and_process_sound, "capture_and_process_sound", 2048, NULL, 10, NULL);
    // xTaskCreate(display_led_strip, "update_and_display_LED_strip", 2048, NULL, 10, NULL);
}
