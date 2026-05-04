/* Inferensi Continuous ESP32-S3 Zero dengan INMP441 & ESP-NOW TX */

#include <PerintahSuaraKursiRoda-ss_inferencing.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h" 
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// --- KONFIGURASI I2S ESP32-S3 ZERO ---
#define I2S_WS   9  // LRCLK
#define I2S_SCK  8  // BCLK
#define I2S_SD   7  // DOUT dari mic

i2s_chan_handle_t rx_chan;

// --- KONFIGURASI ESP-NOW ---
// TODO: GANTI DENGAN MAC ADDRESS ESP32-S3 UTAMA (KURS RODA)
uint8_t alamatReceiver[] = { 0x30, 0xED, 0xA0, 0xBD, 0x6A, 0x44 }; 
esp_now_peer_info_t peerInfo;

// --- VARIABEL VOTING SISTEM ---
String lastCommand = "";
int commandCount = 0;
const int VOTES_NEEDED = 2; // Butuh 2 frame berturut-turut untuk validasi

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

// Deklarasi fungsi
static bool microphone_inference_start(uint32_t n_samples);
static bool microphone_inference_record(void);
static int microphone_audio_signal_get_data(size_t offset, size_t length, float *out_ptr);
static void microphone_inference_end(void);
static int i2s_init(uint32_t sampling_rate);
static int i2s_deinit(void);

// Callback ESP-NOW (Opsional, untuk cek status pengiriman)
// void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
//     // Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Kirim OK" : "Kirim Gagal");
// }
// Callback ESP-NOW (Disesuaikan untuk ESP32 Core 3.x)
void OnDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
    // Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Kirim OK" : "Kirim Gagal");
}

void setup()
{
    Serial.begin(115200);
    while (!Serial);
    Serial.println("\n[S3 Zero] Inisialisasi Sistem...");

    // 1. Setup WiFi & ESP-NOW
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    WiFi.setChannel(1);
    
    if (esp_now_init() != ESP_OK) {
        Serial.println("ERR: Gagal inisialisasi ESP-NOW");
        return;
    }
    esp_now_register_send_cb(OnDataSent);
    
    memset(&peerInfo, 0, sizeof(peerInfo));
    memcpy(peerInfo.peer_addr, alamatReceiver, 6);
    peerInfo.channel = 1;
    peerInfo.encrypt = false;
    
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("ERR: Gagal menambahkan Peer ESP-NOW");
        return;
    }

    // 2. Setup Edge Impulse
    ei_printf("Inferencing settings:\n");
    ei_printf("\tInterval: %.2f ms.\n", (float)EI_CLASSIFIER_INTERVAL_MS);
    ei_printf("\tFrame size: %d\n", EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE);
    ei_printf("\tSample length: %d ms.\n", EI_CLASSIFIER_RAW_SAMPLE_COUNT / 16);

    run_classifier_init();
    
    if (microphone_inference_start(EI_CLASSIFIER_SLICE_SIZE) == false) {
        ei_printf("ERR: Gagal alokasi buffer audio.\n");
        return;
    }

    Serial.println("[S3 Zero] Merekam dan Menganalisis...\n");
}

