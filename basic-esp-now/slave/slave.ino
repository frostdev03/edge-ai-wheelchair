#include <esp_now.h>
#include <WiFi.h>

typedef struct struct_message {
    // char a[32];
  int b;
} struct_message;

struct_message myData;

void OnDataRecv(const esp_now_recv_info *info, const uint8_t *incomingData, int len) {
  memcpy(&myData, incomingData, sizeof(myData));
  Serial0.printf("Received: %d\n", myData.b);
}

void setup() {
  Serial0.begin(115200);
  delay(2000);

  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) {
    Serial0.println("ESP-NOW init failed");
    return;
  }

  esp_now_register_recv_cb(OnDataRecv);

  Serial0.print("Receiver MAC: ");
  Serial0.println(WiFi.macAddress());
  Serial0.println("Ready");
}

void loop() {}