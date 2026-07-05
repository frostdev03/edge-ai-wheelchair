// ===== KONFIGURASI PIN HARDWARE =====
const int RPWM_R = 12; // Motor Kanan 
const int LPWM_R = 13; // 
const int RPWM_L = 10; // Motor Kiri 
const int LPWM_L = 11; // 

void setup() {
  Serial.begin(115200);
  
  pinMode(RPWM_R, OUTPUT); 
  pinMode(LPWM_R, OUTPUT);
  pinMode(RPWM_L, OUTPUT); 
  pinMode(LPWM_L, OUTPUT);

  // Pastikan motor diam saat pertama menyala
  setMotorKiri(0);
  setMotorKanan(0);
  
  Serial.println("=== Sistem Siap. Mulai Pengujian Motor ===");
  delay(2000); // Jeda 2 detik sebelum motor bergerak
}

void loop() {
  // ----------------------------------------------------------------------
  // AREA UJI COBA: Silakan ganti nilai di dalam fungsi ini untuk testing
  // Batas nilai: -255 sampai 255
  // ----------------------------------------------------------------------
  
  Serial.println("Menguji Motor Kiri POSITIF dan Motor Kanan POSITIF...");
  setMotorKiri(50);  // Uji nilai positif motor kiri [cite: 19]
  setMotorKanan(0); // Uji nilai positif motor kanan [cite: 19]
  delay(3000);       // Jalan selama 3 detik

  Serial.println("Motor DIAM...");
  setMotorKiri(0);
  setMotorKanan(0);
  delay(2000);       // Diam selama 2 detik

  Serial.println("Menguji Motor Kiri NEGATIF dan Motor Kanan NEGATIF...");
  setMotorKiri(-50);  // Uji nilai negatif motor kiri [cite: 20]
  setMotorKanan(0); // Uji nilai negatif motor kanan [cite: 20]
  delay(3000);        // Jalan selama 3 detik

  Serial.println("Motor DIAM...");
  setMotorKiri(0);
  setMotorKanan(0);
  delay(4000);       // Diam selama 4 detik sebelum mengulang sirkulus
}

// ===== FUNGSI KHUSUS MOTOR KIRI =====
void setMotorKiri(int speed) {
  speed = constrain(speed, -255, 255); // [cite: 18]
  if (speed >= 0) { // [cite: 19]
    analogWrite(RPWM_L, speed); // [cite: 19]
    analogWrite(LPWM_L, 0); // [cite: 19]
  } else { // [cite: 19]
    analogWrite(RPWM_L, 0); // [cite: 19]
    analogWrite(LPWM_L, -speed); // [cite: 20]
  }
}

// ===== FUNGSI KHUSUS MOTOR KANAN =====
void setMotorKanan(int speed) {
  speed = constrain(speed, -255, 255); // [cite: 18]
  if (speed >= 0) { // [cite: 19]
    analogWrite(RPWM_R, speed); // [cite: 19]
    analogWrite(LPWM_R, 0); // [cite: 19]
  } else { // [cite: 19]
    analogWrite(RPWM_R, 0); // [cite: 19]
    analogWrite(LPWM_R, -speed); // [cite: 20]
  }
}
