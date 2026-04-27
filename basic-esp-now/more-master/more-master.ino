#include <esp_now.h>
#include <WiFi.h>

// --- GANTI DENGAN MAC ADDRESS ESP32-S3 (RECEIVER) ---
// Contoh format: {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC}
uint8_t receiverAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}; 

// Membuat struktur data yang akan dikirim
typedef struct DataPesan {
  int id_pesan;
  float suhu;
  bool status_motor;
} DataPesan;

// Membuat variabel dari struktur di atas
DataPesan dataKirim;

// Objek untuk menyimpan informasi *peer* (perangkat tujuan)
esp_now_peer_info_t peerInfo;

// --- FUNGSI CALLBACK: Dipanggil otomatis setiap kali selesai mengirim ---
// Fungsi ini berguna untuk mengecek apakah data berhasil sampai ke S3 atau gagal
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("\r\nStatus Pengiriman Terakhir:\t");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Berhasil Terkirim" : "Gagal Terkirim");
}

void setup() {
  Serial.begin(115200);

  // 1. Set mode WiFi ke Station (Wajib untuk ESP-NOW)
  WiFi.mode(WIFI_STA);

  // 2. Inisialisasi protokol ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Gagal menginisialisasi ESP-NOW");
    return;
  }

  // 3. Mendaftarkan fungsi Callback pengiriman
  esp_now_register_send_cb(OnDataSent);

  // 4. Mendaftarkan Receiver (ESP32-S3) sebagai Peer
  // Salin MAC address tujuan ke variabel peerInfo
  memcpy(peerInfo.peer_addr, receiverAddress, 6);
  peerInfo.channel = 0;      // Gunakan channel WiFi saat ini
  peerInfo.encrypt = false;  // Tidak menggunakan enkripsi (untuk dasar)

  // Tambahkan peer dan cek apakah berhasil
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Gagal menambahkan peer");
    return;
  }
}

void loop() {
  // Isi data buatan untuk diuji
  dataKirim.id_pesan = random(1, 100);
  dataKirim.suhu = 28.5;
  dataKirim.status_motor = true;

  // 5. Mengirim Data via ESP-NOW
  // Parameter: (MAC address tujuan, alamat data, ukuran data)
  esp_err_t hasil = esp_now_send(receiverAddress, (uint8_t *) &dataKirim, sizeof(dataKirim));
   
  if (hasil == ESP_OK) {
    Serial.println("Data berhasil dikirim ke antrean ESP-NOW");
  } else {
    Serial.println("Gagal mengirim data");
  }

  // Jeda 2 detik sebelum mengirim data berikutnya
  delay(2000);
}