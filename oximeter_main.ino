#include <Wire.h>
#include "MAX30105.h"
#include "spo2_algorithm.h"
#include <LiquidCrystal_I2C.h>
#include <cmath>

// 硬體配置
#define LCD_ADDRESS 0x27
#define LCD_COLUMNS 16
#define LCD_ROWS 2
#define BUZZER_PIN 18
#define PULSE_LED_PIN 11
#define STATUS_LED_PIN 13

// 系統參數
#define BUFFER_SIZE 100
#define FINGER_THRESHOLD 50000  // 提高手指檢測閾值
#define DISPLAY_UPDATE_INTERVAL 1000  // LCD更新間隔 (毫秒)
#define MEASUREMENT_SAMPLES 25  // 每次更新的樣本數

// 建立物件
LiquidCrystal_I2C lcd(LCD_ADDRESS, LCD_COLUMNS, LCD_ROWS);
MAX30105 particleSensor;

// 數據緩衝區
#if defined(__AVR_ATmega328P__) || defined(__AVR_ATmega168__)
uint16_t irBuffer[BUFFER_SIZE];
uint16_t redBuffer[BUFFER_SIZE];
#else
uint32_t irBuffer[BUFFER_SIZE];
uint32_t redBuffer[BUFFER_SIZE];
#endif

// 測量變數
int32_t bufferLength;
int32_t spo2;
int8_t validSPO2;
int32_t heartRate;
int8_t validHeartRate;

// 狀態管理
unsigned long lastDisplayUpdate = 0;
unsigned long lastPulseUpdate = 0;
bool fingerDetected = false;
bool systemReady = false;

// 系統狀態枚舉
enum SystemState {
  STATE_INITIALIZING,
  STATE_WAITING_FINGER,
  STATE_MEASURING,
  STATE_DISPLAYING_RESULTS,
  STATE_ERROR
};

SystemState currentState = STATE_INITIALIZING;

void setup() {
  // 初始化序列埠
  Serial.begin(115200);
  Serial.println("=== 脈搏血氧儀啟動 ===");
  
  // 初始化I2C通訊
  Wire.begin(20, 21);  // ESP32: SDA=20, SCL=21
  
  // 初始化GPIO
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(PULSE_LED_PIN, OUTPUT);
  pinMode(STATUS_LED_PIN, OUTPUT);
  
  // 確保蜂鳴器關閉
  digitalWrite(BUZZER_PIN, LOW);
  
  // 初始化LCD
  if (!initializeLCD()) {
    Serial.println("LCD 初始化失敗!");
    currentState = STATE_ERROR;
    return;
  }
  
  // 初始化MAX30105感測器
  if (!initializeSensor()) {
    Serial.println("MAX30105 感測器初始化失敗!");
    displayError("Sensor Error!");
    currentState = STATE_ERROR;
    return;
  }
  
  // 顯示啟動完成訊息
  displayStartupComplete();
  currentState = STATE_WAITING_FINGER;
  systemReady = true;
  
  Serial.println("系統初始化完成，等待手指放置...");
}

void loop() {
  // 系統錯誤，閃爍LED
  if (!systemReady) {
    digitalWrite(STATUS_LED_PIN, millis() % 500 < 250);
    return;
  }
  
  switch (currentState) {
    case STATE_WAITING_FINGER:
      handleWaitingState();
      break;
      
    case STATE_MEASURING:
      handleMeasuringState();
      break;
      
    case STATE_DISPLAYING_RESULTS:
      handleDisplayState();
      break;
      
    case STATE_ERROR:
      handleErrorState();
      break;
  }
  
  updateStatusLED();
  delay(50);  // 減少CPU負載
}

bool initializeLCD() {
  lcd.init();
  lcd.clear();
  lcd.backlight();
  
  // 測試LCD是否正常工作
  lcd.setCursor(0, 0);
  lcd.print("Pulse Oximeter");
  lcd.setCursor(0, 1);
  lcd.print("Initializing...");
  delay(2000);
  
  return true; 
}

