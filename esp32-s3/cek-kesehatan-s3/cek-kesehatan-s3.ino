#include <Arduino.h>

// Daftar seluruh GPIO digital potensial pada ESP32-S3 yang biasa diakses
const int target_gpios[] = {
  1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 
  17, 18, 19, 20, 21, 35, 36, 37, 38, 39, 40, 41, 42, 45, 46, 47, 48
};
const int total_pins = sizeof(target_gpios) / sizeof(target_gpios[0]);

void setup() {
  Serial0.begin(115200);
  while (!Serial0) { delay(10); }

  Serial0.println("\n=============================================");
  Serial0.println("   ESP32-S3 TOTAL PINOUT HARDWARE SCANNER    ");
  Serial0.println("=============================================\n");
  Serial0.println("[INFO] Memulai pemindaian gerbang logika...");
  Serial0.println("(Pastikan semua kabel beban/sensor SUDAH DICABUT!)\n");
  delay(1000);

  int short_gnd_count = 0;
  int short_vcc_count = 0;
  int healthy_count = 0;

  for (int i = 0; i < total_pins; i++) {
    int pin = target_gpios[i];

    // SKIP pin Serial jika menggunakan hardware UART reguler agar monitor tidak terputus
    if (pin == 43 || pin == 44) continue; 

    // Test 1: Tarik ke HIGH menggunakan Internal Pull-Up
    pinMode(pin, INPUT_PULLUP);
    delay(5);
    int state_high = digitalRead(pin);

    // Test 2: Tarik ke LOW menggunakan Internal Pull-Down
    pinMode(pin, INPUT_PULLDOWN);
    delay(5);
    int state_low = digitalRead(pin);

    // Kembalikan ke posisi aman (INPUT biasa)
    pinMode(pin, INPUT);

    // Evaluasi Kesehatan Kaki Pin
    Serial0.printf("GPIO %02d: ", pin);
    if (state_high == HIGH && state_low == LOW) {
      Serial0.println("[OK] Sehat / Normal");
      healthy_count++;
    } else if (state_high == LOW) {
      Serial0.println("[❌ SHORT TO GND] Gate jebol terikat ke Ground!");
      short_gnd_count++;
    } else if (state_low == HIGH) {
      Serial0.println("[❌ SHORT TO VCC] Gate leak/bocor ke VCC!");
      short_vcc_count++;
    }
  }

  Serial0.println("\n=============================================");
  Serial0.println("           RINGKASAN DIAGNOSIS               ");
  Serial0.println("=============================================");
  Serial0.printf("  - Total Pin Diuji   : %d\n", total_pins);
  Serial0.printf("  - Pin Sehat/Normal  : %d\n", healthy_count);
  Serial0.printf("  - Korsleting ke GND : %d\n", short_gnd_count);
  Serial0.printf("  - Kebocoran ke VCC  : %d\n", short_vcc_count);
  Serial0.println("=============================================");

  if (short_gnd_count == 0 && short_vcc_count == 0) {
    Serial0.println("\n🎉 SILIKON CHIP AMAN TOTAL! Seluruh GPIO berfungsi normal.");
  } else {
    Serial0.println("\n⚠️ PERINGATAN: Ada pin yang rusak internal. Hindari penggunaan pin tersebut!");
  }
}

void loop() {
  // Cukup dijalankan sekali saat setup
}
