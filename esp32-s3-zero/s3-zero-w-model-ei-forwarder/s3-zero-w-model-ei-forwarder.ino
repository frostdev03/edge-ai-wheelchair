#include <Arduino.h>
#include "driver/i2s_std.h"

// PENTING: Ganti dengan header hasil export-mu
//#include <SmartWheelchair_inferencing.h>
#include <DirectionKSP-Id_inferencing.h>

#define EIDSP_QUANTIZE_FILTERBANK   0

// PINOUT ESP32-S3 ZERO
#define I2S_WS     9
#define I2S_SCK    8
#define I2S_SD     7

#define SAMPLE_RATE   EI_CLASSIFIER_FREQUENCY
#define WIN_SAMPLES   EI_CLASSIFIER_RAW_SAMPLE_COUNT

#define GAIN_FACTOR   8
#define CHUNK_SAMPLES 512
#define AMBANG_BATAS_VOLUME 1000 // Sesuaikan dengan hasil log VAD-mu

// State Machine persis seperti ide ESP-NOW milikmu
enum StatusSistem { MENDENGAR, MEREKAM, INFERENSI };
volatile StatusSistem statusSaatIni = MENDENGAR;

int16_t *audioBuffer = nullptr;
volatile int bufferIndex = 0;

i2s_chan_handle_t rx_chan;
TaskHandle_t inferenceTaskHandle = NULL;

// =========================================================
// CEK PSRAM & INIT I2S
// =========================================================
static bool checkPSRAM() {
    if (heap_caps_get_total_size(MALLOC_CAP_SPIRAM) == 0) {
        Serial.println("[ERR] PSRAM tidak terdeteksi!");
        return false;
    }
    return true;
}

void setupI2S() {
    i2s_chan_config_t rx_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&rx_cfg, NULL, &rx_chan));

    i2s_std_config_t rx_std = {
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
    rx_std.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_chan, &rx_std));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_chan));
}

// =========================================================
// GETTER EDGE IMPULSE
// =========================================================
static int microphone_audio_signal_get_data(size_t offset, size_t length, float *out_ptr) {
    if ((offset + length) > (size_t)WIN_SAMPLES) return -1;
    numpy::int16_to_float(audioBuffer + offset, out_ptr, length);
    return 0;
}

// =========================================================
// TASK CORE 0: BACA MIKROFON & VAD (LOGIKA FORWARD-RECORDING)
// =========================================================
void TaskAudioCapture(void *pvParameters) {
    int32_t raw[CHUNK_SAMPLES];
    int16_t pcm16[CHUNK_SAMPLES];

    while (1) {
        size_t bytes_read = 0;
        esp_err_t ret = i2s_channel_read(rx_chan, raw, sizeof(raw), &bytes_read, portMAX_DELAY);
        if (ret != ESP_OK || bytes_read == 0) continue;

        int n = bytes_read / 4;
        int32_t sumAbs = 0;

        // Ekstraksi 16-bit & Hitung Volume
        for (int i = 0; i < n; i++) {
            int32_t s = (int32_t)(raw[i] >> 14) * GAIN_FACTOR;
            if (s >  32767) s =  32767;
            if (s < -32768) s = -32768;
            pcm16[i] = (int16_t)s;
            sumAbs += abs(pcm16[i]);
        }
        int32_t avgAbs = sumAbs / n;

        // State Machine
        if (statusSaatIni == MENDENGAR) {
            if (avgAbs > AMBANG_BATAS_VOLUME) {
                Serial.printf("\n[VAD] Suara Terdeteksi (Vol: %ld). Merekam...\n", avgAbs);
                statusSaatIni = MEREKAM;
                bufferIndex = 0;
                
                // Masukkan chunk pertama (awalan kata) ke buffer
                for(int i = 0; i < n && bufferIndex < WIN_SAMPLES; i++) {
                    audioBuffer[bufferIndex++] = pcm16[i];
                }
            }
        } 
        else if (statusSaatIni == MEREKAM) {
            // Lanjutkan mengisi buffer sampai penuh 1 window
            for(int i = 0; i < n && bufferIndex < WIN_SAMPLES; i++) {
                audioBuffer[bufferIndex++] = pcm16[i];
            }

            if (bufferIndex >= WIN_SAMPLES) {
                statusSaatIni = INFERENSI;
                // Bangunkan Task Core 1 untuk memproses AI
                xTaskNotifyGive(inferenceTaskHandle);
            }
        }
    }
}

// =========================================================
// TASK CORE 1: INFERENSI MFCC & CNN
// =========================================================
void TaskInference(void *pvParameters) {
    while (1) {
        // Tidur pulas sampai dibangunkan oleh TaskAudioCapture
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        Serial.println("[AI] 1 Window audio terkumpul. Memulai MFCC & CNN...");

        signal_t signal;
        signal.total_length = WIN_SAMPLES;
        signal.get_data     = &microphone_audio_signal_get_data;
        ei_impulse_result_t result = { 0 };

        uint32_t t0 = millis();
        EI_IMPULSE_ERROR r = run_classifier(&signal, &result, false);
        uint32_t t1 = millis();

        if (r != EI_IMPULSE_OK) {
            Serial.printf("[ERR] Classifier gagal: %d\n", r);
        } else {
            Serial.printf("Waktu Proses : DSP %d ms, Klasifikasi %d ms\n", result.timing.dsp, result.timing.classification);
            Serial.println("--- Hasil Prediksi ---");
            for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
                Serial.printf("  %-8s : %.4f\n", result.classification[ix].label, result.classification[ix].value);
            }
            Serial.println("----------------------");
        }

        // Jeda waktu sebelum mendengarkan perintah baru
        vTaskDelay(pdMS_TO_TICKS(500)); 
        Serial.println("\n[Sistem] Siap mendengarkan perintah baru...");
        statusSaatIni = MENDENGAR;
    }
}

// =========================================================
// SETUP & LOOP
// =========================================================
void setup() {
    Serial.begin(115200);
    delay(2000);

    Serial.println("\n=================================");
    Serial.println("Sistem Kendali Suara Lokal");
    Serial.println("Mode: Forward-Recording State Machine");
    Serial.println("=================================\n");

    if (!checkPSRAM()) { while (1) delay(1000); }

    // Alokasi PSRAM (Hanya butuh 1 buffer sekarang, memori jauh lebih lega!)
    audioBuffer = (int16_t*)heap_caps_malloc(WIN_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    memset(audioBuffer, 0, WIN_SAMPLES * sizeof(int16_t));

    setupI2S();

    // Jalankan task ke masing-masing core
    xTaskCreatePinnedToCore(TaskInference, "Inference", 32768, NULL, 1, &inferenceTaskHandle, 1);
    xTaskCreatePinnedToCore(TaskAudioCapture, "AudioCapture", 8192, NULL, 3, NULL, 0);

    Serial.println("[OK] Sistem Siap!");
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}
