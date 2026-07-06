#include "FFT_processing.h"

static float samples[SAMPLE_SIZE * 2];  // [Real, Imag, Real, Imag...]
static float magnitudes[SAMPLE_SIZE / 2];

void process_adc_sound(uint8_t* digi_sound_buf, float silence_offset) {
    ESP_ERROR_CHECK(dsps_fft2r_init_fc32(NULL, CONFIG_DSP_MAX_FFT_SIZE));

    int sum = 0;
    for (int i = 0; i < SAMPLE_SIZE; i++) {
        // The Type1 format stores channel info in high bits; mask them out
        adc_digi_output_data_t* p =
            (adc_digi_output_data_t*)&digi_sound_buf[i * SOC_ADC_DIGI_RESULT_BYTES];
        uint32_t val = p->type2.data;
        sum += val;

        // ESP_LOGI(TAG, "read val: %.1f", (float)val);
        samples[i * 2 + 0] = (float)val - silence_offset;  // Real (Remove DC Bias)
        samples[i * 2 + 1] = 0.0f;                         // Imaginary
    }

    // Perform FFT & time domain to frequency domain
    dsps_fft2r_fc32(samples, SAMPLE_SIZE);
    dsps_bit_rev_fc32(samples, SAMPLE_SIZE);

    // Analyze Frequencies
    float max_mag = 0;
    int peak_idx = 0;
    for (int i = 2; i < SAMPLE_SIZE / 2; i++) {  // Skip bin 0 & 1 (DC noise)
        float r = samples[i * 2 + 0];
        float im = samples[i * 2 + 1];
        magnitudes[i] = sqrtf(r * r + im * im);

        if (magnitudes[i] > max_mag) {
            max_mag = magnitudes[i];
            peak_idx = i;
        }
    }

    float freq = (float)peak_idx * SAMPLE_FREQ_HZ / SAMPLE_SIZE;
    if (max_mag > 1000) {  // Detection threshold
        ESP_LOGI(TAG, "Average val: %.1d | Peak Frequency: %.1f Hz | Magnitude: %.1f",
                 sum / SAMPLE_SIZE, freq, max_mag);
    }
}

/**
 * @brief Simple helper function, converting HSV color space to RGB color space
 *
 * Wiki: https://en.wikipedia.org/wiki/HSL_and_HSV
 *
 */
void led_strip_hsv2rgb(uint32_t h, uint32_t s, uint32_t v, uint32_t* r, uint32_t* g, uint32_t* b) {
    h %= 360;  // h -> [0,360]
    uint32_t rgb_max = v * 2.55f;
    uint32_t rgb_min = rgb_max * (100 - s) / 100.0f;

    uint32_t i = h / 60;
    uint32_t diff = h % 60;

    // RGB adjustment amount by hue
    uint32_t rgb_adj = (rgb_max - rgb_min) * diff / 60;

    switch (i) {
        case 0:
            *r = rgb_max;
            *g = rgb_min + rgb_adj;
            *b = rgb_min;
            break;
        case 1:
            *r = rgb_max - rgb_adj;
            *g = rgb_max;
            *b = rgb_min;
            break;
        case 2:
            *r = rgb_min;
            *g = rgb_max;
            *b = rgb_min + rgb_adj;
            break;
        case 3:
            *r = rgb_min;
            *g = rgb_max - rgb_adj;
            *b = rgb_max;
            break;
        case 4:
            *r = rgb_min + rgb_adj;
            *g = rgb_min;
            *b = rgb_max;
            break;
        default:
            *r = rgb_max;
            *g = rgb_min;
            *b = rgb_max - rgb_adj;
            break;
    }
}

void frequency_to_LED_position1(uint8_t* digi_sound_buf, uint8_t* led_color_buf,
                                float silence_offset) {
    process_adc_sound(digi_sound_buf, silence_offset);
    unsigned int freq_count = SAMPLE_SIZE / 2;
    memset(led_color_buf, 0, LED_NUMBERS * 3);

    for (int freq_idx = 0; freq_idx < freq_count; freq_idx++) {
        uint32_t red = 0;
        uint32_t green = 0;
        uint32_t blue = 0;
        uint32_t hue = freq_idx * 360 / freq_count;
        uint32_t value = magnitudes[freq_idx];
        uint32_t led_idx = freq_idx * LED_NUMBERS / freq_count;

        led_strip_hsv2rgb(hue, 128, value, &red, &green, &blue);
        led_color_buf[led_idx * 3 + 0] = green;
        led_color_buf[led_idx * 3 + 1] = blue;
        led_color_buf[led_idx * 3 + 2] = red;
    }
}

// Used when LED_NUMBERS > SAMPLE_SIZE / 2
void frequency_to_LED_position2(uint8_t* digi_sound_buf, uint8_t* led_color_buf,
                                float silence_offset) {
    process_adc_sound(digi_sound_buf, silence_offset);
    unsigned int led_per_freq = LED_NUMBERS / SAMPLE_SIZE;
    unsigned int freq_count = SAMPLE_SIZE / 2;
    memset(led_color_buf, 0, LED_NUMBERS * 3);

    for (int led_idx = 0; led_idx < LED_NUMBERS; led_idx += led_per_freq) {
        uint32_t red = 0;
        uint32_t green = 0;
        uint32_t blue = 0;
        uint32_t freq_idx = led_idx * freq_count / LED_NUMBERS;
        uint32_t hue = freq_idx * 360 / freq_count;
        uint32_t value = magnitudes[freq_idx];

        led_strip_hsv2rgb(hue, 128, value, &red, &green, &blue);
        for (unsigned int i = 0; i < led_per_freq; i++) {
            uint32_t idx = led_idx + i;
            led_color_buf[idx * 3 + 0] = green;
            led_color_buf[idx * 3 + 1] = blue;
            led_color_buf[idx * 3 + 2] = red;
        }
    }
}