bool initializeSensor() {
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    return false;
  }
  
  // 優化的感測器配置
  byte ledBrightness = 60;    // LED亮度 (0-255)
  byte sampleAverage = 4;     // 樣本平均 (1,2,4,8,16,32)
  byte ledMode = 2;           // 模式: 1=紅光, 2=紅光+紅外光, 3=紅光+紅外光+綠光
  byte sampleRate = 100;      // 取樣率 (50,100,200,400,800,1000,1600,3200)
  int pulseWidth = 411;       // 脈衝寬度 (69,118,215,411)
  int adcRange = 4096;        // ADC範圍 (2048,4096,8192,16384)
  
  particleSensor.setup(ledBrightness, sampleAverage, ledMode, 
                      sampleRate, pulseWidth, adcRange);
  
  return true;
}

void displayStartupComplete() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("System Ready!");
  lcd.setCursor(0, 1);
  lcd.print("Place finger...");
  delay(1500);
}

void displayError(const char* message) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("ERROR:");
  lcd.setCursor(0, 1);
  lcd.print(message);
}

void handleWaitingState() {
  // 檢查是否有手指放置
  if (particleSensor.available()) {
    uint32_t currentIR = particleSensor.getIR();
    uint32_t currentRed = particleSensor.getRed();
    
    if (currentIR > FINGER_THRESHOLD && currentRed > FINGER_THRESHOLD) {
      fingerDetected = true;
      currentState = STATE_MEASURING;
      
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Finger detected");
      lcd.setCursor(0, 1);
      lcd.print("Collecting data");
      
      // 開始收集初始數據
      collectInitialData();
      return;
    }
    particleSensor.nextSample();
  }
  
  // 更新等待顯示
  if (millis() - lastDisplayUpdate > DISPLAY_UPDATE_INTERVAL) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Place finger on");
    lcd.setCursor(0, 1);
    lcd.print("sensor...");
    lastDisplayUpdate = millis();
  }
}

void handleMeasuringState() {
  // 更新數據緩衝區
  updateDataBuffers();
  
  // 重新計算生理參數
  maxim_heart_rate_and_oxygen_saturation(irBuffer, bufferLength, redBuffer, 
                                        &spo2, &validSPO2, &heartRate, &validHeartRate);
  
  // 檢查手指是否仍在感測器上
  if (irBuffer[BUFFER_SIZE-1] < FINGER_THRESHOLD || redBuffer[BUFFER_SIZE-1] < FINGER_THRESHOLD) {
    fingerDetected = false;
    currentState = STATE_WAITING_FINGER;
    return;
  }
  
  // 檢查是否有有效的測量結果
  if (validSPO2 && validHeartRate && spo2 > 70 && heartRate > 30 && heartRate < 200) {
    currentState = STATE_DISPLAYING_RESULTS;
  }
  
  // 更新測量中的顯示
  if (millis() - lastDisplayUpdate > DISPLAY_UPDATE_INTERVAL) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Measuring...");
    lcd.setCursor(0, 1);
    
    // 顯示信號強度
    uint32_t signalStrength = irBuffer[BUFFER_SIZE-1];
    if (signalStrength > FINGER_THRESHOLD * 3) {
      lcd.print("Signal: Strong");
    } else if (signalStrength > FINGER_THRESHOLD * 2) {
      lcd.print("Signal: Good");
    } else {
      lcd.print("Signal: Weak");
    }
    
    lastDisplayUpdate = millis();
  }
}

void handleDisplayState() {
  // 更新數據
  updateDataBuffers();
  maxim_heart_rate_and_oxygen_saturation(irBuffer, bufferLength, redBuffer, 
                                        &spo2, &validSPO2, &heartRate, &validHeartRate);
  
  // 檢查手指是否移除
  if (irBuffer[BUFFER_SIZE-1] < FINGER_THRESHOLD || redBuffer[BUFFER_SIZE-1] < FINGER_THRESHOLD) {
    fingerDetected = false;
    currentState = STATE_WAITING_FINGER;
    return;
  }
  
  // 更新顯示
  if (millis() - lastDisplayUpdate > DISPLAY_UPDATE_INTERVAL) {
    displayMeasurements();
    lastDisplayUpdate = millis();
  }
  
  // 處理警報
  handleAlerts();
  
}

void handleErrorState() {
  // 錯誤狀態處理
  if (millis() - lastDisplayUpdate > 2000) {
    displayError("System Error");
    lastDisplayUpdate = millis();
  }
}

