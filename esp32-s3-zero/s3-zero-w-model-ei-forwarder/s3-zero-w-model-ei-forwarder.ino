#include <Arduino.h>
#include "driver/i2s_std.h"

/*
 * ============================================================
 * WAJIB SEBELUM COMPILE — Arduino IDE Board Settings:
 *   Tools → Board  → "ESP32S3 Dev Module"
 *   Tools → PSRAM  → "QSPI PSRAM"    ← ESP32-S3FH4R2 = Quad SPI
 *   Tools → Flash Size → "4MB"
 *   Tools → Partition Scheme → "Default 4MB with spiffs"
 *
 * OPI PSRAM  = untuk chip N8R8/N16R8 (Octal SPI) → SALAH untuk S3-Zero
 * QSPI PSRAM = untuk chip FH4R2 (Quad SPI)       → BENAR untuk S3-Zero
 * ============================================================
 *
 * KENAPA WINDOW_SAMPLES TIDAK LAGI DIDEFINISIKAN DI SINI:
 *   Sebelumnya WINDOW_SAMPLES = 8000 diklaim sebagai "1000ms × 16kHz"
 *   padahal 8000 / 16000 Hz = 500ms (salah hitung).
 *   Sekarang menggunakan EI_CLASSIFIER_RAW_SAMPLE_COUNT yang nilainya
 *   diambil langsung dari model yang diexport — dijamin cocok.
 *
 *   Untuk model dengan impulse 1000ms window @ 16kHz:
 *   EI_CLASSIFIER_RAW_SAMPLE_COUNT = 16000
 *   EI_CLASSIFIER_FREQUENCY        = 16000
 * ============================================================
 */

/* Pakai filterbank int16 untuk MFCC → hemat ~10KB SRAM */
#define EIDSP_QUANTIZE_FILTERBANK   1

//#include <SmartWheelchair_inferencing.h>
#include <SmartWheelchairWithKeywordSpotting_inferencing.h>

/* =========================================================
 * PIN CONFIG
 * ========================================================= */
#define I2S_WS     9
#define I2S_SCK    8
#define I2S_SD     7

/* =========================================================
 * AUDIO CONFIG — semua ukuran window mengikuti model EI
 *
 * EI_CLASSIFIER_RAW_SAMPLE_COUNT : jumlah sampel per window
 *   = window_ms × (frequency / 1000)
 *   = 1000ms × 16  = 16000 sampel
 *
 * Stride = 50% dari window (overlap setengah) → lebih responsif
 * terhadap perintah yang mulai di tengah window.
 * ========================================================= */
#define SAMPLE_RATE   EI_CLASSIFIER_FREQUENCY
#define WIN_SAMPLES   EI_CLASSIFIER_RAW_SAMPLE_COUNT
#define STR_SAMPLES   (EI_CLASSIFIER_RAW_SAMPLE_COUNT / 2)

/*
 * GAIN_FACTOR = 8.
 * Harus identik dengan dataset-record.ino saat merekam dataset.
 * Berbeda → amplitudo training ≠ inference → akurasi turun.
 */
#define GAIN_FACTOR   8

/* =========================================================
 * CHUNK CONFIG
 *
 * INMP441: mikrofon 24-bit, left-justified dalam container 32-bit I2S:
 *   bit31 ............ bit8 | bit7 ..... bit0
 *   [  24-bit audio data   ] [  zero-pad   ]
 *
 * >> 14 → mengekstrak 18-bit atas sebagai signed value.
 *
 * 512 sample × 4 byte = 2KB per batch baca → aman di stack 8KB.
 * ========================================================= */
#define CHUNK_SAMPLES 512

/* =========================================================
 * BUFFER — dialokasikan di PSRAM saat runtime
 *
 * Tidak boleh global BSS biasa karena akan makan SRAM yang
 * dibutuhkan EI TFLite arena.
 *
 * Ring buffer = WIN_SAMPLES × 2:
 *   - Window "lama" tersedia saat snapshot diambil
 *   - Window "baru" sedang diisi AudioCapture
 * ========================================================= */
static int16_t *ringBuffer      = nullptr;
static int16_t *inferenceBuffer = nullptr;

static volatile uint32_t writeIndex = 0;

SemaphoreHandle_t ringMutex;
i2s_chan_handle_t rx_chan;

/* =========================================================
 * CEK & PRINT INFO PSRAM/SRAM
 * ========================================================= */
