#include "KY037_sound_sensor.h"

#include "esp_check.h"

#define SOUND_DIGITAL_PIN GPIO_NUM_4
#define SOUND_ANALOG_UNIT ADC_UNIT_1
#define SOUND_ANALOG_CHAN ADC_CHANNEL_4  // GPIO 5

static QueueHandle_t gpio_evt_queue = NULL;
static SemaphoreHandle_t adc_semaphore = NULL;
static adc_continuous_handle_t adc_handle = {};

static uint8_t* raw_samples = NULL;
static uint32_t* parsed_samples = NULL;
adc_continuous_data_t parsed_data[SAMPLE_BUFFER_SIZE / SOC_ADC_DIGI_RESULT_BYTES];

static const char* SOUND_TAG = "sound_sensor";

// ISR Handler: Sends a trigger message to the queue
static void IRAM_ATTR DO_isr_handler(void* arg) {
    uint32_t gpio_num = (uint32_t)arg;
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
}

// Task to handle the sound trigger (Printing outside of ISR)
static void sound_event_task(void* arg) {
    uint32_t io_num;
    for (;;) {
        if (xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY)) {
            ESP_LOGW(SOUND_TAG, "GPIO[%" PRIu32 "] Triggered: Sound Threshold Exceeded!", io_num);
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
    gpio_isr_handler_add(SOUND_DIGITAL_PIN, DO_isr_handler, (void*)SOUND_DIGITAL_PIN);
}

void stop_sensor() {
    // Flush residual data out of the internal ring buffer
    uint8_t dummy_buf[128];
    uint32_t bytes_read = 0;
    while (adc_continuous_read(adc_handle, dummy_buf, sizeof(dummy_buf), &bytes_read, 0) ==
           ESP_OK) {
        // Loop until empty
    }

    ESP_ERROR_CHECK(adc_continuous_stop(adc_handle));
}

void start_sound_sensor() {
    ESP_ERROR_CHECK(adc_continuous_start(adc_handle));
}

void setup_adc_AO() {
    adc_semaphore = xSemaphoreCreateBinary();

    adc_continuous_handle_cfg_t adc_config = {
        .max_store_buf_size = SAMPLE_BUFFER_SIZE * 2,
        .conv_frame_size = SAMPLE_BUFFER_SIZE,
        // When the pool is full, the old data in the buffer pool will be
        // automatically flushed and new data will be written. Otherwise,
        // when the pool is full, new conversion results will be lost.
        .flags.flush_pool = true,
    };
    ESP_ERROR_CHECK(adc_continuous_new_handle(&adc_config, &adc_handle));

    adc_digi_pattern_config_t adc_pattern = {0};
    adc_pattern.atten = ADC_ATTEN_DB_12;
    adc_pattern.unit = SOUND_ANALOG_UNIT;
    adc_pattern.channel = SOUND_ANALOG_CHAN & 0x7;
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
}

float init_KY037_sound_sensor() {
    // setup_adc_DO();
    setup_adc_AO();

    int sum = 0;
    int count = 0;

    raw_samples = (uint8_t*)malloc(SAMPLE_BUFFER_SIZE);
    parsed_samples = (uint32_t*)malloc(SAMPLE_COUNT);

    // get silence offset
    ESP_ERROR_CHECK(adc_continuous_start(adc_handle));
    xSemaphoreTake(adc_semaphore, portMAX_DELAY);
    memset(raw_samples, 0, SAMPLE_BUFFER_SIZE);
    uint32_t ret_num = 0;
    ESP_ERROR_CHECK(adc_continuous_read(adc_handle, raw_samples, SAMPLE_BUFFER_SIZE, &ret_num, 0));

    for (int i = 2; i < SAMPLE_COUNT; i++) {
        // The Type1 format stores channel info in high bits; mask them out
        adc_digi_output_data_t* p =
            (adc_digi_output_data_t*)&raw_samples[i * SOC_ADC_DIGI_RESULT_BYTES];
        uint32_t val = p->type2.data;
        sum += val;
        count++;
    }
    float silence_offset = (float)sum / count;
    ESP_LOGI(SOUND_TAG, "Calibrated silence offset: %.2f", silence_offset);
    stop_sensor();

    return silence_offset;
}

void deinit_KY037_sensor() {
    free(raw_samples);
    free(parsed_samples);
    ESP_ERROR_CHECK(adc_continuous_stop(adc_handle));
    ESP_ERROR_CHECK(adc_continuous_deinit(adc_handle));
}

uint32_t* capture_and_parse_sound(uint32_t* size) {
    uint32_t ret_num = 0;
    // Wait for hardware to fill the buffer
    xSemaphoreTake(adc_semaphore, portMAX_DELAY);
    memset(raw_samples, 0, SAMPLE_BUFFER_SIZE);
    // ESP_LOGI(
    //     SOUND_TAG, "Return value: %s",
    //     esp_err_to_name(adc_continuous_read(adc_handle, samples, SAMPLE_BUFFER_SIZE, &ret_num,
    //     0)));
    ESP_ERROR_CHECK(adc_continuous_read(adc_handle, raw_samples, SAMPLE_BUFFER_SIZE, &ret_num, 0));

    uint32_t num_parsed_samples = 0;

    esp_err_t parse_ret = adc_continuous_parse_data(adc_handle, raw_samples, ret_num, parsed_data,
                                                    &num_parsed_samples);

    // ESP_GOTO_ON_FALSE(num_parsed_samples == SAMPLE_COUNT, ESP_ERR_INVALID_STATE, err, SOUND_TAG,
    //                   "invalid argument");

    *size = num_parsed_samples * SOC_ADC_DIGI_RESULT_BYTES;
    if (parse_ret == ESP_OK) {
        for (int i = 0; i < num_parsed_samples; i++) {
            if (parsed_data[i].valid) {
                // ESP_LOGI(SOUND_TAG, "ADC%d, Channel: %d, Value: %" PRIu32, parsed_data[i].unit +
                // 1,
                //          parsed_data[i].channel, parsed_data[i].raw_data);

                parsed_samples[i] = parsed_data[i].raw_data;
            } else {
                ESP_LOGW(SOUND_TAG, "Invalid data [ADC%d_Ch%d_%" PRIu32 "]",
                         parsed_data[i].unit + 1, parsed_data[i].channel, parsed_data[i].raw_data);
            }
        }
    } else {
        ESP_LOGE(SOUND_TAG, "Data parsing failed: %s", esp_err_to_name(parse_ret));
    }

    /**
     * Because printing is slow, so every time you call `ulTaskNotifyTake`, it will immediately
     * return. To avoid a task watchdog timeout, add a delay here. When you replace the way you
     * process the data, usually you don't need this delay (as this task will block for a while).
     */
    vTaskDelay(1);

    return parsed_samples;
}
