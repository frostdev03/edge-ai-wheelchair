#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>

// =======================================================
// 1. STRUKTUR DATA & ANTARA (QUEUE)
// =======================================================
typedef struct __attribute__((packed)) AudioPacket {
  uint32_t id_paket;
  int16_t data_suara[120];
} AudioPacket;

QueueHandle_t audioQueue;

// =======================================================
// 2. VARIABEL BUFFER AUDIO & VAD
// =======================================================
#define SAMPLE_RATE 16000
int16_t audioBuffer[SAMPLE_RATE];
int bufferIndex = 0;
uint32_t last_id = 0;

// AMBANG BATAS VAD (Kalibrasi angka ini nanti!)
// Nilai ini menentukan sekeras apa suara untuk memicu rekaman
const int AMBANG_BATAS_VOLUME = 300; 

enum StatusSistem { MENDENGAR, MEREKAM };
StatusSistem statusSaatIni = MENDENGAR;

// Fungsi ringkas untuk menghitung rata-rata amplitudo absolut (Volume)
int hitungVolumeRataRata(int16_t* data, int panjang) {
  int32_t total = 0;
  for (int i = 0; i < panjang; i++) {
    total += abs(data[i]);
  }
  return total / panjang;
}

// =======================================================
// 3. FUNGSI CALLBACK ESP-NOW (Sangat Cepat)
// =======================================================
void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
  if (len == sizeof(AudioPacket)) {
    AudioPacket paketMasuk;
    memcpy(&paketMasuk, incomingData, sizeof(AudioPacket));
    
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(audioQueue, &paketMasuk, &xHigherPriorityTaskWoken);
  }
}

// =======================================================
// 4. TASK FREERTOS: PERAKIT AUDIO & VAD
// =======================================================
void TaskRakitAudio(void *pvParameters) {
  AudioPacket paketDiterima;
  
  while(1) {
    if (xQueueReceive(audioQueue, &paketDiterima, portMAX_DELAY)) {
      
      // Hitung volume paket yang baru masuk
      int volumePaket = hitungVolumeRataRata(paketDiterima.data_suara, 120);

      // --- LOGIKA STATE MACHINE VAD ---
      if (statusSaatIni == MENDENGAR) {
        // Jika sedang sepi, cek apakah ada lonjakan suara
        if (volumePaket > AMBANG_BATAS_VOLUME) {
          Serial0.printf("\n[VAD Trigger!] Suara terdeteksi (Vol: %d). Mulai merekam 1 detik...\n", volumePaket);
          statusSaatIni = MEREKAM;
          bufferIndex = 0;
          last_id = paketDiterima.id_paket;
          
          // Masukkan paket pemicu ini agar awal kata tidak terpotong
          for(int i = 0; i < 120; i++) {
            audioBuffer[bufferIndex++] = paketDiterima.data_suara[i];
          }
        }
        // Jika masih sepi, abaikan paket (buffer tidak diisi)

      } else if (statusSaatIni == MEREKAM) {
        // --- FITUR KEAMANAN: Cek Paket Hilang ---
        if (paketDiterima.id_paket > last_id + 1) {
          int paketHilang = paketDiterima.id_paket - last_id - 1;
          int sampelHilang = paketHilang * 120;
          
          for(int i = 0; i < sampelHilang; i++) {
            if (bufferIndex < SAMPLE_RATE) audioBuffer[bufferIndex++] = 0; // Zero padding
          }
        }
        last_id = paketDiterima.id_paket;

        // --- RAKIT AUDIO ---
        for(int i = 0; i < 120; i++) {
          if (bufferIndex < SAMPLE_RATE) {
            audioBuffer[bufferIndex++] = paketDiterima.data_suara[i];
          }
        }

        // --- JIKA 1 DETIK SUDAH PENUH ---
        if (bufferIndex >= SAMPLE_RATE) {
          Serial0.println("[Sistem] 1 Detik Audio (16.000 Sampel) Terkumpul Utuh!");
          Serial0.println("-> Eksekusi MFCC dan CNN di sini...");
          
          // Nanti di sini: prosesModelAI(audioBuffer);
          
          // Kembali ke mode mendengarkan
          statusSaatIni = MENDENGAR; 
        }
      }
    }
  }
}

// =======================================================
// 5. SETUP & LOOP
// =======================================================
void setup() {
  Serial0.begin(115200);
  delay(2000); 

  audioQueue = xQueueCreate(100, sizeof(AudioPacket));

  xTaskCreatePinnedToCore(
    TaskRakitAudio, "TaskAudio", 16384, NULL, 1, NULL, 1
  );

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  WiFi.setChannel(1); 

  if (esp_now_init() != ESP_OK) {
    Serial0.println("Gagal inisialisasi ESP-NOW");
    return;
  }
  esp_now_register_recv_cb(OnDataRecv);
  
  Serial0.println("\n[RX] Sistem VAD Aktif. Menunggu perintah suara...");
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}