#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <PerintahSuaraKursiRoda-ss_inferencing.h>

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

String lastCommand = "";
int commandCount = 0;
const int VOTES_NEEDED = 2;

void TaskRakitAudio(void *pvParameters) {
  AudioPacket paketDiterima;

  while (1) {
    while (bufferIndex < SAMPLE_RATE) {
      if (xQueueReceive(audioQueue, &paketDiterima, portMAX_DELAY)) {
        // Anti packet loss
        if (bufferIndex > 0 && paketDiterima.id_paket > last_id + 1) {
          int hilang = paketDiterima.id_paket - last_id - 1;
          for (int i = 0; i < hilang * 120 && bufferIndex < SAMPLE_RATE; i++) {
            audioBuffer[bufferIndex++] = 0;
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

    EI_IMPULSE_ERROR r = run_classifier(&signal, &result, false);

    if (r == EI_IMPULSE_OK) {
      float probMaju = 0, probMundur = 0, probKanan = 0, probKiri = 0, probStop = 0, probDerau = 0;

      Serial0.println("--- Hasil Prediksi ---");
      for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
        Serial0.printf("    %s: %.5f\n", result.classification[ix].label, result.classification[ix].value);
        if (strcmp(result.classification[ix].label, "maju") == 0) probMaju = result.classification[ix].value;
        else if (strcmp(result.classification[ix].label, "mundur") == 0) probMundur = result.classification[ix].value;
        else if (strcmp(result.classification[ix].label, "kanan") == 0) probKanan = result.classification[ix].value;
        else if (strcmp(result.classification[ix].label, "kiri") == 0) probKiri = result.classification[ix].value;
        else if (strcmp(result.classification[ix].label, "stop") == 0) probStop = result.classification[ix].value;
        else if (strcmp(result.classification[ix].label, "derau") == 0) probDerau = result.classification[ix].value;
      }
      Serial0.println("----------------------");

      String detectedCommand = "";
      if (probDerau > 0.75) detectedCommand = "derau";
      else if (probStop > 0.60) detectedCommand = "stop";
      else if (probMaju > 0.60) detectedCommand = "maju";
      else if (probMundur > 0.60) detectedCommand = "mundur";
      else if (probKanan > 0.60) detectedCommand = "kanan";
      else if (probKiri > 0.60) detectedCommand = "kiri";
      else detectedCommand = "";

      // Voting logic
      // if (detectedCommand != "" && detectedCommand != "derau") {
        // if (detectedCommand == lastCommand) {
        //   commandCount++;
        //   if (commandCount >= VOTES_NEEDED) {
        //     Serial0.printf(">>> %s! <<<\n", detectedCommand.c_str());
        //     // lakukan aksi aktuator di sini
        //     commandCount = 0;  // reset setelah eksekusi
        //     lastCommand = "";  // ← tambahkan ini
        //   }

        // } else if (detectedCommand == "derau" || detectedCommand == "") {
        //   // Tidak ada yang direset, biarkan voting tetap seperti adanya
        //   // Tapi reset kalau sudah terlalu lama (anti false positive)
        //   // Tidak perlu tambah kode apapun di sini
        // } else {
        //   lastCommand = detectedCommand;
        //   commandCount = 1;
        // }

        // Voting 
        if (detectedCommand == "derau" || detectedCommand == "") {
          // gpp
        } else if (detectedCommand == lastCommand) {
          commandCount++;
          if (commandCount >= VOTES_NEEDED) {
            Serial0.printf(">>> %s! <<<\n", detectedCommand.c_str());
            // aksi aktuator
            commandCount = 0;  // reset 
            lastCommand = "";
          }
        } else {
          lastCommand = detectedCommand;
          commandCount = 1;
        }
      
    }

    // Overlap 50%
    memcpy(audioBuffer, audioBuffer + (SAMPLE_RATE / 2), (SAMPLE_RATE / 2) * sizeof(int16_t));
    bufferIndex = SAMPLE_RATE / 2;
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