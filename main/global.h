#define TAG "FREQ_ANALYZER"

// --- Configuration ---
#define SAMPLE_FREQ_HZ 20000  //  sampling rate
#define SAMPLE_SIZE 256       // Must be power of 2 for FFT
#define SOUND_DIGITAL_PIN GPIO_NUM_4
#define SOUND_ANALOG_CHAN ADC_CHANNEL_4  // GPIO 5
//
#define LED_NUMBERS 500
