#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h> // Menggunakan library SH1106

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

// --- TENTUKAN PIN KUSTOM ESP32-S3 DI SINI ---
#define I2C_SDA 4 
#define I2C_SCL 5 

// Deklarasi layar SH1106
Adafruit_SH1106G display = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

#define NUMFLAKES     10 // Number of snowflakes in the animation example
#define LOGO_HEIGHT   16
#define LOGO_WIDTH    16
static const unsigned char PROGMEM logo_bmp[] =
{ 0b00000000, 0b11000000,
  0b00000001, 0b11000000,
  0b00000001, 0b11000000,
  0b00000011, 0b11100000,
  0b11110011, 0b11100000,
  0b11111110, 0b11111000,
  0b01111110, 0b11111111,
  0b00110011, 0b10011111,
  0b00011111, 0b11111100,
  0b00001101, 0b01110000,
  0b00011011, 0b10100000,
  0b00111111, 0b11100000,
  0b00111111, 0b11110000,
  0b01111100, 0b11110000,
  0b01110000, 0b01110000,
  0b00000000, 0b00110000 };

void setup() {
  Serial.begin(115200);

  // 1. Mulai I2C di pin custom
  Wire.begin(I2C_SDA, I2C_SCL);
  delay(500);

  // 2. Inisialisasi SH1106 (Alamat 0x3C)
  if(!display.begin(0x3C, true)) {
    Serial.println(F("SH1106 allocation failed"));
    for(;;); // Berhenti jika gagal
  }

  // Tampilkan splash screen bawaan library
  display.display();
  delay(2000); 

  display.clearDisplay();
  display.drawPixel(10, 10, SH110X_WHITE);
  display.display();
  delay(2000);

  // Jalankan semua tes grafis
  testdrawline();
  testdrawrect();
  testfillrect();
  testdrawcircle();
  testfillcircle();
  testdrawroundrect();
  testfillroundrect();
  testdrawtriangle();
  testfilltriangle();
  testdrawchar();
  testdrawstyles();
  testscrolltext();
  testdrawbitmap();

  display.invertDisplay(true);
  delay(1000);
  display.invertDisplay(false);
  delay(1000);

  testanimate(logo_bmp, LOGO_WIDTH, LOGO_HEIGHT);
}

void loop() {
}

void testdrawline() {
  int16_t i;
  display.clearDisplay(); 
  for(i=0; i<display.width(); i+=4) {
    display.drawLine(0, 0, i, display.height()-1, SH110X_WHITE);
    display.display(); 
    delay(1);
  }
  for(i=0; i<display.height(); i+=4) {
    display.drawLine(0, 0, display.width()-1, i, SH110X_WHITE);
    display.display();
    delay(1);
  }
  delay(250);
  display.clearDisplay();
  for(i=0; i<display.width(); i+=4) {
    display.drawLine(0, display.height()-1, i, 0, SH110X_WHITE);
    display.display();
    delay(1);
  }
  for(i=display.height()-1; i>=0; i-=4) {
    display.drawLine(0, display.height()-1, display.width()-1, i, SH110X_WHITE);
    display.display();
    delay(1);
  }
  delay(250);
  display.clearDisplay();
  for(i=display.width()-1; i>=0; i-=4) {
    display.drawLine(display.width()-1, display.height()-1, i, 0, SH110X_WHITE);
    display.display();
    delay(1);
  }
  for(i=display.height()-1; i>=0; i-=4) {
    display.drawLine(display.width()-1, display.height()-1, 0, i, SH110X_WHITE);
    display.display();
    delay(1);
  }
  delay(250);
  display.clearDisplay();
  for(i=0; i<display.height(); i+=4) {
    display.drawLine(display.width()-1, 0, 0, i, SH110X_WHITE);
    display.display();
    delay(1);
  }
  for(i=0; i<display.width(); i+=4) {
    display.drawLine(display.width()-1, 0, i, display.height()-1, SH110X_WHITE);
    display.display();
    delay(1);
  }
  delay(2000); 
}

void testdrawrect(void) {
  display.clearDisplay();
  for(int16_t i=0; i<display.height()/2; i+=2) {
    display.drawRect(i, i, display.width()-2*i, display.height()-2*i, SH110X_WHITE);
    display.display(); 
    delay(1);
  }
  delay(2000);
}

void testfillrect(void) {
  display.clearDisplay();
  for(int16_t i=0; i<display.height()/2; i+=3) {
    display.fillRect(i, i, display.width()-i*2, display.height()-i*2, SH110X_INVERSE);
    display.display(); 
    delay(1);
  }
  delay(2000);
}

void testdrawcircle(void) {
  display.clearDisplay();
  for(int16_t i=0; i<max(display.width(),display.height())/2; i+=2) {
    display.drawCircle(display.width()/2, display.height()/2, i, SH110X_WHITE);
    display.display();
    delay(1);
  }
  delay(2000);
}

