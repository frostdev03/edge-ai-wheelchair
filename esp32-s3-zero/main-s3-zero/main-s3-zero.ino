#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include "driver/i2s_std.h"

// KONFIGURASI PERANGKAT KERAS & JARINGAN
uint8_t alamatReceiver[] = {0x30, 0xED, 0xA0, 0xBD, 0x6A, 0x44};

// Pinout I2S INMP441 (Sudah disesuaikan ke 9, 8, 7)
#define I2S_WS   9   // LRCLK
#define I2S_SCK  8   // BCLK
#define I2S_SD   7   // DOUT

i2s_chan_handle_t rx_chan;
esp_now_peer_info_t peerInfo;

// 2. STRUKTUR DATA AUDIO 
// __attribute__((packed)) memastikan memori diatur rapat tanpa ruang kosong
typedef struct __attribute__((packed)) AudioPacket {
  uint32_t id_paket;         // 4 Byte: Nomor urut paket
  int16_t data_suara[120];   // 240 Byte: 120 sampel suara (16-bit)
                             // Total = 244 Byte (Sangat optimal)
} AudioPacket;

AudioPacket paketKirim;

// =======================================================
// 3. FUNGSI INISIALISASI
// =======================================================
void setupI2S() {
  i2s_chan_config_t rx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
  i2s_new_channel(&rx_chan_cfg, NULL, &rx_chan);

  i2s_std_config_t rx_std_cfg = {
      .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(16000), // Sampling 16 kHz
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
      .gpio_cfg = {
          .mclk = I2S_GPIO_UNUSED,
          .bclk = (gpio_num_t)I2S_SCK,
          .ws   = (gpio_num_t)I2S_WS,
          .dout = I2S_GPIO_UNUSED,
          .din  = (gpio_num_t)I2S_SD,
          .invert_flags = {false, false, false},
      },
  };
  rx_std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;

  i2s_channel_init_std_mode(rx_chan, &rx_std_cfg);
  i2s_channel_enable(rx_chan);
}

// Callback pengiriman (Sengaja dikosongkan agar tidak membebani loop)
void OnDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {}

void setup() {
  Serial.begin(115200);
  delay(2000); 
  Serial.println("\n[Sistem Kalung] Memulai Inisialisasi...");

  // Setup WiFi
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  WiFi.setChannel(1);
  esp_wifi_set_ps(WIFI_PS_NONE); // Matikan power saving agar transmisi agresif

  // Setup ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Gagal inisialisasi ESP-NOW");
    return;
  }
  esp_now_register_send_cb(OnDataSent);

  memset(&peerInfo, 0, sizeof(peerInfo)); 
  memcpy(peerInfo.peer_addr, alamatReceiver, 6);
  peerInfo.channel = 1;      
  peerInfo.ifidx = WIFI_IF_STA;
  peerInfo.encrypt = false;  

  if (esp_now_add_peer(&peerInfo) != ESP_OK){
    Serial.println("Gagal menambahkan Peer");
    return;
  }

  setupI2S();
  paketKirim.id_paket = 0;
  
  Serial.println("[Sistem Kalung] I2S & ESP-NOW Siap. Mulai Streaming Audio...");
}

// =======================================================
// 4. LOOP UTAMA (ALWAYS ON & ALWAYS TRANSMIT)
// =======================================================
void loop() {
  int32_t raw_samples[120]; // Buffer sementara untuk membaca 32-bit dari mic
  size_t bytesRead = 0;
  
  // Baca suara mentah dari INMP441 sampai buffer 120 sampel penuh
  if (i2s_channel_read(rx_chan, raw_samples, sizeof(raw_samples), &bytesRead, portMAX_DELAY) == ESP_OK) {
    
    int sampleCount = bytesRead / 4; // 1 sampel 32-bit = 4 Byte

    // Konversi dan masukkan ke struktur paket ESP-NOW
    for (int i = 0; i < sampleCount; i++) {
      paketKirim.data_suara[i] = raw_samples[i] >> 14; 
    }

    // Tembakkan paket audio secara nirkabel!
    esp_err_t hasil = esp_now_send(alamatReceiver, (uint8_t *) &paketKirim, sizeof(paketKirim));
    
    if (hasil == ESP_OK) {
      paketKirim.id_paket++; // Naikkan ID untuk paket berikutnya
    } else {
      // Jika butuh debugging sesekali saat gagal tembak
      // Serial.println("Gagal menembak paket!"); 
    }
  }
}