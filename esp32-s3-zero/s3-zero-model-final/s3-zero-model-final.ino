/**
 * =============================================================
 *  SmartWheelchair — Voice Command Inference Firmware
 * =============================================================
 *  Board   : Waveshare ESP32-S3 Zero (ESP32-S3FH4R2)
 *  Mic     : INMP441 (I2S)
 *  Model   : Edge Impulse — MFCC + CNN (int8 quantized)
 *
 *  Arsitektur FreeRTOS:
 *    Core 0  →  audio_capture_task  : I2S DMA → double-buffer
 *    Core 1  →  loop()              : MFCC + CNN + output perintah
 *
 *  Kelas (urutan alfabetik / index Edge Impulse):
 *    0 = derau   1 = kanan   2 = kiri
 *    3 = maju    4 = mundur  5 = stop
 *
 *  CATATAN INTEGRASI ESP-NOW:
 *    Tambahkan logika ESP-NOW di fungsi on_command_detected().
 *    Variabel `cmd_index` dan `cmd_label` sudah tersedia di sana.
 * =============================================================
 *
 *  DEPENDENSI:
 *    - Library Edge Impulse hasil deployment (SmartWheelchair_inferencing)
 *    - Arduino ESP32 core >= 3.x (ESP-IDF 5.x, new I2S API)
 *
 *  CARA INSTALL:
 *    1. Download Arduino library dari Edge Impulse → Deployment → Arduino
 *    2. Tambahkan ke Arduino IDE via Sketch → Include Library → Add .ZIP
 *    3. Buka sketch ini, compile & upload
 * =============================================================
 */

// Simpan ~10 KB RAM: gunakan int filterbank (bukan float).
// Akurasi tidak terpengaruh secara signifikan untuk MFCC.
#define EIDSP_QUANTIZE_FILTERBANK   0

/* ------------------------------------------------------------------ */
/*  INCLUDES                                                           */
/* ------------------------------------------------------------------ */
#include <SmartWheelchair_inferencing.h>   // Ganti nama jika berbeda

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "driver/i2s_std.h"               // New ESP-IDF I2S API (ESP32 Arduino >= 3.x)
#include "esp_log.h"

/* ------------------------------------------------------------------ */
/*  KONFIGURASI PIN  (sesuai dataset-record.ino)                      */
/* ------------------------------------------------------------------ */
#define I2S_PIN_WS    9    // Word Select  / LRCK
#define I2S_PIN_SCK   8    // Bit Clock    / BCLK
#define I2S_PIN_SD    7    // Serial Data  / DIN

/* ------------------------------------------------------------------ */
/*  KONFIGURASI AUDIO                                                  */
/* ------------------------------------------------------------------ */
#define SAMPLE_RATE         EI_CLASSIFIER_FREQUENCY   // 16000 Hz dari model
#define GAIN_FACTOR         8                          // Sama dengan dataset-record.ino
#define I2S_READ_CHUNK      256    // Jumlah int32_t sample per I2S read (~16 ms)

// DMA: 8 descriptor × 512 frame → total ~4 KB buffer hardware
// Cukup untuk menyerap burst tanpa kehilangan sample
#define I2S_DMA_DESC_NUM    8
#define I2S_DMA_FRAME_NUM   512

/* ------------------------------------------------------------------ */
/*  KONFIGURASI INFERENCE                                              */
/* ------------------------------------------------------------------ */
// Confidence minimum untuk menerima perintah.
// 0.75 konservatif — naikkan ke 0.85 jika ada false positive.
#define CONFIDENCE_THRESHOLD    0.75f

// Jeda minimum antara dua laporan perintah (ms).
// Mencegah satu ucapan dilaporkan berkali-kali.
#define DEBOUNCE_MS             1500

// Index kelas "derau" (noise). Urutan alfabetik Edge Impulse → derau = 0.
// VERIFIKASI: cek ei_classifier_inferencing_categories[] di model_metadata.h
#define NOISE_CLASS_INDEX       0