static bool checkPSRAM()
{
    size_t psramTotal = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    size_t psramFree  = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t sramFree   = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);

    Serial.printf("[MEM] PSRAM total : %u KB\n", psramTotal / 1024);
    Serial.printf("[MEM] PSRAM free  : %u KB\n", psramFree  / 1024);
    Serial.printf("[MEM] SRAM  free  : %u KB\n", sramFree   / 1024);

    if (psramTotal == 0) {
        Serial.println("[ERR] PSRAM tidak terdeteksi!");
        Serial.println("      Pastikan: Tools → PSRAM → QSPI PSRAM");
        return false;
    }
    return true;
}

/* =========================================================
 * SIGNAL GETTER — dipanggil EI saat run_classifier()
 * ========================================================= */
static int microphone_audio_signal_get_data(
    size_t offset, size_t length, float *out_ptr)
{
    if ((offset + length) > (size_t)WIN_SAMPLES) return -1;

    numpy::int16_to_float(
        inferenceBuffer + offset,
        out_ptr,
        length
    );
    return 0;
}

/* =========================================================
 * I2S INIT
 * ========================================================= */
void setupI2S()
{
    i2s_chan_config_t rx_cfg =
        I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&rx_cfg, NULL, &rx_chan));

    i2s_std_config_t rx_std = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                        I2S_DATA_BIT_WIDTH_32BIT,
                        I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = (gpio_num_t)I2S_SCK,
            .ws   = (gpio_num_t)I2S_WS,
            .dout = I2S_GPIO_UNUSED,
            .din  = (gpio_num_t)I2S_SD,
            .invert_flags = { false, false, false },
        },
    };
    /* INMP441 output di LEFT channel */
    rx_std.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_chan, &rx_std));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_chan));
}

/* =========================================================
 * AUDIO CAPTURE TASK — Core 0, priority 3
 * ========================================================= */
void TaskAudioCapture(void *pvParameters)
{
    int32_t raw[CHUNK_SAMPLES]; // 2KB di stack → aman

    while (1) {
        size_t bytes_read = 0;

        esp_err_t ret = i2s_channel_read(
            rx_chan, raw, sizeof(raw),
            &bytes_read, pdMS_TO_TICKS(100)
        );

        if (ret == ESP_ERR_TIMEOUT) continue;
        if (ret != ESP_OK || bytes_read == 0) {
            Serial.printf("[ERR] I2S read: %d\n", ret);
            continue;
        }

        int n = bytes_read / 4;

        if (xSemaphoreTake(ringMutex, pdMS_TO_TICKS(10))) {
            for (int i = 0; i < n; i++) {
                int32_t s = (int32_t)(raw[i] >> 14) * GAIN_FACTOR;
                if (s >  32767) s =  32767;
                if (s < -32768) s = -32768;

                ringBuffer[writeIndex] = (int16_t)s;
                if (++writeIndex >= (uint32_t)(WIN_SAMPLES * 2))
                    writeIndex = 0;
            }
            xSemaphoreGive(ringMutex);
        }
    }
}

/* =========================================================
 * COPY SNAPSHOT
 * ========================================================= */
//void copyLatestWindow()
//{
//    if (xSemaphoreTake(ringMutex, pdMS_TO_TICKS(50))) {
//        int32_t start = (int32_t)writeIndex - WIN_SAMPLES;
//        if (start < 0) start += WIN_SAMPLES * 2;
//
//        for (int i = 0; i < WIN_SAMPLES; i++) {
//            inferenceBuffer[i] = ringBuffer[(start + i) % (WIN_SAMPLES * 2)];
//        }
//        xSemaphoreGive(ringMutex);
//    }
//}

void copyLatestWindow() {
    if (xSemaphoreTake(ringMutex, pdMS_TO_TICKS(50))) {
        int32_t start = (int32_t)writeIndex - WIN_SAMPLES;
        if (start < 0) start += WIN_SAMPLES * 2;

        // Jika pembacaan tidak terbelah di ujung ring buffer
        if (start + WIN_SAMPLES <= WIN_SAMPLES * 2) {
            memcpy(inferenceBuffer, ringBuffer + start, WIN_SAMPLES * sizeof(int16_t));
        } else {
            // Jika terbelah, salin bagian akhir lalu sambung ke bagian awal ring buffer
            size_t firstPart = (WIN_SAMPLES * 2) - start;
            size_t secondPart = WIN_SAMPLES - firstPart;
            memcpy(inferenceBuffer, ringBuffer + start, firstPart * sizeof(int16_t));
            memcpy(inferenceBuffer + firstPart, ringBuffer, secondPart * sizeof(int16_t));
        }
        xSemaphoreGive(ringMutex);
    }
}

/* =========================================================
 * INFERENCE TASK — Core 1, priority 1
 * ========================================================= */
