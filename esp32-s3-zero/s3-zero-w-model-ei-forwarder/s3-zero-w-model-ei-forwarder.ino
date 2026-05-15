/* * Firmware Debugging Inferensi - Tampilkan Semua Prediksi
 * Menggunakan driver I2S modern dari dataset-record.ino
 */

#include <SmartWheelchair_inferencing.h> 
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Pinout S3 Zero [cite: 3, 4]
#define I2S_WS          9
#define I2S_SCK         8
#define I2S_SD          7
#define SAMPLE_RATE     16000
#define GAIN_FACTOR     8 // Samakan dengan saat merekam [cite: 11]

typedef struct {
    int16_t *buffers[2];
    uint8_t  buf_select;
    uint8_t  buf_ready;
    uint32_t buf_count;
    uint32_t n_samples;
} inference_t;

static inference_t inference;
static i2s_chan_handle_t rx_chan;
static bool record_status = false;

void setupI2S() {
    i2s_chan_config_t rx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    i2s_new_channel(&rx_chan_cfg, NULL, &rx_chan);

    i2s_std_config_t rx_std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = (gpio_num_t)I2S_SCK,
            .ws   = (gpio_num_t)I2S_WS,
            .dout = I2S_GPIO_UNUSED,
            .din  = (gpio_num_t)I2S_SD,
            .invert_flags = { false, false, false },
        },
    };
    rx_std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
    i2s_channel_init_std_mode(rx_chan, &rx_std_cfg);
    i2s_channel_enable(rx_chan);
}

static void capture_samples_task(void* arg) {
    const uint32_t samples_to_read = 128;
    int32_t raw_samples[samples_to_read];
    size_t bytes_read = 0;

    while (record_status) {
        if (i2s_channel_read(rx_chan, raw_samples, sizeof(raw_samples), &bytes_read, portMAX_DELAY) == ESP_OK) {
            uint32_t sampleCount = bytes_read / 4;
            for (uint32_t i = 0; i < sampleCount; i++) {
                // Bit-shift 14 kali agar format data sama dengan dataset [cite: 10]
                int32_t pcm16 = (int32_t)(raw_samples[i] >> 14);
                pcm16 *= GAIN_FACTOR; 

                if (pcm16 > 32767) pcm16 = 32767; 
                if (pcm16 < -32768) pcm16 = -32768; 

                inference.buffers[inference.buf_select][inference.buf_count++] = (int16_t)pcm16;

                if (inference.buf_count >= inference.n_samples) {
                    inference.buf_select ^= 1;
                    inference.buf_count = 0;
                    inference.buf_ready = 1;
                }
            }
        }
    }
    vTaskDelete(NULL);
}

static int microphone_audio_signal_get_data(size_t offset, size_t length, float *out_ptr) {
    numpy::int16_to_float(&inference.buffers[inference.buf_select ^ 1][offset], out_ptr, length); 
    return 0;
}

void setup() {
    Serial.begin(115200);
    while (!Serial);

    Serial.println("=== DEBUG MODE: Menampilkan Semua Prediksi ===");

    inference.n_samples = EI_CLASSIFIER_SLICE_SIZE;
    inference.buffers[0] = (int16_t *)malloc(inference.n_samples * sizeof(int16_t));
    inference.buffers[1] = (int16_t *)malloc(inference.n_samples * sizeof(int16_t));
    
    run_classifier_init(); 
    setupI2S();

    record_status = true;
    xTaskCreate(capture_samples_task, "CaptureSamples", 1024 * 8, NULL, 10, NULL);
}

void loop() {
    while (inference.buf_ready == 0) {
        delay(1);
    }
    inference.buf_ready = 0;

    signal_t signal;
    signal.total_length = EI_CLASSIFIER_SLICE_SIZE;
    signal.get_data = &microphone_audio_signal_get_data; 

    ei_impulse_result_t result = {0};
    // Menggunakan run_classifier biasa (bukan continuous) agar kita bisa melihat output mentah
    EI_IMPULSE_ERROR r = run_classifier(&signal, &result, false); 
    
    if (r != EI_IMPULSE_OK) return;

    // CETAK SEMUA HASIL 
    Serial.print("Output: ");
    for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
        Serial.print(result.classification[ix].label);
        Serial.print(": ");
        Serial.print(result.classification[ix].value, 4); // 4 angka di belakang koma
        Serial.print(" | ");
    }
    Serial.println();
}