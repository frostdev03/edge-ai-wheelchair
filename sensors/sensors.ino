//// ===== HC-SR04 Pins =====
//#define TRIG1 5
//#define ECHO1 4
//
//#define TRIG2 6
//#define ECHO2 7
//
//#define TRIG3 12
//#define ECHO3 15
//
//// ===== TC-RT5000 Pins =====
//// #define IR1 38
//// #define IR2 35
//
//// long readUltrasonic(int trigPin, int echoPin) {
////   digitalWrite(trigPin, LOW);
////   delayMicroseconds(2);
//
////   digitalWrite(trigPin, HIGH);
////   delayMicroseconds(10);
////   digitalWrite(trigPin, LOW);
//
////   long duration = pulseIn(echoPin, HIGH, 30000); // timeout 30 ms
////   return duration; // raw microseconds
//// }
//
//long readUltrasonicCM(int trigPin, int echoPin) {
//  digitalWrite(trigPin, LOW);
//  delayMicroseconds(2);
//  digitalWrite(trigPin, HIGH);
//  delayMicroseconds(10);
//  digitalWrite(trigPin, LOW);
//
//  long duration = pulseIn(echoPin, HIGH, 30000);  // µs
//  if (duration == 0) return -1;                   // no echo
//  return duration / 58;                           // centimeters
//}
//
//void setup() {
//  Serial0.begin(115200);
//  delay(1000);
//
//  pinMode(TRIG1, OUTPUT);
//  pinMode(ECHO1, INPUT);
//
//  pinMode(TRIG2, OUTPUT);
//  pinMode(ECHO2, INPUT);
//
//  pinMode(TRIG3, OUTPUT);
//  pinMode(ECHO3, INPUT);
//
//  // pinMode(IR1, INPUT);
//  // pinMode(IR2, INPUT);
//
//  Serial0.println("ESP32-S3 Sensor Read Test Started");
//}
//
//void loop() {
//  // long us1 = readUltrasonic(TRIG1, ECHO1);
//  // long us2 = readUltrasonic(TRIG2, ECHO2);
//  // long us3 = readUltrasonic(TRIG3, ECHO3);
//  long d1 = readUltrasonicCM(TRIG1, ECHO1);
//  long d2 = readUltrasonicCM(TRIG2, ECHO2);
//  long d3 = readUltrasonicCM(TRIG3, ECHO3);
//
//
//  // int ir1 = digitalRead(IR1);
//  // int ir2 = digitalRead(IR2);
//
//  Serial0.println("---- RAW SENSOR VALUES ----");
//  Serial0.print("HC-SR04 #1 (cm): ");
//  Serial0.println(d1);
//  Serial0.print("HC-SR04 #2 (cm): ");
//  Serial0.println(d2);
//  Serial0.print("HC-SR04 #3 (cm): ");
//  Serial0.println(d3);
//  // Serial0.print("HC-SR04 #1 (us): ");
//  // Serial0.println(us1);
//  // Serial0.print("HC-SR04 #2 (us): ");
//  // Serial0.println(us2);
//  // Serial0.print("HC-SR04 #3 (us): ");
//  // Serial0.println(us3);
//  // Serial0.print("TC-RT5000 #1 (ADC): ");
//  // Serial0.println(ir1);
//  // Serial0.print("TC-RT5000 #2 (ADC): ");
//  // Serial0.println(ir2);
//  Serial0.println();
//
//  delay(1000);
//}
// ===== KODE DIAGNOSTIK KABEL JST (SOLO TEST) =====
#define TEST_TRIG 9  // Sesuaikan dengan pin yang kamu pakai untuk sensor kiri
#define TEST_ECHO 8

void setup() {
  Serial0.begin(115200);
  pinMode(TEST_TRIG, OUTPUT);
  pinMode(TEST_ECHO, INPUT);
  Serial0.println(F("[DIAGNOSTIK KABEL JST STARTED]"));
}

void loop() {
  // 1. Kirim sinyal HIGH ke kabel JST Trig
  digitalWrite(TEST_TRIG, HIGH);
  delayMicroseconds(50);
  
  // 2. Baca apakah sinyalnya kembali lewat kabel JST Echo
  int status_echo = digitalRead(TEST_ECHO);
  digitalWrite(TEST_TRIG, LOW);

  if (status_echo == HIGH) {
    Serial0.println(F("Konektivitas Jalur JST -> [ AMAN / TERHUBUNG ]"));
  } else {
    Serial0.println(F("Konektivitas Jalur JST -> [ PUTUS / RUSAK ]"));
  }

  delay(500); // Cek setiap setengah detik
}
