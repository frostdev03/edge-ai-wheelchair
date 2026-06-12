const int PIN_TCRT = 15; 

void setup() {
  Serial0.begin(115200);
  
  // Menggunakan INPUT_PULLUP agar pin stabil
  pinMode(PIN_TCRT, INPUT_PULLUP);
  
  Serial0.println("Memulai tes sensor TCRT5000...");
  delay(1000);
}

void loop() {
  // Membaca data digital dari sensor (akan bernilai 1/HIGH atau 0/LOW)
  int nilaiSensor = digitalRead(PIN_TCRT);
  
  Serial0.print("Nilai Sensor: ");
  Serial0.print(nilaiSensor);
  Serial0.print(" -> ");

  // Rata-rata modul TCRT5000 mengeluarkan logika 0 (LOW) saat membaca PUTIH
  // dan mengeluarkan logika 1 (HIGH) saat membaca HITAM atau tidak ada halangan
  if (nilaiSensor == 0) {
    Serial0.println("Terdeteksi: PUTIH / MANTUL");
  } else {
    Serial0.println("Terdeteksi: HITAM / KOSONG");
  }

  // Jeda 200 milidetik agar Serial Monitor tidak terlalu cepat bergulir
  delay(200);
}