/* ------------------------------------------------------------------ */
/*  KONFIGURASI FREERTOS                                               */
/* ------------------------------------------------------------------ */
#define CAPTURE_TASK_STACK      4096   // byte — cukup untuk loop I2S sederhana
#define CAPTURE_TASK_PRIORITY   10     // Tinggi agar sample tidak tertinggal
#define CAPTURE_TASK_CORE       0      // Core 0 untuk capture, Core 1 untuk inference

/* ------------------------------------------------------------------ */
/*  STRUKTUR DOUBLE-BUFFER                                             */
/*                                                                     */
/*  buf_select  → buffer yang sedang DIISI oleh capture task          */
/*  buf_select^1 → buffer yang sedang DIBACA oleh inference           */
/*  buf_ready   → flag: buffer ^1 sudah penuh dan siap diproses       */
/* ------------------------------------------------------------------ */
typedef struct {
    int16_t         *buffers[2];
    volatile uint8_t buf_select;
    volatile uint8_t buf_ready;
    volatile uint32_t buf_count;
    uint32_t         n_samples;
} inference_t;

/* ------------------------------------------------------------------ */
/*  VARIABEL GLOBAL                                                    */
/* ------------------------------------------------------------------ */
static inference_t       g_inference         = {};
static i2s_chan_handle_t g_rx_handle          = NULL;
static SemaphoreHandle_t g_slice_ready_sem    = NULL;
static volatile bool     g_capture_running    = false;
static bool              g_debug_nn           = false;   // true = cetak raw MFCC scores

// State debounce output perintah
static int      g_last_cmd_idx   = -1;
static uint32_t g_last_cmd_time  = 0;

static const char *TAG = "VoiceCmd";

/* ================================================================== */
/*  FORWARD DECLARATION                                                */
/* ================================================================== */
static bool  i2s_driver_init(void);
static void  i2s_driver_deinit(void);
static void  audio_capture_task(void *arg);
static int   microphone_audio_signal_get_data(size_t offset, size_t length, float *out_ptr);
static bool  microphone_inference_start(uint32_t n_samples);
static bool  microphone_inference_record(void);
static void  microphone_inference_end(void);
static void  process_result(ei_impulse_result_t *result);
static void  on_command_detected(int cmd_index, const char *cmd_label, float confidence);

/* ================================================================== */
/*  INISIALISASI I2S  (new ESP-IDF API, identik dengan                */
/*  dataset-record.ino — 32-bit Philips, MONO, LEFT slot)             */
/* ================================================================== */
static bool i2s_driver_init(void)
{
    // 1. Buat channel RX
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num  = I2S_DMA_DESC_NUM;
    chan_cfg.dma_frame_num = I2S_DMA_FRAME_NUM;

    esp_err_t ret = i2s_new_channel(&chan_cfg, NULL, &g_rx_handle);
    if (ret != ESP_OK) {
        ei_printf("ERR: i2s_new_channel gagal (0x%x)\n", ret);
        return false;
    }

    // 2. Konfigurasi mode Standard (Philips I2S)
    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG((uint32_t)SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                        I2S_DATA_BIT_WIDTH_32BIT,
                        I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk        = I2S_GPIO_UNUSED,
            .bclk        = (gpio_num_t)I2S_PIN_SCK,
            .ws          = (gpio_num_t)I2S_PIN_WS,
            .dout        = I2S_GPIO_UNUSED,
            .din         = (gpio_num_t)I2S_PIN_SD,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };
    // INMP441: data ada di channel LEFT
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;

    ret = i2s_channel_init_std_mode(g_rx_handle, &std_cfg);
    if (ret != ESP_OK) {
        ei_printf("ERR: i2s_channel_init_std_mode gagal (0x%x)\n", ret);
        i2s_del_channel(g_rx_handle);
        g_rx_handle = NULL;
        return false;
    }

    // 3. Enable channel
    ret = i2s_channel_enable(g_rx_handle);
    if (ret != ESP_OK) {
        ei_printf("ERR: i2s_channel_enable gagal (0x%x)\n", ret);
        i2s_del_channel(g_rx_handle);
        g_rx_handle = NULL;
        return false;
    }

    ei_printf("I2S OK — %d Hz, 32-bit Philips MONO (LEFT), pins SCK=%d WS=%d SD=%d\n",
              SAMPLE_RATE, I2S_PIN_SCK, I2S_PIN_WS, I2S_PIN_SD);
    return true;
}

