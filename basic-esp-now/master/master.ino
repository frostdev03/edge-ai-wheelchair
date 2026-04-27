#include <esp_now.h>
#include <WiFi.h>
#include "esp_wifi.h"

uint8_t broadcastAddress[] = {0x30, 0xED, 0xA0, 0xBD, 0x6A, 0x44}; // S3 MAC here

typedef struct struct_message {
  // char a[32];
  int b;
  // float c;
  // bool d;
} struct_message;

struct_message myData;
esp_now_peer_info_t peerInfo;

// ✅ Volatile flag instead of relying on callback
volatile bool sendDone = false;
volatile bool sendSuccess = false;

void OnDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  sendSuccess = (status == ESP_NOW_SEND_SUCCESS);
  sendDone = true;
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(200);

  // ✅ Explicitly set country for Indonesia (channels 1-13)
  wifi_country_t country = {
    .cc = "ID",
    .schan = 1,
    .nchan = 13,
    .policy = WIFI_COUNTRY_POLICY_MANUAL
  };
  esp_wifi_set_country(&country);

  esp_wifi_set_ps(WIFI_PS_NONE);
  esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
  esp_wifi_set_max_tx_power(84);

  Serial.print("C3 Sender MAC: ");
  Serial.println(WiFi.macAddress());

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }

  esp_now_register_send_cb(OnDataSent);

  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Add peer failed");
    return;
  }

  Serial.println("Sender ready");
}

void loop() {
  // strcpy(myData.a, "cek cek cek");
  myData.b = random(1, 20);
  // myData.c = 1.2;
  // myData.d = false;

  sendDone = false;
  esp_err_t result = esp_now_send(broadcastAddress, (uint8_t *)&myData, sizeof(myData));

  if (result != ESP_OK) {
    Serial.println("Send error — packet not queued");
    delay(2000);
    return;
  }

  // ✅ Wait for callback confirmation with timeout
  unsigned long start = millis();
  while (!sendDone && millis() - start < 1000) {
    delay(10);
  }

  if (!sendDone) {
    Serial.println("Timeout — no ACK callback received");
  } else {
    Serial.println(sendSuccess ? "Delivery Success" : "Delivery Fail");
  }

  delay(2000);
}