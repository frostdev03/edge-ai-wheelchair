#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>

// 1. MASUKKAN PUSTAKA EDGE IMPULSE MILIKMU DI SINI
// Pastikan nama header ini sesuai persis dengan nama proyekmu di EI
// #include <PerintahSuaraKursiRoda_inferencing.h>
#include <PerintahSuaraKursiRoda-ss_inferencing.h>

// =======================================================
// 2. STRUKTUR DATA & QUEUE ESP-NOW
// =======================================================
typedef struct __attribute__((packed)) AudioPacket {
  uint32_t id_paket;
  int16_t data_suara[120];
} AudioPacket;

QueueHandle_t audioQueue;

// =======================================================
// 3. VARIABEL BUFFER & VAD
// =======================================================
#define SAMPLE_RATE 300
int16_t audioBuffer[SAMPLE_RATE];  // Buffer penampung 1 detik persis
int bufferIndex = 0;
uint32_t last_id = 0;

const int AMBANG_BATAS_VOLUME = 1000;

enum StatusSistem { MENDENGAR,
                    MEREKAM };
StatusSistem statusSaatIni = MENDENGAR;

int hitungVolumeRataRata(int16_t *data, int panjang) {
  int32_t total = 0;
  for (int i = 0; i < panjang; i++) {
    total += abs(data[i]);
  }
  return total / panjang;
}

// =======================================================
// 4. FUNGSI JEMBATAN UNTUK EDGE IMPULSE
// =======================================================
// Edge Impulse butuh data dalam format 'float'. Fungsi ini bertugas
// mengambil data dari audioBuffer (int16) milik kita dan mengonversinya
// ke float untuk diumpankan ke model MFCC & CNN.
static int microphone_audio_signal_get_data(size_t offset, size_t length, float *out_ptr) {
  numpy::int16_to_float(&audioBuffer[offset], out_ptr, length);
  return 0;
}

// =======================================================
// 5. CALLBACK ESP-NOW
// =======================================================
void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
  if (len == sizeof(AudioPacket)) {
    AudioPacket paketMasuk;
    memcpy(&paketMasuk, incomingData, sizeof(AudioPacket));

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(audioQueue, &paketMasuk, &xHigherPriorityTaskWoken);
  }
}

// // =======================================================
// // 6. TASK FREERTOS: VAD & INFERENSI AI
// // =======================================================
// void TaskRakitAudio(void *pvParameters) {
//   AudioPacket paketDiterima;

//   while(1) {
//     if (xQueueReceive(audioQueue, &paketDiterima, portMAX_DELAY)) {

//       int volumePaket = hitungVolumeRataRata(paketDiterima.data_suara, 120);

//       // --- LOGIKA VAD ---
//       if (statusSaatIni == MENDENGAR) {
//         if (volumePaket > AMBANG_BATAS_VOLUME) {
//           Serial0.printf("\n[VAD] Suara dideteksi (Vol: %d). Merekam...\n", volumePaket);
//           statusSaatIni = MEREKAM;
//           bufferIndex = 0;
//           last_id = paketDiterima.id_paket;

//           for(int i = 0; i < 120; i++) audioBuffer[bufferIndex++] = paketDiterima.data_suara[i];
//         }
//       }
//       else if (statusSaatIni == MEREKAM) {

//         // Zero padding jika ada paket ESP-NOW yang hilang di udara
//         if (paketDiterima.id_paket > last_id + 1) {
//           int paketHilang = paketDiterima.id_paket - last_id - 1;
//           for(int i = 0; i < (paketHilang * 120); i++) {
//             if (bufferIndex < SAMPLE_RATE) audioBuffer[bufferIndex++] = 0;
//           }
//         }
//         last_id = paketDiterima.id_paket;

//         for(int i = 0; i < 120; i++) {
//           if (bufferIndex < SAMPLE_RATE) audioBuffer[bufferIndex++] = paketDiterima.data_suara[i];
//         }

//         // --- JIKA 1 DETIK SUDAH PENUH, EKSEKUSI AI ---
//         if (bufferIndex >= SAMPLE_RATE) {
//           Serial0.println("[AI] 1 Detik audio terkumpul. Memulai MFCC & Klasifikasi CNN...");

//           // Siapkan sinyal untuk Edge Impulse
//           signal_t signal;
//           signal.total_length = SAMPLE_RATE;
//           signal.get_data = &microphone_audio_signal_get_data;
//           ei_impulse_result_t result = { 0 };

