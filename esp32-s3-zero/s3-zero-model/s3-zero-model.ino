#include <Arduino.h>
#include "driver/i2s_std.h"
#include <KeywordSpottingDirectionIndonesia_inferencing.h>
#include <WiFi.h>
#include <esp_now.h> // Tambahkan library ESP-NOW

#define EIDSP_QUANTIZE_FILTERBANK   0
#define I2S_WS     9
#define I2S_SCK    8
#define I2S_SD     7

#define SAMPLE_RATE   EI_CLASSIFIER_FREQUENCY
#define WIN_SAMPLES   EI_CLASSIFIER_RAW_SAMPLE_COUNT
#define GAIN_FACTOR   8
#define CHUNK_SAMPLES 512

// ===================== ESP-NOW CONFIGURATION =====================
// Ganti dengan MAC Address ESP32-S3 Main (Penerima) milikmu
uint8_t receiverMacAddress[] = {0x30, 0xED, 0xA0, 0xBD, 0x6A, 0x44}; 

// Definisi ID Perintah menggunakan Enum agar kode mudah dibaca
enum CommandID {
    CMD_DIAM   = 0,
    CMD_MAJU   = 1,
    CMD_MUNDUR = 2,
    CMD_KIRI   = 3,
    CMD_KANAN  = 4,
    CMD_STOP   = 5  // Prioritas Tertinggi!
};

// Struktur paket data yang dikirim via ESP-NOW
typedef struct struct_message {
    uint8_t command;   // Berisi ID dari enum CommandID
    uint32_t msg_id;   // Nomor urut pesan (untuk validasi konfirmasi nanti jika butuh)
} struct_message;

struct_message myData;
esp_now_peer_info_t peerInfo;
volatile uint32_t messageCounter = 0;

// ===================== AUDIO & BUFFER VARIABLES =====================
static int16_t *ringBuffer      = nullptr;
static int16_t *inferenceBuffer = nullptr;
static volatile uint32_t writeIndex = 0;

SemaphoreHandle_t ringMutex;
i2s_chan_handle_t rx_chan;

// Callback bawaan ESP-NOW untuk mendeteksi apakah data fisik "sampai" ke penerima
//void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
//    Serial.print("\r\n[ESP-NOW] Status Pengiriman: ");
//    Serial.println(status == ESP_NOW_SEND_SUCCESS ? "SUKSES Terkirim" : "GAGAL Terkirim");
//}

// Callback bawaan ESP-NOW versi terbaru (menggunakan wifi_tx_info_t)
void OnDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
    Serial.print("\r\n[ESP-NOW] Status Pengiriman: ");
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? "SUKSES Terkirim" : "GAGAL Terkirim");
    
    // Opsional: Jika nanti kamu butuh tahu MAC address tujuan dari tx_info
    // formatnya adalah: tx_info->target_mac
}

static bool checkPSRAM() {
    if (heap_caps_get_total_size(MALLOC_CAP_SPIRAM) == 0) {
        Serial.println("[ERR] PSRAM tidak terdeteksi!"); //[cite: 66]
        return false; //[cite: 67]
    }
    return true; //[cite: 66]
}

static int microphone_audio_signal_get_data(size_t offset, size_t length, float *out_ptr) {
    if ((offset + length) > (size_t)WIN_SAMPLES) return -1; //[cite: 67]
    numpy::int16_to_float(inferenceBuffer + offset, out_ptr, length); //[cite: 68]
    return 0; //[cite: 68]
}

void setupI2S() {
    i2s_chan_config_t rx_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER); //[cite: 68]
    ESP_ERROR_CHECK(i2s_new_channel(&rx_cfg, NULL, &rx_chan)); //[cite: 69]

    i2s_std_config_t rx_std = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = (gpio_num_t)I2S_SCK,
            .ws   = (gpio_num_t)I2S_WS,
            .dout = I2S_GPIO_UNUSED,
            .din  = (gpio_num_t)I2S_SD, //[cite: 70]
            .invert_flags = { false, false, false },
        },
    }; //[cite: 69]
    rx_std.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT; //[cite: 71]
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_chan, &rx_std)); //[cite: 71]
    ESP_ERROR_CHECK(i2s_channel_enable(rx_chan)); //[cite: 71]
}

