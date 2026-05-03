#include <Arduino.h>
#include "driver/i2s_std.h"

// Pinout khusus untuk ESP32-S3 Zero (GPIO Matrix Bebas)
#define I2S_WS   9   // LRCLK
#define I2S_SCK  8   // BCLK
#define I2S_SD   7  // DOUT dari mic

i2s_chan_handle_t rx_chan;
bool isRecording = false; 

void setupI2S() {
  // 1. Alokasi channel (API Baru)
  i2s_chan_config_t rx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
  i2s_new_channel(&rx_chan_cfg, NULL, &rx_chan);

  // 2. Konfigurasi mode standar I2S (16000 Hz, 32-bit, Mono Kiri)
  i2s_std_config_t rx_std_cfg = {
      .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(16000),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
      .gpio_cfg = {
          .mclk = I2S_GPIO_UNUSED,
          .bclk = (gpio_num_t)I2S_SCK,
          .ws   = (gpio_num_t)I2S_WS,
          .dout = I2S_GPIO_UNUSED,
          .din  = (gpio_num_t)I2S_SD,
          .invert_flags = {
              .mclk_inv = false,
              .bclk_inv = false,
              .ws_inv   = false,
          },
      },
  };
  
  // Mengambil data dari slot kiri (pastikan L/R di INMP441 disolder ke GND)
  rx_std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;

  // 3. Terapkan konfigurasi dan nyalakan
  i2s_channel_init_std_mode(rx_chan, &rx_std_cfg);
  i2s_channel_enable(rx_chan);
}

void setup() {
  Serial.begin(115200);
  
  // Jeda untuk inisialisasi Native USB S3 Zero
  delay(2000); 
  
  Serial.println("=========================================");
  Serial.println("Tes I2S INMP441 di ESP32-S3 Zero Siap!");
  Serial.println("Ketik 'r' untuk mulai rekam, 's' untuk stop.");
  Serial.println("=========================================");

  setupI2S();
}

void loop() {
  // Cek perintah dari Serial Monitor
  if (Serial.available()) {
    char cmd = Serial.read();
    if (cmd == 'r') {
      isRecording = true;
      Serial.println("Mulai merekam...");
    } else if (cmd == 's') {
      isRecording = false;
      Serial.println("Rekam berhenti.");
    }
  }

  // Jika sedang merekam, baca data I2S
  if (isRecording) {
    int32_t samples[256];
    size_t bytesRead = 0;
    
    // Fungsi pembacaan I2S API Baru (Menggantikan i2s_read yang lama)
    if (i2s_channel_read(rx_chan, samples, sizeof(samples), &bytesRead, portMAX_DELAY) == ESP_OK) {
      
      int sampleCount = bytesRead / 4; // Karena 1 sampel 32-bit = 4 Byte

      for (int i = 0; i < sampleCount; i++) {
        // Konversi 32-bit ke 16-bit
        int16_t s = samples[i] >> 14; 
        
        // Kirim biner ke PC
        Serial.write((uint8_t*)&s, 2); 
      }
    }
  }
}