#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>

// 1. MAC Address ESP32-S3 DevKit (Receiver di kursi roda)
uint8_t alamatReceiver[] = {0x30, 0xED, 0xA0, 0xBD, 0x6A, 0x44};

// 2. Struktur data (SAMA PERSIS dengan Receiver)
typedef struct PesanData {
  int id;
  float suhu;
  bool status;
} PesanData;

PesanData dataKirim;
esp_now_peer_info_t peerInfo;

// 3. Fungsi Callback (Format ESP32 Core 3.x)
void OnDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  Serial.print("\nStatus Pengiriman: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Sukses Terkirim" : "Gagal Terkirim");
}

void setup() {
  Serial.begin(115200);
  
  // Wajib ada jeda untuk ESP32-S3 karena native USB butuh waktu untuk terbaca di PC
  delay(2000); 
  Serial.println("\nMemulai ESP32-S3 Zero (Kalung) sebagai TX...");

  // 4. Set WiFi ESP32-S3 sebagai Station di Channel 1
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  WiFi.setChannel(1);

  // Matikan fitur Power Saving agar transmisi maksimal dan tidak delay
  esp_wifi_set_ps(WIFI_PS_NONE);

  // 5. Inisialisasi ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Gagal inisialisasi ESP-NOW");
    return;
  }

  esp_now_register_send_cb(OnDataSent);

  // 6. Konfigurasi Peer (Receiver)
  memset(&peerInfo, 0, sizeof(peerInfo)); 
  memcpy(peerInfo.peer_addr, alamatReceiver, 6);
  peerInfo.channel = 1;      
  peerInfo.ifidx = WIFI_IF_STA;
  peerInfo.encrypt = false;  

  if (esp_now_add_peer(&peerInfo) != ESP_OK){
    Serial.println("Gagal menambahkan Peer (Receiver)");
    return;
  }
}

void loop() {
  // 7. Isi data simulasi
  dataKirim.id = random(1, 100);       
  dataKirim.suhu = random(200, 400) / 10.0; 
  dataKirim.status = !dataKirim.status; 

  // 8. Tembakkan Data
  esp_err_t hasil = esp_now_send(alamatReceiver, (uint8_t *) &dataKirim, sizeof(dataKirim));
   
  if (hasil == ESP_OK) {
    Serial.println("Paket diserahkan ke sistem untuk dikirim...");
  } else {
    Serial.println("Gagal menyerahkan paket ke sistem");
  }

  delay(2000);
}