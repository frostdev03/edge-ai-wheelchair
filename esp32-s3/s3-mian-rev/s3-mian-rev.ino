#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <esp_now.h>
#include "esp_wifi.h"

// ===== KONFIGURASI PIN HARDWARE =====
const int RPWM_R = 12; // Motor Kanan
const int LPWM_R = 13;
const int RPWM_L = 10; // Motor Kiri
const int LPWM_L = 11;

#define I2C_SDA 1     
#define I2C_SCL 2
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

#define TRIG_DEPAN_KANAN 4   // Ultrasonik Depan Kanan
#define ECHO_DEPAN_KANAN 5
#define TRIG_DEPAN_KIRI  6   // Ultrasonik Depan Kiri
#define ECHO_DEPAN_KIRI  7
#define TRIG_BKNG        8   // Ultrasonik Belakang
#define ECHO_BKNG        9

SemaphoreHandle_t i2cMutex;

// ===== ENUMERASI PERINTAH =====
enum CommandID { CMD_DIAM = 0, CMD_MAJU = 1, CMD_MUNDUR = 2, CMD_KIRI = 3, CMD_KANAN = 4, CMD_STOP = 5 };

typedef struct struct_message {
  uint8_t command;
  uint32_t msg_id;
} __attribute__((packed)) struct_message;

struct_message incomingData;
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ===== VARIABEL GLOBAL (SHARED RESOURCES) =====
volatile long dist_depan_kanan = 0, dist_depan_kiri = 0, dist_back = 0;
volatile bool safety_front_active = false;  
volatile bool safety_back_active  = false;  
volatile int current_command = CMD_DIAM;

// Nilai PWM yang sedang aktif dieksekusi ke motor (untuk tampilan OLED)
volatile int pwm_output_L_final = 0;
volatile int pwm_output_R_final = 0;

// ===== FUNGSI PENGGERAK MOTOR =====
void setMotorSpeed(int speed, int rpwmPin, int lpwmPin) {
  speed = constrain(speed, -255, 255);
  if (speed >= 0) {
    analogWrite(rpwmPin, speed);
    analogWrite(lpwmPin, 0);
  } else {
    analogWrite(rpwmPin, 0);
    analogWrite(lpwmPin, -speed);
  }
}

// ===== FUNGSI BACA ULTRASONIK =====
long readUltrasonicCM(int trigPin, int echoPin) {
  digitalWrite(trigPin, LOW);  delayMicroseconds(5);
  digitalWrite(trigPin, HIGH); delayMicroseconds(12);
  digitalWrite(trigPin, LOW);
  long duration = pulseIn(echoPin, HIGH, 35000);
  if (duration == 0) return 400;
  return duration / 58;
}

// ===== FUNGSI CEK SISA RAM & FLASH =====
void printMemoryInfo() {
  Serial0.println("\n===== INFORMASI MEMORI MIKROKONTROLER =====");

  // ---- RAM (Heap) ----
  Serial0.println("--- RAM (Heap) ---");
  Serial0.printf("Total Heap       : %u bytes (%.2f KB)\n", ESP.getHeapSize(), ESP.getHeapSize() / 1024.0);
  Serial0.printf("Sisa Heap Bebas  : %u bytes (%.2f KB)\n", ESP.getFreeHeap(), ESP.getFreeHeap() / 1024.0);
  Serial0.printf("Heap Minimum Pernah Tercatat : %u bytes (%.2f KB)\n", ESP.getMinFreeHeap(), ESP.getMinFreeHeap() / 1024.0);
  Serial0.printf("Blok Alokasi Terbesar        : %u bytes (%.2f KB)\n", ESP.getMaxAllocHeap(), ESP.getMaxAllocHeap() / 1024.0);

  float persen_ram_sisa = (ESP.getFreeHeap() * 100.0) / ESP.getHeapSize();
  Serial0.printf("Persentase RAM Sisa          : %.2f %%\n", persen_ram_sisa);

  // ---- Flash ----
  Serial0.println("--- FLASH ---");
  Serial0.printf("Ukuran Sketch (Firmware) : %u bytes (%.2f KB)\n", ESP.getSketchSize(), ESP.getSketchSize() / 1024.0);
  Serial0.printf("Sisa Ruang Sketch Bebas  : %u bytes (%.2f KB)\n", ESP.getFreeSketchSpace(), ESP.getFreeSketchSpace() / 1024.0);

  float persen_flash_terpakai = (ESP.getSketchSize() * 100.0) / (ESP.getSketchSize() + ESP.getFreeSketchSpace());
  float persen_flash_sisa = 100.0 - persen_flash_terpakai;
  Serial0.printf("Persentase Flash Terpakai (partisi app) : %.2f %%\n", persen_flash_terpakai);
  Serial0.printf("Persentase Flash Sisa (partisi app)     : %.2f %%\n", persen_flash_sisa);

  Serial0.println("=============================================\n");
}

