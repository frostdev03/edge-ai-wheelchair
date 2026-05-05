// Mendefinisikan pin berdasarkan skematik Anda
const int RPWM_R = 10;
const int LPWM_R = 11;

// Kecepatan minimal (Skala 0 - 255)
// Kita mulai di angka 60. Jika motor hanya mendengung, naikkan menjadi 70 atau 80.
const int MIN_SPEED = 50; 

void setup() {
  Serial0.begin(115200);
  Serial0.println("Sistem Tes Motor Kanan Dimulai...");

  // Menginisialisasi pin sebagai OUTPUT
  pinMode(RPWM_R, OUTPUT);
  pinMode(LPWM_R, OUTPUT);

  // Protokol Keamanan: Pastikan motor mati saat ESP32 baru menyala
  analogWrite(RPWM_R, 0);
  analogWrite(LPWM_R, 0);
  delay(2000); // Jeda 2 detik sebelum mulai
}

void loop() {
  // 1. Motor bergerak ke satu arah (Maju) dengan kecepatan minimal
  Serial0.println("Motor Kanan: MAJU pelan...");
  analogWrite(RPWM_R, MIN_SPEED);  // Beri sinyal PWM ke RPWM
  analogWrite(LPWM_R, 0);          // Tarik LPWM ke Ground (0)
  delay(3000);                     // Biarkan berputar selama 3 detik

  // 2. Motor Berhenti (Rem statis)
  Serial0.println("Motor Kanan: BERHENTI.");
  analogWrite(RPWM_R, 0);
  analogWrite(LPWM_R, 0);
  delay(5000);                     // Berhenti selama 3 detik
}