void testfillcircle(void) {
  display.clearDisplay();
  for(int16_t i=max(display.width(),display.height())/2; i>0; i-=3) {
    display.fillCircle(display.width() / 2, display.height() / 2, i, SH110X_INVERSE);
    display.display(); 
    delay(1);
  }
  delay(2000);
}

void testdrawroundrect(void) {
  display.clearDisplay();
  for(int16_t i=0; i<display.height()/2-2; i+=2) {
    display.drawRoundRect(i, i, display.width()-2*i, display.height()-2*i,
      display.height()/4, SH110X_WHITE);
    display.display();
    delay(1);
  }
  delay(2000);
}

void testfillroundrect(void) {
  display.clearDisplay();
  for(int16_t i=0; i<display.height()/2-2; i+=2) {
    display.fillRoundRect(i, i, display.width()-2*i, display.height()-2*i,
      display.height()/4, SH110X_INVERSE);
    display.display();
    delay(1);
  }
  delay(2000);
}

void testdrawtriangle(void) {
  display.clearDisplay();
  for(int16_t i=0; i<max(display.width(),display.height())/2; i+=5) {
    display.drawTriangle(
      display.width()/2  , display.height()/2-i,
      display.width()/2-i, display.height()/2+i,
      display.width()/2+i, display.height()/2+i, SH110X_WHITE);
    display.display();
    delay(1);
  }
  delay(2000);
}

void testfilltriangle(void) {
  display.clearDisplay();
  for(int16_t i=max(display.width(),display.height())/2; i>0; i-=5) {
    display.fillTriangle(
      display.width()/2  , display.height()/2-i,
      display.width()/2-i, display.height()/2+i,
      display.width()/2+i, display.height()/2+i, SH110X_INVERSE);
    display.display();
    delay(1);
  }
  delay(2000);
}

void testdrawchar(void) {
  display.clearDisplay();
  display.setTextSize(1);      
  display.setTextColor(SH110X_WHITE); 
  display.setCursor(0, 0);     
  display.cp437(true);         

  for(int16_t i=0; i<256; i++) {
    if(i == '\n') display.write(' ');
    else          display.write(i);
  }
  display.display();
  delay(2000);
}

void testdrawstyles(void) {
  display.clearDisplay();
  display.setTextSize(1);             
  display.setTextColor(SH110X_WHITE);        
  display.setCursor(0,0);             
  display.println(F("Hello, world!"));

  display.setTextColor(SH110X_BLACK, SH110X_WHITE); 
  display.println(3.141592);

  display.setTextSize(2);             
  display.setTextColor(SH110X_WHITE);
  display.print(F("0x")); display.println(0xDEADBEEF, HEX);

  display.display();
  delay(2000);
}

void testscrolltext(void) {
  // Fungsi scroll bawaan (startscrollright dll) umumnya 
  // tidak di-support oleh library SH110X seperti pada SSD1306.
  // Jadi tes ini hanya akan menampilkan teks diam.
  display.clearDisplay();
  display.setTextSize(2); 
  display.setTextColor(SH110X_WHITE);
  display.setCursor(10, 0);
  display.println(F("OLED 1.3"));
  display.display();      
  delay(2000);
}

void testdrawbitmap(void) {
  display.clearDisplay();
  display.drawBitmap(
    (display.width()  - LOGO_WIDTH ) / 2,
    (display.height() - LOGO_HEIGHT) / 2,
    logo_bmp, LOGO_WIDTH, LOGO_HEIGHT, 1);
  display.display();
  delay(1000);
}

#define XPOS   0 
#define YPOS   1
#define DELTAY 2

void testanimate(const uint8_t *bitmap, uint8_t w, uint8_t h) {
  int8_t f, icons[NUMFLAKES][3];

  for(f=0; f< NUMFLAKES; f++) {
    icons[f][XPOS]   = random(1 - LOGO_WIDTH, display.width());
    icons[f][YPOS]   = -LOGO_HEIGHT;
    icons[f][DELTAY] = random(1, 6);
  }

  for(;;) { 
    display.clearDisplay(); 
    for(f=0; f< NUMFLAKES; f++) {
      display.drawBitmap(icons[f][XPOS], icons[f][YPOS], bitmap, w, h, SH110X_WHITE);
    }
    display.display(); 
    delay(200);        

    for(f=0; f< NUMFLAKES; f++) {
      icons[f][YPOS] += icons[f][DELTAY];
      if (icons[f][YPOS] >= display.height()) {
        icons[f][XPOS]   = random(1 - LOGO_WIDTH, display.width());
        icons[f][YPOS]   = -LOGO_HEIGHT;
        icons[f][DELTAY] = random(1, 6);
      }
    }
  }
}    