// ===== ESP-NOW RECEIVER CALLBACK =====
// Hanya bertugas menyimpan command yang masuk. Keputusan "boleh jalan atau
// tidak" karena safety sepenuhnya ditentukan di TaskMotorRamp & TaskSensors,
// supaya tidak ada dua sumber kebenaran untuk hal yang sama.
void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
  memcpy(&incomingData, data, sizeof(incomingData));
  current_command = incomingData.command;
}

// ===== FREE RTOS TASK IMPLEMENTATION =====

// 1. Sensor & Safety Emergency Stop (Core 0)
void TaskSensors(void *pvParameters) {
  for (;;) {
    dist_depan_kanan = readUltrasonicCM(TRIG_DEPAN_KANAN, ECHO_DEPAN_KANAN); vTaskDelay(pdMS_TO_TICKS(20));
    dist_depan_kiri  = readUltrasonicCM(TRIG_DEPAN_KIRI,  ECHO_DEPAN_KIRI);  vTaskDelay(pdMS_TO_TICKS(20));
    dist_back        = readUltrasonicCM(TRIG_BKNG,        ECHO_BKNG);        vTaskDelay(pdMS_TO_TICKS(20));

    // Depan: blokir maju jika ada objek <= 40cm di salah satu sensor depan
    bool front_blocked = (dist_depan_kanan > 0 && dist_depan_kanan <= 40) ||
                         (dist_depan_kiri  > 0 && dist_depan_kiri  <= 40);

    // Belakang: blokir mundur jika ada objek <= 40cm di belakang
    bool back_blocked = (dist_back > 0 && dist_back <= 40);

    safety_front_active = front_blocked;
    safety_back_active  = back_blocked;

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

// 2. Kontrol Motor Open-Loop dengan Ramping (Core 1)
// (Sebelumnya bernama TaskMotorPID, tapi sudah tidak pakai PID — di-rename
// biar tidak membingungkan. Logika PID lama disimpan sebagai referensi di
// akhir file, dikomentari total.)
void TaskMotorRamp(void *pvParameters) {
  (void) pvParameters;

  const int STEP_RAMP = 15; // Kenaikan/penurunan PWM per 40ms

  int current_pwm_L = 0;
  int current_pwm_R = 0;

  for (;;) {
    int target_pwm_L = 0;
    int target_pwm_R = 0;

    // 1. Tentukan Target PWM Berdasarkan Acuan: Maju = Kiri(-) dan Kanan(+)
    switch (current_command) {
      case CMD_MAJU:
        if (!safety_front_active) {
          target_pwm_L = -57; // Kiri negatif untuk maju
          target_pwm_R = 80;    // Kanan positif untuk maju
        }
        break;

      case CMD_MUNDUR:
        if (!safety_back_active) {
          target_pwm_L = -57; // Kiri negatif untuk maju
          target_pwm_R = -80;    // Kanan positif untuk maju
//          target_pwm_L = - 57;  // Kebalikan maju -> Kiri positif
//          target_pwm_R = -45;   // Kebalikan maju -> Kanan negatif
        }
        break;

      case CMD_KIRI:
        // Belok kiri: Roda kiri DIAM, Roda kanan MAJU (+)
        target_pwm_L = 0;
        target_pwm_R = 120;
        break;

      case CMD_KANAN:
        // Belok kanan: Roda kiri MAJU (-), Roda kanan DIAM
        target_pwm_L = -120;
        target_pwm_R = 0;
        break;

      case CMD_STOP:
      case CMD_DIAM:
      default:
        target_pwm_L = 0;
        target_pwm_R = 0;
        break;
    }

    // 2. Potong langsung ke 0 tanpa ramping (rem darurat safety ATAU perintah stop/diam)
    bool hard_stop = (current_command == CMD_STOP || current_command == CMD_DIAM) ||
                     (safety_front_active && current_command == CMD_MAJU) ||
                     (safety_back_active && current_command == CMD_MUNDUR);

    if (hard_stop) {
      current_pwm_L = 0;
      current_pwm_R = 0;
    } else {
      // 3. Proses ramping (akselerasi & deselerasi bertahap)
      if (current_pwm_L < target_pwm_L) current_pwm_L = min(current_pwm_L + STEP_RAMP, target_pwm_L);
      else if (current_pwm_L > target_pwm_L) current_pwm_L = max(current_pwm_L - STEP_RAMP, target_pwm_L);

      if (current_pwm_R < target_pwm_R) current_pwm_R = min(current_pwm_R + STEP_RAMP, target_pwm_R);
      else if (current_pwm_R > target_pwm_R) current_pwm_R = max(current_pwm_R - STEP_RAMP, target_pwm_R);
    }

    int out_L = constrain((int)current_pwm_L,-255, 255);
    int out_R = constrain((int)current_pwm_R, -255, 255);

    // Update variabel global untuk tampilan OLED
    pwm_output_L_final = abs(out_L);
    pwm_output_R_final = abs(out_R);

    // Eksekusi langsung ke fungsi penggerak masing-masing motor
    setMotorSpeed(out_L, RPWM_L, LPWM_L);
    setMotorSpeed(out_R, RPWM_R, LPWM_R);

    vTaskDelay(pdMS_TO_TICKS(40));
  }
}

// 3. Render Visual Data ke Layar OLED 0.96" (Core 0)
void TaskOLED(void *pvParameters) {
  if (xSemaphoreTake(i2cMutex, portMAX_DELAY)) {
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
      xSemaphoreGive(i2cMutex);
      vTaskDelete(NULL);
    }
    xSemaphoreGive(i2cMutex);
  }

  const int CETAK_MEMORI_TIAP_N_SIKLUS = 40; // 40 x 250ms = 10 detik
  int hitung_siklus = 0;

  for (;;) {
    // Cetak status memori berkala ke Serial (tidak mengganggu tampilan OLED),
    // supaya bisa dipantau trennya selama sistem berjalan lama (misal ada
    // kebocoran RAM/fragmentasi heap).
    hitung_siklus++;
    if (hitung_siklus >= CETAK_MEMORI_TIAP_N_SIKLUS) {
      hitung_siklus = 0;
      printMemoryInfo();
    }

    if (xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(50))) {
      display.clearDisplay();
      display.setTextSize(1);
      display.setTextColor(SSD1306_WHITE);

      display.setCursor(0, 0);
      display.print("CMD: ");
      if (safety_front_active && (current_command == CMD_MAJU)) display.println("BLOKIR DEPAN");
      else if (safety_back_active && (current_command == CMD_MUNDUR)) display.println("BLOKIR BLKG");
      else if (current_command == CMD_MAJU) display.println("MAJU");
      else if (current_command == CMD_MUNDUR) display.println("MUNDUR");
      else if (current_command == CMD_KIRI) display.println("BELOK KIRI");
      else if (current_command == CMD_KANAN) display.println("BELOK KANAN");
      else display.println("DIAM / STOP");

      display.setCursor(0, 16);
      display.printf("DR:%ldcm DL:%ldcm B:%ldcm\n", dist_depan_kanan, dist_depan_kiri, dist_back);

      display.setCursor(0, 34);
      display.printf("PWM_L: %d\n", pwm_output_L_final);
      display.printf("PWM_R: %d\n", pwm_output_R_final);

      display.setCursor(0, 54);
      if (safety_front_active) display.print("[BLOKIR DEPAN]");
      else if (safety_back_active) display.print("[BLOKIR BLKG ]");
      else display.print("[ JALUR AMAN  ]");

      display.display();
      xSemaphoreGive(i2cMutex);
    }
    vTaskDelay(pdMS_TO_TICKS(250));
  }
}

