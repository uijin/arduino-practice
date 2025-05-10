#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <U8g2lib.h>
#include <Wire.h>

// ===== 重要：在這裡選擇設備角色 =====
// 取消註釋其中一個，並註釋另一個
#define DEVICE_ROLE_SENDER     // 取消此行註釋使設備成為發送端
//#define DEVICE_ROLE_RECEIVER // 取消此行註釋使設備成為接收端

// 檢查是否正確定義了角色
#if defined(DEVICE_ROLE_SENDER) && defined(DEVICE_ROLE_RECEIVER)
  #error "不能同時定義DEVICE_ROLE_SENDER和DEVICE_ROLE_RECEIVER"
#elif !defined(DEVICE_ROLE_SENDER) && !defined(DEVICE_ROLE_RECEIVER)
  #error "必須定義DEVICE_ROLE_SENDER或DEVICE_ROLE_RECEIVER其中之一"
#endif

// OLED 顯示模組設置 (MN096-12864-B-4G)
// 使用 U8g2lib 創建顯示對象
// 請根據您的連接方式選擇正確的構造函數
#ifdef DEVICE_ROLE_RECEIVER
// 下面是基於I2C連接的設置，如果您使用SPI請調整構造函數
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);
// 如果使用SPI連接，請用以下設置（調整引腳）:
// U8G2_SSD1306_128X64_NONAME_F_4W_SW_SPI u8g2(U8G2_R0, /* clock=*/ 13, /* data=*/ 11, /* cs=*/ 10, /* dc=*/ 9, /* reset=*/ 8);
#endif

// 定義類比搖桿的引腳
#define JOYSTICK_X_PIN A0     // 類比搖桿X軸
#define JOYSTICK_Y_PIN A1     // 類比搖桿Y軸
#define JOYSTICK_BUTTON_PIN D2 // 搖桿按鈕連接到D2

// **重要** 在發送端模式，設置接收端的MAC地址（從接收端的Serial輸出獲取）
// 示例格式: {0x24, 0x0A, 0xC4, 0x9A, 0x58, 0x28}
uint8_t receiverMacAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}; // 替換為接收端的實際MAC地址

// ESP-NOW通信參數
#define ESP_NOW_CHANNEL 1      // 設置ESP-NOW頻道 (1-14)，兩端必須相同
#define ESP_NOW_ENCRYPT false  // 是否加密傳輸
#define MAX_RETRY_COUNT 3      // 發送失敗時的最大重試次數

#ifdef DEVICE_ROLE_SENDER
int retryCount = 0;            // 當前重試計數
bool lastSendSuccess = false;  // 上次發送是否成功
#endif

// 搖桿校正值
#define JOYSTICK_CENTER_X 778  // X軸中心位置
#define JOYSTICK_CENTER_Y 762  // Y軸中心位置
#define JOYSTICK_MAX 4095      // ESP32S3的12位ADC最大值
#define JOYSTICK_DEADZONE 50   // 死區，忽略小幅度移動

// 定義數據結構
typedef struct joystick_message {
  int x_value;         // 原始X值
  int y_value;         // 原始Y值
  int x_normalized;    // 正規化後的X值 (-100 到 100)
  int y_normalized;    // 正規化後的Y值 (-100 到 100)
  bool button_state;   // 按鈕狀態
  uint32_t msg_id;     // 消息ID，用於追蹤
} joystick_message;

// 創建數據結構實例
joystick_message joystickData;
#ifdef DEVICE_ROLE_SENDER
uint32_t messageCounter = 0;  // 消息計數器
#endif

#ifdef DEVICE_ROLE_RECEIVER
// OLED顯示相關變數
unsigned long lastOLEDUpdateTime = 0;  // 上次更新OLED的時間
const int oledUpdateInterval = 100;     // OLED更新頻率 (毫秒)
unsigned long lastDataReceivedTime = 0; // 上次接收數據的時間
const unsigned long dataTimeout = 3000; // 數據接收超時時間 (毫秒)
bool dataReceived = false;             // 是否有接收到數據
char positionText[20] = "Position: None"; // 位置文本
#endif

// ESP-NOW錯誤信息映射
const char* getESPErrorMsg(esp_err_t error) {
  switch (error) {
    case ESP_OK: return "Success";
    case ESP_ERR_ESPNOW_NOT_INIT: return "ESPNOW not initialized";
    case ESP_ERR_ESPNOW_ARG: return "Invalid argument";
    case ESP_ERR_ESPNOW_INTERNAL: return "Internal error";
    case ESP_ERR_ESPNOW_NO_MEM: return "Out of memory";
    case ESP_ERR_ESPNOW_NOT_FOUND: return "Peer not found";
    case ESP_ERR_ESPNOW_IF: return "Interface error";
    default: return "Unknown error";
  }
}