void TaskAudioCapture(void *pvParameters) {
    int32_t raw[CHUNK_SAMPLES]; //[cite: 71]
    while (1) { //[cite: 72]
        size_t bytes_read = 0; //[cite: 72]
        esp_err_t ret = i2s_channel_read(rx_chan, raw, sizeof(raw), &bytes_read, pdMS_TO_TICKS(100)); //[cite: 73]
        if (ret != ESP_OK || bytes_read == 0) continue; //[cite: 73]
        int n = bytes_read / 4; //[cite: 74]
        if (xSemaphoreTake(ringMutex, pdMS_TO_TICKS(10))) { //[cite: 74]
            for (int i = 0; i < n; i++) { //[cite: 74]
                int32_t s = (int32_t)(raw[i] >> 14) * GAIN_FACTOR; //[cite: 74]
                if (s > 32767) s = 32767; //[cite: 75]
                if (s < -32768) s = -32768; //[cite: 75]
                ringBuffer[writeIndex] = (int16_t)s; //[cite: 75]
                if (++writeIndex >= (uint32_t)(WIN_SAMPLES * 2)) writeIndex = 0; //[cite: 76]
            }
            xSemaphoreGive(ringMutex); //[cite: 76]
        }
    }
}

void copyLatestWindow() {
    if (xSemaphoreTake(ringMutex, pdMS_TO_TICKS(50))) { //[cite: 77]
        int32_t start = (int32_t)writeIndex - WIN_SAMPLES; //[cite: 77]
        if (start < 0) start += WIN_SAMPLES * 2; //[cite: 78]

        if (start + WIN_SAMPLES <= WIN_SAMPLES * 2) { //[cite: 78]
            memcpy(inferenceBuffer, ringBuffer + start, WIN_SAMPLES * sizeof(int16_t)); //[cite: 78]
        } else { //[cite: 79]
            size_t firstPart = (WIN_SAMPLES * 2) - start; //[cite: 79]
            size_t secondPart = WIN_SAMPLES - firstPart; //[cite: 80]
            memcpy(inferenceBuffer, ringBuffer + start, firstPart * sizeof(int16_t)); //[cite: 80]
            memcpy(inferenceBuffer + firstPart, ringBuffer, secondPart * sizeof(int16_t)); //[cite: 80]
        }
        xSemaphoreGive(ringMutex); //[cite: 81]
    }
}

// Fungsi untuk mencocokkan string label AI ke ID angka dan langsung mengirimkannya
void kirimPerintahKelektronik(const char* label) {
    uint8_t commandToSend = CMD_DIAM;
    
    // Pemetaan dari hasil keyword spotting Edge Impulse ke ID angka
    if (strcmp(label, "maju") == 0)      commandToSend = CMD_MAJU;
    else if (strcmp(label, "mundur") == 0) commandToSend = CMD_MUNDUR;
    else if (strcmp(label, "kiri") == 0)   commandToSend = CMD_KIRI;
    else if (strcmp(label, "kanan") == 0)  commandToSend = CMD_KANAN;
    else if (strcmp(label, "stop") == 0)   commandToSend = CMD_STOP;
    else return; // Jika mendeteksi _noise_ atau _unknown_, jangan kirim apapun

    // Siapkan Paket Data
    myData.command = commandToSend;
    myData.msg_id = ++messageCounter;

    Serial.printf("[ESP-NOW] Mengirim ID Perintah: %d (Label: %s) ke Kursi Roda...\n", myData.command, label);
    
    // Kirim data secara Non-blocking via ESP-NOW
    esp_err_t result = esp_now_send(receiverMacAddress, (uint8_t *) &myData, sizeof(myData));
    
    if (result != ESP_OK) {
        Serial.println("[ERR] Gagal menginisiasi pengiriman ESP-NOW");
    }
}