void loop()
{
    bool m = microphone_inference_record();
    if (!m) {
        ei_printf("ERR: Gagal merekam audio...\n");
        return;
    }

    signal_t signal;
    signal.total_length = EI_CLASSIFIER_SLICE_SIZE;
    signal.get_data = &microphone_audio_signal_get_data;
    ei_impulse_result_t result = {0};

    EI_IMPULSE_ERROR r = run_classifier_continuous(&signal, &result, debug_nn);
    if (r != EI_IMPULSE_OK) {
        ei_printf("ERR: Gagal eksekusi classifier (%d)\n", r);
        return;
    }

    if (++print_results >= (EI_CLASSIFIER_SLICES_PER_MODEL_WINDOW)) {
        
        // --- 1. LOGIKA ARGMAX (Cari Juara 1) ---
        float probTertinggi = 0.0;
        String kelasTertinggi = "";
        float probDerau = 0.0;

        for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
            if (strcmp(result.classification[ix].label, "derau") == 0) {
                probDerau = result.classification[ix].value;
            } else {
                if (result.classification[ix].value > probTertinggi) {
                    probTertinggi = result.classification[ix].value;
                    kelasTertinggi = result.classification[ix].label;
                }
            }
        }

        // --- 2. THRESHOLDING BERDASARKAN KALIBRASI (0.40) ---
        String detectedCommand = "";
        if (probDerau > 0.60) {
            detectedCommand = "derau";
        } else if (probTertinggi >= 0.40) {
            detectedCommand = kelasTertinggi;
        } else {
            detectedCommand = "";
        }

        // --- 3. SISTEM VOTING & PENGIRIMAN ESP-NOW ---
        if (detectedCommand == "derau" || detectedCommand == "") {
            // Jangan di-reset. Toleransi untuk jeda napas antar frame.
        } 
        else if (detectedCommand == lastCommand) {
            commandCount++;
            if (commandCount >= VOTES_NEEDED) {
                Serial.printf(">>> KIRIM PERINTAH: %s (Conf: %.2f) <<<\n", detectedCommand.c_str(), probTertinggi);
                
                // Kirim data ke ESP32 S3 Utama di kursi roda
                // Mengirim string perintah langsung (bisa disesuaikan pakai struct/integer jika mau)
                esp_err_t hasil = esp_now_send(alamatReceiver, (uint8_t *)detectedCommand.c_str(), detectedCommand.length() + 1);
                
                if (hasil != ESP_OK) {
                    Serial.println("ERR: Gagal kirim ESP-NOW");
                }

                commandCount = 0;  // Reset setelah berhasil tereksekusi
                lastCommand = "";  // Kosongkan agar siap menerima perintah baru
            }
        } 
        else {
            // Jika kata berubah (misal dari "maju" tiba-tiba jadi "kiri")
            lastCommand = detectedCommand;
            commandCount = 1;
        }

        print_results = 0;
    }
}

// =========================================================
// BAGIAN FUNGSI HARDWARE & AUDIO EDGE IMPULSE
// =========================================================

static void audio_inference_callback(uint32_t n_bytes)
{
    for(int i = 0; i < n_bytes>>1; i++) {
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
        if (i2s_channel_read(rx_chan, raw_samples, sizeof(raw_samples), &bytes_read, portMAX_DELAY) == ESP_OK) {
            int sampleCount = bytes_read / 4; 

            // Bit-shift 14 agar sama dengan data training
            for (int i = 0; i < sampleCount; i++) {
                sampleBuffer[i] = (int16_t)(raw_samples[i] >> 14);
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

static bool microphone_inference_start(uint32_t n_samples)
{
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

    if (i2s_init(EI_CLASSIFIER_FREQUENCY) != 0) {
        ei_printf("Failed to start I2S!");
    }

    ei_sleep(100);
    record_status = true;
    xTaskCreate(capture_samples, "CaptureSamples", 1024 * 32, NULL, 10, NULL);
    return true;
}

static bool microphone_inference_record(void)
{
    bool ret = true;
    if (inference.buf_ready == 1) {
        ei_printf("Error: Overrun. Buffer tumpang tindih.\n");
        ret = false;
    }
    while (inference.buf_ready == 0) {
        delay(1);
    }
    inference.buf_ready = 0;
    return true;
}

static int microphone_audio_signal_get_data(size_t offset, size_t length, float *out_ptr)
{
    numpy::int16_to_float(&inference.buffers[inference.buf_select ^ 1][offset], out_ptr, length);
    return 0;
}

static void microphone_inference_end(void)
{
    i2s_deinit();
    ei_free(inference.buffers[0]);
    ei_free(inference.buffers[1]);
}

static int i2s_init(uint32_t sampling_rate) {
    i2s_chan_config_t rx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    i2s_new_channel(&rx_chan_cfg, NULL, &rx_chan);

    i2s_std_config_t rx_std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sampling_rate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = (gpio_num_t)I2S_SCK,
            .ws = (gpio_num_t)I2S_WS,
            .dout = I2S_GPIO_UNUSED,
            .din = (gpio_num_t)I2S_SD,
            .invert_flags = { false, false, false },
        },
    };
    rx_std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;

    esp_err_t err = i2s_channel_init_std_mode(rx_chan, &rx_std_cfg);
    if (err != ESP_OK) return -1;
    
    err = i2s_channel_enable(rx_chan);
    if (err != ESP_OK) return -1;

    return 0;
}

static int i2s_deinit(void) {
    i2s_channel_disable(rx_chan);
    i2s_del_channel(rx_chan);
    return 0;
}

#if !defined(EI_CLASSIFIER_SENSOR) || EI_CLASSIFIER_SENSOR != EI_CLASSIFIER_SENSOR_MICROPHONE
#error "Invalid model for current sensor."
#endif