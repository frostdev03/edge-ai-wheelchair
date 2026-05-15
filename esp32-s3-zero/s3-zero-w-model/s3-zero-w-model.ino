/* 
 * Edge AI Voice Command - ESP32-S3 Zero Standalone Inference
 * Versi: dengan threshold deteksi dan output serial bersih
 */

// Ganti nama file header sesuai model baru yang di-export dari Edge Impulse
#include "CompletelyoWorksKeywordsSpotting_inferencing.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"

// --- KONFIGURASI PINOUT S3 ZERO ---
#define I2S_WS 9
#define I2S_SCK 8
#define I2S_SD 7

// --- KONFIGURASI DETEKSI ---
// Naikkan jika terlalu banyak false positive, turunkan jika keyword sulit terdeteksi
#define THRESHOLD_KEYWORD 0.70f

// Aktifkan (true) untuk mencetak semua probabilitas tiap kelas, matikan (false) untuk output bersih
#define DEBUG_PROBABILITAS true

// Aktifkan (true) untuk mencetak level audio tiap slice
#define DEBUG_AUDIO_LEVEL false

i2s_chan_handle_t rx_chan;

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

// --- DEKLARASI FUNGSI ---
static bool microphone_inference_start(uint32_t n_samples);
static bool microphone_inference_record(void);
static int microphone_audio_signal_get_data(size_t offset, size_t length, float *out_ptr);
static void microphone_inference_end(void);
void setupI2S();

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println("\n[S3 Zero] Sistem Edge AI Kursi Roda Memulai...");
  ei_printf("Model : %d kelas, window %d ms\n",
            (int)(sizeof(ei_classifier_inferencing_categories) / sizeof(ei_classifier_inferencing_categories[0])),
            EI_CLASSIFIER_RAW_SAMPLE_COUNT / 16);
  ei_printf("Threshold keyword : %.2f\n", THRESHOLD_KEYWORD);

  run_classifier_init();
  setupI2S();

  ei_printf("Siap menerima perintah suara...\n\n");

  // if (microphone_inference_start(EI_CLASSIFIER_SLICE_SIZE) == false) {
  //   ei_printf("ERR: Gagal mengalokasikan buffer audio!\r\n");
  //   return;
  // }
  // Pastikan EI_CLASSIFIER_RAW_SAMPLE_COUNT bernilai 16000 (1 detik)
  if (microphone_inference_start(EI_CLASSIFIER_RAW_SAMPLE_COUNT) == false) {
    ei_printf("ERR: Gagal alokasi buffer!\n");
    return;
  }
}

void loop() {

  if (Serial.available() > 0) {
    char incomingChar = Serial.read();
    if (incomingChar == 's') {
      Serial.println("\n[COMMAND] EXPORT_START");
      Serial.println("--- LOG DATA KURSI RODA ---");
      Serial.print("Timestamp Internal: "); Serial.println(millis());
      // Kamu bisa menambahkan variabel status di sini jika ingin export data tertentu
    }
  }

  bool m = microphone_inference_record();
  if (!m) {
    ei_printf("ERR: Gagal merekam audio...\n");
    return;
  }

  signal_t signal;
  signal.total_length = EI_CLASSIFIER_SLICE_SIZE;
  signal.get_data = &microphone_audio_signal_get_data;

  // --- DIAGNOSTIK LEVEL AUDIO (opsional) ---
  if (DEBUG_AUDIO_LEVEL) {
    float energi = 0;
    for (int i = 0; i < EI_CLASSIFIER_SLICE_SIZE; i++) {
      float s;
      microphone_audio_signal_get_data(i, 1, &s);
      energi += abs(s);
    }
    ei_printf("Level Audio (Avg): %.2f\n", energi / EI_CLASSIFIER_SLICE_SIZE);
  }

  ei_impulse_result_t result = { 0 };

  EI_IMPULSE_ERROR r = run_classifier_continuous(&signal, &result, debug_nn);
  if (r != EI_IMPULSE_OK) {
    ei_printf("ERR: Gagal menjalankan classifier (%d)\n", r);
    return;
  }

  if (++print_results >= (EI_CLASSIFIER_SLICES_PER_MODEL_WINDOW)) {

    // --- CETAK SEMUA PROBABILITAS (mode debug) ---
    if (DEBUG_PROBABILITAS) {
      ei_printf("--- Probabilitas ---\n");
      for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
        ei_printf("  %s: ", result.classification[ix].label);
        ei_printf_float(result.classification[ix].value);
        ei_printf("\n");
      }
    }

    // --- LOGIKA DETEKSI DENGAN THRESHOLD ---
    float probTertinggi = 0.0f;
    const char *kelasTertinggi = "";

    for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
      float val = result.classification[ix].value;
      const char *label = result.classification[ix].label;

      // Cari kelas non-derau dengan probabilitas tertinggi
      if (strcmp(label, "derau") != 0 && val > probTertinggi) {
        probTertinggi = val;
        kelasTertinggi = label;
      }
    }

    // Cetak hasil
    if (probTertinggi >= THRESHOLD_KEYWORD) {
      ei_printf("[PERINTAH] %s (%.2f)\n", kelasTertinggi, probTertinggi);
      // TODO: kirim via ESP-NOW ke ESP32-S3 lain
    } else {
      // Tidak ada perintah terdeteksi, tidak perlu dicetak
      // Aktifkan baris di bawah jika ingin melihat kondisi idle:
      // ei_printf("[IDLE]\n");
    }

    print_results = 0;
  }
}