// 重新初始化ESP-NOW
bool reinitESPNow() {
  Serial.println("Reinitializing ESP-NOW...");
  
  // 首先釋放ESP-NOW資源
  esp_now_deinit();
  delay(100);
  
  // 重新初始化WiFi
  WiFi.disconnect();
  delay(50);
  WiFi.mode(WIFI_STA);
  delay(50);
  
  // 設置WiFi頻道
  WiFi.channel(ESP_NOW_CHANNEL);
  delay(50);
  
  // 重新初始化ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return false;
  }
  
  #ifdef DEVICE_ROLE_SENDER
  // 發送端初始化
  esp_now_register_send_cb(OnDataSent);
  
  // 註冊對等設備
  esp_now_peer_info_t peerInfo;
  memset(&peerInfo, 0, sizeof(peerInfo));
  memcpy(peerInfo.peer_addr, receiverMacAddress, 6);
  peerInfo.channel = ESP_NOW_CHANNEL;  
  peerInfo.encrypt = ESP_NOW_ENCRYPT;
  
  // 添加對等設備
  esp_err_t addStatus = esp_now_add_peer(&peerInfo);
  if (addStatus != ESP_OK) {
    Serial.print("Failed to add peer: ");
    Serial.println(getESPErrorMsg(addStatus));
    return false;
  }
  
  Serial.println("ESP-NOW reinitialized and peer added successfully");
  #else
  // 接收端初始化
  esp_now_register_recv_cb(OnDataRecv);
  Serial.println("ESP-NOW reinitialized, receiver ready");
  #endif
  
  return true;
}

#ifdef DEVICE_ROLE_SENDER
// 發送狀態回調函數 - 僅發送端使用
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("Last Packet Send Status: ");
  if (status == ESP_NOW_SEND_SUCCESS) {
    Serial.println("Delivery Success");
    lastSendSuccess = true;
    retryCount = 0; // 成功後重置重試計數
  } else {
    Serial.println("Delivery Failed");
    lastSendSuccess = false;
  }
}

// 將搖桿值正規化到-100到100之間
int normalizeJoystick(int value, int center) {
  int result = 0;
  
  // 計算偏移量
  int offset = value - center;
  
  // 應用死區
  if (abs(offset) < JOYSTICK_DEADZONE) {
    return 0;
  }
  
  // 正規化到-100到100
  if (offset > 0) {
    // 右/下方向
    result = map(offset, JOYSTICK_DEADZONE, JOYSTICK_MAX - center, 0, 100);
  } else {
    // 左/上方向
    result = map(offset, -center, -JOYSTICK_DEADZONE, -100, 0);
  }
  
  // 確保值在-100到100之間
  result = constrain(result, -100, 100);
  
  return result;
}

// 發送數據的函數，帶有重試機制
bool sendESPNowData(const uint8_t* data, size_t len) {
  esp_err_t result = esp_now_send(receiverMacAddress, data, len);
  
  if (result != ESP_OK) {
    Serial.print("Error sending the data: ");
    Serial.println(getESPErrorMsg(result));
    
    // 如果因為對等點未找到或其他錯誤，嘗試重新初始化
    if (result == ESP_ERR_ESPNOW_NOT_FOUND || result == ESP_ERR_ESPNOW_IF) {
      Serial.println("Trying to reinitialize ESP-NOW...");
      if (reinitESPNow()) {
        // 重新嘗試發送
        result = esp_now_send(receiverMacAddress, data, len);
        if (result == ESP_OK) {
          Serial.println("Data sent after reinitialization");
          return true;
        } else {
          Serial.print("Still failed after reinitialization: ");
          Serial.println(getESPErrorMsg(result));
        }
      }
    }
    return false;
  }
  return true;
}
#endif

#ifdef DEVICE_ROLE_RECEIVER
// 獲取位置描述
void updatePositionText() {
  if (!dataReceived || (millis() - lastDataReceivedTime > dataTimeout)) {
    strcpy(positionText, "Position: None");
    return;
  }
  
  if (abs(joystickData.x_normalized) < 10 && abs(joystickData.y_normalized) < 10) {
    strcpy(positionText, "Position: Center");
  } else {
    strcpy(positionText, "Position: ");
    
    if (joystickData.y_normalized < -30) strcat(positionText, "Up");
    else if (joystickData.y_normalized > 30) strcat(positionText, "Down");
    
    if (joystickData.x_normalized < -30) {
      if (strlen(positionText) > 10) strcat(positionText, " ");
      strcat(positionText, "Left");
    }
    else if (joystickData.x_normalized > 30) {
      if (strlen(positionText) > 10) strcat(positionText, " ");
      strcat(positionText, "Right");
    }
  }
}

