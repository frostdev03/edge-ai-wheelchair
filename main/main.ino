#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include "esp_wifi.h"

/* ================= CONFIG ================= */

// #define AUDIO_CHUNK_MAX 256
#define AUDIO_CHUNK_SIZE 120
#define QUEUE_LENGTH 8

/* ================= AUDIO STRUCT (DISABLED) ================= */

typedef struct {
  int16_t samples[AUDIO_CHUNK_SIZE];
  uint16_t count;
} audio_packet_t;

/* ================= QUEUE (AUDIO - DISABLED) ================= */

// QueueHandle_t audioQueue;

/* ================= ESP-NOW RX CALLBACK ================= */

void onDataRecv(const uint8_t *mac_addr,
                const uint8_t *data,
                int len) {

  Serial.print("📥 RX from ");
  for (int i = 0; i < 6; i++) {
    Serial.printf("%02X", mac_addr[i]);
    if (i < 5) Serial.print(":");
  }
  Serial.print(" | len=");
  Serial.println(len);

  /* ---------- STRING MODE (ACTIVE) ---------- */
  Serial.print("📨 STRING: ");
  Serial.println((char *)data);

  /* ---------- AUDIO MODE (DISABLED) ---------- */
  /*
  if (len == sizeof(audio_packet_t)) {
    audio_packet_t packet;
    memcpy(&packet, data, sizeof(packet));

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(audioQueue, &packet, &xHigherPriorityTaskWoken);

    if (xHigherPriorityTaskWoken) {
      portYIELD_FROM_ISR();
    }
  }
  */
}

/* ================= RTOS TASK (DISABLED) ================= */

// void audioRxTask(void *param) {
//   audio_packet_t packet;

//   while (true) {
//     if (xQueueReceive(audioQueue, &packet, portMAX_DELAY) == pdTRUE) {
//       Serial.print("[RX] Samples: ");
//       Serial.print(packet.count);
//       Serial.print(" | First sample: ");
//       Serial.println(packet.samples[0]);
//     }
//   }
// }

/* ================= SETUP ================= */

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("🔥 ESP32-S3 ESPNOW SLAVE (STRING MODE)");

  /* --- WiFi --- */
  WiFi.mode(WIFI_STA);
  // esp_wifi_set_ps(WIFI_PS_NONE);
  delay(100);
  // WiFi.disconnect(true, true);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  delay(100);

  /* --- ESP-NOW --- */
  if (esp_now_init() != ESP_OK) {
    Serial.println("❌ ESP-NOW init failed");
    return;
  }

  esp_now_register_recv_cb(onDataRecv);

  /* --- AUDIO QUEUE (DISABLED) --- */
  // audioQueue = xQueueCreate(QUEUE_LENGTH, sizeof(audio_packet_t));

  Serial.println("✅ SLAVE READY — LISTENING");
  Serial.print("📡 MAC: ");
  Serial.println(WiFi.macAddress());
}

/* ================= LOOP ================= */

void loop() {
  vTaskDelay(portMAX_DELAY);
}
