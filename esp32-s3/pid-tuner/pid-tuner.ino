#include <Arduino.h>

// ============================================================================
// KONFIGURASI PIN & DRIVER MOTOR (IDENTIK)
// ============================================================================
#define RPWM_L 12
#define LPWM_L 13
#define RPWM_R 10
#define LPWM_R 11

#define ENCODER_L_PIN 1
#define ENCODER_R_PIN 2

// ============================================================================
// PARAMETER SISTEM & TUNING PID (IDENTIK DENGAN CORETAN TERBARU)
// ============================================================================
const float WHEEL_DIAMETER = 0.60; 
const int PULSES_PER_REV = 1;      

// 🌟 PARAMETER PARAMETER TUNING KAMU (Silakan ganti nilai ini untuk tabel TA)
static float Kp = 35.0; // Sesuai tes terakhirmu
static float Ki = 0.0; // 🌟 DIWAJIBKAN AKTIF AGAR MOTOR JALAN PAS DIDUDUKKI!
static float Kd = 0.0;

// Konstanta Kompensasi Mekanis Rantai Bawaanmu
float KOMPENSASI_PWM_KANAN = 0.75; 
float KOMPENSASI_PWM_KIRI  = 1.00; 

// ============================================================================
// VARIABEL GLOBAL (IDENTIK)
// ============================================================================
enum Command { CMD_DIAM, CMD_MAJU, CMD_MUNDUR, CMD_KANAN, CMD_KIRI };
volatile Command current_command = CMD_DIAM;

volatile unsigned long pulse_count_L = 0;
volatile unsigned long pulse_count_R = 0;

float speed_L_kmh = 0.0;
float speed_R_kmh = 0.0;
float setpoint_L = 0.0;
float setpoint_R = 0.0;

float ramped_speed_L = 0;
float ramped_speed_R = 0;

TaskHandle_t TaskPIDHandle = NULL;
TaskHandle_t TaskSeqTestHandle = NULL;

// ============================================================================
// INTERRUPT SERVICE ROUTINE (ISR)
// ============================================================================
void IRAM_ATTR ISRS_L() { pulse_count_L++; }
void IRAM_ATTR ISRS_R() { pulse_count_R++; }

// ============================================================================
// FUNGSI KONTROL MOTOR (SAMAKAN PERSIS LOGIKANYA)
// ============================================================================
void setMotorSpeed(int speed, int rPwmPin, int lPwmPin) {
  if (speed >= 0) {
    analogWrite(rPwmPin, speed);
    analogWrite(lPwmPin, 0);
  } else {
    analogWrite(rPwmPin, 0);
    analogWrite(lPwmPin, abs(speed));
  }
}