static void i2s_driver_deinit(void)
{
    if (g_rx_handle) {
        i2s_channel_disable(g_rx_handle);
        i2s_del_channel(g_rx_handle);
        g_rx_handle = NULL;
    }
}

/* ================================================================== */
/*  AUDIO CAPTURE TASK  (Core 0)                                      */
/*                                                                     */
/*  Membaca I2S DMA → konversi 32-bit ke 16-bit → isi double-buffer   */
/*  Memberi sinyal ke inference task via semaphore saat slice penuh.  */
/* ================================================================== */
static void audio_capture_task(void *arg)
{
    int32_t raw_buf[I2S_READ_CHUNK];
    size_t  bytes_read = 0;

    while (g_capture_running) {

        esp_err_t ret = i2s_channel_read(
            g_rx_handle,
            raw_buf,
            sizeof(raw_buf),    // I2S_READ_CHUNK × 4 byte
            &bytes_read,
            pdMS_TO_TICKS(200)  // Timeout per-read — jauh lebih besar dari durasi chunk
        );

        if (ret == ESP_ERR_TIMEOUT) {
            // Timeout sesekali normal; lanjutkan
            continue;
        }
        if (ret != ESP_OK || bytes_read == 0) {
            ei_printf("WARN: i2s_channel_read error (0x%x, bytes=%u)\n", ret, bytes_read);
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        int samples_read = (int)(bytes_read / sizeof(int32_t));

        for (int i = 0; i < samples_read; i++) {

            // === Konversi identik dengan dataset-record.ino ===
            // 32-bit Philips → geser >> 14 untuk dapat nilai 16-bit yang bermakna
            // (bit data INMP441 ada di bit 31..14 pada format 32-bit Philips)
            int32_t val = (int32_t)(raw_buf[i] >> 14) * GAIN_FACTOR;

            // Hard clipping
            if (val >  32767) val =  32767;
            if (val < -32768) val = -32768;

            // Simpan ke buffer aktif
            g_inference.buffers[g_inference.buf_select][g_inference.buf_count++] = (int16_t)val;

            // Cek apakah slice penuh (n_samples = EI_CLASSIFIER_SLICE_SIZE)
            if (g_inference.buf_count >= g_inference.n_samples) {

                if (g_inference.buf_ready) {
                    // === OVERRUN: inference belum mengkonsumsi slice sebelumnya ===
                    // Buang slice ini dan mulai isi ulang dari awal.
                    // Ini lebih aman daripada menulis ke buffer yang masih dibaca.
                    g_inference.buf_count = 0;
                    // Tidak perlu print karena bisa spam; aktifkan saat debug
                    // ei_printf("WARN: slice overrun\n");
                    continue;
                }

                // === Flip buffer ===
                // buf_select berpindah → capture task lanjut ke buffer baru
                // inference task membaca dari buffer lama (buf_select ^ 1)
                g_inference.buf_select ^= 1;
                g_inference.buf_count   = 0;
                g_inference.buf_ready   = 1;

                // Bangunkan inference task
                xSemaphoreGive(g_slice_ready_sem);
            }
        }
    }

    vTaskDelete(NULL);
}

/* ================================================================== */
/*  CALLBACK: Suplai data audio ke Edge Impulse SDK                   */
/* ================================================================== */
static int microphone_audio_signal_get_data(size_t offset, size_t length, float *out_ptr)
{
    // Inference membaca dari buffer yang TIDAK sedang diisi capture task
    numpy::int16_to_float(
        &g_inference.buffers[g_inference.buf_select ^ 1][offset],
        out_ptr,
        length
    );
    return 0;
}

/* ================================================================== */
/*  INISIALISASI MIKROFON                                              */
/* ================================================================== */
static bool microphone_inference_start(uint32_t n_samples)
{
    g_inference.n_samples  = n_samples;
    g_inference.buf_select = 0;
    g_inference.buf_count  = 0;
    g_inference.buf_ready  = 0;

    // Alokasi dua buffer slice (masing-masing n_samples × 2 byte)
    // Untuk n_samples = 8000 (500ms @ 16kHz): 16 KB per buffer, 32 KB total
    g_inference.buffers[0] = (int16_t*)malloc(n_samples * sizeof(int16_t));
    if (!g_inference.buffers[0]) {
        ei_printf("ERR: malloc buffer[0] gagal (%u byte)\n",
                  (unsigned)(n_samples * sizeof(int16_t)));
        return false;
    }

    g_inference.buffers[1] = (int16_t*)malloc(n_samples * sizeof(int16_t));
    if (!g_inference.buffers[1]) {
        ei_printf("ERR: malloc buffer[1] gagal\n");
        free(g_inference.buffers[0]);
        g_inference.buffers[0] = NULL;
        return false;
    }

    memset(g_inference.buffers[0], 0, n_samples * sizeof(int16_t));
    memset(g_inference.buffers[1], 0, n_samples * sizeof(int16_t));

    // Buat semaphore untuk sinkronisasi capture → inference
    g_slice_ready_sem = xSemaphoreCreateBinary();
    if (!g_slice_ready_sem) {
        ei_printf("ERR: Semaphore gagal dibuat\n");
        free(g_inference.buffers[0]);
        free(g_inference.buffers[1]);
        return false;
    }

    // Inisialisasi I2S
    if (!i2s_driver_init()) {
        free(g_inference.buffers[0]);
        free(g_inference.buffers[1]);
        vSemaphoreDelete(g_slice_ready_sem);
        return false;
    }

    // Beri waktu INMP441 stabil
    vTaskDelay(pdMS_TO_TICKS(100));

    // Start capture task di Core 0, prioritas tinggi
    g_capture_running = true;
    BaseType_t task_ret = xTaskCreatePinnedToCore(
        audio_capture_task,
        "AudioCapture",
        CAPTURE_TASK_STACK,
        NULL,
        CAPTURE_TASK_PRIORITY,
        NULL,
        CAPTURE_TASK_CORE
    );

    if (task_ret != pdPASS) {
        ei_printf("ERR: Gagal membuat AudioCapture task\n");
        g_capture_running = false;
        i2s_driver_deinit();
        free(g_inference.buffers[0]);
        free(g_inference.buffers[1]);
        vSemaphoreDelete(g_slice_ready_sem);
        return false;
    }

    return true;
}

/* ================================================================== */
/*  TUNGGU SLICE BARU  (dipanggil dari loop() di Core 1)              */
/* ================================================================== */
static bool microphone_inference_record(void)
{
    // Timeout = 2× durasi window untuk keamanan (2000 ms)
    if (xSemaphoreTake(g_slice_ready_sem, pdMS_TO_TICKS(2000)) != pdTRUE) {
        ei_printf("WARN: Timeout menunggu slice audio — cek koneksi INMP441\n");
        return false;
    }
    g_inference.buf_ready = 0;
    return true;
}

/* ================================================================== */
/*  HENTIKAN MIKROFON DAN BEBASKAN RESOURCE                          */
/* ================================================================== */
static void microphone_inference_end(void)
{
    g_capture_running = false;
    // Beri waktu task selesai secara alami
    vTaskDelay(pdMS_TO_TICKS(300));

    i2s_driver_deinit();

    if (g_slice_ready_sem) {
        vSemaphoreDelete(g_slice_ready_sem);
        g_slice_ready_sem = NULL;
    }
    if (g_inference.buffers[0]) { free(g_inference.buffers[0]); g_inference.buffers[0] = NULL; }
    if (g_inference.buffers[1]) { free(g_inference.buffers[1]); g_inference.buffers[1] = NULL; }
}

/* ================================================================== */
/*  PEMROSESAN HASIL KLASIFIKASI                                       */
/* ================================================================== */
static void process_result(ei_impulse_result_t *result)
{
    // Cari kelas dengan confidence tertinggi
    int   best_idx = 0;
    float best_val = 0.0f;

    for (size_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
        if (result->classification[i].value > best_val) {
            best_val = result->classification[i].value;
            best_idx = (int)i;
        }
    }

    // Abaikan kelas noise (derau)
    if (best_idx == NOISE_CLASS_INDEX) return;

    // Abaikan confidence rendah
    if (best_val < CONFIDENCE_THRESHOLD) return;

    uint32_t now = millis();

    // Debounce: abaikan perintah yang sama dalam DEBOUNCE_MS
    if (best_idx == g_last_cmd_idx && (now - g_last_cmd_time) < DEBOUNCE_MS) {
        return;
    }

    g_last_cmd_idx  = best_idx;
    g_last_cmd_time = now;

    on_command_detected(best_idx, result->classification[best_idx].label, best_val);
}

/* ================================================================== */
/*  HANDLER PERINTAH TERDETEKSI                                        */
/*                                                                     */
/*  Ini titik integrasi utama:                                         */
/*    - Serial output sudah ada                                        */
/*    - Tambahkan ESP-NOW di sini nanti                                */
/* ================================================================== */
static void on_command_detected(int cmd_index, const char *cmd_label, float confidence)
{
    // Output ke Serial (format mudah di-parse oleh host/debugging)
    ei_printf("[CMD] idx=%d label=%s conf=%.3f\n", cmd_index, cmd_label, confidence);

    // ----------------------------------------------------------------
    // TODO (ESP-NOW): Kirim cmd_index atau cmd_label via ESP-NOW
    //
    // Contoh struktur:
    //   typedef struct { uint8_t cmd_id; float confidence; } cmd_packet_t;
    //   cmd_packet_t pkt = { (uint8_t)cmd_index, confidence };
    //   esp_now_send(peer_mac, (uint8_t*)&pkt, sizeof(pkt));
    // ----------------------------------------------------------------
}

/* ================================================================== */
/*  SETUP                                                              */
/* ================================================================== */
void setup()
{
    Serial.begin(115200);

    // Tunggu Serial max 3 detik (USB CDC pada S3 butuh waktu enumerate)
    uint32_t t0 = millis();
    while (!Serial && (millis() - t0) < 3000) { vTaskDelay(10); }

    ei_printf("\n");
    ei_printf("======================================\n");
    ei_printf(" SmartWheelchair Voice Command\n");
    ei_printf("======================================\n");
    ei_printf(" Model   : %s\n",     EI_CLASSIFIER_PROJECT_NAME);
    ei_printf(" Freq    : %d Hz\n",  EI_CLASSIFIER_FREQUENCY);
    ei_printf(" Window  : %d ms\n",  (EI_CLASSIFIER_RAW_SAMPLE_COUNT * 1000) / EI_CLASSIFIER_FREQUENCY);
    ei_printf(" Stride  : %d ms\n",  (EI_CLASSIFIER_SLICE_SIZE * 1000) / EI_CLASSIFIER_FREQUENCY);
    ei_printf(" Slices  : %d\n",     EI_CLASSIFIER_SLICES_PER_MODEL_WINDOW);
    ei_printf(" Kelas   : %d\n",     EI_CLASSIFIER_LABEL_COUNT);
    ei_printf(" Threshold: %.2f\n",  CONFIDENCE_THRESHOLD);
    ei_printf(" Debounce : %d ms\n", DEBOUNCE_MS);
    ei_printf("--------------------------------------\n");

    // Cetak daftar kelas untuk verifikasi index
    ei_printf(" Label index:\n");
    for (size_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
        ei_printf("   [%u] %s%s\n",
                  i,
                  ei_classifier_inferencing_categories[i],
                  (i == NOISE_CLASS_INDEX) ? "  ← noise (diabaikan)" : "");
    }
    ei_printf("======================================\n\n");

    // Inisialisasi state classifier continuous
    run_classifier_init();

    ei_printf("Menginisialisasi mikrofon...\n");
    if (!microphone_inference_start(EI_CLASSIFIER_SLICE_SIZE)) {
        ei_printf("ERR FATAL: Gagal menginisialisasi mikrofon. Sistem berhenti.\n");
        // Blink LED internal (GPIO 21 = WS2812 di S3 Zero) sebagai indikator error?
        // Cukup hang — user akan lihat pesan di Serial
        while (true) { vTaskDelay(pdMS_TO_TICKS(1000)); }
    }

    ei_printf("Siap. Mendengarkan perintah...\n\n");
}

/* ================================================================== */
/*  MAIN LOOP  (Core 1)                                                */
/*                                                                     */
/*  Alur per iterasi:                                                  */
/*    1. Tunggu slice 500ms dari Core 0 (via semaphore)               */
/*    2. Jalankan MFCC + CNN (run_classifier_continuous)              */
/*    3. Setelah buffer MFCC penuh (2 slice), proses hasil            */
/* ================================================================== */
static int s_print_results = -(EI_CLASSIFIER_SLICES_PER_MODEL_WINDOW);

void loop()
{
    // 1. Tunggu slice audio siap (dilepas oleh capture task via semaphore)
    if (!microphone_inference_record()) {
        // Timeout — cek hardware
        return;
    }

    // 2. Siapkan signal descriptor untuk Edge Impulse
    signal_t signal;
    signal.total_length = EI_CLASSIFIER_SLICE_SIZE;
    signal.get_data     = &microphone_audio_signal_get_data;

    // 3. Jalankan classifier (continuous mode — internal ring buffer MFCC)
    ei_impulse_result_t result = { 0 };
    EI_IMPULSE_ERROR err = run_classifier_continuous(&signal, &result, g_debug_nn);

    if (err != EI_IMPULSE_OK) {
        ei_printf("ERR: run_classifier_continuous gagal (%d)\n", (int)err);
        return;
    }

    // 4. Tunggu buffer MFCC terisi penuh sebelum mulai output
    //    (EI_CLASSIFIER_SLICES_PER_MODEL_WINDOW = 2 untuk window 1000ms / stride 500ms)
    if (++s_print_results >= EI_CLASSIFIER_SLICES_PER_MODEL_WINDOW) {
        s_print_results = 0;

        // Proses dan keluarkan perintah (dengan threshold + debounce)
        process_result(&result);

        // Mode debug: cetak semua nilai confidence setiap slice
        if (g_debug_nn) {
            ei_printf("[DBG] DSP=%dms CNN=%dms | ",
                      result.timing.dsp, result.timing.classification);
            for (size_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
                ei_printf("%s:%.2f ",
                          result.classification[i].label,
                          result.classification[i].value);
            }
            ei_printf("\n");
        }
    }
}

/* ------------------------------------------------------------------ */
/*  VALIDASI SENSOR — error compile jika model bukan untuk mikrofon   */
/* ------------------------------------------------------------------ */
#if !defined(EI_CLASSIFIER_SENSOR) || EI_CLASSIFIER_SENSOR != EI_CLASSIFIER_SENSOR_MICROPHONE
#error "Model tidak valid untuk sensor mikrofon. Periksa deployment di Edge Impulse."
#endif
