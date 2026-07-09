#ifndef LED_STRIP_H
#define LED_STRIP_H

#include <stdint.h>

#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "string.h"

#define LED_COUNT 500
#define LED_BUFFER_SIZE (LED_COUNT * 3)

typedef struct {
    uint32_t resolution; /*!< Encoder resolution, in Hz */
} led_strip_encoder_config_t;

esp_err_t create_led_strip_encoder(const led_strip_encoder_config_t* config,
                                   rmt_encoder_handle_t* ret_encoder);
uint8_t* init_WS2815_LED_strip();
void update_led_strip();

#endif  // LED_STRIP_H