//           // Eksekusi model! (false = matikan debug log bawaan EI)
//           EI_IMPULSE_ERROR r = run_classifier(&signal, &result, false);

//           if (r != EI_IMPULSE_OK) {
//               Serial0.printf("ERR: Klasifikasi Gagal (%d)\n", r);
//           } else {
//               // Tampilkan waktu proses (Sangat penting untuk laporan skripsi!)
//               Serial0.printf("Waktu Proses: DSP (MFCC): %d ms, Klasifikasi (CNN): %d ms\n",
//                             result.timing.dsp, result.timing.classification);

//               // Tampilkan hasil prediksi
//               Serial0.println("--- Hasil Prediksi ---");
//               for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
//                   Serial0.printf("    %s: %.5f\n", result.classification[ix].label, result.classification[ix].value);

//                   // --- LOGIKA PENGGERAK MOTOR NANTI DITARUH DI SINI ---
//                   // Contoh:
//                   // if (strcmp(result.classification[ix].label, "maju") == 0 && result.classification[ix].value > 0.8) {
//                   //     maju();
//                   // }
//               }
//               Serial0.println("----------------------");
//           }

//           // Reset kembali ke mode mendengarkan
//           Serial0.println("\n[VAD] Sistem siap mendengarkan perintah baru...");
//           statusSaatIni = MENDENGAR;
//         }
//       }
//     }
//   }
// }

