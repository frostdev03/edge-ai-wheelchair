#include <Arduino.h>

// ===== PIN MOTOR =====
const int RPWM_R = 12;
const int LPWM_R = 13;
const int RPWM_L = 10;
const int LPWM_L = 11;

// =====================
// PARAMETER UTAMA (UBAH DI SINI)
// =====================
const int BOOST_PWM       = 185;     // PWM boost awal (tinggi)
const int BOOST_DURATION  = 500;     // durasi boost dalam ms (0.55 detik)

const int CRUISE_PWM      = 145;     // PWM setelah boost (steady)
const int RAMP_STEP       = 8;       // kenaikan PWM per step
const int RAMP_DELAY      = 80;      // delay antar step (ms)

const int TEST_DURATION   = 3000;    // berapa lama test maju (ms)

// =====================

void setMotorSpeed(int speed, int rpwmPin, int lpwmPin)
{
  speed = constrain(speed, -255, 255);
  if (speed >= 0)
  {
    analogWrite(rpwmPin, speed);
    analogWrite(lpwmPin, 0);
  }
  else
  {
    analogWrite(rpwmPin, 0);
    analogWrite(lpwmPin, -speed);
  }
}

void stopMotor()
{
  setMotorSpeed(0, RPWM_L, LPWM_L);
  setMotorSpeed(0, RPWM_R, LPWM_R);
}

void setup()
{
  Serial.begin(115200);
  
  pinMode(RPWM_R, OUTPUT);
  pinMode(LPWM_R, OUTPUT);
  pinMode(RPWM_L, OUTPUT);
  pinMode(LPWM_L, OUTPUT);

  stopMotor();

  Serial.println("\n==============================");
  Serial.println("   TEST MOTOR KURSI RODA");
  Serial.println("   (DENGAN BEBAN / DIDUDUKI)");
  Serial.println("==============================");
  Serial.printf("Boost PWM     : %d selama %d ms\n", BOOST_PWM, BOOST_DURATION);
  Serial.printf("Cruise PWM    : %d\n", CRUISE_PWM);
  Serial.println("Siapkan posisi duduk...");
  Serial.println("Motor akan jalan dalam 5 detik...\n");

  delay(5000);

  unsigned long startTime = millis();
  unsigned long boostEndTime = startTime + BOOST_DURATION;

  Serial.println("=== BOOST START ===");

  // === PHASE 1: BOOST ===
  while (millis() - startTime < BOOST_DURATION)
  {
    setMotorSpeed(-BOOST_PWM, RPWM_L, LPWM_L);  // Kiri maju
    setMotorSpeed( BOOST_PWM, RPWM_R, LPWM_R);  // Kanan maju

    Serial.printf("BOOST | PWM: %d | Waktu: %lu ms\n", 
                  BOOST_PWM, millis() - startTime);
    
    delay(50);
  }

  Serial.println("=== BOOST SELESAI - MASUK CRUISE MODE ===");

  // === PHASE 2: RAMP DOWN ke Cruise PWM ===
  int currentPWM = BOOST_PWM;

  while (currentPWM > CRUISE_PWM)
  {
    currentPWM -= RAMP_STEP;
    if (currentPWM < CRUISE_PWM) currentPWM = CRUISE_PWM;

    setMotorSpeed(-currentPWM, RPWM_L, LPWM_L);
    setMotorSpeed( currentPWM, RPWM_R, LPWM_R);

    Serial.printf("RAMP  | PWM: %d\n", currentPWM);
    delay(RAMP_DELAY);
  }

  Serial.println("=== CRUISE MODE ===");
  
  // Jalankan di PWM cruise selama TEST_DURATION
  unsigned long cruiseStart = millis();
  while (millis() - cruiseStart < TEST_DURATION)
  {
    setMotorSpeed(-CRUISE_PWM, RPWM_L, LPWM_L);
    setMotorSpeed( CRUISE_PWM, RPWM_R, LPWM_R);
    
    delay(200); // update pelan
  }

  Serial.println("\n=== TEST SELESAI ===");
  stopMotor();
  Serial.println("Motor STOP");

  while(true)
  {
    delay(1000);
  }
}

void loop() {}
