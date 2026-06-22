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

#define IR_L 14       // TCRT5000 Kiri (Sesuai klarifikasi)
#define IR_R 15       // TCRT5000 Kanan (Sesuai klarifikasi)

#define I2C_SDA 1     // Jalur OLED 0.96"
#define I2C_SCL 2
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

#define TRIG_RANYA 4  // Ultrasonik belakang
#define ECHO_RANYA 5
#define TRIG_KIRI  6  // Ultrasonik kanan
#define ECHO_KIRI  7
#define TRIG_BKNG  9  // Ultrasonik kiri
#define ECHO_BKNG  8

SemaphoreHandle_t i2cMutex;

// ===== VARIABEL PENGUJI RESPON TRANSIEN =====
bool testing_transient = false;
unsigned long start_test_time = 0;
float peak_speed_L = 0.0;
float peak_speed_R = 0.0;

// Parameter Hasil untuk Tabel Buku TA
float tr_L = 0.0, tr_R = 0.0; 
float mp_L = 0.0, mp_R = 0.0; 
float ts_L = 0.0, ts_R = 0.0; 
float ess_L = 0.0, ess_R = 0.0; 

// ===== PARAMETER FISIK KURSI RODA AKTUAL =====
const float WHEEL_DIAMETER = 0.60;     // Diameter roda aktual 60 cm
const int PULSES_PER_REV = 1;          // 1 Titik isolasi putih di roda
const float TARGET_SPEED_KMH = 4.0;    // Setpoint maksimal 4 km/jam

// ===== ENUMERASI PERINTAH =====
enum CommandID { CMD_DIAM = 0, CMD_MAJU = 1, CMD_MUNDUR = 2, CMD_KIRI = 3, CMD_KANAN = 4, CMD_STOP = 5 };
enum Direction { STOPPED, CW, CCW };

typedef struct struct_message {
  uint8_t command;
  uint32_t msg_id;
} __attribute__((packed)) struct_message;

struct_message incomingData;
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ===== VARIABEL GLOBAL (SHARED RESOURCES) =====
volatile long dist_right = 0, dist_left = 0, dist_back = 0;
volatile bool safety_stop_active = false;
volatile int current_command = CMD_DIAM;

volatile unsigned long pulse_count_L = 0;
volatile unsigned long pulse_count_R = 0;
volatile float speed_L_kmh = 0.0;
volatile float speed_R_kmh = 0.0;
Direction R_Rotation = STOPPED;
Direction L_Rotation = STOPPED;

float setpoint_L = 0.0;
float setpoint_R = 0.0;
float ramped_speed_L = 0.0;
float ramped_speed_R = 0.0;

// ===== PEKERJAAN INTERRUPT DENGAN PROTEKSI DEBOUNCING =====
void IRAM_ATTR countPulseL() {
  static unsigned long last_interrupt_time_L = 0;
  unsigned long interrupt_time = millis();
  if (interrupt_time - last_interrupt_time_L > 150) {
    pulse_count_L++;
    last_interrupt_time_L = interrupt_time;
  }
}

void IRAM_ATTR countPulseR() {
  static unsigned long last_interrupt_time_R = 0;
  unsigned long interrupt_time = millis();
  if (interrupt_time - last_interrupt_time_R > 150) {
    pulse_count_R++;
    last_interrupt_time_R = interrupt_time;
  }
}

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

// ===== ESP-NOW RECEIVER CALLBACK =====
void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
  memcpy(&incomingData, data, sizeof(incomingData));
  current_command = incomingData.command;

  if (safety_stop_active) return;

  switch (current_command) {
    case CMD_MAJU:
      setpoint_L = TARGET_SPEED_KMH; setpoint_R = TARGET_SPEED_KMH;
      break;
    case CMD_MUNDUR:
      setpoint_L = -TARGET_SPEED_KMH; setpoint_R = -TARGET_SPEED_KMH;
      break;
    case CMD_KIRI:  
      setpoint_L = -(TARGET_SPEED_KMH * 0.6); setpoint_R = (TARGET_SPEED_KMH * 0.6);
      break;
    case CMD_KANAN: 
      setpoint_L = (TARGET_SPEED_KMH * 0.6); setpoint_R = -(TARGET_SPEED_KMH * 0.6);
      break;
    case CMD_STOP:
    case CMD_DIAM:
    default:
      setpoint_L = 0; setpoint_R = 0;
      break;
  }
}

// ===== FREE RTOS TASK IMPLEMENTATION =====