// 更新OLED顯示
void updateOLEDDisplay() {
  u8g2.clearBuffer();
  
  // 設置字體
  u8g2.setFont(u8g2_font_6x12_tr);
  
  // 標題
  u8g2.drawStr(0, 10, "ESP32 Joystick Receiver");
  u8g2.drawLine(0, 12, 128, 12);
  
  // 檢查是否有數據以及數據是否超時
  if (!dataReceived || (millis() - lastDataReceivedTime > dataTimeout)) {
    u8g2.drawStr(0, 30, "Waiting for data...");
  } else {
    // 原始值
    char buffer[32];
    sprintf(buffer, "Raw X: %d", joystickData.x_value);
    u8g2.drawStr(0, 24, buffer);
    
    sprintf(buffer, "Raw Y: %d", joystickData.y_value);
    u8g2.drawStr(0, 34, buffer);
    
    // 正規化值
    sprintf(buffer, "Norm X: %d", joystickData.x_normalized);
    u8g2.drawStr(0, 44, buffer);
    
    sprintf(buffer, "Norm Y: %d", joystickData.y_normalized);
    u8g2.drawStr(0, 54, buffer);
    
    // 按鈕狀態
    sprintf(buffer, "Button: %s", joystickData.button_state ? "Pressed" : "Released");
    u8g2.drawStr(0, 64, buffer);
  }
  
  // 更新位置文字
  updatePositionText();
  // 顯示在最下方
  // u8g2.drawStr(0, 64, positionText); // 如果顯示空間不足，可以註釋掉此行
  
  // 發送緩衝區內容到顯示器
  u8g2.sendBuffer();
}

// 數據接收回調函數 - 僅接收端使用
void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *incomingData, int len) {
  // 獲取發送者的MAC地址
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
           recv_info->src_addr[0], recv_info->src_addr[1], recv_info->src_addr[2], 
           recv_info->src_addr[3], recv_info->src_addr[4], recv_info->src_addr[5]);
  
  Serial.print("Received from: ");
  Serial.println(macStr);
  
  if (len == sizeof(joystickData)) {
    memcpy(&joystickData, incomingData, sizeof(joystickData));
    
    // 更新數據接收時間和狀態
    lastDataReceivedTime = millis();
    dataReceived = true;
    
    // 輸出接收到的數據
    Serial.print("Bytes received: ");
    Serial.print(len);
    Serial.print(", Msg ID: ");
    Serial.println(joystickData.msg_id);
    
    Serial.print("Raw - X: ");
    Serial.print(joystickData.x_value);
    Serial.print(", Y: ");
    Serial.print(joystickData.y_value);
    Serial.print(", Normalized - X: ");
    Serial.print(joystickData.x_normalized);
    Serial.print(", Y: ");
    Serial.print(joystickData.y_normalized);
    Serial.print(", Button: ");
    Serial.println(joystickData.button_state ? "Pressed" : "Released");
    
    // 更新位置文本並在串口顯示
    updatePositionText();
    Serial.println(positionText);
  } else {
    Serial.print("Error: Received data size doesn't match. Expected ");
    Serial.print(sizeof(joystickData));
    Serial.print(" but got ");
    Serial.println(len);
  }
}
#endif

void printWiFiStatus() {
  Serial.print("WiFi Mode: ");
  switch(WiFi.getMode()) {
    case WIFI_OFF: Serial.println("OFF"); break;
    case WIFI_STA: Serial.println("STA"); break;
    case WIFI_AP: Serial.println("AP"); break;
    case WIFI_AP_STA: Serial.println("AP+STA"); break;
    default: Serial.println("UNKNOWN");
  }
  
  Serial.print("MAC Address: ");
  Serial.println(WiFi.macAddress());
  
  Serial.print("Channel: ");
  Serial.println(WiFi.channel());
  
  Serial.print("WiFi Status: ");
  switch(WiFi.status()) {
    case WL_CONNECTED: Serial.println("CONNECTED"); break;
    case WL_IDLE_STATUS: Serial.println("IDLE"); break;
    case WL_DISCONNECTED: Serial.println("DISCONNECTED"); break;
    case WL_CONNECT_FAILED: Serial.println("CONNECT_FAILED"); break;
    case WL_NO_SSID_AVAIL: Serial.println("NO_SSID_AVAIL"); break;
    default: Serial.println(WiFi.status());
  }
}

