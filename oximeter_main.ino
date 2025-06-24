#include <Wire.h>
#include "MAX30105.h"
#include "spo2_algorithm.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <cmath>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

MAX30105 particleSensor;

#if defined(__AVR_ATmega328P__) || defined(__AVR_ATmega168__)
uint16_t irBuffer[100];
uint16_t redBuffer[100];
#else
uint32_t irBuffer[100];
uint32_t redBuffer[100];
#endif

int32_t bufferLength;
int32_t spo2;
int8_t validSPO2;
int32_t heartRate;
int8_t validHeartRate;

byte pulseLED = 11;
byte readLED = 13;
byte buzzerPin = 18;

void setup() {
  Wire.begin(20, 21);
  Serial.begin(115200);
  pinMode(buzzerPin, OUTPUT);
  pinMode(pulseLED, OUTPUT);
  pinMode(readLED, OUTPUT);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    while (1);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Hello, this is");
  display.println("my Oximeter");
  display.display();
  delay(2000);
  display.clearDisplay();

  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println(F("MAX30105 was not found. Please check wiring/power."));
    while (1);
  }

  byte ledBrightness = 60;
  byte sampleAverage = 4;
  byte ledMode = 2;
  byte sampleRate = 100;
  int pulseWidth = 411;
  int adcRange = 4096;
  particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange);

  Serial.println(F("Attach sensor to finger with rubber band. Press any key to start conversion"));
  while (Serial.available() == 0);
  Serial.read();
}

void loop() {
  bufferLength = 100;

  for (byte i = 0; i < bufferLength; i++) {
    while (!particleSensor.available()) particleSensor.check();
    redBuffer[i] = particleSensor.getRed();
    irBuffer[i] = particleSensor.getIR();
    particleSensor.nextSample();
  }

  maxim_heart_rate_and_oxygen_saturation(irBuffer, bufferLength, redBuffer, &spo2, &validSPO2, &heartRate, &validHeartRate);

  while (1) {
    for (byte i = 25; i < 100; i++) {
      redBuffer[i - 25] = redBuffer[i];
      irBuffer[i - 25] = irBuffer[i];
    }

    for (byte i = 75; i < 100; i++) {
      while (!particleSensor.available()) particleSensor.check();
      redBuffer[i] = particleSensor.getRed();
      irBuffer[i] = particleSensor.getIR();
      particleSensor.nextSample();
    }

    maxim_heart_rate_and_oxygen_saturation(irBuffer, bufferLength, redBuffer, &spo2, &validSPO2, &heartRate, &validHeartRate);

    if (irBuffer[0] > 5000) {
      digitalWrite(readLED, HIGH);
    } else {
      digitalWrite(readLED, millis() % 1000 < 500 ? HIGH : LOW);
    }

    if (redBuffer[0] < 5000 || irBuffer[0] < 5000) {
      display.clearDisplay();
      display.setCursor(0, 0);
      display.println("Plz put finger");
      display.println("on sensor...");
      display.display();
      delay(1000);
    } else if (spo2 > 80) {
      digitalWrite(buzzerPin, HIGH);
      delay(100);
      digitalWrite(buzzerPin, LOW);
      display.clearDisplay();
      display.setCursor(0, 0);
      display.print("HR: ");
      display.println(heartRate);
      display.print("SpO2: ");
      display.println(spo2);
      display.display();
      delay(2000);
      if (spo2 < 90) {
        digitalWrite(buzzerPin, HIGH);
        delay(2000);
        digitalWrite(buzzerPin, LOW);
      }
    } else {
      display.clearDisplay();
      display.setCursor(0, 0);
      display.println("Still measuring...");
      display.println("Try move finger.");
      display.display();
      delay(1000);
    }
  }
}
