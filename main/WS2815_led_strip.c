/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "WS2815_led_strip.h"

#include "esp_check.h"

static const char* LED_STRIP_TAG = "led_strip_encoder";

#define RMT_LED_STRIP_RESOLUTION_HZ \
    10000000  // 10MHz resolution, 1 tick = 0.1us (led strip needs a high
              // resolution)
#define RMT_LED_STRIP_GPIO_NUM 1

#define CHASE_SPEED_MS 10

rmt_channel_handle_t led_chan = NULL;
rmt_encoder_handle_t led_encoder = NULL;

uint8_t* led_strip_pixels = NULL;

typedef struct {
    rmt_encoder_t base;
    rmt_encoder_t* bytes_encoder;
    rmt_encoder_t* copy_encoder;
    int state;
    rmt_symbol_word_t reset_code;
} rmt_led_strip_encoder_t;

RMT_ENCODER_FUNC_ATTR
static size_t rmt_encode_led_strip(rmt_encoder_t* encoder, rmt_channel_handle_t channel,
                                   const void* primary_data, size_t data_size,
                                   rmt_encode_state_t* ret_state) {
    rmt_led_strip_encoder_t* led_encoder = __containerof(encoder, rmt_led_strip_encoder_t, base);
    rmt_encoder_handle_t bytes_encoder = led_encoder->bytes_encoder;
    rmt_encoder_handle_t copy_encoder = led_encoder->copy_encoder;
    rmt_encode_state_t session_state = RMT_ENCODING_RESET;
    rmt_encode_state_t state = RMT_ENCODING_RESET;
    size_t encoded_symbols = 0;
    switch (led_encoder->state) {
        case 0:  // send RGB data
            encoded_symbols += bytes_encoder->encode(bytes_encoder, channel, primary_data,
                                                     data_size, &session_state);
            if (session_state & RMT_ENCODING_COMPLETE) {
                led_encoder->state =
                    1;  // switch to next state when current encoding session finished
            }
            if (session_state & RMT_ENCODING_MEM_FULL) {
                state |= RMT_ENCODING_MEM_FULL;
                goto out;  // yield if there's no free space for encoding artifacts
            }
        // fall-through
        case 1:  // send reset code
            encoded_symbols +=
                copy_encoder->encode(copy_encoder, channel, &led_encoder->reset_code,
                                     sizeof(led_encoder->reset_code), &session_state);
            if (session_state & RMT_ENCODING_COMPLETE) {
                led_encoder->state = RMT_ENCODING_RESET;  // back to the initial encoding session
                state |= RMT_ENCODING_COMPLETE;
            }
            if (session_state & RMT_ENCODING_MEM_FULL) {
                state |= RMT_ENCODING_MEM_FULL;
                goto out;  // yield if there's no free space for encoding artifacts
            }
    }
out:
    *ret_state = state;
    return encoded_symbols;
}

static esp_err_t rmt_del_led_strip_encoder(rmt_encoder_t* encoder) {
    rmt_led_strip_encoder_t* led_encoder = __containerof(encoder, rmt_led_strip_encoder_t, base);
    rmt_del_encoder(led_encoder->bytes_encoder);
    rmt_del_encoder(led_encoder->copy_encoder);
    free(led_encoder);
    return ESP_OK;
}

RMT_ENCODER_FUNC_ATTR
static esp_err_t rmt_led_strip_encoder_reset(rmt_encoder_t* encoder) {
    rmt_led_strip_encoder_t* led_encoder = __containerof(encoder, rmt_led_strip_encoder_t, base);
    rmt_encoder_reset(led_encoder->bytes_encoder);
    rmt_encoder_reset(led_encoder->copy_encoder);
    led_encoder->state = RMT_ENCODING_RESET;
    return ESP_OK;
}

