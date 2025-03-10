#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>

// 定義OLED模組選擇
// 0: MN096-12864-B-4G (SSD1306)
// 1: SH1106
// 2: GME-12864-49
#define OLED_MODULE 2

// 根據選擇創建相應的OLED對象
#if OLED_MODULE == 0
  // MN096-12864-B-4G模組 (SSD1315控制器)
  U8G2_SSD1315_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);
  #define OLED_NAME "MN096"
  #define CONTRAST_VALUE 128
#elif OLED_MODULE == 1
  // SH1106模組
  U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);
  #define OLED_NAME "SH1106"
  #define CONTRAST_VALUE 255 // 通常不需要調整
#else
  // GME-12864-49模組 (通常使用SSD1306控制器)
  U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);
  #define OLED_NAME "GME-12864"
  #define CONTRAST_VALUE 128
  
  // 如果GME-12864-49不使用SSD1306控制器，可以取消註釋以下替代選項:
  
  // 如果使用SSD1306控制器:
  // U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);
  
  // 如果使用SH1106控制器:
  // U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);
#endif

// I2C地址可能需要調整
// 如果默認地址無法工作，可以嘗試其他常見地址如0x3C或0x3D
// 例如:
// U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE, /* clock=*/ SCL, /* data=*/ SDA);

void setup(void) {
  Serial.begin(9600);
  Serial.print("OLED Test: ");
  Serial.println(OLED_NAME);
  
  // 嘗試檢測I2C設備
  Wire.begin();
  Serial.println("Scanning for I2C devices...");
  byte error, address;
  int deviceCount = 0;
  
  for (address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();
    
    if (error == 0) {
      Serial.print("I2C device found at address 0x");
      if (address < 16) Serial.print("0");
      Serial.print(address, HEX);
      Serial.println(" !");
      deviceCount++;
    }
  }
  
  if (deviceCount == 0) {
    Serial.println("No I2C devices found - check connections");
  }
  
  // 初始化OLED顯示器
  if (!u8g2.begin()) {
    Serial.println("OLED initialization failed");
    while (1); // 初始化失敗時停止
  }
  
  Serial.println("OLED initialized successfully");
  u8g2.setContrast(CONTRAST_VALUE); // 設置顯示對比度
  delay(1000);
}

void drawLogo(void) {
  u8g2.setFont(u8g2_font_ncenB14_tr);
  u8g2.drawStr(0, 15, "Arduino");
  
  // 根據模塊名稱調整位置以確保適合屏幕
  int nameWidth = u8g2.getStrWidth(OLED_NAME);
  int x = (128 - nameWidth) / 2;  // 居中顯示名稱
  u8g2.drawStr(x, 45, OLED_NAME);
}

void drawText(void) {
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(0, 10, "Temperature:");
  u8g2.drawStr(0, 30, "Humidity:");
  u8g2.drawStr(0, 50, "Module:");
  
  // 示例值
  u8g2.drawStr(70, 10, "25°C");
  u8g2.drawStr(70, 30, "60%");
  u8g2.drawStr(50, 50, OLED_NAME);
}

void drawGraphics(void) {
  u8g2.drawFrame(0,0,128,64);      // 框架
  u8g2.drawBox(10,10,20,20);       // 填充方框
  u8g2.drawCircle(64, 32, 10);     // 圓形
  u8g2.drawLine(0, 0, 127, 63);    // 對角線
  
  // 繪製像素圖案來測試顯示分辨率
  for(int y=0; y<64; y+=4) {
    for(int x=0; x<128; x+=4) {
      if((x+y) % 8 == 0) {
        u8g2.drawPixel(x, y);
      }
    }
  }
}

void drawScrollText() {
  static int scrollPos = 128;
  const char *message = "Testing OLED module - ";
  int msgWidth = u8g2.getStrWidth(message) + u8g2.getStrWidth(OLED_NAME);
  
  u8g2.setFont(u8g2_font_ncenB10_tr);
  u8g2.drawStr(scrollPos, 35, message);
  u8g2.drawStr(scrollPos + u8g2.getStrWidth(message), 35, OLED_NAME);
  
  scrollPos -= 2;
  if (scrollPos < -msgWidth) {
    scrollPos = 128;
  }
}

void loop(void) {
  // 顯示標誌2秒
  u8g2.clearBuffer();
  drawLogo();
  u8g2.sendBuffer();
  delay(2000);
  
  // 顯示文本2秒
  u8g2.clearBuffer();
  drawText();
  u8g2.sendBuffer();
  delay(2000);
  
  // 顯示圖形2秒
  u8g2.clearBuffer();
  drawGraphics();
  u8g2.sendBuffer();
  delay(2000);
  
  // 顯示滾動文本5秒
  unsigned long startTime = millis();
  while (millis() - startTime < 5000) {
    u8g2.clearBuffer();
    drawScrollText();
    u8g2.sendBuffer();
    delay(50);  // 控制滾動速度
  }
  
  // 顯示測試確認信息2秒
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB10_tr);
  char testMsg[32];
  sprintf(testMsg, "%s Test OK!", OLED_NAME);
  u8g2.drawStr((128 - u8g2.getStrWidth(testMsg))/2, 35, testMsg);  // 居中顯示
  u8g2.sendBuffer();
  delay(2000);
}