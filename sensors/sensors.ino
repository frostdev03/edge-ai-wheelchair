// ===== HC-SR04 Pins =====
#define TRIG1 1
#define ECHO1 2

#define TRIG2 42
#define ECHO2 41

#define TRIG3 40
#define ECHO3 39

// ===== TC-RT5000 Pins =====
#define IR1 38
#define IR2 35

// long readUltrasonic(int trigPin, int echoPin) {
//   digitalWrite(trigPin, LOW);
//   delayMicroseconds(2);

//   digitalWrite(trigPin, HIGH);
//   delayMicroseconds(10);
//   digitalWrite(trigPin, LOW);

//   long duration = pulseIn(echoPin, HIGH, 30000); // timeout 30 ms
//   return duration; // raw microseconds
// }

long readUltrasonicCM(int trigPin, int echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  long duration = pulseIn(echoPin, HIGH, 30000);  // µs
  if (duration == 0) return -1;                   // no echo
  return duration / 58;                           // centimeters
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(TRIG1, OUTPUT);
  pinMode(ECHO1, INPUT);

  pinMode(TRIG2, OUTPUT);
  pinMode(ECHO2, INPUT);

  pinMode(TRIG3, OUTPUT);
  pinMode(ECHO3, INPUT);

  pinMode(IR1, INPUT);
  pinMode(IR2, INPUT);

  Serial.println("ESP32-S3 Sensor Read Test Started");
}

void loop() {
  // long us1 = readUltrasonic(TRIG1, ECHO1);
  // long us2 = readUltrasonic(TRIG2, ECHO2);
  // long us3 = readUltrasonic(TRIG3, ECHO3);
  long d1 = readUltrasonicCM(TRIG1, ECHO1);
  long d2 = readUltrasonicCM(TRIG2, ECHO2);
  long d3 = readUltrasonicCM(TRIG3, ECHO3);


  int ir1 = digitalRead(IR1);
  int ir2 = digitalRead(IR2);

  Serial.println("---- RAW SENSOR VALUES ----");
  Serial.print("HC-SR04 #1 (cm): ");
  Serial.println(d1);
  Serial.print("HC-SR04 #2 (cm): ");
  Serial.println(d2);
  Serial.print("HC-SR04 #3 (cm): ");
  Serial.println(d3);
  // Serial.print("HC-SR04 #1 (us): ");
  // Serial.println(us1);
  // Serial.print("HC-SR04 #2 (us): ");
  // Serial.println(us2);
  // Serial.print("HC-SR04 #3 (us): ");
  // Serial.println(us3);
  Serial.print("TC-RT5000 #1 (ADC): ");
  Serial.println(ir1);
  Serial.print("TC-RT5000 #2 (ADC): ");
  Serial.println(ir2);
  Serial.println();

  delay(1000);
}
