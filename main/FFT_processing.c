#include "FFT_processing.h"

static float fft_input[SAMPLE_SIZE * 2];  // [Real, Imag, Real, Imag...]
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
        fft_input[i * 2 + 0] = (float)val - silence_offset;  // Real (Remove DC Bias)
        fft_input[i * 2 + 1] = 0.0f;                         // Imaginary
    }

    // Perform FFT & time domain to frequency domain
    dsps_fft2r_fc32(fft_input, SAMPLE_SIZE);
    dsps_bit_rev_fc32(fft_input, SAMPLE_SIZE);

    // Analyze Frequencies
    float max_mag = 0;
    int peak_idx = 0;
    for (int i = 2; i < SAMPLE_SIZE / 2; i++) {  // Skip bin 0 & 1 (DC noise)
        float r = fft_input[i * 2 + 0];
        float im = fft_input[i * 2 + 1];
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

void frequency_to_LED_position() {}
void frequency_to_LED_color() {}
