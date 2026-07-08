// ===== TEST SEKUENS GERAKAN =====
// Maju 2 detik -> stop -> kanan 1 detik -> stop, diulang 5x per siklus.
// Karena ditaruh di loop(), begitu 5x selesai, seluruh siklus mulai lagi dari awal.

// ===== KONFIGURASI PIN HARDWARE (samain dengan sketch utama) =====
const int RPWM_R = 12; // Motor Kanan
const int LPWM_R = 13;
const int RPWM_L = 10; // Motor Kiri
const int LPWM_L = 11;

// ===== NILAI PWM (samain/samakan dengan hasil tuning di sketch utama) =====
const int BASE_PWM_L = 31; // magnitude PWM roda kiri saat maju/mundur
const int BASE_PWM_R = 60; // magnitude PWM roda kanan saat maju/mundur
const int TURN_PWM_L = 0; // magnitude PWM roda kiri saat belok kanan
const int TURN_PWM_R = 60;   // magnitude PWM roda kanan saat belok kanan (diam)

const int JUMLAH_LOOP = 5;

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

void gerakMaju() {
  // Acuan: Maju = Kiri(-) dan Kanan(+)
  setMotorSpeed(-BASE_PWM_L, RPWM_L, LPWM_L);
  setMotorSpeed(BASE_PWM_R, RPWM_R, LPWM_R);
}

void belokKanan() {
  // Roda kiri maju (-), roda kanan diam
  setMotorSpeed(-TURN_PWM_L, RPWM_L, LPWM_L);
  setMotorSpeed(TURN_PWM_R, RPWM_R, LPWM_R);
}

void berhenti() {
  setMotorSpeed(0, RPWM_L, LPWM_L);
  setMotorSpeed(0, RPWM_R, LPWM_R);
}

void setup() {
  Serial0.begin(115200);

  pinMode(RPWM_R, OUTPUT); pinMode(LPWM_R, OUTPUT);
  pinMode(RPWM_L, OUTPUT); pinMode(LPWM_L, OUTPUT);

  berhenti();
}

void loop() {
  
    Serial0.println("Maju 2 detik...");
    gerakMaju();
    delay(2000);

    Serial0.println("Stop...");
    berhenti();
    delay(500);

    Serial0.println("Belok kanan 1 detik...");
    belokKanan();
    delay(2000);

    Serial0.println("Stop...");
    berhenti();
    delay(500);

}