// =======================================================
// 6. TASK FREERTOS: VAD & INFERENSI AI (DENGAN PRE-ROLL)
// =======================================================
void TaskRakitAudio(void *pvParameters) {
  AudioPacket paketDiterima;

  // Array untuk menyimpan "masa lalu" (3 paket = 360 sampel = ~22,5 ms)
  int16_t preRollBuffer[360] = { 0 };

  while (1) {
    if (xQueueReceive(audioQueue, &paketDiterima, portMAX_DELAY)) {

      int volumePaket = hitungVolumeRataRata(paketDiterima.data_suara, 120);

      // --- LOGIKA VAD ---
      if (statusSaatIni == MENDENGAR) {

        // 1. Simpan paket ini ke masa lalu secara bergeser (Sliding Window)
        // Geser 2 paket lama ke kiri
        for (int i = 0; i < 240; i++) preRollBuffer[i] = preRollBuffer[i + 120];
        // Masukkan paket terbaru ke ujung kanan
        for (int i = 0; i < 120; i++) preRollBuffer[240 + i] = paketDiterima.data_suara[i];

        // 2. Cek apakah ada lonjakan volume keras (Trigger)
        if (volumePaket > AMBANG_BATAS_VOLUME) {
          Serial.printf("\n[VAD] Suara dideteksi (Vol: %d). Merekam...\n", volumePaket);
          statusSaatIni = MEREKAM;
          bufferIndex = 0;
          last_id = paketDiterima.id_paket;

          // 3. MASUKKAN MASA LALU (Pre-Roll) KE DALAM BUFFER UTAMA DULU
          // Ini akan menyelamatkan huruf "S" pada "Stop" dan "K" pada "Kiri"
          for (int i = 0; i < 360; i++) {
            audioBuffer[bufferIndex++] = preRollBuffer[i];
          }
        }
      } else if (statusSaatIni == MEREKAM) {

        // Zero padding jika ada paket ESP-NOW yang hilang di udara
        if (paketDiterima.id_paket > last_id + 1) {
          int paketHilang = paketDiterima.id_paket - last_id - 1;
          for (int i = 0; i < (paketHilang * 120); i++) {
            if (bufferIndex < SAMPLE_RATE) audioBuffer[bufferIndex++] = 0;
          }
        }
        last_id = paketDiterima.id_paket;

        for (int i = 0; i < 120; i++) {
          if (bufferIndex < SAMPLE_RATE) audioBuffer[bufferIndex++] = paketDiterima.data_suara[i];
        }

        // --- JIKA 1 DETIK SUDAH PENUH, EKSEKUSI AI ---
        if (bufferIndex >= SAMPLE_RATE) {
          Serial.println("[AI] 1 Detik audio terkumpul. Memulai MFCC & Klasifikasi CNN...");

          signal_t signal;
          signal.total_length = SAMPLE_RATE;
          signal.get_data = &microphone_audio_signal_get_data;
          ei_impulse_result_t result = { 0 };

          EI_IMPULSE_ERROR r = run_classifier(&signal, &result, false);

          if (r != EI_IMPULSE_OK) {
              Serial.printf("ERR: Klasifikasi Gagal (%d)\n", r);
          } else {
              Serial.println("--- Hasil Prediksi ---");
              for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
                  Serial.printf("    %s: %.5f\n", result.classification[ix].label, result.classification[ix].value);
              }
              Serial.println("----------------------");
          }

          // if (r != EI_IMPULSE_OK) {
          //   Serial.printf("ERR: Klasifikasi Gagal (%d)\n", r);
          // } else {
          //   Serial.println("--- Hasil Prediksi ---");

          //   // Variabel sementara untuk menyimpan probabilitas tertinggi
          //   float probMaju = 0, probMundur = 0, probKanan = 0, probKiri = 0, probStop = 0;

          //   for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
          //     Serial.printf("    %s: %.5f\n", result.classification[ix].label, result.classification[ix].value);

          //     // Simpan nilai probabilitas berdasarkan labelnya
          //     if (strcmp(result.classification[ix].label, "maju") == 0) probMaju = result.classification[ix].value;
          //     else if (strcmp(result.classification[ix].label, "mundur") == 0) probMundur = result.classification[ix].value;
          //     else if (strcmp(result.classification[ix].label, "kanan") == 0) probKanan = result.classification[ix].value;
          //     else if (strcmp(result.classification[ix].label, "kiri") == 0) probKiri = result.classification[ix].value;
          //     else if (strcmp(result.classification[ix].label, "stop") == 0) probStop = result.classification[ix].value;
          //   }
          //   Serial.println("----------------------");

          //   // --- LOGIKA FILTER KEPERCAYAAN (CONFIDENCE FILTER) ---
          //   // Hanya eksekusi jika AI yakin di atas 80% (0.80)
          //   // Prioritaskan "STOP" karena ini perintah paling krusial untuk keselamatan
          //   if (probStop > 0.80) {
          //     Serial.println(">>> AKSI: KURSI RODA BERHENTI! <<<");
          //     // TODO: Beri sinyal ke PID Task untuk mengubah target kecepatan jadi 0 km/h
          //   } else if (probMaju > 0.80) {
          //     Serial.println(">>> AKSI: KURSI RODA MAJU! <<<");
          //     // TODO: Beri sinyal ke PID Task untuk mengubah target kecepatan jadi 4 km/h (Maju)
          //   } else if (probMundur > 0.80) {
          //     Serial.println(">>> AKSI: KURSI RODA MUNDUR! <<<");
          //     // TODO: Beri sinyal ke PID Task untuk arah mundur
          //   } else if (probKanan > 0.80) {
          //     Serial.println(">>> AKSI: BELOK KANAN! <<<");
          //     // TODO: Motor kiri maju, motor kanan diam
          //   } else if (probKiri > 0.80) {
          //     Serial.println(">>> AKSI: BELOK KIRI! <<<");
          //     // TODO: Motor kanan maju, motor kiri diam
          //   } else {
          //     // Jika tidak ada probabilitas yang tembus 80%, sistem akan mengabaikannya
          //     // Ini otomatis menangani kelas "Derau" atau suara yang tidak jelas
          //     Serial.println(">>> AKSI: Suara tidak jelas (Diabaikan) <<<");
          //   }
          // }

          // Bersihkan "mesin waktu" agar tidak membawa data usang ke kata berikutnya
          memset(preRollBuffer, 0, sizeof(preRollBuffer));

          Serial.println("\n[VAD] Sistem siap mendengarkan perintah baru...");
          statusSaatIni = MENDENGAR;
        }
      }
    }
  }
}

// =======================================================
// 7. SETUP & LOOP
// =======================================================
void setup() {
  Serial0.begin(115200);
  delay(2000);

  // Inisialisasi Edge Impulse (Wajib)
  run_classifier_init();

  audioQueue = xQueueCreate(100, sizeof(AudioPacket));

  // Beri Stack Size besar (32KB) karena AI Edge Impulse butuh RAM lega saat berhitung
  xTaskCreatePinnedToCore(TaskRakitAudio, "TaskAudioAI", 32768, NULL, 1, NULL, 1);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  WiFi.setChannel(1);

  if (esp_now_init() != ESP_OK) {
    Serial0.println("Gagal inisialisasi ESP-NOW");
    return;
  }
  esp_now_register_recv_cb(OnDataRecv);

  Serial0.println("\n[Sistem] Inisialisasi Selesai. VAD & Edge AI Aktif!");
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}