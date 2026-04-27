#include <esp_now.h>
#include <WiFi.h>

// 1. Struktur data (WAJIB SAMA PERSIS dengan Transmitter di S3 Zero)
typedef struct PesanData {
  int id;
  float suhu;
  bool status;
} PesanData;

PesanData dataMasuk;

// 2. Fungsi Callback Penerima versi 3.x
void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
  // Pastikan ukuran paket yang masuk sesuai dengan ukuran struct kita
  if (len == sizeof(dataMasuk)) {
    memcpy(&dataMasuk, incomingData, sizeof(dataMasuk));

    Serial0.println("--- Paket Diterima ---");
    Serial0.print("ID     : "); Serial0.println(dataMasuk.id);
    Serial0.print("Suhu   : "); Serial0.println(dataMasuk.suhu);
    Serial0.print("Status : "); Serial0.println(dataMasuk.status ? "Nyala" : "Mati");
    Serial0.println("----------------------");
  } else {
    Serial0.print("Ukuran paket tidak cocok. Diterima: ");
    Serial0.println(len);
  }
}

void setup() {
  Serial0.begin(115200);
  
  // Jeda agar Native USB sempat terbaca
  delay(2000); 

  Serial0.println("\nMemulai ESP32-S3 DevKit (Receiver) sebagai RX...");

  // 3. Set WiFi sebagai Station
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  // 4. Kunci di Channel 1 (SANGAT KRUSIAL! Harus sama dengan TX)
  WiFi.setChannel(1);

  // Tampilkan MAC Address untuk di-copy ke kode TX S3 Zero
  Serial0.print("MAC Address RX (Masukkan ke kode TX!): ");
  Serial0.println(WiFi.macAddress());

  // 5. Inisialisasi ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial0.println("Gagal inisialisasi ESP-NOW");
    return;
  }

  // 6. Daftarkan fungsi Callback
  esp_now_register_recv_cb(OnDataRecv);
  Serial0.println("RX Siap Menerima Data di Channel 1...");
}

void loop() {
  // Biarkan kosong, penerimaan otomatis dieksekusi di background lewat OnDataRecv
  delay(1000);
}