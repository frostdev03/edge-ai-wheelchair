#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <WiFi.h>
#include <esp_now.h>

// ===================== PIN CONFIGURATION =====================
// Motor Kanan (Berdasarkan skematik motor-testing.ino)
const int RPWM_R = 10; // [cite: 1]
const int LPWM_R = 11; // [cite: 1]

// Motor Kiri (Berdasarkan skematik motor-testing.ino)
const int RPWM_L = 12; // [cite: 2]
const int LPWM_L = 13; // [cite: 2]

// Sensor IR TC-RT5000 (Berdasarkan sensors.ino)
#define IR1 14 // 
#define IR2 15 // 

// OLED I2C Pins (Sudah dipindah ke Pin 1 & 2 agar tidak bentrok)
#define I2C_SDA 1 
#define I2C_SCL 2 
#define SCREEN_WIDTH 128 // [cite: 23]
#define SCREEN_HEIGHT 64 // [cite: 23]

// Sensor Ultrasonik HC-SR04 (Kembali ke Pin 4 & 5 bawaan sensors.ino)
#define TRIG1 4  // 
#define ECHO1 5  // 
#define TRIG2 6  // 
#define ECHO2 7  // 
#define TRIG3 8  // 
#define ECHO3 9  // 

// ===================== GLOBAL VARIABLES & ENUMS =====================
const int MIN_SPEED = 50; // [cite: 3]

// Definisi ID Perintah (Wajib sama persis dengan sisi Transmitter/S3 Zero)
enum CommandID {
    CMD_DIAM   = 0,
    CMD_MAJU   = 1,
    CMD_MUNDUR = 2,
    CMD_KIRI   = 3,
    CMD_KANAN  = 4,
    CMD_STOP   = 5
};

// Struktur paket data ESP-NOW
typedef struct struct_message {
    uint8_t command;   
    uint32_t msg_id;   
} struct_message;

struct_message incomingData;

// Shared Resources antar-Task
volatile int global_ir1_value = 0;
volatile int global_ir2_value = 0;
volatile long global_dist1 = 0;
volatile long global_dist2 = 0;
volatile long global_dist3 = 0;

volatile int target_speed_left = 0;
volatile int target_speed_right = 0;
volatile uint32_t last_received_msg_id = 0;

// Deklarasi Layar OLED SH1106
Adafruit_SH1106G display = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1); // [cite: 23]

// FreeRTOS Task Handles
TaskHandle_t TaskSensorsOLEDHandle = NULL;
TaskHandle_t TaskMotorPIDHandle    = NULL;
TaskHandle_t TaskESPNowHandle      = NULL;

// ===================== HELPER FUNCTIONS =====================
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

long readUltrasonicCM(int trigPin, int echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2); // [cite: 11]
  digitalWrite(trigPin, HIGH); // [cite: 11]
  delayMicroseconds(10); // [cite: 11]
  digitalWrite(trigPin, LOW); // [cite: 11]

  long duration = pulseIn(echoPin, HIGH, 30000);  // [cite: 12]
  if (duration == 0) return -1; // [cite: 12, 13]
  return duration / 58; // [cite: 13]
}

// 🔥 CALLBACK ESP-NOW VERSI ARDUINO CORE 3.0+
void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
    // Menyalin data mentah yang masuk ke dalam struktur data incomingData
    memcpy(&incomingData, data, sizeof(incomingData));
    
    // Simpan ID pesan terakhir sebagai penanda ada data baru masuk
    last_received_msg_id = incomingData.msg_id;

    // Logika penerjemahan ID perintah ke target kecepatan motor
    switch (incomingData.command) {
        case CMD_MAJU:
            target_speed_left = MIN_SPEED;
            target_speed_right = MIN_SPEED;
            break;
            
        case CMD_MUNDUR:
            target_speed_left = -MIN_SPEED;
            target_speed_right = -MIN_SPEED;
            break;
            
        case CMD_KIRI:
            // Roda kiri mundur/diam, roda kanan maju agar berbelok ke kiri
            target_speed_left = -MIN_SPEED;
            target_speed_right = MIN_SPEED;
            break;
            
        case CMD_KANAN:
            // Roda kiri maju, roda kanan mundur/diam agar berbelok ke kanan
            target_speed_left = MIN_SPEED;
            target_speed_right = -MIN_SPEED;
            break;
            
        case CMD_STOP:
        case CMD_DIAM:
        default:
            target_speed_left = 0;
            target_speed_right = 0;
            break;
    }
}

// ===================== RTOS TASKS IMPLEMENTATION =====================