// 1. Sensor & Safety Emergency Stop (Core 0)
void TaskSensors(void *pvParameters) {
  for (;;) {
    dist_right = readUltrasonicCM(TRIG_RANYA, ECHO_RANYA); vTaskDelay(pdMS_TO_TICKS(20));
    dist_left  = readUltrasonicCM(TRIG_KIRI, ECHO_KIRI);   vTaskDelay(pdMS_TO_TICKS(20));
    dist_back  = readUltrasonicCM(TRIG_BKNG, ECHO_BKNG);   vTaskDelay(pdMS_TO_TICKS(20));

    // Cek emergency stop jika ada sensor memotret jarak <= 30cm
    if ((dist_right > 0 && dist_right <= 30) || (dist_left > 0 && dist_left <= 30) || (dist_back > 0 && dist_back <= 30)) {
      if (!safety_stop_active) {
        safety_stop_active = true;
        setpoint_L = 0; setpoint_R = 0;
      }
    } else {
      safety_stop_active = false;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

// 2. Real-Time PID & Analisis Transien (Core 1)
void TaskMotorPID(void *pvParameters) {
  (void) pvParameters;
  unsigned long last_time = millis();

  static float Kp = 20, Ki = 0, Kd = 0;
  static float integral_L = 0.0, integral_R = 0.0;
  static float last_err_L = 0.0, last_err_R = 0.0;

  float KOMPENSASI_PWM_KANAN = 1.00; 
  float KOMPENSASI_PWM_KIRI  = 0.75; 

  for (;;) {
    unsigned long now = millis();
    float dt = (now - last_time) / 1000.0;
    if (dt <= 0.0) dt = 0.04;

    noInterrupts();
    unsigned long p_L = pulse_count_L; pulse_count_L = 0;
    unsigned long p_R = pulse_count_R; pulse_count_R = 0;
    interrupts();

    speed_L_kmh = ((float)p_L / PULSES_PER_REV) * 3.14159 * WHEEL_DIAMETER * (3.6 / dt);
    speed_R_kmh = ((float)p_R / PULSES_PER_REV) * 3.14159 * WHEEL_DIAMETER * (3.6 / dt);
    last_time = now;

    float target_steady = 4.0;
    float rise_threshold = target_steady * 0.9; 

    if (current_command == CMD_MAJU && !testing_transient && speed_L_kmh < 0.2 && speed_R_kmh < 0.2) {
      testing_transient = true;
      start_test_time = millis();
      peak_speed_L = 0.0; peak_speed_R = 0.0;
      tr_L = 0; tr_R = 0; mp_L = 0; mp_R = 0; ts_L = 0; ts_R = 0;
      integral_L = 0; integral_R = 0;
      Serial0.println("\n[⏱️ START] Pengujian Transien PID Dimulai...");
    }

    if (testing_transient) {
      unsigned long elapsed = millis() - start_test_time;
      float elapsed_sec = elapsed / 1000.0;

      if (speed_L_kmh >= rise_threshold && tr_L == 0) tr_L = elapsed_sec;
      if (speed_R_kmh >= rise_threshold && tr_R == 0) tr_R = elapsed_sec;

      if (speed_L_kmh > peak_speed_L) peak_speed_L = speed_L_kmh;
      if (speed_R_kmh > peak_speed_R) peak_speed_R = speed_R_kmh;

      if (peak_speed_L > target_steady) mp_L = ((peak_speed_L - target_steady) / target_steady) * 100.0;
      if (peak_speed_R > target_steady) mp_R = ((peak_speed_R - target_steady) / target_steady) * 100.0;

      if (speed_L_kmh >= 3.8 && speed_L_kmh <= 4.2) ts_L = elapsed_sec;
      if (speed_R_kmh >= 3.8 && speed_R_kmh <= 4.2) ts_R = elapsed_sec;

      ess_L = target_steady - speed_L_kmh;
      ess_R = target_steady - speed_R_kmh;

      if (elapsed_sec >= 10.0 || current_command == CMD_STOP || current_command == CMD_DIAM) {
        testing_transient = false;
        Serial0.println(F("\n======================================================="));
        Serial0.println(F("     REKAPITULASI DATA TRANSIEN UNTUK TABEL BUKU TA    "));
        Serial0.println(F("======================================================="));
        Serial0.printf("Konstanta Aktif -> Kp: %.1f | Ki: %.1f | Kd: %.1f\n\n", Kp, Ki, Kd);
        Serial0.println(F("[RODA KIRI]"));
        Serial0.printf("  - Rise Time (tr)       : %.2f detik\n", tr_L);
        Serial0.printf("  - Max Overshoot (Mp)   : %.1f %%\n", mp_L);
        Serial0.printf("  - Settling Time (ts)   : %.2f detik\n", ts_L);
        Serial0.printf("  - Steady-State Err(Ess): %.2f Km/h\n\n", abs(ess_L));
        Serial0.println(F("[RODA KANAN]"));
        Serial0.printf("  - Rise Time (tr)       : %.2f detik\n", tr_R);
        Serial0.printf("  - Max Overshoot (Mp)   : %.1f %%\n", mp_R);
        Serial0.printf("  - Settling Time (ts)   : %.2f detik\n", ts_R);
        Serial0.printf("  - Steady-State Err(Ess): %.2f Km/h\n", abs(ess_R));
        Serial0.println(F("=======================================================\n"));
      }
    }

    float max_change = 0.15;
    ramped_speed_L += constrain(setpoint_L - ramped_speed_L, -max_change, max_change);
    ramped_speed_R += constrain(setpoint_R - ramped_speed_R, -max_change, max_change);

    float err_L = abs(ramped_speed_L) - speed_L_kmh;
    float err_R = abs(ramped_speed_R) - speed_R_kmh;

    integral_L = constrain(integral_L + (err_L * dt), -50.0, 50.0);
    integral_R = constrain(integral_R + (err_R * dt), -50.0, 50.0);

    float derivative_L = (err_L - last_err_L) / dt;
    float derivative_R = (err_R - last_err_R) / dt;

    int pwm_base_L = (Kp * err_L) + (Ki * integral_L) + (Kd * derivative_L);
    int pwm_base_R = (Kp * err_R) + (Ki * integral_R) + (Kd * derivative_R);

    int pwm_output_L_final = (ramped_speed_L == 0) ? 0 : constrain((int)(pwm_base_L * KOMPENSASI_PWM_KIRI) + 80, 0, 180);
    int pwm_output_R_final = (ramped_speed_R == 0) ? 0 : constrain((int)(pwm_base_R * KOMPENSASI_PWM_KANAN) + 70, 0, 180);

    // Pembalikan tanda minus disesuaikan dengan posisi penukaran kabel fisikmu kemarin
    int out_L = (ramped_speed_L >= 0) ? -pwm_output_L_final : pwm_output_L_final;
    int out_R = (ramped_speed_R >= 0) ? pwm_output_R_final : -pwm_output_R_final;

    setMotorSpeed(out_L, RPWM_L, LPWM_L);
    setMotorSpeed(out_R, RPWM_R, LPWM_R);

    L_Rotation = (out_L < -5) ? CW : ((out_L > 5) ? CCW : STOPPED);
    R_Rotation = (out_R > 5) ? CW : ((out_R < -5) ? CCW : STOPPED);

    last_err_L = err_L; last_err_R = err_R;
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

  for (;;) {
    if (xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(50))) {
      display.clearDisplay();
      display.setTextSize(1);
      display.setTextColor(SSD1306_WHITE);

      display.setCursor(0, 0);
      display.print("CMD: ");
      if (safety_stop_active) display.println("EMERGENCY STOP");
      else if (current_command == CMD_MAJU) display.println("MAJU (4 km/h)");
      else if (current_command == CMD_MUNDUR) display.println("MUNDUR");
      else if (current_command == CMD_KIRI) display.println("BELOK KIRI");
      else if (current_command == CMD_KANAN) display.println("BELOK KANAN");
      else display.println("DIAM / STOP");

      // Menyesuaikan label tampilan oled sesuai posisi fisik klarifikasi baru
      display.setCursor(0, 16);
      display.printf("R: %ldcm | L: %ldcm | B: %ldcm\n", dist_right, dist_left, dist_back);

      display.setCursor(0, 34);
      display.printf("V_L: %.1f km/h (%s)\n", speed_L_kmh, (L_Rotation == CW) ? "CW" : (L_Rotation == CCW) ? "CCW" : "STP");
      display.printf("V_R: %.1f km/h (%s)\n", speed_R_kmh, (R_Rotation == CW) ? "CW" : (R_Rotation == CCW) ? "CCW" : "STP");

      display.setCursor(0, 54);
      display.print(safety_stop_active ? "[REM AKTIF]" : "[ JALUR AMAN ]");

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

  pinMode(IR_L, INPUT); attachInterrupt(digitalPinToInterrupt(IR_L), countPulseL, FALLING);
  pinMode(IR_R, INPUT); attachInterrupt(digitalPinToInterrupt(IR_R), countPulseR, FALLING);

  pinMode(TRIG_RANYA, OUTPUT); pinMode(ECHO_RANYA, INPUT);
  pinMode(TRIG_KIRI, OUTPUT);  pinMode(ECHO_KIRI, INPUT);
  pinMode(TRIG_BKNG, OUTPUT);  pinMode(ECHO_BKNG, INPUT);

  setMotorSpeed(0, RPWM_R, LPWM_R);
  setMotorSpeed(0, RPWM_L, LPWM_L);

  WiFi.mode(WIFI_STA);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE); 
  esp_wifi_set_promiscuous(false);

  if (esp_now_init() == ESP_OK) {
    esp_now_register_recv_cb(esp_now_recv_cb_t(OnDataRecv));
  }

  xTaskCreatePinnedToCore(TaskSensors, "SensorsTask", 3072, NULL, 3, NULL, 0);
  xTaskCreatePinnedToCore(TaskOLED, "OLEDTask", 3072, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(TaskMotorPID, "MotorPIDTask", 4096, NULL, 4, NULL, 1);
}

void loop() {
  vTaskDelete(NULL);
}
