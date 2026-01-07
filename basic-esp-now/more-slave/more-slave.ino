#include <WiFi.h>
#include <esp_now.h>
#include "esp_wifi.h"

#define ESPNOW_CHANNEL 1
#define LED 48

/* ================= STRUCT ================= */

typedef struct struct_message {
  char  a[32];
  int   b;
  float c;
  char  d[32];
  bool  e;
} struct_message;

struct_message inData;

/* ================= CALLBACK ================= */

void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
  Serial.print("📥 From: ");
  for (int i = 0; i < 6; i++) {
    Serial.printf("%02X", mac[i]);
    if (i < 5) Serial.print(":");
  }
  Serial.println();

  memcpy(&inData, incomingData, sizeof(inData));

  Serial.printf(
    "A=%s | B=%d | C=%.2f | D=%s | E=%d\n\n",
    inData.a,
    inData.b,
    inData.c,
    inData.d,
    inData.e
  );

  digitalWrite(LED, inData.e ? HIGH : LOW);
}

/* ================= SETUP ================= */

void setup() {
  Serial.begin(115200);
  delay(1000);

  /* --- WiFi clean state --- */
  // esp_netif_init();
  // esp_event_loop_create_default();
  WiFi.mode(WIFI_STA);
  // WiFi.disconnect(true, true);
  delay(100);

  Serial.print("📡 S3 MAC: ");
  Serial.println(WiFi.macAddress());

  // esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

  /* --- ESP-NOW init --- */
  if (esp_now_init() != ESP_OK) {
    Serial.println("❌ ESP-NOW init failed");
    return;
  }

  esp_now_register_recv_cb(OnDataRecv);

  pinMode(LED, OUTPUT);
  Serial.println("✅ ESP32-S3 ready (ESPNOW)");
}

/* ================= LOOP ================= */

void loop() {
  // receiver pasif
}
