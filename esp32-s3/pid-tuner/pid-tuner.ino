#include <Arduino.h>

// ===== KONFIGURASI PIN HARDWARE =====
const int RPWM_R = 12; // Motor Kanan
const int LPWM_R = 13;
const int RPWM_L = 10; // Motor Kiri
const int LPWM_L = 11;

#define IR_L 14       // TCRT5000 Kiri
#define IR_R 15       // TCRT5000 Kanan

// ===== PARAMETER FISIK KURSI RODA AKTUAL =====
const float WHEEL_DIAMETER = 0.60;     // Diameter roda 60 cm
const int PULSES_PER_REV = 1;          // 1 Titik isolasi putih di roda
const float TARGET_SPEED_KMH = 4.0;    // Target Kecepatan Utama

// ===== VARIABEL ENKODER & KECEPATAN GLOBAL =====
volatile unsigned long pulse_count_L = 0;
volatile unsigned long pulse_count_R = 0;
float speed_L_kmh = 0.0;
float speed_R_kmh = 0.0;

// ===== 1 SET KONSTANTA PID TUNGGAL (DAPAT DIINPUT VIA SERIAL) =====
static float Kp = 5.0;
static float Ki = 0.0;
static float Kd = 0.0;

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

// ===== PARSING INPUT SERIAL TUNGGAL (Kp;Ki;Kd) =====
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
        Serial0.printf("  Gain Tunggal Baru -> Kp: %.2f | Ki: %.2f | Kd: %.2f\n", Kp, Ki, Kd);
        Serial0.println(F("=======================================\n"));
      } else {
        Serial0.println(F("[ERROR] Format input salah! Gunakan format: Kp;Ki;Kd (Contoh: 4.5;0.1;0.5)"));
      }
    }
  }
}

void setup() {
  Serial0.begin(115200);
  Serial0.println(F("========================================================="));
  Serial0.println(F("   SIMULASI 1 PID + KOMPENSASI PASCA-PID (RHO TUNGGAL)   "));
  Serial0.println(F("========================================================="));
  Serial0.println(F("Ketik nilai PID di Serial Monitor dengan format: Kp;Ki;Kd\n"));

  pinMode(RPWM_R, OUTPUT); pinMode(LPWM_R, OUTPUT);
  pinMode(RPWM_L, OUTPUT); pinMode(LPWM_L, OUTPUT);
  
  pinMode(IR_L, INPUT); attachInterrupt(digitalPinToInterrupt(IR_L), countPulseL, FALLING);
  pinMode(IR_R, INPUT); attachInterrupt(digitalPinToInterrupt(IR_R), countPulseR, FALLING);

  // Rem diam saat menyala awal
  setMotorSpeed(0, RPWM_R, LPWM_R);
  setMotorSpeed(0, RPWM_L, LPWM_L);
}

void loop() {
  static unsigned long last_time = millis();
  static float integral_L = 0.0, integral_R = 0.0;
  static float last_err_L = 0.0, last_err_R = 0.0;

  // 🌟 ADJUST DI SINI: Faktor Penyeimbang Rantai Mekanis Pasca-PID
  // Karena rantai kanan lebih panjang/ringan, kita redam output PWM-nya di sini
  float KOMPENSASI_PWM_KANAN = 0.75; 
  float KOMPENSASI_PWM_KIRI  = 1.00; 

  unsigned long now = millis();
  float dt = (now - last_time) / 1000.0;
  if (dt <= 0.0) dt = 0.04;

  // Membaca perintah penggantian nilai PID interaktif dari Serial Monitor
  checkSerialInput();

  // Ambil data pulsa interupsi real-time 40ms
  noInterrupts();
  unsigned long p_L = pulse_count_L; pulse_count_L = 0;
  unsigned long p_R = pulse_count_R; pulse_count_R = 0;
  interrupts();

  // Perhitungan kecepatan sinkron real-time
  speed_L_kmh = ((float)p_L / PULSES_PER_REV) * 3.14159 * WHEEL_DIAMETER * (3.6 / dt);
  speed_R_kmh = ((float)p_R / PULSES_PER_REV) * 3.14159 * WHEEL_DIAMETER * (3.6 / dt);
  last_time = now;

  // Target kecepatan disamakan murni 4.0 km/jam
  float target_L = TARGET_SPEED_KMH;
  float target_R = TARGET_SPEED_KMH;

  // Algoritma Loop Tertutup 1 PID Tunggal
  float err_L = target_L - speed_L_kmh;
  float err_R = target_R - speed_R_kmh;

  integral_L = constrain(integral_L + (err_L * dt), -40.0, 40.0); 
  integral_R = constrain(integral_R + (err_R * dt), -40.0, 40.0);

  float derivative_L = (err_L - last_err_L) / dt;
  float derivative_R = (err_R - last_err_R) / dt;

  // Perhitungan PWM dasar menggunakan 1 set konstanta Kp, Ki, Kd yang sama
  int pwm_base_L = (Kp * err_L) + (Ki * integral_L) + (Kd * derivative_L);
  int pwm_base_R = (Kp * err_R) + (Ki * integral_R) + (Kd * derivative_R);

  // 🌟 PASCA-PID ADJUSTMENT: Kalikan hasil kalkulasi dengan faktor penyeimbang rantai
  int pwm_output_L_final = constrain((int)(pwm_base_L * KOMPENSASI_PWM_KIRI) + 60, 0, 120);
  int pwm_output_R_final = constrain((int)(pwm_base_R * KOMPENSASI_PWM_KANAN) + 50, 0, 120);

  // Logika arah inversi sasis kabel fisik
  int out_L = -pwm_output_L_final; 
  int out_R = pwm_output_R_final;  

  setMotorSpeed(out_L, RPWM_L, LPWM_L);
  setMotorSpeed(out_R, RPWM_R, LPWM_R);

  // Printout data analisis performa sampling setiap 500ms
  static unsigned long last_print = 0;
  if (now - last_print >= 500) {
    Serial0.println(F("-----------------------------------------------------------------------"));
    Serial0.printf("GLOBAL GAIN -> Kp: %.2f | Ki: %.2f | Kd: %.2f\n", Kp, Ki, Kd);
    Serial0.printf("[LEFT  MOTOR] -> Pulsa: %lu | Speed: %.2f Km/h | PWM Out: %d\n", p_L, speed_L_kmh, out_L);
    Serial0.printf("[RIGHT MOTOR] -> Pulsa: %lu | Speed: %.2f Km/h | PWM Out: %d (Kompensasi: %.2f)\n", p_R, speed_R_kmh, out_R, KOMPENSASI_PWM_KANAN);
    last_print = now;
  }

  last_err_L = err_L;
  last_err_R = err_R;
  delay(40); // Interval loop konstan 25 Hz
}
