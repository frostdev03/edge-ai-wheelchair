/* * ESP32-S3 Zero Data Forwarder untuk Edge Impulse
   Mengirim data audio mentah ke Serial Monitor
*/

#include "driver/i2s_std.h"

// --- KONFIGURASI PINOUT S3 ZERO ---
#define I2S_WS   9
#define I2S_SCK  8
#define I2S_SD   7

// --- KONFIGURASI AUDIO ---
#define SAMPLE_RATE     16000
#define GAIN_FACTOR     8      // Penguatan agar sinyal lebih terlihat di dashboard

i2s_chan_handle_t rx_chan;

void setupI2S() {
  i2s_chan_config_t rx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
  i2s_new_channel(&rx_chan_cfg, NULL, &rx_chan);

  i2s_std_config_t rx_std_cfg = {
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

  // Konfigurasi Channel LEFT
  rx_std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;

  i2s_channel_init_std_mode(rx_chan, &rx_std_cfg);
  i2s_channel_enable(rx_chan);
}

void setup() {
  // Gunakan Baud Rate tinggi agar tidak ada data yang tertinggal (loss)
  Serial.begin(115200);
  while (!Serial);

  setupI2S();

  // Memberikan waktu bagi hardware untuk stabil
  delay(1000);
}

void loop() {
  const int32_t samples_to_read = 128;
  int32_t raw_samples[samples_to_read];
  size_t bytes_read = 0;

  // Membaca data dari I2S
  if (i2s_channel_read(rx_chan, raw_samples, sizeof(raw_samples), &bytes_read, portMAX_DELAY) == ESP_OK) {
    int sampleCount = bytes_read / 4;

    for (int i = 0; i < sampleCount; i++) {
      // Konversi 32-bit ke 16-bit dengan shift >> 14 sesuai dataset
      int32_t pcm16 = (int32_t)(raw_samples[i] >> 14);

      // Terapkan Gain
      pcm16 *= GAIN_FACTOR;

      // Batasi rentang (Clipping)
      if (pcm16 > 32767) pcm16 = 32767;
      if (pcm16 < -32768) pcm16 = -32768;

      // Cetak ke Serial (Format yang dikenali Data Forwarder)
      Serial.println((int16_t)pcm16);
    }
  }
}
