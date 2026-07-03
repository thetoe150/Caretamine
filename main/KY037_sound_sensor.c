#include "KY037_sound_sensor.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "dsps_fft2r.h"
#include "esp_adc/adc_continuous.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "FREQ_ANALYZER"

// --- Configuration ---
#define SAMPLE_FREQ_HZ 10000  //  sampling rate
#define READ_LEN 512          // Must be power of 2 for FFT
#define SOUND_DIGITAL_PIN GPIO_NUM_4
#define SOUND_ANALOG_CHAN ADC_CHANNEL_4  // GPIO 5

static float fft_input[READ_LEN * 2];  // [Real, Imag, Real, Imag...]
static float magnitudes[READ_LEN / 2];

static QueueHandle_t gpio_evt_queue = NULL;
static SemaphoreHandle_t adc_semaphore = NULL;

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

void setup_adc_AO(adc_continuous_handle_t* i_handle) {
    adc_semaphore = xSemaphoreCreateBinary();

    adc_continuous_handle_cfg_t adc_config = {
        .max_store_buf_size = 2048,
        .conv_frame_size =
            READ_LEN * SOC_ADC_DIGI_DATA_BYTES_PER_CONV,  // 2 bytes per sample (12-bit)
    };
    ESP_ERROR_CHECK(adc_continuous_new_handle(&adc_config, i_handle));

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

    ESP_ERROR_CHECK(adc_continuous_config(*i_handle, &dig_cfg));

    adc_continuous_evt_cbs_t cbs = {.on_conv_done = adc_conv_done_cb};
    ESP_ERROR_CHECK(adc_continuous_register_event_callbacks(*i_handle, &cbs, 0));
    ESP_ERROR_CHECK(adc_continuous_start(*i_handle));
}

float get_silence_offset(adc_continuous_handle_t adc_handle, uint8_t* buf, size_t size) {
    // Wait for hardware to fill the buffer
    xSemaphoreTake(adc_semaphore, portMAX_DELAY);

    uint32_t ret_num = 0;
    int sum = 0;
    int count = 0;
    float silence_offset = 0;
    if (adc_continuous_read(adc_handle, buf, size, &ret_num, 0) == ESP_OK) {
        for (int i = 5; i < READ_LEN; i++) {
            // The Type1 format stores channel info in high bits; mask them out
            adc_digi_output_data_t* p =
                (adc_digi_output_data_t*)&buf[i * SOC_ADC_DIGI_RESULT_BYTES];
            uint32_t val = p->type2.data;
            sum += val;
            count++;
        }
        silence_offset = sum / count;
        ESP_LOGI(TAG, "Calibrated silence offset: %.2f", (float)silence_offset);
    }

    return silence_offset;
}

void init_KY037_sound_sensor(void) {}

void capture_sound(void* args) {
    setup_adc_DO();
    adc_continuous_handle_t adc_handle = {};
    setup_adc_AO(&adc_handle);

    ESP_ERROR_CHECK(dsps_fft2r_init_fc32(NULL, CONFIG_DSP_MAX_FFT_SIZE));

    uint8_t* result_buf = (uint8_t*)malloc(READ_LEN * SOC_ADC_DIGI_RESULT_BYTES);
    memset(result_buf, 0, READ_LEN * SOC_ADC_DIGI_RESULT_BYTES);

    float silence_offset =
        get_silence_offset(adc_handle, result_buf, READ_LEN * SOC_ADC_DIGI_RESULT_BYTES);

    uint32_t ret_num = 0;
    while (1) {
        // Wait for hardware to fill the buffer
        xSemaphoreTake(adc_semaphore, portMAX_DELAY);

        if (adc_continuous_read(adc_handle, result_buf, READ_LEN * SOC_ADC_DIGI_RESULT_BYTES,
                                &ret_num, 0) == ESP_OK) {
            if (ret_num < READ_LEN * SOC_ADC_DIGI_RESULT_BYTES) {
                // Not enough data! Skip this loop iteration
                continue;
            }
            int sum = 0;
            for (int i = 0; i < READ_LEN; i++) {
                // The Type1 format stores channel info in high bits; mask them out
                adc_digi_output_data_t* p =
                    (adc_digi_output_data_t*)&result_buf[i * SOC_ADC_DIGI_RESULT_BYTES];
                uint32_t val = p->type2.data;
                sum += val;

                // ESP_LOGI(TAG, "read val: %.1f", (float)val);
                fft_input[i * 2 + 0] = (float)val - silence_offset;  // Real (Remove DC Bias)
                fft_input[i * 2 + 1] = 0.0f;                         // Imaginary
            }

            // Perform FFT & time domain to frequency domain
            dsps_fft2r_fc32(fft_input, READ_LEN);
            dsps_bit_rev_fc32(fft_input, READ_LEN);

            // Analyze Frequencies
            float max_mag = 0;
            int peak_idx = 0;
            for (int i = 2; i < READ_LEN / 2; i++) {  // Skip bin 0 & 1 (DC noise)
                float r = fft_input[i * 2 + 0];
                float im = fft_input[i * 2 + 1];
                magnitudes[i] = sqrtf(r * r + im * im);

                if (magnitudes[i] > max_mag) {
                    max_mag = magnitudes[i];
                    peak_idx = i;
                }
            }

            float freq = (float)peak_idx * SAMPLE_FREQ_HZ / READ_LEN;
            if (max_mag > 1000) {  // Detection threshold
                ESP_LOGI(TAG, "Average val: %.1d | Peak Frequency: %.1f Hz | Magnitude: %.1f",
                         sum / READ_LEN, freq, max_mag);
            }
        }
    }
}
