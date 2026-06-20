#include <Arduino.h>

// ===== KONFIGURASI PIN HARDWARE =====
const int RPWM_R = 12; // Motor Kanan
const int LPWM_R = 13;
const int RPWM_L = 10; // Motor Kiri
const int LPWM_L = 11;

#define IR_L 15       // TCRT5000 Kiri
#define IR_R 14       // TCRT5000 Kanan

// ===== PARAMETER FISIK KURSI RODA =====
const float WHEEL_DIAMETER = 0.60;     // Diameter roda 30cm
const int PULSES_PER_REV = 1;          // 4 Titik Pulsa per Putaran
const float TARGET_SPEED_KMH = 1.0;    // Target Kecepatan 4 km/jam

// ===== VARIABEL ENKODER & KECEPATAN =====
volatile unsigned long pulse_count_L = 0;
volatile unsigned long pulse_count_R = 0;
float speed_L_kmh = 0.0;
float speed_R_kmh = 0.0;

// ===== KONSTANTA PID (DAPAT DIINPUT VIA SERIAL) =====
static float Kp = 5.0;
static float Ki = 0.0;
static float Kd = 0.0;

// ===== FUNGSI INTERRUPT ENKODER =====
//void IRAM_ATTR countPulseL() { pulse_count_L++; }
//void IRAM_ATTR countPulseR() { pulse_count_R++; }

// ===== PEKERJAAN INTERRUPT DENGAN PROTEKSI DEBOUNCING =====
void IRAM_ATTR countPulseL() {
  static unsigned long last_interrupt_time_L = 0;
  unsigned long interrupt_time = millis();
  
  // Hanya hitung pulsa jika jeda dari pulsa sebelumnya lebih dari 150 milidetik
  if (interrupt_time - last_interrupt_time_L > 150) {
    pulse_count_L++;
    last_interrupt_time_L = interrupt_time;
  }
}