void collectInitialData() {
  Serial.println("開始收集初始數據...");
  bufferLength = BUFFER_SIZE;
  
  for (byte i = 0; i < bufferLength; i++) {
    while (!particleSensor.available()) {
      particleSensor.check();
    }
    
    redBuffer[i] = particleSensor.getRed();
    irBuffer[i] = particleSensor.getIR();
    particleSensor.nextSample();
    
    // 每20個樣本更新一次進度
    if (i % 20 == 0) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Collecting...");
      lcd.setCursor(0, 1);
      lcd.print("Progress: ");
      lcd.print((i * 100) / bufferLength);
      lcd.print("%");
    }
  }
  
  // 計算初始值
  maxim_heart_rate_and_oxygen_saturation(irBuffer, bufferLength, redBuffer, 
                                        &spo2, &validSPO2, &heartRate, &validHeartRate);
  
  Serial.println("初始數據收集完成");
}

void updateDataBuffers() {
  // 移動舊數據
  for (byte i = MEASUREMENT_SAMPLES; i < BUFFER_SIZE; i++) {
    redBuffer[i - MEASUREMENT_SAMPLES] = redBuffer[i];
    irBuffer[i - MEASUREMENT_SAMPLES] = irBuffer[i];
  }
  
  // 添加新數據
  for (byte i = BUFFER_SIZE - MEASUREMENT_SAMPLES; i < BUFFER_SIZE; i++) {
    while (!particleSensor.available()) {
      particleSensor.check();
    }
    
    redBuffer[i] = particleSensor.getRed();
    irBuffer[i] = particleSensor.getIR();
    particleSensor.nextSample();
  }
}

void displayMeasurements() {
  lcd.clear();
  
  // 第一行顯示心率
  lcd.setCursor(0, 0);
  lcd.print("HR:");
  lcd.setCursor(4, 0);
  if (validHeartRate && heartRate > 0) {
    lcd.print(heartRate);
    lcd.print(" BPM");
  } else {
    lcd.print("---");
  }
  
  // 第二行顯示血氧和狀態
  lcd.setCursor(0, 1);
  lcd.print("SpO2:");
  lcd.setCursor(6, 1);
  if (validSPO2 && spo2 > 0) {
    lcd.print(spo2);
    lcd.print("%");
    
    // 顯示狀態指示
    lcd.setCursor(12, 1);
    if (spo2 >= 95) {
      lcd.print("OK");
    } else if (spo2 >= 90) {
      lcd.print("LO");
    } else {
      lcd.print("!!!");
    }
  } else {
    lcd.print("---%");
  }
}

void handleAlerts() {
  // 只在有有效數據時處理警報
  if (!validSPO2 || !fingerDetected) {
    return;
  }
  
  static unsigned long lastAlertTime = 0;
  unsigned long currentTime = millis();
  
  // 防止警報過於頻繁
  if (currentTime - lastAlertTime < 3000) {
    return;
  }
  
  // 血氧過低警報
  if (spo2 < 90 && spo2 > 80) {
    // 雙短音警報
    digitalWrite(BUZZER_PIN, HIGH);
    delay(200);
    digitalWrite(BUZZER_PIN, LOW);
    delay(300);
    digitalWrite(BUZZER_PIN, HIGH);
    delay(200);
    digitalWrite(BUZZER_PIN, LOW);
    lastAlertTime = currentTime;
  } else if (spo2 <= 80 && spo2 > 0) {
    // 緊急警報 - 長音
    digitalWrite(BUZZER_PIN, HIGH);
    delay(1000);
    digitalWrite(BUZZER_PIN, LOW);
    lastAlertTime = currentTime;
  }
  
  // 心率異常警報
  if (validHeartRate && (heartRate > 120 || heartRate < 50)) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(100);
    digitalWrite(BUZZER_PIN, LOW);
  }
}



void updateStatusLED() {
  switch (currentState) {
    case STATE_WAITING_FINGER:
      // 慢閃表示等待
      digitalWrite(STATUS_LED_PIN, (millis() % 1000) < 500);
      break;
      
    case STATE_MEASURING:
      // 快閃表示測量中
      digitalWrite(STATUS_LED_PIN, (millis() % 200) < 100);
      break;
      
    case STATE_DISPLAYING_RESULTS:
      // 常亮表示正常顯示
      digitalWrite(STATUS_LED_PIN, HIGH);
      break;
      
    case STATE_ERROR:
      // 非常快閃表示錯誤
      digitalWrite(STATUS_LED_PIN, (millis() % 100) < 50);
      break;
  }
}