void TaskInference(void *pvParameters) {
    vTaskDelay(pdMS_TO_TICKS(1500)); //[cite: 81]

    while (1) { //[cite: 82]
        copyLatestWindow(); //[cite: 82]
        int sampelVAD = SAMPLE_RATE / 10; //[cite: 83]
        int32_t sumAbs = 0; //[cite: 83]
        for (int i = WIN_SAMPLES - sampelVAD; i < WIN_SAMPLES; i++) { //[cite: 84]
            sumAbs += abs(inferenceBuffer[i]); //[cite: 84]
        }
        int32_t avgAbs = sumAbs / sampelVAD; //[cite: 85]
        if (avgAbs < 1200) { //[cite: 86]
            vTaskDelay(pdMS_TO_TICKS(50)); //[cite: 86]
            continue; //[cite: 86]
        }

        Serial.printf("\n[DEBUG] TRIGGER VAD! Vol puncak: %ld\n", avgAbs); //[cite: 87]
        vTaskDelay(pdMS_TO_TICKS(500)); //[cite: 88]
        copyLatestWindow(); //[cite: 89]

        signal_t signal; //[cite: 92]
        signal.total_length = WIN_SAMPLES; //[cite: 92]
        signal.get_data     = &microphone_audio_signal_get_data; //[cite: 92]
        ei_impulse_result_t result = { 0 }; //[cite: 93]

        EI_IMPULSE_ERROR r = run_classifier(&signal, &result, false); //[cite: 93]
        if (r != EI_IMPULSE_OK) { //[cite: 93]
            Serial.printf("[ERR] Classifier gagal: %d\n", r); //[cite: 94]
        } else {
            Serial.println("--- Hasil Prediksi ---"); //[cite: 95]
            
            const char* highest_label = "noise";
            float highest_value = 0.0;

            for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) { //[cite: 96]
                Serial.printf("  %-8s : %.4f\n", result.classification[ix].label, result.classification[ix].value); //[cite: 96]
                
                // Cari kata dengan akurasi tertinggi dalam deteksi saat ini
                if (result.classification[ix].value > highest_value) {
                    highest_value = result.classification[ix].value;
                    highest_label = result.classification[ix].label;
                }
            }
            Serial.println("----------------------"); //[cite: 97]

            // 🎯 JIKA AKURASI > 75% (0.75), KIRIM DATA VIA ESP-NOW
            if (highest_value >= 0.75) {
                kirimPerintahKelektronik(highest_label);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(800)); // Cooldown ditingkatkan sedikit agar tidak spam transmisi //[cite: 98]
    }
}

void setup() {
    Serial.begin(115200); 
    delay(2000); 
    
    Serial.println("\n=================================");
    Serial.println("Sistem Kendali Suara - Transmitter ESP-NOW");
    Serial.println("=================================\n");

    if (!checkPSRAM()) { while (1) delay(1000); } ////[cite: 99]

    // Inisialisasi Wi-Fi ke Mode Station (Wajib untuk ESP-NOW)
    WiFi.mode(WIFI_STA);
    Serial.print("[INFO] MAC Address S3 Zero ini: ");
    Serial.println(WiFi.macAddress()); // Catat ini untuk dimasukkan ke kode S3 Main nanti!

    // Inisialisasi ESP-NOW
    if (esp_now_init() != ESP_OK) {
        Serial.println("[ERR] Gagal Inisialisasi ESP-NOW");
        return;
    }

    // Daftarkan callback kirim data
    esp_now_register_send_cb(OnDataSent);

    // Daftarkan Peer (Penerima)
    memcpy(peerInfo.peer_addr, receiverMacAddress, 6);
    peerInfo.channel = 0;  
    peerInfo.encrypt = false;
    
    if (esp_now_add_peer(&peerInfo) != ESP_OK){
        Serial.println("[ERR] Gagal menambahkan Peer");
        return;
    }

    ringBuffer = (int16_t*)heap_caps_malloc(WIN_SAMPLES * 2 * sizeof(int16_t), MALLOC_CAP_SPIRAM); //[cite: 100]
    inferenceBuffer = (int16_t*)heap_caps_malloc(WIN_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM); //[cite: 100]
    memset(ringBuffer, 0, WIN_SAMPLES * 2 * sizeof(int16_t)); //[cite: 101]
    memset(inferenceBuffer, 0, WIN_SAMPLES * sizeof(int16_t)); //[cite: 101]

    setupI2S(); //[cite: 101]
    ringMutex = xSemaphoreCreateMutex(); //[cite: 101]
    xTaskCreatePinnedToCore(TaskInference, "Inference", 32768, NULL, 1, NULL, 1); //[cite: 102]
    xTaskCreatePinnedToCore(TaskAudioCapture, "AudioCapture", 8192, NULL, 3, NULL, 0); //[cite: 102]
}

void loop() {
    vTaskDelete(NULL); // Hapus task loop utama agar hemat daya 
}