void IRAM_ATTR countPulseR() {
  static unsigned long last_interrupt_time_R = 0;
  unsigned long interrupt_time = millis();
  
  // Filter penahan riak noise solatip putih (150ms disesuaikan dengan kecepatan roda)
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

// ===== PARSING INPUT SERIAL (Kp;Ki;Kd) =====
void checkSerialInput() {
  if (Serial0.available() > 0) {
    String input = Serial0.readStringUntil('\n');
    input.trim();
    
    if (input.length() > 0) {
      int firstSemi = input.indexOf(';');
      int secondSemi = input.indexOf(';', firstSemi + 1);
      
      if (firstSemi != -1 && secondSemi != -1) {
        Kp = input.substring(0, firstSemi).toFloat();
        Ki = input.substring(firstSemi + 1, secondSemi).toFloat();
        Kd = input.substring(secondSemi + 1).toFloat();
        
        Serial0.println(F("\n======================================="));
        Serial0.println(F("          KONSTANTA PID DIPERBARUI     "));
        Serial0.println(F("======================================="));
        Serial0.printf("  Gain Baru -> Kp: %.2f | Ki: %.2f | Kd: %.2f\n", Kp, Ki, Kd);
        Serial0.println(F("=======================================\n"));
      } else {
        Serial0.println(F("[ERROR] Format salah! Gunakan: Kp;Ki;Kd (Contoh: 3.5;0.1;0.5)"));
      }
    }
  }
}

void setup() {
  Serial0.begin(115200);
  Serial0.println(F("================================================="));
  Serial0.println(F("   DIAGNOSTIK ENKODER & INTERACTIVE TUNING PID   "));
  Serial0.println(F("================================================="));
  Serial0.println(F("Ketik nilai PID di Serial Monitor dengan format: Kp;Ki;Kd\n"));

  pinMode(RPWM_R, OUTPUT); pinMode(LPWM_R, OUTPUT);
  pinMode(RPWM_L, OUTPUT); pinMode(LPWM_L, OUTPUT);
  
  pinMode(IR_L, INPUT); attachInterrupt(digitalPinToInterrupt(IR_L), countPulseL, FALLING);
  pinMode(IR_R, INPUT); attachInterrupt(digitalPinToInterrupt(IR_R), countPulseR, FALLING);

  // Pastikan roda diam saat boot awal
  setMotorSpeed(0, RPWM_R, LPWM_R);
  setMotorSpeed(0, RPWM_L, LPWM_L);
}

void loop() {
  // 1. Kunci kecepatan motor secara statis (Open-Loop tanpa PID)
  // Kita beri PWM konstan kecil (misal 65) agar roda berputar pelan secara stabil
  int out_L = -15; 
  int out_R = 15;  

  setMotorSpeed(out_L, RPWM_L, LPWM_L);
  setMotorSpeed(out_R, RPWM_R, LPWM_R);

  // 2. Ambil akumulasi data pulsa (Kita naikkan interval sampling ke 2 detik)
  static unsigned long last_sampling = millis();
  unsigned long now = millis();
  
  if (now - last_sampling >= 2000) { // Sampling setiap 2 detik sekali
    float dt_sampling = (now - last_sampling) / 1000.0;

    noInterrupts();
    unsigned long p_L = pulse_count_L; pulse_count_L = 0;
    unsigned long p_R = pulse_count_R; pulse_count_R = 0;
    interrupts();

    // Hitung kecepatan berdasarkan interval sampling 2 detik
    speed_L_kmh = ((float)p_L / PULSES_PER_REV) * 3.14159 * WHEEL_DIAMETER * (3.6 / dt_sampling);
    speed_R_kmh = ((float)p_R / PULSES_PER_REV) * 3.14159 * WHEEL_DIAMETER * (3.6 / dt_sampling);

    // Cetak ke Serial Monitor untuk pembuktian fisik
    Serial0.println(F("======================================================="));
    Serial0.printf("[DIAGNOSTIK] Interval Cek: %.1f detik\n", dt_sampling);
    Serial0.printf("MOTOR KIRI  -> Total Pulsa Terbaca: %lu | Kecepatan: %.2f Km/h\n", p_L, speed_L_kmh);
    Serial0.printf("MOTOR KANAN -> Total Pulsa Terbaca: %lu | Kecepatan: %.2f Km/h\n", p_R, speed_R_kmh);
    Serial0.println(F("======================================================="));

    last_sampling = now;
  }
  
  delay(10);
}

//void loop() {
//  static unsigned long last_time = millis();
//  static float integral_L = 0.0, integral_R = 0.0;
//  static float last_err_L = 0.0, last_err_R = 0.0;
//  
//  float KOMPENSASI_PWM_KANAN = 0.75; // Faktor penyeimbang rantai sasis
//  float KOMPENSASI_PWM_KIRI  = 1.00;
//
//  unsigned long now = millis();
//  float dt = (now - last_time) / 1000.0;
//  if (dt <= 0.0) dt = 0.04;
//
//  // Cek input parameter baru dari Serial Monitor laptop
//  checkSerialInput();
//
//  // Ambil data akumulasi pulsa TCRT5000
//  noInterrupts();
//  unsigned long p_L = pulse_count_L; pulse_count_L = 0;
//  unsigned long p_R = pulse_count_R; pulse_count_R = 0;
//  interrupts();
//
//  // Hitung kecepatan aktual real-time tiap loop 40ms
//  speed_L_kmh = ((float)p_L / PULSES_PER_REV) * 3.14159 * WHEEL_DIAMETER * (3.6 / dt);
//  speed_R_kmh = ((float)p_R / PULSES_PER_REV) * 3.14159 * WHEEL_DIAMETER * (3.6 / dt);
//  last_time = now;
//
//  // Target kecepatan langsung dikunci ke 4.0 km/jam
//  float target_L = TARGET_SPEED_KMH;
//  float target_R = TARGET_SPEED_KMH;
//
//  // Sirkuit Loop Tertutup PID
//  float err_L = target_L - speed_L_kmh;
//  float err_R = target_R - speed_R_kmh;
//
//  integral_L = constrain(integral_L + (err_L * dt), -20.0, 20.0); 
//  integral_R = constrain(integral_R + (err_R * dt), -20.0, 20.0);
//
//  float derivative_L = (err_L - last_err_L) / dt;
//  float derivative_R = (err_R - last_err_R) / dt;
//
//  int pwm_base_L = (Kp * err_L) + (Ki * integral_L) + (Kd * derivative_L);
//  int pwm_base_R = (Kp * err_R) + (Ki * integral_R) + (Kd * derivative_R);
//
//  int pwm_output_L_final = constrain((int)(pwm_base_L * KOMPENSASI_PWM_KIRI) + 55, 0, 120);
//  int pwm_output_R_final = constrain((int)(pwm_base_R * KOMPENSASI_PWM_KANAN) + 45, 0, 120);
//
//  // Output motor fisik (Arah baru setelah pin dibalik)
//  int out_L = -pwm_output_L_final; 
//  int out_R = pwm_output_R_final;  
//
//  setMotorSpeed(out_L, RPWM_L, LPWM_L);
//  setMotorSpeed(out_R, RPWM_R, LPWM_R);
//
//  // Print data analisis ke Serial Monitor tiap 500ms
//  static unsigned long last_print = 0;
//  if (now - last_print >= 500) {
//    Serial0.println(F("-------------------------------------------------------"));
//    Serial0.printf("GAIN ACTIVE -> Kp: %.2f | Ki: %.2f | Kd: %.2f\n", Kp, Ki, Kd);
//    Serial0.printf("[LEFT MOTOR]  -> Pulsa: %lu | Speed: %.2f Km/h | PWM Out: %d\n", p_L, speed_L_kmh, out_L);
//    Serial0.printf("[RIGHT MOTOR] -> Pulsa: %lu | Speed: %.2f Km/h | PWM Out: %d\n", p_R, speed_R_kmh, out_R);
//    last_print = now;
//  }
//
//  last_err_L = err_L;
//  last_err_R = err_R;
//  delay(40); // Jeda loop 25 Hz
//}