// 1. TASK SENSORS & OLED (Core 0)
void TaskSensorsOLED(void *pvParameters) {
  (void) pvParameters;

  Wire.begin(I2C_SDA, I2C_SCL);
  vTaskDelay(pdMS_TO_TICKS(500));

  if(!display.begin(0x3C, true)) {
    Serial0.println(F("SH1106 allocation failed"));
    for(;;); 
  }
  
  display.clearDisplay();
  display.setTextSize(1);      
  display.setTextColor(SH110X_WHITE);
  display.setCursor(0, 0);
  display.println("Receiver Ready...");
  display.display();

  for (;;) {
    global_ir1_value = digitalRead(IR1); // [cite: 18]
    global_ir2_value = digitalRead(IR2); // [cite: 18]

    global_dist1 = readUltrasonicCM(TRIG1, ECHO1);
    global_dist2 = readUltrasonicCM(TRIG2, ECHO2);
    global_dist3 = readUltrasonicCM(TRIG3, ECHO3);

    // Render data ke OLED
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    
    display.setCursor(0, 0);
    display.printf("IR1: %d | IR2: %d\n", global_ir1_value, global_ir2_value);
    
    display.setCursor(0, 12);
    display.printf("US1 (Kiri) : %ld cm\n", global_dist1);
    display.setCursor(0, 24);
    display.printf("US2 (Depan): %ld cm\n", global_dist2);
    display.setCursor(0, 36);
    display.printf("US3 (Kanan): %ld cm\n", global_dist3);
    
    display.setCursor(0, 52);
    display.printf("L: %d | R: %d (ID:%ld)\n", target_speed_left, target_speed_right, last_received_msg_id);
    
    display.display();

    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

// 2. TASK MOTOR & PID (Core 1)
void TaskMotorPID(void *pvParameters) {
  (void) pvParameters;

  for (;;) {
    /* [TEMPAT CODING PID BESOK SIANG] */
    
    // Kompensasi arah fisik: motor kiri diberi nilai normal (+), motor kanan dibalik (-) secara programmatic
    setMotorSpeed(target_speed_left, RPWM_L, LPWM_L); 
    setMotorSpeed(-target_speed_right, RPWM_R, LPWM_R); 

    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

// 3. TASK ESP-NOW STATUS LOG (Core 0)
void TaskESPNow(void *pvParameters) {
  (void) pvParameters;

  for (;;) {
    // Menampilkan log monitoring ke serial komputer setiap 500ms
    Serial0.printf("[MONITOR] Target Speed -> L: %d | R: %d | Last Msg ID: %ld\n", 
                  target_speed_left, target_speed_right, last_received_msg_id);
    vTaskDelay(pdMS_TO_TICKS(500)); 
  }
}

// ===================== ARDUINO SETUP & LOOP =====================
void setup() {
  Serial0.begin(115200);
  Serial0.println("Memulai Setup Receiver Kursi Roda...");

  // Inisialisasi Hardware Pin
  pinMode(RPWM_R, OUTPUT); pinMode(LPWM_R, OUTPUT); // [cite: 4, 5]
  pinMode(RPWM_L, OUTPUT); pinMode(LPWM_L, OUTPUT); // [cite: 4, 5]
  pinMode(IR1, INPUT); pinMode(IR2, INPUT); // [cite: 15]
  pinMode(TRIG1, OUTPUT); pinMode(ECHO1, INPUT); // [cite: 14]
  pinMode(TRIG2, OUTPUT); pinMode(ECHO2, INPUT); // [cite: 14]
  pinMode(TRIG3, OUTPUT); pinMode(ECHO3, INPUT); // [cite: 14, 15]

  // Keamanan Utama: Matikan semua motor saat menyala
  setMotorSpeed(0, RPWM_R, LPWM_R);
  setMotorSpeed(0, RPWM_L, LPWM_L);

  // Inisialisasi Wi-Fi Mode Station (Wajib untuk ESP-NOW)
  WiFi.mode(WIFI_STA);
  Serial0.print("[INFO] MAC Address Receiver Ini: ");
  Serial0.println(WiFi.macAddress());

  // Inisialisasi Protokol ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial0.println("[ERR] Gagal menginisialisasi ESP-NOW");
    return;
  }

  // Daftarkan Fungsi Callback Penerima Data
  esp_now_register_recv_cb(esp_now_recv_cb_t(OnDataRecv));

  // Pembuatan Jalur Distribusi Task FreeRTOS
  xTaskCreatePinnedToCore(TaskSensorsOLED, "SensorsOLEDTask", 4096, NULL, 2, &TaskSensorsOLEDHandle, 0);
  xTaskCreatePinnedToCore(TaskMotorPID, "MotorPIDTask", 4096, NULL, 3, &TaskMotorPIDHandle, 1);
  xTaskCreatePinnedToCore(TaskESPNow, "ESPNowTask", 4096, NULL, 2, &TaskESPNowHandle, 0);

  Serial0.println("Seluruh Arsitektur Receiver Sukses Dikonfigurasi.");
}

void loop() {
  vTaskDelete(NULL); 
}
