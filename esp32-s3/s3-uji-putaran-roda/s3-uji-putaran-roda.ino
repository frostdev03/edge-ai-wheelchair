// ===== KONFIGURASI PIN TCRT5000 =====
#define IR_L 14       // Pin TCRT5000 Kiri
#define IR_R 15       // Pin TCRT5000 Kanan

// Variabel untuk menyimpan jumlah putaran
volatile unsigned long pulse_count_L = 0;
volatile unsigned long pulse_count_R = 0;

// ===== FUNGSI PENGHITUNG PUTARAN RODA =====
// IRAM_ATTR digunakan agar fungsi ini masuk ke RAM internal yang sangat cepat dieksekusi

void IRAM_ATTR countPulseL() {
  static unsigned long last_interrupt_time_L = 0;
  unsigned long interrupt_time = millis();
  
  // Beri jeda 150ms (debouncing) agar tidak terjadi pembacaan ganda
  if (interrupt_time - last_interrupt_time_L > 150) {
    pulse_count_L++;
    last_interrupt_time_L = interrupt_time;
  }
}

void IRAM_ATTR countPulseR() {
  static unsigned long last_interrupt_time_R = 0;
  unsigned long interrupt_time = millis();
  
  // Beri jeda 150ms (debouncing) agar tidak terjadi pembacaan ganda
  if (interrupt_time - last_interrupt_time_R > 150) {
    pulse_count_R++;
    last_interrupt_time_R = interrupt_time;
  }
}

void setup() {
  Serial.begin(115200);

  // Set pin TCRT5000 sebagai INPUT
  pinMode(IR_L, INPUT);
  pinMode(IR_R, INPUT);
  
  // Pasang interrupt. FALLING berarti memicu saat sinyal turun dari 1 (HIGH) ke 0 (LOW)
  attachInterrupt(digitalPinToInterrupt(IR_L), countPulseL, FALLING);
  attachInterrupt(digitalPinToInterrupt(IR_R), countPulseR, FALLING);
}

void loop() {
  // Matikan interrupt sementara untuk mengambil data dengan aman
  noInterrupts();
  unsigned long p_L = pulse_count_L; 
  unsigned long p_R = pulse_count_R;
  
  // (Opsional) Reset nilai ke 0 jika kamu ingin menghitung putaran per detik/menit
  // pulse_count_L = 0; 
  // pulse_count_R = 0;
  interrupts(); // Hidupkan kembali interrupt

  // Tampilkan data ke Serial Monitor
  Serial.print("Putaran Kiri: ");
  Serial.print(p_L);
  Serial.print(" | Putaran Kanan: ");
  Serial.println(p_R);

  delay(1000); // Tampilkan setiap 1 detik
}