void setup() {
  Serial0.begin(115200);
  i2cMutex = xSemaphoreCreateMutex();
  Wire.begin(I2C_SDA, I2C_SCL, 100000);
  vTaskDelay(pdMS_TO_TICKS(100));

  pinMode(RPWM_R, OUTPUT); pinMode(LPWM_R, OUTPUT);
  pinMode(RPWM_L, OUTPUT); pinMode(LPWM_L, OUTPUT);

  pinMode(TRIG_DEPAN_KANAN, OUTPUT); pinMode(ECHO_DEPAN_KANAN, INPUT);
  pinMode(TRIG_DEPAN_KIRI,  OUTPUT); pinMode(ECHO_DEPAN_KIRI,  INPUT);
  pinMode(TRIG_BKNG,        OUTPUT); pinMode(ECHO_BKNG,        INPUT);

  setMotorSpeed(0, RPWM_R, LPWM_R);
  setMotorSpeed(0, RPWM_L, LPWM_L);

  WiFi.mode(WIFI_STA);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  if (esp_now_init() == ESP_OK) {
    esp_now_register_recv_cb(esp_now_recv_cb_t(OnDataRecv));
  }

  xTaskCreatePinnedToCore(TaskSensors,   "SensorsTask",  3072, NULL, 3, NULL, 0);
  xTaskCreatePinnedToCore(TaskOLED,      "OLEDTask",     3072, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(TaskMotorRamp, "MotorRampTask", 4096, NULL, 4, NULL, 1);

  vTaskDelay(pdMS_TO_TICKS(200)); // beri jeda sebentar biar semua task sempat start
  Serial0.println("\n[SETUP SELESAI] Kondisi memori setelah semua modul aktif:");
  printMemoryInfo();
}

void loop() {
  vTaskDelete(NULL);
}
