#define TAG "FREQ_ANALYZER"

// --- Configuration ---
#define SAMPLE_FREQ_HZ 10000  //  sampling rate
#define SAMPLE_SIZE 512       // Must be power of 2 for FFT
#define SOUND_DIGITAL_PIN GPIO_NUM_4
#define SOUND_ANALOG_CHAN ADC_CHANNEL_4  // GPIO 5
