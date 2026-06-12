// Mendefinisikan pin motor kanan berdasarkan skematik
const int RPWM_R = 10;
const int LPWM_R = 11;

// Mendefinisikan pin motor kiri berdasarkan skematik
const int RPWM_L = 12;
const int LPWM_L = 13;

// Kecepatan minimal (Skala 0 - 255)
// Jika motor hanya mendengung, naikkan menjadi 70 atau 80.
const int MIN_SPEED = 50;

void setup() {
  Serial0.begin(115200);
  Serial0.println("Sistem Tes Motor Kanan dan Kiri Dimulai...");

  // Menginisialisasi pin sebagai OUTPUT
  pinMode(RPWM_R, OUTPUT);
  pinMode(LPWM_R, OUTPUT);
  pinMode(RPWM_L, OUTPUT);
  pinMode(LPWM_L, OUTPUT);

  // Protokol Keamanan: Pastikan semua motor mati saat ESP32 baru menyala
  analogWrite(RPWM_R, 0);
  analogWrite(LPWM_R, 0);
  analogWrite(RPWM_L, 0);
  analogWrite(LPWM_L, 0);
  
  delay(2000); // Jeda 2 detik sebelum mulai
}

void loop() {
  // 1. Kedua motor bergerak ke satu arah (Maju) dengan kecepatan minimal
  Serial0.println("Kedua Motor: MAJU pelan...");
  
  // Penggerak Motor Kanan
  analogWrite(RPWM_R, MIN_SPEED);  
  analogWrite(LPWM_R, 0);          
  
  // Penggerak Motor Kiri
  analogWrite(RPWM_L, MIN_SPEED);  
  analogWrite(LPWM_L, 0);          
  
  delay(3000); // Biarkan berputar selama 3 detik

  // 2. Kedua Motor Berhenti (Rem statis)
  Serial0.println("Kedua Motor: BERHENTI.");
  
  analogWrite(RPWM_R, 0);
  analogWrite(LPWM_R, 0);
  
  analogWrite(RPWM_L, 0);
  analogWrite(LPWM_L, 0);
  
  delay(5000); // Berhenti selama 5 detik
}
