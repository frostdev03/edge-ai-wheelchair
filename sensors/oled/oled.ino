#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

// Definisi resolusi OLED 1.3"
#define SCREEN_WIDTH 128 //[cite: 23]
#define SCREEN_HEIGHT 64 //[cite: 23]

// Pin I2C Default untuk ESP32 DevKit V1
#define I2C_SDA 21
#define I2C_SCL 22

// Deklarasi objek display (Alamat I2C default umumnya 0x3C)
Adafruit_SH1106G display = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1); //[cite: 23]

void setup() {
  Serial.begin(115200);
  
  // 1. Inisialisasi bus I2C pada pin default ESP32 DevKit V1
  Wire.begin(I2C_SDA, I2C_SCL);
  delay(500);

  // 2. Mulai inisialisasi layar OLED SH1106
  if(!display.begin(0x3C, true)) { //[cite: 26]
    Serial.println(F("SH1106 allocation failed")); //[cite: 26]
    for(;;); // Berhenti di sini jika OLED tidak terdeteksi //[cite: 26]
  }

  Serial.println("OLED SH1106 Berhasil Diinisialisasi!");

  // Clear buffer bawaan splash screen library
  display.clearDisplay(); //[cite: 27]

  // 3. Menampilkan Teks Statis di Layar
  display.setTextSize(1);             // Ukuran teks (1 standar, 2 besar) //[cite: 50]
  display.setTextColor(SH110X_WHITE); // Warna teks putih //[cite: 50]
  
  // Baris 1
  display.setCursor(0, 0);            // Koordinat (X, Y) //[cite: 50]
  display.println("ESP32 DevKit V1");
  
  // Baris 2
  display.setCursor(0, 16);
  display.setTextSize(2);             // Perbesar teks untuk status //[cite: 53]
  display.println("OLED TEST");
  
  // Baris 3
  display.setCursor(0, 40);
  display.setTextSize(1);
  display.println("I2C Address: 0x3C");
  
  // Baris 4
  display.setCursor(0, 52);
  display.println("Status: Pin 21 & 22 OK");

  // Kirim data buffer ke hardware layar agar muncul
  display.display(); //[cite: 53]
}

void loop() {
  // Kosong karena hanya untuk pengujian visual statis di atas meja
}
