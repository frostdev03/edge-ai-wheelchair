#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
// #include <PerintahSuaraKursiRoda-ss_inferencing.h>
// #include <SmartWheelchair_inferencing.h>
#include <SmartWheelchair-Final_inferencing.h>


typedef struct __attribute__((packed)) AudioPacket {
  uint32_t id_paket;
  int16_t data_suara[120];
} AudioPacket;

QueueHandle_t audioQueue;

#define SAMPLE_RATE 16000
int16_t audioBuffer[SAMPLE_RATE];
int bufferIndex = 0;
uint32_t last_id = 0;

static int microphone_audio_signal_get_data(size_t offset, size_t length, float *out_ptr) {
  if (offset + length > SAMPLE_RATE) return -1;
  numpy::int16_to_float(&audioBuffer[offset], out_ptr, length);
  return 0;
}

void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
  if (len == sizeof(AudioPacket)) {
    if (audioQueue != NULL) {
      AudioPacket paketMasuk;
      memcpy(&paketMasuk, incomingData, sizeof(AudioPacket));
      BaseType_t xHigherPriorityTaskWoken = pdFALSE;
      xQueueSendFromISR(audioQueue, &paketMasuk, &xHigherPriorityTaskWoken);
    }
  }
}

// Tambah di atas TaskRakitAudio, ganti dead code lama
#define HISTORY_SIZE 3
static float probHistory[HISTORY_SIZE][EI_CLASSIFIER_LABEL_COUNT];
static int historyIdx = 0;
static int historyCount = 0;
static int lostPackets = 0;

