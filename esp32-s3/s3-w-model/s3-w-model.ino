/* * Edge AI Voice Command - ESP32-S3 Zero Standalone Inference
 */

#include <SmartWheelchair-Final_inferencing.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"

// --- KONFIGURASI PINOUT S3 ZERO ---
#define I2S_WS   9   // LRCLK
#define I2S_SCK  8   // BCLK
#define I2S_SD   7   // DOUT

i2s_chan_handle_t rx_chan;

/** Audio buffers, pointers and selectors */
typedef struct {
    signed short *buffers[2];
    unsigned char buf_select;
    unsigned char buf_ready;
    unsigned int buf_count;
    unsigned int n_samples;
} inference_t;

static inference_t inference;
static const uint32_t sample_buffer_size = 2048;
static signed short sampleBuffer[sample_buffer_size];
static bool debug_nn = false; 
static int print_results = -(EI_CLASSIFIER_SLICES_PER_MODEL_WINDOW);
static bool record_status = true;

// --- DEKLARASI FUNGSI ---
static bool microphone_inference_start(uint32_t n_samples);
static bool microphone_inference_record(void);
static int microphone_audio_signal_get_data(size_t offset, size_t length, float *out_ptr);
static void microphone_inference_end(void);
void setupI2S();

void setup() {
    Serial.begin(115200);
    delay(2000); // Waktu inisialisasi Native USB S3 Zero

    Serial.println("\n[S3 Zero] Sistem Edge AI Kursi Roda Memulai...");

    // Ringkasan pengaturan model Edge Impulse
    ei_printf("Pengaturan Inferensi:\n");
    ei_printf("\tInterval: "); ei_printf_float((float)EI_CLASSIFIER_INTERVAL_MS); ei_printf(" ms.\n");
    ei_printf("\tFrame size: %d\n", EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE);
    ei_printf("\tSample length: %d ms.\n", EI_CLASSIFIER_RAW_SAMPLE_COUNT / 16);
    ei_printf("\tNo. of classes: %d\n", sizeof(ei_classifier_inferencing_categories) / sizeof(ei_classifier_inferencing_categories[0]));

    run_classifier_init();
    
    // Inisialisasi Mikrofon
    setupI2S();

    ei_printf("\nMemulai Continuous Inference...\n");

    if (microphone_inference_start(EI_CLASSIFIER_SLICE_SIZE) == false) {
        ei_printf("ERR: Gagal mengalokasikan buffer audio!\r\n");
        return;
    }
}

void loop() {
    bool m = microphone_inference_record();
    if (!m) {
        ei_printf("ERR: Gagal merekam audio...\n");
        return;
    }

    signal_t signal;
    signal.total_length = EI_CLASSIFIER_SLICE_SIZE;
    signal.get_data = &microphone_audio_signal_get_data;
    ei_impulse_result_t result = {0};

    // Jalankan klasifikasi
    EI_IMPULSE_ERROR r = run_classifier_continuous(&signal, &result, debug_nn);
    if (r != EI_IMPULSE_OK) {
        ei_printf("ERR: Gagal menjalankan classifier (%d)\n", r);
        return;
    }

    if (++print_results >= (EI_CLASSIFIER_SLICES_PER_MODEL_WINDOW)) {
        ei_printf("\n--- Hasil Prediksi ---\n");
        
        float probTertinggi = 0.0;
        String kelasTertinggi = "";

        for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
            float val = result.classification[ix].value;
            const char *label = result.classification[ix].label;
            
            ei_printf("    %s: ", label);
            ei_printf_float(val);
            ei_printf("\n");

            // Cari kelas tertinggi selain derau untuk mempermudah logika nanti
            if (strcmp(label, "derau") != 0 && val > probTertinggi) {
                probTertinggi = val;
                kelasTertinggi = label;
            }
        }

        print_results = 0;
    }
}

// ==========================================================
// FUNGSI PENGOLAHAN AUDIO (DIADAPTASI UNTUK S3 ZERO)
// ==========================================================

static void audio_inference_callback(uint32_t n_samples) {
    for(int i = 0; i < n_samples; i++) {
        inference.buffers[inference.buf_select][inference.buf_count++] = sampleBuffer[i];

        if(inference.buf_count >= inference.n_samples) {
            inference.buf_select ^= 1;
            inference.buf_count = 0;
            inference.buf_ready = 1;
        }
    }
}

static void capture_samples(void* arg) {
    const int32_t samples_to_read = 512; 
    int32_t raw_samples[samples_to_read];
    size_t bytes_read = 0;

    while (record_status) {
        // Baca data 32-bit mentah dari I2S API Baru
        if (i2s_channel_read(rx_chan, raw_samples, sizeof(raw_samples), &bytes_read, portMAX_DELAY) == ESP_OK) {
            int sampleCount = bytes_read / 4; // 1 sampel = 4 byte

            // Konversi 32-bit ke 16-bit dengan logika yang SAMA PERSIS dengan dataset
            for (int i = 0; i < sampleCount; i++) {
                sampleBuffer[i] = (int16_t)(raw_samples[i] >> 14);
            }

            if (record_status) {
                audio_inference_callback(sampleCount);
            } else {
                break;
            }
        }
    }
    vTaskDelete(NULL);
}

// Inisialisasi I2S API Baru (Menggantikan i2s_init lama dari EI)
void setupI2S() {
    i2s_chan_config_t rx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    i2s_new_channel(&rx_chan_cfg, NULL, &rx_chan);

    i2s_std_config_t rx_std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(16000),
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

static bool microphone_inference_start(uint32_t n_samples) {
    inference.buffers[0] = (signed short *)malloc(n_samples * sizeof(signed short));
    if (inference.buffers[0] == NULL) return false;

    inference.buffers[1] = (signed short *)malloc(n_samples * sizeof(signed short));
    if (inference.buffers[1] == NULL) {
        ei_free(inference.buffers[0]);
        return false;
    }

    inference.buf_select = 0;
    inference.buf_count = 0;
    inference.n_samples = n_samples;
    inference.buf_ready = 0;

    record_status = true;

    // Jalankan Task pembacaan mic di background
    xTaskCreate(capture_samples, "CaptureSamples", 1024 * 32, NULL, 10, NULL);
    return true;
}

static bool microphone_inference_record(void) {
    bool ret = true;
    if (inference.buf_ready == 1) {
        ei_printf("Error sample buffer overrun.\n");
        ret = false;
    }
    while (inference.buf_ready == 0) {
        delay(1);
    }
    inference.buf_ready = 0;
    return true;
}

static int microphone_audio_signal_get_data(size_t offset, size_t length, float *out_ptr) {
    numpy::int16_to_float(&inference.buffers[inference.buf_select ^ 1][offset], out_ptr, length);
    return 0;
}

static void microphone_inference_end(void) {
    i2s_channel_disable(rx_chan);
    i2s_del_channel(rx_chan);
    ei_free(inference.buffers[0]);
    ei_free(inference.buffers[1]);
}