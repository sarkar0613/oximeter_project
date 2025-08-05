# 指尖脈搏血氧儀 (ESP32 + MAX30102 + LCD)

本專案為嵌入式系統課程期末專題，使用 MAX30102 感測器搭配 ESP32 與 LCD 顯示器 (I2C)，即時偵測與顯示心跳與血氧值，並具備顯示器與蜂鳴器提示

## Hardware
- ESP32 開發板
- MAX30102 血氧感測模組
- LCD 16*2 顯示器 (I2C)
- 有源蜂鳴器
- LED
- 杜邦線、麵包板

## Library
- SparkFun MAX30105 Sensor Library
- LiquidCrystal_I2C


## 使用方式
1. 安裝必要函式庫於 Arduino IDE。
2. 將 `oximeter_main.ino` 上傳至 ESP32。
3. 觀察 OLED 顯示與 LED/蜂鳴器反應。