void TaskRakitAudio(void *pvParameters) {
  AudioPacket paketDiterima;

  while (1) {
    while (bufferIndex < SAMPLE_RATE) {
      if (xQueueReceive(audioQueue, &paketDiterima, portMAX_DELAY)) {
        // Anti packet loss
        // if (bufferIndex > 0 && paketDiterima.id_paket > last_id + 1) {
        //   int hilang = paketDiterima.id_paket - last_id - 1;
        //   for (int i = 0; i < hilang * 120 && bufferIndex < SAMPLE_RATE; i++) {
        //     audioBuffer[bufferIndex++] = last_valid_sample;
        //   }
        // }

        static bool firstPacket = true;
        if (firstPacket) {
          last_id = paketDiterima.id_paket - 1;
          firstPacket = false;
        }


        // Anti packet loss yang lebih aman untuk MFCC
        if (bufferIndex > 0 && paketDiterima.id_paket > last_id + 1) {
          int hilang = paketDiterima.id_paket - last_id - 1;
          lostPackets += hilang;


          // Ambil nilai audio terakhir yang valid sebelum paket hilang
          int16_t last_valid_sample = audioBuffer[bufferIndex - 1];

          for (int i = 0; i < hilang * 120 && bufferIndex < SAMPLE_RATE; i++) {
            // Isi dengan sampel terakhir, BUKAN angka nol mutlak
            audioBuffer[bufferIndex++] = last_valid_sample;
          }
        }
        last_id = paketDiterima.id_paket;

        for (int i = 0; i < 120; i++) {
          if (bufferIndex < SAMPLE_RATE) audioBuffer[bufferIndex++] = paketDiterima.data_suara[i];
        }
      }
    }

    // Klasifikasi
    signal_t signal;
    signal.total_length = SAMPLE_RATE;
    signal.get_data = &microphone_audio_signal_get_data;
    ei_impulse_result_t result = { 0 };

    Serial0.println(audioBuffer[0]);

    int32_t sumAbs = 0;
    int16_t peakAbs = 0;
    for (int i = 0; i < SAMPLE_RATE; i++) {
      int16_t a = abs(audioBuffer[i]);
      sumAbs += a;
      if (a > peakAbs) peakAbs = a;
    }

    Serial0.printf("[PRE-INF] avgAbs=%ld, peak=%d\n", sumAbs / SAMPLE_RATE, peakAbs);

    // Software AGC — hanya aktif jika ada kemungkinan sinyal suara
    if (peakAbs > 800) {
      const float targetPeak = 12000.0f;
      float gain = targetPeak / (float)peakAbs;
      if (gain > 8.0f) gain = 8.0f;  // Cap gain maksimal 8x
      if (gain < 0.5f) gain = 0.5f;  // Jangan attenuate terlalu jauh

      for (int i = 0; i < SAMPLE_RATE; i++) {
        int32_t s = (int32_t)((float)audioBuffer[i] * gain);
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;
        audioBuffer[i] = (int16_t)s;
      }
      Serial0.printf("[AGC] gain=%.2f, targetPeak=%d\n", gain, (int)targetPeak);
    }

    Serial0.printf("[LOSS] total lost per window: %d\n", lostPackets);
    lostPackets = 0;

    EI_IMPULSE_ERROR r = run_classifier(&signal, &result, false);

    if (r == EI_IMPULSE_OK) {

      Serial0.println("--- Hasil Prediksi ---");

      float probTertinggi = 0.0;
      String kelasTertinggi = "";
      float probDerau = 0.0;  // Tetap simpan derau secara terpisah jika butuh threshold beda
      float probUncertain = 0.0;

      for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
        float val = result.classification[ix].value;
        const char *label = result.classification[ix].label;

        Serial0.printf("    %s: %.5f\n", label, val);

        if (strcmp(label, "derau") == 0) probDerau = val;
        else if (strcmp(label, "uncertain") == 0) probUncertain = val;
        else if (val > probTertinggi) {
          probTertinggi = val;
          kelasTertinggi = label;
        }

        //   // Serial0.printf("    %s: %.5f\n", result.classification[ix].label, result.classification[ix].value);

        //   // Cek derau
        //   if (strcmp(result.classification[ix].label, "derau") == 0) {
        //     probDerau = result.classification[ix].value;
        //   }
        //   // Cari probabilitas tertinggi untuk kelas perintah
        //   else {
        //     if (result.classification[ix].value > probTertinggi) {
        //       probTertinggi = result.classification[ix].value;
        //       kelasTertinggi = result.classification[ix].label;
        //     }
        //   }
        // }
        // Serial0.println("----------------------");

        // String detectedCommand = "";

        // // Logika Penentuan Keputusan yang Adil
        // if (probDerau > 0.60) {
        //   detectedCommand = "derau";
        // } else if (probTertinggi > 0.4) {
        //   detectedCommand = kelasTertinggi;  // Akan memilih maju/mundur/dsb yang PALING YAKIN
        // } else {
        //   detectedCommand = "";  // Tidak ada yang lolos threshold
        // }
      }

      // Simpan hasil ke history
      for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
        probHistory[historyIdx][ix] = result.classification[ix].value;
      }
      historyIdx = (historyIdx + 1) % HISTORY_SIZE;
      if (historyCount < HISTORY_SIZE) historyCount++;

      // Rata-ratakan history
      float avgDerau = 0, avgTertinggi = 0;
      String avgKelas = "";
      for (int h = 0; h < historyCount; h++) {
        for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
          const char *lbl = result.classification[ix].label;  // Gunakan label dari hasil terakhir
          // Lebih mudah: loop terpisah
        }
      }

      String detectedCommand = "";

      // Sesuai config EI: threshold 0.53
      // if (probUncertain > 0.35) {
      //   detectedCommand = "uncertain";  // Model ragu, abaikan
      // } else
      if (probDerau > 0.44) {
        detectedCommand = "derau";
      } else if (probTertinggi > 0.4) {  // ← Sesuai detection threshold EI
        detectedCommand = kelasTertinggi;
      }

      static unsigned long lastCommandTime = 0;
      if (detectedCommand != "" && detectedCommand != "derau") {
        unsigned long now = millis();
        if (now - lastCommandTime > 1360) {  // ← Sesuai suppression period EI
          // Serial0.printf(">>> PERINTAH: %s <<<\n", detectedCommand.c_str());
          lastCommandTime = now;
          // Aksi aktuator di sini
        }
      }

      // Overlap 50%
      memcpy(audioBuffer, audioBuffer + (SAMPLE_RATE / 2), (SAMPLE_RATE / 2) * sizeof(int16_t));
      bufferIndex = SAMPLE_RATE / 2;
    }
  }
}

void setup() {
  Serial0.begin(115200);
  delay(2000);

  audioQueue = xQueueCreate(50, sizeof(AudioPacket));
  if (audioQueue == NULL) {
    Serial0.println("FATAL: Gagal membuat Queue!");
    while (1)
      ;
  }

  run_classifier_init();
  xTaskCreatePinnedToCore(TaskRakitAudio, "TaskAudioAI", 32768, NULL, 1, NULL, 1);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  WiFi.setChannel(1);

  if (esp_now_init() != ESP_OK) {
    Serial0.println("Gagal inisialisasi ESP-NOW");
    return;
  }
  esp_now_register_recv_cb(OnDataRecv);

  Serial0.println("\n[Sistem] Inisialisasi Selesai. Always-On AI Aktif!");
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}