void setup() {
  // 初始化串口
  Serial.begin(115200);
  
  // 等待串口準備就緒
  delay(1000);
  
  #ifdef DEVICE_ROLE_SENDER
  Serial.println("\n\n=== ESP32S3 ESP-NOW - SENDER MODE ===");
  Serial.println("Joystick Calibration Info:");
  Serial.print("Center X: ");
  Serial.print(JOYSTICK_CENTER_X);
  Serial.print(", Center Y: ");
  Serial.print(JOYSTICK_CENTER_Y);
  Serial.print(", Deadzone: ");
  Serial.println(JOYSTICK_DEADZONE);
  
  // 設置搖桿引腳
  pinMode(JOYSTICK_X_PIN, INPUT);
  pinMode(JOYSTICK_Y_PIN, INPUT);
  pinMode(JOYSTICK_BUTTON_PIN, INPUT_PULLUP);
  
  // 設置ADC解析度為12位
  analogReadResolution(12);
  Serial.println("ADC Resolution set to 12-bit");
  #else
  Serial.println("\n\n=== ESP32S3 ESP-NOW - RECEIVER MODE with OLED ===");
  
  // 初始化OLED
  u8g2.begin();
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x12_tr);
  u8g2.drawStr(0, 10, "Initializing...");
  u8g2.sendBuffer();
  #endif
  
  // 完全斷開現有WiFi連接
  WiFi.disconnect(true);
  delay(100);
  
  // 設置WiFi模式
  WiFi.mode(WIFI_STA);
  delay(100);
  
  // 獲取並顯示MAC地址
  String macAddress = WiFi.macAddress();
  Serial.print("MAC Address: ");
  Serial.println(macAddress);
  
  #ifdef DEVICE_ROLE_RECEIVER
  Serial.println("*** IMPORTANT: Copy this MAC address to sender's code ***");
  
  // 以0x格式顯示MAC地址，方便複製到發送端
  byte mac[6];
  WiFi.macAddress(mac);
  Serial.print("Set receiverMacAddress in sender to:");
  Serial.print("{0x");
  Serial.print(mac[0], HEX);
  Serial.print(", 0x");
  Serial.print(mac[1], HEX);
  Serial.print(", 0x");
  Serial.print(mac[2], HEX);
  Serial.print(", 0x");
  Serial.print(mac[3], HEX);
  Serial.print(", 0x");
  Serial.print(mac[4], HEX);
  Serial.print(", 0x");
  Serial.print(mac[5], HEX);
  Serial.println("}");
  
  // 在OLED上顯示MAC地址，方便設置
  u8g2.clearBuffer();
  u8g2.drawStr(0, 10, "MAC Address:");
  
  char macBuffer[20];
  sprintf(macBuffer, "%02X:%02X:%02X:%02X:%02X:%02X", 
          mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  u8g2.drawStr(0, 24, macBuffer);
  
  u8g2.drawStr(0, 38, "Set this in sender");
  u8g2.drawStr(0, 48, "Initializing WiFi...");
  u8g2.sendBuffer();
  #endif
  
  // 設置WiFi頻道
  WiFi.channel(ESP_NOW_CHANNEL);
  delay(100);
  
  Serial.print("WiFi Channel set to: ");
  Serial.println(ESP_NOW_CHANNEL);
  
  // 初始化ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    
    #ifdef DEVICE_ROLE_RECEIVER
    u8g2.clearBuffer();
    u8g2.drawStr(0, 20, "ESP-NOW Init Error!");
    u8g2.drawStr(0, 34, "Restarting...");
    u8g2.sendBuffer();
    #endif
    
    delay(2000);
    ESP.restart(); // 如果初始化失敗，重啟設備
    return;
  } else {
    Serial.println("ESP-NOW initialized successfully");
    
    #ifdef DEVICE_ROLE_RECEIVER
    u8g2.drawStr(0, 58, "ESP-NOW Ready!");
    u8g2.sendBuffer();
    #endif
  }
  
  #ifdef DEVICE_ROLE_SENDER
  // 發送端初始化
  esp_now_register_send_cb(OnDataSent);
  
  // 註冊對等設備
  esp_now_peer_info_t peerInfo;
  memset(&peerInfo, 0, sizeof(peerInfo));
  memcpy(peerInfo.peer_addr, receiverMacAddress, 6);
  peerInfo.channel = ESP_NOW_CHANNEL;  
  peerInfo.encrypt = ESP_NOW_ENCRYPT;
  
  // 檢查對等點是否已存在
  bool exists = esp_now_is_peer_exist(receiverMacAddress);
  if (exists) {
    // 已存在，先刪除
    Serial.println("Peer already exists, removing it first");
    esp_now_del_peer(receiverMacAddress);
  }
  
  // 添加對等設備
  esp_err_t addStatus = esp_now_add_peer(&peerInfo);
  if (addStatus == ESP_OK) {
    Serial.println("Peer added successfully");
  } else {
    Serial.print("Failed to add peer: ");
    Serial.println(getESPErrorMsg(addStatus));
    Serial.println("Will retry during operation");
  }
  
  // 測試搖桿讀數
  int x = analogRead(JOYSTICK_X_PIN);
  int y = analogRead(JOYSTICK_Y_PIN);
  bool button = !digitalRead(JOYSTICK_BUTTON_PIN);
  
  Serial.println("Joystick test readings:");
  Serial.print("X: ");
  Serial.print(x);
  Serial.print(", Y: ");
  Serial.print(y);
  Serial.print(", Button: ");
  Serial.println(button ? "Pressed" : "Released");
  #else
  // 接收端初始化
  esp_now_register_recv_cb(OnDataRecv);
  Serial.println("Receiver Ready");
  #endif
  
  // 打印WiFi詳細狀態
  printWiFiStatus();
  
  Serial.println("Setup Complete");
  
  #ifdef DEVICE_ROLE_RECEIVER
  delay(2000); // 讓用戶有時間看到初始化信息
  #endif
}

