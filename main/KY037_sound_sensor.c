#include "KY037_sound_sensor.h"

static QueueHandle_t gpio_evt_queue = NULL;
static SemaphoreHandle_t adc_semaphore = NULL;
static adc_continuous_handle_t adc_handle = {};

uint32_t sample_size = 0;
uint8_t* sound_samples = NULL;

// ISR Handler: Sends a trigger message to the queue
static void IRAM_ATTR DO_sound_isr_handler(void* arg) {
    uint32_t gpio_num = (uint32_t)arg;
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
}

// Task to handle the sound trigger (Printing outside of ISR)
static void sound_event_task(void* arg) {
    uint32_t io_num;
    for (;;) {
        if (xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY)) {
            ESP_LOGW(TAG, "GPIO[%" PRIu32 "] Triggered: Sound Threshold Exceeded!", io_num);
        }
    }
}

// Callback for when ADC has a chunk of data ready
static bool IRAM_ATTR adc_conv_done_cb(adc_continuous_handle_t handle,
                                       const adc_continuous_evt_data_t* edata, void* user_data) {
    BaseType_t mustYield = pdFALSE;
    xSemaphoreGiveFromISR(adc_semaphore, &mustYield);
    return (mustYield == pdTRUE);
}

void setup_adc_DO(void) {
    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    xTaskCreate(sound_event_task, "sound_event_task", 2048, NULL, 10, NULL);

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << SOUND_DIGITAL_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_POSEDGE,
    };
    gpio_config(&io_conf);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(SOUND_DIGITAL_PIN, DO_sound_isr_handler, (void*)SOUND_DIGITAL_PIN);
}

void setup_adc_AO() {
    adc_semaphore = xSemaphoreCreateBinary();

    adc_continuous_handle_cfg_t adc_config = {
        .max_store_buf_size = 2048,
        .conv_frame_size =
            SAMPLE_SIZE * SOC_ADC_DIGI_DATA_BYTES_PER_CONV,  // 2 bytes per sample (12-bit)
    };
    ESP_ERROR_CHECK(adc_continuous_new_handle(&adc_config, &adc_handle));

    adc_digi_pattern_config_t adc_pattern = {0};
    adc_pattern.atten = ADC_ATTEN_DB_12;
    adc_pattern.channel = SOUND_ANALOG_CHAN & 0x7;
    adc_pattern.unit = ADC_UNIT_1;
    adc_pattern.bit_width = ADC_BITWIDTH_12;

    adc_continuous_config_t dig_cfg = {0};
    dig_cfg.sample_freq_hz = SAMPLE_FREQ_HZ;
    dig_cfg.conv_mode = ADC_CONV_SINGLE_UNIT_1;
    dig_cfg.format = ADC_DIGI_OUTPUT_FORMAT_TYPE2;
    dig_cfg.pattern_num = 1;
    dig_cfg.adc_pattern = &adc_pattern;

    ESP_ERROR_CHECK(adc_continuous_config(adc_handle, &dig_cfg));

    adc_continuous_evt_cbs_t cbs = {.on_conv_done = adc_conv_done_cb};
    ESP_ERROR_CHECK(adc_continuous_register_event_callbacks(adc_handle, &cbs, 0));
    ESP_ERROR_CHECK(adc_continuous_start(adc_handle));
}

uint8_t* init_KY037_sound_sensor(float* silence_offset, uint32_t* size) {
    setup_adc_DO();
    setup_adc_AO();

    xSemaphoreTake(adc_semaphore, portMAX_DELAY);
    uint32_t ret_num = 0;
    int sum = 0;
    int count = 0;

    *size = SAMPLE_SIZE * SOC_ADC_DIGI_RESULT_BYTES;
    uint8_t* samples = (uint8_t*)malloc(*size);
    sound_samples = samples;
    sample_size = *size;

    uint32_t read_size = capture_sound(samples, *size);

    for (int i = 5; i < SAMPLE_SIZE; i++) {
        // The Type1 format stores channel info in high bits; mask them out
        adc_digi_output_data_t* p =
            (adc_digi_output_data_t*)&samples[i * SOC_ADC_DIGI_RESULT_BYTES];
        uint32_t val = p->type2.data;
        sum += val;
        count++;
    }
    *silence_offset = (float)sum / count;
    ESP_LOGI(TAG, "Calibrated silence offset: %.2f", *silence_offset);

    return samples;
}

void deinit_KY037_sound_sensor() {
    free(sound_samples);
}

uint32_t capture_sound(uint8_t* samples, uint32_t size) {
    uint32_t ret_num = 0;
    // Wait for hardware to fill the buffer
    xSemaphoreTake(adc_semaphore, portMAX_DELAY);

    if (adc_continuous_read(adc_handle, samples, size, &ret_num, 0) == ESP_OK) {
        return ret_num;
    }
}
