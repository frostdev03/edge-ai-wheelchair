/* * Firmware Penguji Frekuensi Mikrofon (Tanpa ML)
 * Menampilkan Peak Frequency dan Average Frequency
 * Cocok untuk pengisian tabel pengujian.
*/

#include <Arduino.h>
#include "driver/i2s_std.h"
#include "arduinoFFT.h" // Instal via Arduino Library Manager

// --- KONFIGURASI PINOUT S3 ZERO ---
#define I2S_WS   9
#define I2S_SCK  8
#define I2S_SD   7

// --- KONFIGURASI AUDIO & FFT ---
#define SAMPLE_RATE     16000
#define GAIN_FACTOR     8
#define SAMPLES         1024  // Harus pangkat 2 (resolusi FFT)
#define NOISE_THRESHOLD 80000 // Ubah angka ini jika terlalu sensitif / kurang sensitif

i2s_chan_handle_t rx_chan;

// Array untuk kalkulasi FFT
double vReal[SAMPLES];
double vImag[SAMPLES];

// Inisialisasi object FFT (Syntax arduinoFFT v2.x)
ArduinoFFT<double> FFT = ArduinoFFT<double>(vReal, vImag, SAMPLES, SAMPLE_RATE);

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

  rx_std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
  i2s_channel_init_std_mode(rx_chan, &rx_std_cfg);
  i2s_channel_enable(rx_chan);
}

void setup() {
  // Menggunakan baud rate standar agar mudah dibaca di Serial Monitor
  Serial.begin(115200);
  while (!Serial);

  setupI2S();
  delay(1000);
  
  Serial.println("--------------------------------------------------");
  Serial.println("Sistem Mikrofon Aktif. Silakan ucapkan perintah...");
  Serial.println("--------------------------------------------------");
}

void loop() {
  size_t bytes_read = 0;
  int32_t raw_samples[128]; 
  int sample_index = 0;

  // 1. Kumpulkan sampel audio sampai array penuh (1024 sampel)
  while (sample_index < SAMPLES) {
    if (i2s_channel_read(rx_chan, raw_samples, sizeof(raw_samples), &bytes_read, portMAX_DELAY) == ESP_OK) {
      int sampleCount = bytes_read / 4;
      
      for (int i = 0; i < sampleCount; i++) {
        if (sample_index >= SAMPLES) break;

        // Konversi dan Gain sesuai konfigurasi awalmu
        int32_t pcm16 = (int32_t)(raw_samples[i] >> 14);
        pcm16 *= GAIN_FACTOR;

        if (pcm16 > 32767) pcm16 = 32767;
        if (pcm16 < -32768) pcm16 = -32768;

        vReal[sample_index] = (double)pcm16;
        vImag[sample_index] = 0.0; // Imaginer selalu 0 untuk audio mentah
        sample_index++;
      }
    }
  }

  // 2. Proses Fast Fourier Transform (FFT)
  FFT.windowing(FFTWindow::Hamming, FFTDirection::Forward);
  FFT.compute(FFTDirection::Forward);
  FFT.complexToMagnitude();

  // 3. Kalkulasi Peak dan Average (Spectral Centroid)
  double peakFreq = 0;
  double maxMagnitude = 0;
  double sumFreqMag = 0;
  double sumMag = 0;

  // Mulai dari index 3 (~46 Hz) untuk mengabaikan DC Offset dan noise frekuensi sangat rendah
  for (int i = 3; i < (SAMPLES / 2); i++) {
    double freq = (i * 1.0 * SAMPLE_RATE) / SAMPLES;
    double mag = vReal[i];

    // Cari Frekuensi Dominan (Peak)
    if (mag > maxMagnitude) {
      maxMagnitude = mag;
      peakFreq = freq;
    }

    // Akumulasi untuk Rata-rata (Avg)
    sumFreqMag += (freq * mag);
    sumMag += mag;
  }

  // 4. Tampilkan data HANYA jika ada suara yang cukup keras masuk
  if (maxMagnitude > NOISE_THRESHOLD) {
    double avgFreq = (sumMag > 0) ? (sumFreqMag / sumMag) : 0;
    
    Serial.printf("Suara Terdeteksi! -> Peak Freq: %.2f Hz | Avg Freq: %.2f Hz\n", peakFreq, avgFreq);
    
    // Beri jeda 1 detik agar kamu sempat mencatatnya ke Tabel 4.3 dan tidak spam di monitor
    delay(1000); 
  }
}