// ==========================================================
// FUNGSI AUDIO - TIDAK BERUBAH
// ==========================================================

static void audio_inference_callback(uint32_t n_samples) {
  for (int i = 0; i < n_samples; i++) {
    inference.buffers[inference.buf_select][inference.buf_count++] = sampleBuffer[i];
    if (inference.buf_count >= inference.n_samples) {
      inference.buf_select ^= 1;
      inference.buf_count = 0;
      inference.buf_ready = 1;
    }
  }
}

// static void capture_samples(void *arg) {
//   const int32_t samples_to_read = 512;
//   int32_t raw_samples[samples_to_read];
//   size_t bytes_read = 0;

//   while (record_status) {
//     if (i2s_channel_read(rx_chan, raw_samples, sizeof(raw_samples), &bytes_read, portMAX_DELAY) == ESP_OK) {
//       int sampleCount = bytes_read / 4;
//       // for (int i = 0; i < sampleCount; i++) {
//       //     sampleBuffer[i] = (int16_t)(raw_samples[i] >> 14);
//       // }
//       float GAIN_FACTOR = 2.0;  // Mulai dari 2.0, naikkan perlahan jika perlu (misal 4.0 atau 8.0)

//       for (int i = 0; i < sampleCount; i++) {
//         // Tetap gunakan >> 14 sesuai dataset
//         int16_t raw_val = (int16_t)(raw_samples[i] >> 14);

//         // Kalikan dengan gain, lalu pastikan tidak melebihi batas int16 (clipping)
//         int32_t amplified = raw_val * GAIN_FACTOR;

//         if (amplified > 32767) amplified = 32767;
//         if (amplified < -32768) amplified = -32768;

//         sampleBuffer[i] = (int16_t)amplified;
//       }
//       if (record_status) {
//         audio_inference_callback(sampleCount);
//       } else {
//         break;
//       }
//     }
//   }
//   vTaskDelete(NULL);
// }

static void capture_samples(void *arg) {
  const int32_t samples_to_read = 512;
  int32_t raw_samples[samples_to_read];
  size_t bytes_read = 0;

  // Nilai pengali gain, bisa dicoba dari 4 sampai 8
  const int16_t GAIN_FACTOR = 8;

  while (record_status) {
    if (i2s_channel_read(rx_chan, raw_samples, sizeof(raw_samples), &bytes_read, portMAX_DELAY) == ESP_OK) {
      int sampleCount = bytes_read / 4;
      for (int i = 0; i < sampleCount; i++) {
        // Tetap gunakan >> 14 sesuai dataset
        int32_t pcm16 = (int32_t)(raw_samples[i] >> 14);

        // Kalikan dengan gain factor untuk meningkatkan volume
        pcm16 *= GAIN_FACTOR;

        // Batasi (clipping) agar tidak melebihi rentang int16_t
        if (pcm16 > 32767) pcm16 = 32767;
        if (pcm16 < -32768) pcm16 = -32768;

        sampleBuffer[i] = (int16_t)pcm16;
      }
      if (record_status) {
        audio_inference_callback(sampleCount);
      } else {
        break;
      }
    }
  }
  vTaskDelete(NULL);
}

void setupI2S() {
  i2s_chan_config_t rx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
  i2s_new_channel(&rx_chan_cfg, NULL, &rx_chan);

  i2s_std_config_t rx_std_cfg = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
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

  i2s_channel_init_std_mode(rx_chan, &rx_std_cfg);
  i2s_channel_enable(rx_chan);
}

static bool microphone_inference_start(uint32_t n_samples) {
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
  record_status = true;

  xTaskCreate(capture_samples, "CaptureSamples", 1024 * 32, NULL, 10, NULL);
  return true;
}

static bool microphone_inference_record(void) {
  if (inference.buf_ready == 1) {
    ei_printf("ERR: Buffer overrun - slice tidak sempat diproses.\n");
  }
  while (inference.buf_ready == 0) {
    delay(1);
  }
  inference.buf_ready = 0;
  return true;
}

static int microphone_audio_signal_get_data(size_t offset, size_t length, float *out_ptr) {
  numpy::int16_to_float(&inference.buffers[inference.buf_select ^ 1][offset], out_ptr, length);
  return 0;
}

static void microphone_inference_end(void) {
  i2s_channel_disable(rx_chan);
  i2s_del_channel(rx_chan);
  ei_free(inference.buffers[0]);
  ei_free(inference.buffers[1]);
}