esp_err_t create_led_strip_encoder(const led_strip_encoder_config_t* config,
                                   rmt_encoder_handle_t* ret_encoder) {
    esp_err_t ret = ESP_OK;
    rmt_led_strip_encoder_t* led_encoder = NULL;
    ESP_GOTO_ON_FALSE(config && ret_encoder, ESP_ERR_INVALID_ARG, err, LED_STRIP_TAG,
                      "invalid argument");
    led_encoder = rmt_alloc_encoder_mem(sizeof(rmt_led_strip_encoder_t));
    ESP_GOTO_ON_FALSE(led_encoder, ESP_ERR_NO_MEM, err, LED_STRIP_TAG,
                      "no mem for led strip encoder");
    led_encoder->base.encode = rmt_encode_led_strip;
    led_encoder->base.del = rmt_del_led_strip_encoder;
    led_encoder->base.reset = rmt_led_strip_encoder_reset;
    // different led strip might have its own timing requirements, following
    // parameter is for WS2812
    // rmt_bytes_encoder_config_t bytes_encoder_config = {
    //     .bit0 =
    //         {
    //             .level0 = 1,
    //             .duration0 = 0.3 * config->resolution / 1000000,  // T0H=0.3us
    //             .level1 = 0,
    //             .duration1 = 0.9 * config->resolution / 1000000,  // T0L=0.9us
    //         },
    //     .bit1 =
    //         {
    //             .level0 = 1,
    //             .duration0 = 0.9 * config->resolution / 1000000,  // T1H=0.9us
    //             .level1 = 0,
    //             .duration1 = 0.3 * config->resolution / 1000000,  // T1L=0.3us
    //         },
    //     .flags.msb_first = 1  // WS2812 transfer bit order: G7...G0R7...R0B7...B0
    // };
    // WS2815
    // 1 T0H (Bit 0, High State): 250ns -> approx 3 ticks (Range: 220ns - 380ns)
    // 2 T0L (Bit 0, Low State):  1000ns -> approx 10 ticks (Range: 580ns - 1.6µs)
    // 3 T1H (Bit 1, High State): 600ns -> approx 6 ticks (Range: 550ns - 850ns)
    // 4 T1L (Bit 1, Low State):  650ns -> approx 7 ticks (Range: 550ns - 850ns)
    rmt_bytes_encoder_config_t bytes_encoder_config = {
        .bit0 =
            {
                .level0 = 1,
                .duration0 = 3,  // 3 ticks * 100ns = 300 ns
                .level1 = 0,
                .duration1 = 10,  // 10 ticks * 100ns = 1 us
            },
        .bit1 =
            {
                .level0 = 1,
                .duration0 = 10,  // 6 ticks * 100ns = 6 ns
                .level1 = 0,
                .duration1 = 3,  // 7 ticks * 100ns = 7 ns
            },
        .flags.msb_first = 1  // WS2815 transfer bit order: G7...G0R7...R0B7...B0
    };
    ESP_GOTO_ON_ERROR(rmt_new_bytes_encoder(&bytes_encoder_config, &led_encoder->bytes_encoder),
                      err, LED_STRIP_TAG, "create bytes encoder failed");
    rmt_copy_encoder_config_t copy_encoder_config = {};
    ESP_GOTO_ON_ERROR(rmt_new_copy_encoder(&copy_encoder_config, &led_encoder->copy_encoder), err,
                      LED_STRIP_TAG, "create copy encoder failed");

    uint32_t reset_ticks = 4000 / 2;  // reset code duration defaults to 280 us
    led_encoder->reset_code = (rmt_symbol_word_t){
        .level0 = 0,
        .duration0 = reset_ticks,
        .level1 = 0,
        .duration1 = reset_ticks,
    };
    *ret_encoder = &led_encoder->base;
    return ESP_OK;
err:
    if (led_encoder) {
        if (led_encoder->bytes_encoder) {
            rmt_del_encoder(led_encoder->bytes_encoder);
        }
        if (led_encoder->copy_encoder) {
            rmt_del_encoder(led_encoder->copy_encoder);
        }
        free(led_encoder);
    }
    return ret;
}

uint8_t* init_WS2815_LED_strip() {
    ESP_LOGI(LED_STRIP_TAG, "Create RMT TX channel");
    rmt_tx_channel_config_t tx_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,  // select source clock
        .gpio_num = RMT_LED_STRIP_GPIO_NUM,
        .mem_block_symbols = 256,  // increase the block size can make the LED less flickering
        .resolution_hz = RMT_LED_STRIP_RESOLUTION_HZ,
        .trans_queue_depth = 4,  // set the number of transactions that can be
                                 // pending in the background
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_chan_config, &led_chan));

    ESP_LOGI(LED_STRIP_TAG, "Install led strip encoder");
    led_strip_encoder_config_t encoder_config = {
        .resolution = RMT_LED_STRIP_RESOLUTION_HZ,
    };
    ESP_ERROR_CHECK(create_led_strip_encoder(&encoder_config, &led_encoder));

    ESP_LOGI(LED_STRIP_TAG, "Enable RMT TX channel");
    ESP_ERROR_CHECK(rmt_enable(led_chan));

    led_strip_pixels = (uint8_t*)malloc(LED_NUMBERS * 3);

    return led_strip_pixels;
}

void update_led_strip() {
    ESP_LOGI(LED_STRIP_TAG, "Start LED rainbow chase");
    rmt_transmit_config_t tx_config = {
        .loop_count = 0,  // no transfer loop
    };
    while (1) {
        for (int j = i; j < LED_NUMBERS; j += 3) {
            led_strip_pixels[j * 3 + 0] = 0;
            led_strip_pixels[j * 3 + 1] = 0;
            led_strip_pixels[j * 3 + 2] = 110;
        }
        // Flush RGB values to LEDs
        ESP_ERROR_CHECK(
            rmt_transmit(led_chan, led_encoder, led_strip_pixels, LED_NUMBERS * 3, &tx_config));
        ESP_ERROR_CHECK(rmt_tx_wait_all_done(led_chan, portMAX_DELAY));

        // vTaskDelay(pdMS_TO_TICKS(CHASE_SPEED_MS));
        // memset(led_strip_pixels, 0, sizeof(led_strip_pixels));
        // ESP_ERROR_CHECK(rmt_transmit(led_chan, led_encoder, led_strip_pixels,
        //                              sizeof(led_strip_pixels), &tx_config));
        // ESP_ERROR_CHECK(rmt_tx_wait_all_done(led_chan, portMAX_DELAY));
        // vTaskDelay(pdMS_TO_TICKS(CHASE_SPEED_MS));
    }
}