void loop() {
  static unsigned long lastWifiCheckTime = 0;
  
  // 每10秒檢查一次WiFi狀態
  if (millis() - lastWifiCheckTime > 10000) {
    printWiFiStatus();
    lastWifiCheckTime = millis();
  }
  
  #ifdef DEVICE_ROLE_SENDER
  // 發送端邏輯
  // 讀取搖桿數據
  joystickData.x_value = analogRead(JOYSTICK_X_PIN);
  joystickData.y_value = analogRead(JOYSTICK_Y_PIN);
  joystickData.button_state = !digitalRead(JOYSTICK_BUTTON_PIN);  // 按鈕通常是低電平觸發
  
  // 正規化搖桿值
  joystickData.x_normalized = normalizeJoystick(joystickData.x_value, JOYSTICK_CENTER_X);
  joystickData.y_normalized = normalizeJoystick(joystickData.y_value, JOYSTICK_CENTER_Y);
  
  // 添加消息ID
  joystickData.msg_id = messageCounter++;
  
  // 透過串口顯示數據（調試用）
  Serial.print("Raw - X: ");
  Serial.print(joystickData.x_value);
  Serial.print(", Y: ");
  Serial.print(joystickData.y_value);
  Serial.print(", Normalized - X: ");
  Serial.print(joystickData.x_normalized);
  Serial.print(", Y: ");
  Serial.print(joystickData.y_normalized);
  Serial.print(", Button: ");
  Serial.println(joystickData.button_state ? "Pressed" : "Released");
  
  // 顯示位置說明
  String position = "Position: ";
  if (abs(joystickData.x_normalized) < 10 && abs(joystickData.y_normalized) < 10) {
    position += "Center";
  } else {
    if (joystickData.y_normalized < -30) position += "Up";
    else if (joystickData.y_normalized > 30) position += "Down";
    
    if (joystickData.x_normalized < -30) position += " Left";
    else if (joystickData.x_normalized > 30) position += " Right";
  }
  Serial.println(position);
  
  // 使用改進的發送函數
  if (!sendESPNowData((uint8_t *)&joystickData, sizeof(joystickData))) {
    // 發送失敗處理
    retryCount++;
    if (retryCount > MAX_RETRY_COUNT) {
      Serial.println("Max retry count reached, waiting before next attempt");
      retryCount = 0;
      
      // 長時間失敗後，嘗試重新初始化ESP-NOW
      reinitESPNow();
      delay(1000); // 等待較長時間再嘗試
    } else {
      Serial.print("Retry #");
      Serial.println(retryCount);
      delay(50); // 短暫延遲後重試
    }
  } else {
    // 成功發送
    delay(100);  // 控制發送頻率
  }
  #else
  // 接收端邏輯
  // 更新OLED顯示 (限制更新頻率以避免閃爍)
  if (millis() - lastOLEDUpdateTime > oledUpdateInterval) {
    updateOLEDDisplay();
    lastOLEDUpdateTime = millis();
  }
  
  // 檢查數據接收超時
  if (dataReceived && (millis() - lastDataReceivedTime > dataTimeout)) {
    Serial.println("Data reception timed out!");
    dataReceived = false;
  }
  #endif
  
  delay(10);
}