// ============================================================================
// CORE TASK KENDALI PID (LOGIKA DAN RUMUS 100% SAMA PERSIS DENGAN ORIGINAL)
// ============================================================================
void TaskMotorPID(void *pvParameters) {
  const float dt = 0.04; 
  float integral_L = 0, integral_R = 0;
  float previous_error_L = 0, previous_error_R = 0;
  const float r_step = 0.2; 

  for (;;) {
    noInterrupts();
    unsigned long p_L = pulse_count_L; pulse_count_L = 0;
    unsigned long p_R = pulse_count_R; pulse_count_R = 0;
    interrupts();

    speed_L_kmh = ((float)p_L / PULSES_PER_REV) * 3.14159 * WHEEL_DIAMETER * (3.6 / dt);
    speed_R_kmh = ((float)p_R / PULSES_PER_REV) * 3.14159 * WHEEL_DIAMETER * (3.6 / dt);

    // Pemetaan Perintah Otomatis Sekuensial
    float target_L = 0.0;
    float target_R = 0.0;

    switch (current_command) {
      case CMD_MAJU:   target_L = 4.0;  target_R = 4.0;  break;
      case CMD_MUNDUR: target_L = -4.0; target_R = -4.0; break;
      case CMD_KANAN:  target_L = 3.5;  target_R = -3.5; break;
      case CMD_KIRI:   target_L = -3.5; target_R = 3.5;  break;
      default:         target_L = 0.0;  target_R = 0.0;  break;
    }

    setpoint_L = target_L;
    setpoint_R = target_R;

    // Logika Ramping (Identik)
    if (ramped_speed_L < setpoint_L) {
      ramped_speed_L += r_step; if (ramped_speed_L > setpoint_L) ramped_speed_L = setpoint_L;
    } else if (ramped_speed_L > setpoint_L) {
      ramped_speed_L -= r_step; if (ramped_speed_L < setpoint_L) ramped_speed_L = setpoint_L;
    }

    if (ramped_speed_R < setpoint_R) {
      ramped_speed_R += r_step; if (ramped_speed_R > setpoint_R) ramped_speed_R = setpoint_R;
    } else if (ramped_speed_R > setpoint_R) {
      ramped_speed_R -= r_step; if (ramped_speed_R < setpoint_R) ramped_speed_R = setpoint_R;
    }

    // Perhitungan Error PID (Identik)
    float err_L = abs(ramped_speed_L) - speed_L_kmh;
    float err_R = abs(ramped_speed_R) - speed_R_kmh;

    integral_L += err_L * dt;
    integral_R += err_R * dt;
    integral_L = constrain(integral_L, -50.0, 50.0);
    integral_R = constrain(integral_R, -50.0, 50.0);

    float derivative_L = (err_L - previous_error_L) / dt;
    float derivative_R = (err_R - previous_error_R) / dt;

    float pwm_base_L = (Kp * err_L) + (Ki * integral_L) + (Kd * derivative_L);
    float pwm_base_R = (Kp * err_R) + (Ki * integral_R) + (Kd * derivative_R);

    previous_error_L = err_L;
    previous_error_R = err_R;

    // Saturation & Offset Logic (100% IDENTIK DENGAN FILE ORIGINAL KAMU)
    int pwm_output_L_final = (ramped_speed_L == 0) ? 0 : constrain((int)(pwm_base_L * KOMPENSASI_PWM_KIRI) + 100, 0, 180);
    int pwm_output_R_final = (ramped_speed_R == 0) ? 0 : constrain((int)(pwm_base_R * KOMPENSASI_PWM_KANAN) + 90, 0, 180);

    // Penentuan Arah Menggunakan Logika Penjumlahan/Pengurangan Asli dari s3-main-rtos.ino
    int out_L = 0;
    int out_R = 0;

    if (ramped_speed_L > 0) {
      out_L = -pwm_output_L_final;
    } else if (ramped_speed_L < 0) {
      out_L = pwm_output_L_final;
    }

    if (ramped_speed_R > 0) {
      out_R = pwm_output_R_final;
    } else if (ramped_speed_R < 0) {
      out_R = -pwm_output_R_final;
    }

    // Kirim Ke Driver Motor
    setMotorSpeed(out_L, RPWM_L, LPWM_L);
    setMotorSpeed(out_R, RPWM_R, LPWM_R);

    // Cetak Data Untuk Plotter Rekap Hasil Transien TA
    Serial0.printf("CMD:%d | Set_L:%.2f Act_L:%.2f PWM_L:%d | Set_R:%.2f Act_R:%.2f PWM_R:%d\n", 
                  current_command, abs(ramped_speed_L), speed_L_kmh, pwm_output_L_final,
                  abs(ramped_speed_R), speed_R_kmh, pwm_output_R_final);

    vTaskDelay(pdMS_TO_TICKS(40));
  }
}

// ============================================================================
// TASK AUTOMATED SEQUENTIAL TEST (SEKUENS MATI-WAKTU ANTI-BERISIK)
// ============================================================================
void TaskSequentialTest(void *pvParameters) {
  // Jeda 5 detik di awal untuk persiapan naik ke sasis setelah aki ON
  vTaskDelay(pdMS_TO_TICKS(5000)); 

  current_command = CMD_MAJU;    vTaskDelay(pdMS_TO_TICKS(3000)); // Maju 3 detik
  current_command = CMD_MUNDUR;  vTaskDelay(pdMS_TO_TICKS(2000)); // Mundur 2 detik
  current_command = CMD_KANAN;   vTaskDelay(pdMS_TO_TICKS(2000)); // Kanan 2 detik
  current_command = CMD_KIRI;    vTaskDelay(pdMS_TO_TICKS(3000)); // Kiri 3 detik
  current_command = CMD_DIAM;
  
  Serial0.println(F("[UJI COBA SEKUENSIAL SELESAI]"));
  vTaskDelete(NULL); 
}

// ============================================================================
// SETUP INITIALIZATION
// ============================================================================
void setup() {
  Serial0.begin(115200);

  pinMode(RPWM_L, OUTPUT); pinMode(LPWM_L, OUTPUT);
  pinMode(RPWM_R, OUTPUT); pinMode(LPWM_R, OUTPUT);

  setMotorSpeed(0, RPWM_L, LPWM_L);
  setMotorSpeed(0, RPWM_R, LPWM_R);

  pinMode(ENCODER_L_PIN, INPUT_PULLUP);
  pinMode(ENCODER_R_PIN, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(ENCODER_L_PIN), ISRS_L, RISING);
  attachInterrupt(digitalPinToInterrupt(ENCODER_R_PIN), ISRS_R, RISING);

  // Jalankan kedua Task secara sinkron di Core 1 sesuai arsitektur aslimu
  xTaskCreatePinnedToCore(TaskMotorPID, "MotorPID", 4096, NULL, 3, &TaskPIDHandle, 1);
  xTaskCreatePinnedToCore(TaskSequentialTest, "SeqTest", 4096, NULL, 2, &TaskSeqTestHandle, 1);
}

void loop() {}
