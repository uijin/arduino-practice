# Arduino Practice

在完成 Coursera 課程[《Generative AI for Software Development》](https://www.coursera.org/professional-certificates/generative-ai-for-software-development)學習之後，想找一個平常鮮少接觸的程式領域來實際練習 AI 使用技巧，幾經考量，選擇練習的領域是結合微控制器（MCU）與感測模組的 IoT 應用系統。感測模組使用範例在網路上很常見，這些範例應該都包含在 AI 模型訓練素材裡面，不難用 AI 模型產生程式碼；簡單的 IoT 應用系統，例如如溫度感測記錄器，需要編寫的程式數量不多，會比較容易掌握整體系統運作方式，讀懂產生的程式碼。

剛好，工具箱裡面有幾片塵封已久的開發板，有 Arduino 系統也有 Raspberry Pi。另外還找到超音波距離感測模組，藍牙傳輸模組，伺服馬達，步進馬達等等。捲起袖子動手做吧！



## 示範專案

以下是開發告一段落的實驗專案，當中的內容較為完整：
- Solar_Tracker
- Pixel_Campfire
- Sunlight_Meter_V2
- Sunlight_Meter_V1
- Rangefinder_Ultrasonic

還在進行中的專案：
- Pixel_Show
- Solar_Charger_V1
- Solar_Tracker_ESP8266

另外有些較簡單的 Chedker 專案，用來檢查電子模組運作正常與否。產生這類專案的代碼，對 2025 年 AI 模型而言輕而易舉，推測 AI 在網路上學了很多類似的專案。
- WS2812B_LED_Checker
- ESP32_WiFi_Checker
- OLED_Display_Checker
