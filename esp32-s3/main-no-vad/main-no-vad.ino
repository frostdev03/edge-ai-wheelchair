#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>

// 1. Struktur data WAJIB SAMA PERSIS dengan Transmitter
typedef struct __attribute__((packed)) AudioPacket {
  uint32_t id_paket;
  int16_t data_suara[120];
} AudioPacket;

// 2. Deklarasi Queue FreeRTOS
QueueHandle_t audioQueue;

// 3. Buffer Utama untuk mengumpulkan 1 Detik Audio (16.000 sampel)
#define SAMPLE_RATE 16000
int16_t audioBuffer[SAMPLE_RATE];
int bufferIndex = 0;
uint32_t last_id = 0;

// =======================================================
// FUNGSI CALLBACK ESP-NOW (Area Cepat / Interrupt)
// =======================================================
void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
  if (len == sizeof(AudioPacket)) {
    AudioPacket paketMasuk;
    memcpy(&paketMasuk, incomingData, sizeof(AudioPacket));
    
    // Lempar paket ke Queue secepat mungkin tanpa memblokir sistem
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(audioQueue, &paketMasuk, &xHigherPriorityTaskWoken);
  }
}

// =======================================================
// TASK FREERTOS: PERAKIT AUDIO (Area Background)
// =======================================================
void TaskRakitAudio(void *pvParameters) {
  AudioPacket paketDiterima;
  
  while(1) {
    // Menunggu paket keluar dari Queue (Tunggu maksimal portMAX_DELAY)
    if (xQueueReceive(audioQueue, &paketDiterima, portMAX_DELAY)) {
      
      // -- FITUR KEAMANAN: Cek Paket Hilang --
      // Jika ID paket melompat, berarti ada paket yang hilang di udara
      if (last_id != 0 && paketDiterima.id_paket > last_id + 1) {
        int paketHilang = paketDiterima.id_paket - last_id - 1;
        Serial0.printf("Terdeteksi: %d paket hilang (diisi dengan keheningan)\n", paketHilang);
        
        // Zero Padding: Isi buffer dengan 0 (hening) sebanyak paket yang hilang
        // untuk menjaga keutuhan timeline 1 detik
        int sampelHilang = paketHilang * 120;
        for(int i = 0; i < sampelHilang; i++) {
          if (bufferIndex < SAMPLE_RATE) {
            audioBuffer[bufferIndex] = 0;
            bufferIndex++;
          }
        }
      }
      last_id = paketDiterima.id_paket;

      // -- RAKIT AUDIO --
      // Masukkan 120 sampel suara ke dalam buffer raksasa
      for(int i = 0; i < 120; i++) {
        if (bufferIndex < SAMPLE_RATE) {
          audioBuffer[bufferIndex] = paketDiterima.data_suara[i];
          bufferIndex++;
        }
      }

      // -- JIKA 1 DETIK SUARA SUDAH TERKUMPUL --
      if (bufferIndex >= SAMPLE_RATE) {
        Serial0.println("\n[Sistem] 1 Detik Audio Terkumpul! (16.000 Sampel)");
        Serial0.println("-> Siap dikirim ke fungsi ekstraksi MFCC & CNN...");
        
        // DI SINI NANTI KAMU MENARUH PEMANGGILAN FUNGSI AI
        // contoh: prosesModelAI(audioBuffer);
        
        // Kosongkan buffer untuk merekam 1 detik berikutnya
        bufferIndex = 0; 
      }
    }
  }
}

// =======================================================
// SETUP & LOOP
// =======================================================
void setup() {
  Serial0.begin(115200);
  delay(2000); 

  // Buat Queue yang bisa menampung maksimal 100 antrean paket
  audioQueue = xQueueCreate(100, sizeof(AudioPacket));

  // Buat Task FreeRTOS dan lekatkan di Core 1
  xTaskCreatePinnedToCore(
    TaskRakitAudio,   // Fungsi yang dijalankan
    "TaskAudio",      // Nama task
    16384,            // Ukuran Stack (Alokasi memori diperbesar untuk array)
    NULL,             // Parameter
    1,                // Prioritas task
    NULL,             // Task Handle
    1                 // Dijalankan di Core 1 (Core 0 biasanya untuk WiFi/Radio)
  );

  // Setup WiFi & ESP-NOW di Channel 1
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  WiFi.setChannel(1); 

  if (esp_now_init() != ESP_OK) {
    Serial0.println("Gagal inisialisasi ESP-NOW");
    return;
  }
  esp_now_register_recv_cb(OnDataRecv);
  
  Serial0.println("\n[RX] Sistem Kursi Roda Siap Menerima Data...");
}

void loop() {
  // Loop dibiarkan kosong
  // FreeRTOS sudah mengambil alih seluruh kendali operasional
  vTaskDelay(pdMS_TO_TICKS(1000));
}