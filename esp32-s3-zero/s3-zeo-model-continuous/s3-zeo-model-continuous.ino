#include <Arduino.h>
#include "driver/i2s_std.h"
// Pastikan nama library Edge Impulse-mu benar!
#include <KeywordSpottingDirectionIndonesia_inferencing.h>

#define EIDSP_QUANTIZE_FILTERBANK   0
#define I2S_WS     9
#define I2S_SCK    8
#define I2S_SD     7

#define SAMPLE_RATE   EI_CLASSIFIER_FREQUENCY
#define WIN_SAMPLES   EI_CLASSIFIER_RAW_SAMPLE_COUNT
#define GAIN_FACTOR   8
#define CHUNK_SAMPLES 512

static int16_t *ringBuffer      = nullptr;
static int16_t *inferenceBuffer = nullptr;
static volatile uint32_t writeIndex = 0;

SemaphoreHandle_t ringMutex;
i2s_chan_handle_t rx_chan;

static bool checkPSRAM() {
    if (heap_caps_get_total_size(MALLOC_CAP_SPIRAM) == 0) {
        Serial.println("[ERR] PSRAM tidak terdeteksi!");
        return false;
    }
    return true;
}

static int microphone_audio_signal_get_data(size_t offset, size_t length, float *out_ptr) {
    if ((offset + length) > (size_t)WIN_SAMPLES) return -1;
    numpy::int16_to_float(inferenceBuffer + offset, out_ptr, length);
    return 0;
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

// ==========================================================
// CORE 0: MEREKAM AUDIO NON-STOP KE DALAM PSRAM
// ==========================================================
void TaskAudioCapture(void *pvParameters) {
    int32_t raw[CHUNK_SAMPLES];
    while (1) {
        size_t bytes_read = 0;
        esp_err_t ret = i2s_channel_read(rx_chan, raw, sizeof(raw), &bytes_read, pdMS_TO_TICKS(100));
        if (ret != ESP_OK || bytes_read == 0) continue;

        int n = bytes_read / 4;
        if (xSemaphoreTake(ringMutex, pdMS_TO_TICKS(10))) {
            for (int i = 0; i < n; i++) {
                int32_t s = (int32_t)(raw[i] >> 14) * GAIN_FACTOR;
                if (s > 32767) s = 32767;
                if (s < -32768) s = -32768;
                ringBuffer[writeIndex] = (int16_t)s;
                if (++writeIndex >= (uint32_t)(WIN_SAMPLES * 2)) writeIndex = 0;
            }
            xSemaphoreGive(ringMutex);
        }
    }
}

// Fungsi untuk menjepret 1 detik terakhir dari memori
void copyLatestWindow() {
    if (xSemaphoreTake(ringMutex, pdMS_TO_TICKS(50))) {
        int32_t start = (int32_t)writeIndex - WIN_SAMPLES;
        if (start < 0) start += WIN_SAMPLES * 2;

        if (start + WIN_SAMPLES <= WIN_SAMPLES * 2) {
            memcpy(inferenceBuffer, ringBuffer + start, WIN_SAMPLES * sizeof(int16_t));
        } else {
            size_t firstPart = (WIN_SAMPLES * 2) - start;
            size_t secondPart = WIN_SAMPLES - firstPart;
            memcpy(inferenceBuffer, ringBuffer + start, firstPart * sizeof(int16_t));
            memcpy(inferenceBuffer + firstPart, ringBuffer, secondPart * sizeof(int16_t));
        }
        xSemaphoreGive(ringMutex);
    }
}

// ==========================================================
// CORE 1: INFERENSI KONTINU (MENGGANTIKAN VAD)
// ==========================================================
void TaskInference(void *pvParameters) {
    vTaskDelay(pdMS_TO_TICKS(1500)); // Tunggu ring buffer terisi penuh di awal

    while (1) {
        // Langsung jepret 1 detik terakhir tanpa VAD
        copyLatestWindow();

        signal_t signal;
        signal.total_length = WIN_SAMPLES;
        signal.get_data     = &microphone_audio_signal_get_data;
        ei_impulse_result_t result = { 0 };

        EI_IMPULSE_ERROR r = run_classifier(&signal, &result, false);

        if (r != EI_IMPULSE_OK) {
            Serial.printf("[ERR] Classifier gagal: %d\n", r);
        } else {
            // Cari kelas dengan probabilitas tertinggi
            float probTertinggi = 0.0;
            const char* kelasTertinggi = "";

            for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
                if (result.classification[ix].value > probTertinggi) {
                    probTertinggi = result.classification[ix].value;
                    kelasTertinggi = result.classification[ix].label;
                }
            }

            // LOGIKA SLIDING WINDOW:
            // Jika AI sangat yakin (> 0.80) dan tebakannya BUKAN derau
            if (strcmp(kelasTertinggi, "derau") != 0 && probTertinggi > 0.60) {
                Serial.printf("\n[%.2f] >> PERINTAH DITERIMA: %s <<\n", probTertinggi, kelasTertinggi);
                
                // Beri jeda 1 detik agar kursi roda tidak menerima spaming perintah yang sama
                // dari pantulan kata di window berikutnya
                vTaskDelay(pdMS_TO_TICKS(1000)); 
            } else {
                // Jika isinya hanya derau/hening, geser jendela waktu maju 250ms
                // Ini memastikan kata "kanan" atau "stop" pasti akan jatuh pas di tengah window
                vTaskDelay(pdMS_TO_TICKS(500));
            }
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(2000);

    Serial.println("\n=============================================");
    Serial.println(" Sistem Kendali Suara - Continuous AI Mode");
    Serial.println("=============================================\n");

    if (!checkPSRAM()) { while (1) delay(1000); }

    // Alokasi memori di RAM Eksternal
    ringBuffer = (int16_t*)heap_caps_malloc(WIN_SAMPLES * 2 * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    inferenceBuffer = (int16_t*)heap_caps_malloc(WIN_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    memset(ringBuffer, 0, WIN_SAMPLES * 2 * sizeof(int16_t));
    memset(inferenceBuffer, 0, WIN_SAMPLES * sizeof(int16_t));

    setupI2S();
    ringMutex = xSemaphoreCreateMutex();

    // Memecah beban: Merekam di Core 0, Berpikir (AI) di Core 1
    xTaskCreatePinnedToCore(TaskInference, "Inference", 32768, NULL, 1, NULL, 1);
    xTaskCreatePinnedToCore(TaskAudioCapture, "AudioCapture", 8192, NULL, 3, NULL, 0);
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}
