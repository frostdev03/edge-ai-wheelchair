#include <esp_now.h>
#include <WiFi.h>

// Struktur data harus SAMA PERSIS dengan Transmitter
typedef struct DataPesan {
  int id_pesan;
  float suhu;
  bool status_motor;
} DataPesan;

// Variabel untuk menampung data yang masuk
DataPesan dataTerima;

// --- FUNGSI CALLBACK: Dipanggil otomatis saat ada data masuk ---
void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  // Pindahkan data mentah (incomingData) ke dalam format struktur (dataTerima)
  memcpy(&dataTerima, incomingData, sizeof(dataTerima));
  
  // Tampilkan alamat pengirim
  Serial.print("Data diterima dari MAC: ");
  for (int i = 0; i < 6; i++) {
    Serial.printf("%02X", mac[i]);
    if (i < 5) Serial.print(":");
  }
  Serial.println();

  // Tampilkan ukuran dan isi data
  Serial.print("Ukuran Data (Bytes): ");
  Serial.println(len);
  Serial.print("ID Pesan: ");
  Serial.println(dataTerima.id_pesan);
  Serial.print("Suhu: ");
  Serial.println(dataTerima.suhu);
  Serial.print("Status Motor: ");
  Serial.println(dataTerima.status_motor ? "ON" : "OFF");
  Serial.println("-------------------------");
}

void setup() {
  Serial.begin(115200);

  // 1. Set mode WiFi ke Station
  WiFi.mode(WIFI_STA);

  // Cetak MAC Address dari ESP32-S3 ini (PENTING!)
  // Salin alamat ini dan tempel di baris ke-6 pada kode Transmitter
  Serial.print("MAC Address Receiver (S3) ini: ");
  Serial.println(WiFi.macAddress());

  // 2. Inisialisasi protokol ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Gagal menginisialisasi ESP-NOW");
    return;
  }

  // 3. Mendaftarkan fungsi Callback penerimaan
  esp_now_register_recv_cb(OnDataRecv);
}

void loop() {
  // Loop bisa dibiarkan kosong
  // ESP-NOW berjalan di background dan akan otomatis memicu OnDataRecv saat ada data
  delay(1000); 
}