void TaskInference(void *pvParameters)
{
    /* Tunggu WIN_SAMPLES audio masuk + sedikit margin */
    uint32_t waitMs = (uint32_t)WIN_SAMPLES * 1000 / SAMPLE_RATE + 200;
    vTaskDelay(pdMS_TO_TICKS(waitMs));

    while (1) {

        copyLatestWindow();

        /* ---- Debug: level amplitudo ---- */
        int32_t sumAbs = 0;
        int16_t peakAbs = 0;
        for (int i = 0; i < WIN_SAMPLES; i++) {
            int16_t a = abs(inferenceBuffer[i]);
            sumAbs += a;
            if (a > peakAbs) peakAbs = a;
        }
        int32_t avgAbs = sumAbs / WIN_SAMPLES;

        Serial.printf("\n[AMP] avg=%ld  peak=%d\n", avgAbs, peakAbs);

        /*
         * VAD: skip jika sunyi.
         * Tuning: catat avg saat sunyi → set threshold sedikit di atasnya.
         */
        if (avgAbs < 1000) {
            Serial.println("[VAD] silence — skip");
            vTaskDelay(pdMS_TO_TICKS(STR_SAMPLES * 1000 / SAMPLE_RATE));
            continue;
        }

        /* ---- Inferensi ---- */
        signal_t signal;
        signal.total_length = WIN_SAMPLES;
        signal.get_data     = &microphone_audio_signal_get_data;

        ei_impulse_result_t result = { 0 };
        uint32_t t0 = millis();
        EI_IMPULSE_ERROR r = run_classifier(&signal, &result, false);
        uint32_t t1 = millis();

        if (r != EI_IMPULSE_OK) {
            Serial.printf("[ERR] Classifier: %d\n", r);
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        /* ---- Cetak semua probabilitas (mode observasi) ---- */
        Serial.printf("[TIME] %lu ms\n", (unsigned long)(t1 - t0));
        Serial.println("=== Classification ===");
        for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
            Serial.printf("  %-8s : %.4f\n",
                result.classification[ix].label,
                result.classification[ix].value);
        }

        vTaskDelay(pdMS_TO_TICKS(STR_SAMPLES * 1000 / SAMPLE_RATE));
    }
}

/* =========================================================
 * SETUP
 * ========================================================= */
void setup()
{
    Serial.begin(115200);
    delay(2000);

    Serial.println("\n=================================");
    Serial.println("Smart Wheelchair — Voice Detect");
    Serial.printf ("Model  : %s\n", EI_CLASSIFIER_PROJECT_NAME);
    Serial.printf ("Window : %d ms  (%d samples)\n",
                   WIN_SAMPLES * 1000 / SAMPLE_RATE, WIN_SAMPLES);
    Serial.printf ("Stride : %d ms  (%d samples)\n",
                   STR_SAMPLES * 1000 / SAMPLE_RATE, STR_SAMPLES);
    Serial.printf ("Freq   : %d Hz\n", SAMPLE_RATE);
    Serial.printf ("Gain   : x%d\n",  GAIN_FACTOR);
    Serial.println("=================================\n");

    if (!checkPSRAM()) { while (1) delay(1000); }

    /* Alokasi di PSRAM */
    ringBuffer = (int16_t*)heap_caps_malloc(
        WIN_SAMPLES * 2 * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    inferenceBuffer = (int16_t*)heap_caps_malloc(
        WIN_SAMPLES     * sizeof(int16_t), MALLOC_CAP_SPIRAM);

    if (!ringBuffer || !inferenceBuffer) {
        Serial.println("[ERR] Gagal alokasi buffer di PSRAM!");
        while (1) delay(1000);
    }

    memset(ringBuffer,      0, WIN_SAMPLES * 2 * sizeof(int16_t));
    memset(inferenceBuffer, 0, WIN_SAMPLES     * sizeof(int16_t));

    Serial.printf("[MEM] ringBuffer      : %u KB → PSRAM\n",
                  WIN_SAMPLES * 2 * sizeof(int16_t) / 1024);
    Serial.printf("[MEM] inferenceBuffer : %u KB → PSRAM\n",
                  WIN_SAMPLES     * sizeof(int16_t) / 1024);
    Serial.printf("[MEM] SRAM free (EI)  : %u KB\n",
                  heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);

    setupI2S();

    ringMutex = xSemaphoreCreateMutex();
    if (!ringMutex) {
        Serial.println("[ERR] Mutex creation failed");
        while (1) delay(1000);
    }

    xTaskCreatePinnedToCore(
        TaskAudioCapture, "AudioCapture",
        8192, NULL, 3, NULL, 0);

    xTaskCreatePinnedToCore(
        TaskInference, "Inference",
        24576, NULL, 1, NULL, 1);

    Serial.println("[OK] System Ready\n");
}

void loop()
{
    vTaskDelay(pdMS_TO_TICKS(1000));
}
