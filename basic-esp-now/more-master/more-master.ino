#include <WiFi.h>
#include <esp_now.h>
#include "esp_wifi.h"
#include "esp_pm.h"

/* ================= CONFIG ================= */

#define ESPNOW_CHANNEL 1
#define LED 8  // Built-in LED ESP32-C3

uint8_t MACaddress[] = { 0x30, 0xED, 0xA0, 0xBD, 0x6A, 0x44 };

/* ================= STRUCT ================= */

typedef struct struct_message {
  char  a[32];
  int   b;
  float c;
  char  d[32];
  bool  e;
} struct_message;

struct_message outData;

/* ================= CALLBACK ================= */

void OnDataSent(const uint8_t *mac, esp_now_send_status_t status) {
  Serial.print("Send Status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "SUCCESS" : "FAILED");
}

/* ================= SETUP ================= */

// void setup() {
//   Serial.begin(115200);
//   delay(1000);

//   /* --- WiFi clean state (WAJIB di C3) --- */
//   // esp_netif_init();
//   // esp_event_loop_create_default();
//   WiFi.mode(WIFI_STA);
//   // WiFi.disconnect(true, true);
//   delay(100);

//   esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

//   /* --- Init ESP-NOW --- */
//   if (esp_now_init() != ESP_OK) {
//     Serial.println("❌ ESP-NOW init failed");
//     return;
//   }

//   esp_now_register_send_cb(OnDataSent);

//   /* --- Register Peer (HARUS zeroed) --- */
//   esp_now_peer_info_t peerInfo = {};
//   memcpy(peerInfo.peer_addr, MACaddress, 6);
//   peerInfo.channel = ESPNOW_CHANNEL;
//   peerInfo.encrypt = false;
//   peerInfo.ifidx = WIFI_IF_STA;

//   if (esp_now_add_peer(&peerInfo) != ESP_OK) {
//     Serial.println("❌ Failed to add peer");
//     return;
//   }

//   pinMode(LED, OUTPUT);
//   Serial.println("✅ ESP-NOW ready (ESP32-C3)");
// }

void setup() {
  Serial.begin(115200);
  delay(1000);

  WiFi.mode(WIFI_STA);
  delay(100);

  esp_wifi_set_ps(WIFI_PS_NONE);   // 🔥 WIFI FULL POWER
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    Serial.println("❌ ESP-NOW init failed");
    return;
  }

  esp_now_register_send_cb(OnDataSent);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, MACaddress, 6);
  peerInfo.channel = ESPNOW_CHANNEL;
  peerInfo.encrypt = false;
  peerInfo.ifidx = WIFI_IF_STA;

  esp_now_add_peer(&peerInfo);

  pinMode(LED, OUTPUT);
  Serial.println("✅ ESP-NOW READY — NO SLEEP, FULL SEND 🔥");
}

/* ================= LOOP ================= */

void loop() {
  static bool ledState = false;

  strcpy(outData.a, "THIS IS A CHAR");
  strcpy(outData.d, "Hello");
  outData.b = random(1, 20);
  outData.c = 1.23;
  outData.e = ledState;

  digitalWrite(LED, ledState);
  ledState = !ledState;

  Serial.println("📤 Sending data...");

  esp_err_t result = esp_now_send(
    MACaddress,
    (uint8_t *)&outData,
    sizeof(outData)
  );

  if (result != ESP_OK) {
    Serial.printf("❌ Send error: %d\n", result);
  }

  delay(3000);
}
