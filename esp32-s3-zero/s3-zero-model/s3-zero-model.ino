/* * ESP32-S3 Zero Continuous Keyword Spotting
 * Membaca audio dari INMP441 dan menjalankan model klasifikasi secara real-time
 */

// GANTI NAMA LIBRARY INI SESUAI DENGAN HASIL EXPORT DARI EDGE IMPULSE
#include <SmartWheelchair_inferencing.h> 

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"

// --- KONFIGURASI PINOUT S3 ZERO & AUDIO ---
#define I2S_WS   9
#define I2S_SCK  8
#define I2S_SD   7
#define GAIN_FACTOR 8

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

i2s_chan_handle_t rx_chan; // Handle I2S standar baru

// Deklarasi fungsi
static bool microphone_inference_start(uint32_t n_samples);
static bool microphone_inference_record(void);
static int microphone_audio_signal_get_data(size_t offset, size_t length, float *out_ptr);
static void microphone_inference_end(void);
static int i2s_init_custom(uint32_t sampling_rate);
static int i2s_deinit_custom(void);

void setup() {
    Serial.begin(115200);
    while (!Serial);
    Serial.println("Edge Impulse Continuous Inferencing Demo - ESP32-S3 Zero");

    // Ringkasan konfigurasi model
    ei_printf("Inferencing settings:\n");
    ei_printf("\tInterval: %.2f ms.\n", (float)EI_CLASSIFIER_INTERVAL_MS);
    ei_printf("\tFrame size: %d\n", EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE);
    ei_printf("\tSample length: %d ms.\n", EI_CLASSIFIER_RAW_SAMPLE_COUNT / 16);
    ei_printf("\tNo. of classes: %d\n", sizeof(ei_classifier_inferencing_categories) / sizeof(ei_classifier_inferencing_categories[0]));

    run_classifier_init();
    ei_printf("\nStarting continuous inference in 2 seconds...\n");
    ei_sleep(2000);

    if (microphone_inference_start(EI_CLASSIFIER_SLICE_SIZE) == false) {
        ei_printf("ERR: Could not allocate audio buffer. Check memory.\n");
        return;
    }
    ei_printf("Sistem Siap. Mulai berbicara...\n");
}

void loop() {
    bool m = microphone_inference_record();
    if (!m) {
        ei_printf("ERR: Failed to record audio...\n");
        return;
    }

    signal_t signal;
    signal.total_length = EI_CLASSIFIER_SLICE_SIZE;
    signal.get_data = &microphone_audio_signal_get_data;
    ei_impulse_result_t result = {0};

    // Eksekusi klasifikasi
    EI_IMPULSE_ERROR r = run_classifier_continuous(&signal, &result, debug_nn);
    if (r != EI_IMPULSE_OK) {
        ei_printf("ERR: Failed to run classifier (%d)\n", r);
        return;
    }

    // Tampilkan hasil saat 1 jendela waktu penuh tercapai
    if (++print_results >= (EI_CLASSIFIER_SLICES_PER_MODEL_WINDOW)) {
        ei_printf("\n--- Hasil Prediksi ---\n");
        for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
            ei_printf("%10s: %.5f\n", result.classification[ix].label, result.classification[ix].value);
        }
        print_results = 0;
    }
}

// Callback untuk memasukkan data yang sudah di-scale ke buffer EI
static void audio_inference_callback(uint32_t n_bytes) {
    for(int i = 0; i < n_bytes>>1; i++) {
        inference.buffers[inference.buf_select][inference.buf_count++] = sampleBuffer[i];
        if(inference.buf_count >= inference.n_samples) {
            inference.buf_select ^= 1;
            inference.buf_count = 0;
            inference.buf_ready = 1;
        }
    }
}

// Task RTOS untuk mengambil sampel dari perangkat keras mikrofon
static void capture_samples(void* arg) {
    const int32_t SAMPLES_PER_READ = 512;
    int32_t raw_samples[SAMPLES_PER_READ];
    size_t bytes_read = 0;

    while (record_status) {
        if (i2s_channel_read(rx_chan, raw_samples, sizeof(raw_samples), &bytes_read, portMAX_DELAY) == ESP_OK) {
            int sampleCount = bytes_read / 4; 
            for (int i = 0; i < sampleCount; i++) {
                // Konversi format I2S (32-bit) ke format yang dikenali EI (16-bit PCM)
                int32_t pcm16 = (raw_samples[i] >> 14) * GAIN_FACTOR;
                
                // Mencegah overflow sinyal (clipping)
                if (pcm16 > 32767) pcm16 = 32767;
                if (pcm16 < -32768) pcm16 = -32768;
                
                sampleBuffer[i] = (int16_t)pcm16;
            }

            if (record_status) {
                audio_inference_callback(sampleCount * 2); 
            } else {
                break;
            }
        }
    }
    vTaskDelete(NULL);
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

    if (i2s_init_custom(EI_CLASSIFIER_FREQUENCY)) {
        ei_printf("Gagal menginisialisasi I2S!\n");
        return false;
    }

    ei_sleep(100);
    record_status = true;
    
    // Menjalankan proses pembacaan audio di background (core)
    xTaskCreate(capture_samples, "CaptureSamples", 1024 * 32, NULL, 10, NULL);
    return true;
}

static bool microphone_inference_record(void) {
    if (inference.buf_ready == 1) {
        ei_printf("Error: sample buffer overrun.\n");
        return false;
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
    i2s_deinit_custom();
    ei_free(inference.buffers[0]);
    ei_free(inference.buffers[1]);
}

// Inisialisasi menggunakan API i2s_std.h
static int i2s_init_custom(uint32_t sampling_rate) {
    i2s_chan_config_t rx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    if (i2s_new_channel(&rx_chan_cfg, NULL, &rx_chan) != ESP_OK) return 1;

    i2s_std_config_t rx_std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(sampling_rate),
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

    if (i2s_channel_init_std_mode(rx_chan, &rx_std_cfg) != ESP_OK) return 1;
    if (i2s_channel_enable(rx_chan) != ESP_OK) return 1;

    return 0;
}

static int i2s_deinit_custom(void) {
    i2s_channel_disable(rx_chan);
    i2s_del_channel(rx_chan);
    return 0;
}
