#include "KY037_sound_sensor.h"
#include "WS2815_led_strip.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void app_main(void) {
    // xTaskCreate(capture_sound, "capture_sound", 2048, NULL, 10, NULL);
    xTaskCreate(update_led_strip, "capture_sound", 2048, NULL, 10, NULL);

    // vTaskStartScheduler();
}
