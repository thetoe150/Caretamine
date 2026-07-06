#include <stdint.h>

#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "global.h"
#include "string.h"

typedef struct {
    uint32_t resolution; /*!< Encoder resolution, in Hz */
} led_strip_encoder_config_t;

esp_err_t rmt_new_led_strip_encoder(const led_strip_encoder_config_t* config,
                                    rmt_encoder_handle_t* ret_encoder);

void update_led_strip(void* args);
