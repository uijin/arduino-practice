#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <AiEsp32RotaryEncoder.h>
#include <AiEsp32RotaryEncoderNumberSelector.h>
#include <ESP32Servo.h>

// ===== 重要：在這裡選擇設備角色 =====
// 取消註釋其中一個，並註釋另一個
// #define DEVICE_ROLE_SENDER     // 取消此行註釋使設備成為發送端
#define DEVICE_ROLE_RECEIVER // 取消此行註釋使設備成為接收端

// 檢查是否正確定義了角色
#if defined(DEVICE_ROLE_SENDER) && defined(DEVICE_ROLE_RECEIVER)
  #error "不能同時定義DEVICE_ROLE_SENDER和DEVICE_ROLE_RECEIVER"
#elif !defined(DEVICE_ROLE_SENDER) && !defined(DEVICE_ROLE_RECEIVER)
  #error "必須定義DEVICE_ROLE_SENDER或DEVICE_ROLE_RECEIVER其中之一"
#endif

// OLED 顯示模組設置 (MN096-12864-B-4G)
// 使用 U8g2lib 創建顯示對象
// 請根據您的連接方式選擇正確的構造函數
// 下面是基於I2C連接的設置，如果您使用SPI請調整構造函數
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);
// 如果使用SPI連接，請用以下設置（調整引腳）:
// U8G2_SSD1306_128X64_NONAME_F_4W_SW_SPI u8g2(U8G2_R0, /* clock=*/ 13, /* data=*/ 11, /* cs=*/ 10, /* dc=*/ 9, /* reset=*/ 8);

// 定義第一個旋轉編碼器的引腳
#define ROTARY_ENCODER_CLK_PIN D2     // 旋轉編碼器CLK連接到D2
#define ROTARY_ENCODER_DT_PIN D1      // 旋轉編碼器DT連接到D1
#define ROTARY_ENCODER_SW_PIN D0      // 旋轉編碼器SW連接到D0
#define ROTARY_ENCODER_VCC_PIN -1    // 如果您使用自己的電源，設置為-1
#define ROTARY_ENCODER_STEPS 4       // 旋轉步數配置，一般使用4

// 定義第二個旋轉編碼器的引腳
#define ROTARY_ENCODER2_CLK_PIN D8    // 第二個旋轉編碼器CLK連接到D8
#define ROTARY_ENCODER2_DT_PIN D9     // 第二個旋轉編碼器DT連接到D9
#define ROTARY_ENCODER2_SW_PIN D10    // 第二個旋轉編碼器SW連接到D10
#define ROTARY_ENCODER2_VCC_PIN -1   // 如果您使用自己的電源，設置為-1
#define ROTARY_ENCODER2_STEPS 4      // 旋轉步數配置，一般使用4

AiEsp32RotaryEncoder rotaryEncoder = AiEsp32RotaryEncoder(
  ROTARY_ENCODER_CLK_PIN,
  ROTARY_ENCODER_DT_PIN,
  ROTARY_ENCODER_SW_PIN,
  ROTARY_ENCODER_VCC_PIN,
  ROTARY_ENCODER_STEPS
);

AiEsp32RotaryEncoder rotaryEncoder2 = AiEsp32RotaryEncoder(
  ROTARY_ENCODER2_CLK_PIN,
  ROTARY_ENCODER2_DT_PIN,
  ROTARY_ENCODER2_SW_PIN,
  ROTARY_ENCODER2_VCC_PIN,
  ROTARY_ENCODER2_STEPS
);

// **重要** 在發送端模式，設置接收端的MAC地址（從接收端的Serial輸出獲取）
// 示例格式: {0x24, 0x0A, 0xC4, 0x9A, 0x58, 0x28}
// uint8_t receiverMacAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}; // 替換為接收端的實際MAC地址
uint8_t receiverMacAddress[] = {0xD8, 0x3B, 0xDA, 0x74, 0x1C, 0xEC}; // 替換為接收端的實際MAC地址

// 旋轉編碼器範圍設定
#define ROTARY_MIN_VALUE -100
#define ROTARY_MAX_VALUE 100
#define ROTARY_INITIAL_VALUE 0

// 定義按鈕事件類型
typedef enum {
  NO_EVENT = 0,
  SINGLE_CLICK,
  DOUBLE_CLICK,
  LONG_PRESS
} ButtonEventType;

#define ESP_NOW_CHANNEL 1      // 設置ESP-NOW頻道 (1-14)，兩端必須相同
#define ESP_NOW_ENCRYPT false  // 是否加密傳輸
#define MAX_RETRY_COUNT 3      // 發送失敗時的最大重試次數

// 定義數據結構
typedef struct joystick_message {
  int encoder1_value;   // 第一個編碼器值
  int encoder1_norm;    // 第一個編碼器正規化值 (-100 到 100)
  int encoder2_value;   // 第二個編碼器值
  int encoder2_norm;    // 第二個編碼器正規化值 (-100 到 100)
  bool button_state;    // 第一個按鈕當前物理狀態 (Pressed/Released)
  bool button2_state;   // 第二個按鈕當前物理狀態 (Pressed/Released)
  uint32_t msg_id;      // 消息ID，用於追蹤
  uint8_t button_event; // 按鈕觸發的事件類型 (0:NO_EVENT, 1:SINGLE, 2:DOUBLE, 3:LONG)
  uint8_t button2_event; // 第二個按鈕觸發的事件類型
} joystick_message;

// ACK封包結構體 - 用於發送確認和RSSI值
typedef struct ack_message {
  int rssi;            // 接收訊號強度 (dBm)
  uint32_t ack_id;     // 確認回應的消息ID
} ack_message;

// 創建數據結構實例
joystick_message joystickData;
ack_message ackData;          // ACK封包數據結構

// Sender端的特定變數
#ifdef DEVICE_ROLE_SENDER
uint32_t messageCounter = 0;  // 消息計數器
ack_message receivedAck;      // 接收到的ACK封包
int lastReceivedRSSI = -100;  // 上次接收到的RSSI值
bool ackReceived = false;     // 是否收到ACK
unsigned long lastAckTime = 0; // 上次接收ACK的時間
int retryCount = 0;           // 重試計數器
bool lastSendSuccess = true;  // 上次發送是否成功

// 按鈕事件檢測相關變量
bool lastButtonState = false;         // 上一個按鈕狀態
unsigned long buttonPressTime = 0;    // 按鈕按下的時間
unsigned long buttonReleaseTime = 0;  // 按鈕釋放的時間
int buttonClickCount = 0;             // 短時間內的點擊次數
const unsigned long doubleClickGap = 250; // 雙擊的最大間隔 (毫秒)
const unsigned long longPressDuration = 3000; // 長按的持續時間 (毫秒)
bool longPressActive = false;         // 長按是否已觸發並處理
ButtonEventType currentButtonEvent = NO_EVENT; // 當前檢測到的按鈕事件
#endif

#ifdef DEVICE_ROLE_RECEIVER
// OLED顯示相關變數
unsigned long lastOLEDUpdateTime = 0;  // 上次更新OLED的時間
const int oledUpdateInterval = 100;     // OLED更新頻率 (毫秒)
unsigned long lastDataReceivedTime = 0; // 上次接收數據的時間
const unsigned long dataTimeout = 3000; // 數據接收超時時間 (毫秒)
bool dataReceived = false;             // 是否有接收到數據

// 無線訊號指標相關變數
unsigned long lastAckSentTime = 0; // 上次發送ACK的時間
int lastRSSI = 0;                      // 上次接收的RSSI值
int minRSSI = 0;                       // 最小RSSI值 (最弱訊號)
int maxRSSI = -100;                    // 最大RSSI值 (最強訊號)

// 封包統計相關變數
uint32_t totalPackets = 0;             // 接收到的總封包數
uint32_t lastMsgId = 0;                // 上次接收到的消息ID
uint32_t lostPackets = 0;              // 丟失的封包數
float packetLossRate = 0.0;            // 封包遺失率
int displayPage = 0;                   // 當前顯示頁面
unsigned long lastPageSwitchTime = 0;  // 上次切換顯示頁面的時間
const unsigned long pageSwitchInterval = 5000; // 自動切換顯示頁面的間隔
bool buttonPressedLast_receiver = false; // 用於接收端按鈕邏輯

// 伺服馬達控制
Servo steeringServo;              // 第一個伺服馬達對象
Servo steeringServo2;             // 第二個伺服馬達對象
#define SERVO_PIN D0                    // 第一個伺服馬達連接到D0
#define SERVO2_PIN D7                   // 第二個伺服馬達連接到D7
int lastServoAngle = 90;               // 上次的第一個伺服馬達角度
int lastServoAngle2 = 90;              // 上次的第二個伺服馬達角度
#endif

// ===== 兩者共用的變數 =====
unsigned long lastDataSentTime = 0; // 上次發送數據的時間
const int dataInterval = 50;       // 發送間隔 (ms)
esp_now_peer_info_t peerInfo;      // 對等節點信息

// 初始化ESP-NOW
bool initESPNow() {
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return false;
  }
  return true;
}

// 獲取當前WiFi信道的利用率 (估算)
// 獲取信道利用率的函數
int getChannelUtilization() {
  // ESP32沒有直接API獲取信道利用率，所以這裡是一個模擬實現
  // 實際應用中可以通過測量一段時間內的封包數量或使用專門的API
  // 返回0-100的信道利用率百分比

  // 使用動態加權平均方法，結合隨機變化來模擬信道利用率的變化
  static int lastUtil = 30; // 初始化為30%
  int randomFactor = random(-5, 6); // -5到5的隨機變化

  // 計算新的利用率，範圍限制在5-95之間
  int newUtil = constrain(lastUtil + randomFactor, 5, 95);
  lastUtil = newUtil;

  return newUtil;
}

// 獲取ESP錯誤消息
const char* getESPErrorMsg(esp_err_t err) {
  switch (err) {
    case ESP_OK: return "Success";
    case ESP_ERR_ESPNOW_NOT_INIT: return "ESP-NOW not init";
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
  Serial.println("Re-initializing ESP-NOW...");

  // 清除所有對等點
  esp_now_deinit();
  delay(100);

  // 重新初始化ESP-NOW
  if (!initESPNow()) {
    Serial.println("Failed to re-initialize ESP-NOW");
    return false;
  }

  Serial.println("ESP-NOW re-initialized successfully");

  // 重新註冊回調函數
  #ifdef DEVICE_ROLE_SENDER
  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);
  #else
  esp_now_register_recv_cb(OnDataRecv);
  esp_now_register_send_cb(OnDataSent);
  #endif

  // 重新添加對等點
  #ifdef DEVICE_ROLE_SENDER
  // 添加接收端作為對等點
  memset(&peerInfo, 0, sizeof(peerInfo));
  memcpy(peerInfo.peer_addr, receiverMacAddress, 6);
  peerInfo.channel = ESP_NOW_CHANNEL;
  peerInfo.encrypt = ESP_NOW_ENCRYPT;
  esp_err_t addStatus = esp_now_add_peer(&peerInfo);
  if (addStatus != ESP_OK) {
    Serial.print("Failed to add peer: ");
    Serial.println(getESPErrorMsg(addStatus));
    return false;
  }
  #endif

  return true;
}

// 將RSSI轉換為信號強度級別(0-4)
// 將RSSI值轉換為訊號強度百分比 (0-100%)
int rssiToSignalStrength(int rssi) {
  // RSSI 通常在 -30 (很強) 到 -90 (很弱) 之間
  // 轉換為0-100%的訊號強度
  int strength = constrain(map(rssi, -90, -30, 0, 100), 0, 100);
  return strength;
}

// 繪製訊號強度圖標
void drawSignalStrength(int x, int y, int strength) {
  int barWidth = 3;
  int barSpacing = 2;
  int barHeightMax = 14;
  int numBars = 4;

  // 計算信號條顯示
  for (int i = 0; i < numBars; i++) {
    int barHeight = map(i+1, 1, numBars, 3, barHeightMax);

    // 如果訊號強度足夠，填充條形
    if (strength >= (i+1) * (100/numBars)) {
      u8g2.drawBox(x + i*(barWidth+barSpacing), y - barHeight, barWidth, barHeight);
    } else {
      // 否則繪製空心條形
      u8g2.drawFrame(x + i*(barWidth+barSpacing), y - barHeight, barWidth, barHeight);
    }
  }
}

// 發送數據時的回調函數
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  #ifdef DEVICE_ROLE_SENDER
  lastSendSuccess = (status == ESP_NOW_SEND_SUCCESS);
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);

  if (status == ESP_NOW_SEND_SUCCESS) {
    Serial.print("Message sent successfully to: ");
    Serial.println(macStr);
    retryCount = 0; // 成功後重置重試計數器
  } else {
    Serial.print("Failed to send message to: ");
    Serial.println(macStr);

    // 重試邏輯
    if (retryCount < MAX_RETRY_COUNT) {
      retryCount++;
      Serial.print("Retrying... (");
      Serial.print(retryCount);
      Serial.print("/");
      Serial.print(MAX_RETRY_COUNT);
      Serial.println(")");
      // 延遲重試以避免衝突
      delay(10 * retryCount);
      // 實際重試在loop()中進行
    } else {
      Serial.println("Max retry count reached, giving up");
      retryCount = 0;
    }
  }
  #else
  // 接收端發送ACK後的處理
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);

  if (status == ESP_NOW_SEND_SUCCESS) {
    Serial.print("ACK sent successfully to: ");
    Serial.println(macStr);
  } else {
    Serial.print("Failed to send ACK to: ");
    Serial.println(macStr);
  }
  #endif
}

// 發送ESP-NOW數據
bool sendESPNowData() {
  #ifdef DEVICE_ROLE_SENDER
  if (retryCount > 0 && retryCount <= MAX_RETRY_COUNT) {
    // 這是一個重試發送
    Serial.println("Retrying send...");
  }

  esp_err_t result = esp_now_send(receiverMacAddress, (uint8_t *)&joystickData, sizeof(joystickData));

  if (result != ESP_OK) {
    Serial.print("Send Error: ");
    Serial.println(getESPErrorMsg(result));
    return false;
  }

  return true;
  #endif

  return false; // 接收端不應調用此函數
}

// 控制伺服馬達角度
void controlServos(int value1, int value2) {
  #ifdef DEVICE_ROLE_RECEIVER
  // 將編碼器值 (-100 到 100) 映射到伺服馬達角度 (0 到 180)
  int servoAngle1 = map(value1, -100, 100, 0, 180);
  int servoAngle2 = map(value2, -100, 100, 0, 180);
  
  // 如果角度變化大於一定閾值才更新，避免抖動
  if (abs(servoAngle1 - lastServoAngle) > 1) {
    steeringServo.write(servoAngle1);
    lastServoAngle = servoAngle1;
    
    // 調試輸出
    Serial.print("Servo 1 angle: ");
    Serial.println(servoAngle1);
  }
  
  // 第二個伺服馬達控制
  if (abs(servoAngle2 - lastServoAngle2) > 1) {
    steeringServo2.write(servoAngle2);
    lastServoAngle2 = servoAngle2;
    
    // 調試輸出
    Serial.print("Servo 2 angle: ");
    Serial.println(servoAngle2);
  }
  #endif
}

// 繪製進度條
void drawProgressBar(int x, int y, int width, int height, int percentage) {
  u8g2.drawFrame(x, y, width, height);
  int filledWidth = (percentage * width) / 100;
  if (filledWidth > 0) {
    u8g2.drawBox(x, y, filledWidth, height);
  }
}

// 更新OLED頁面0（主頁面）
// 更新OLED顯示 - 第一頁：RSSI和SNR信息
void updateOLEDPage0() {
  #ifdef DEVICE_ROLE_RECEIVER
  u8g2.clearBuffer();

  // 設置字體
  u8g2.setFont(u8g2_font_6x12_tr);

  // 標題
  u8g2.drawStr(0, 10, "Signal & Packets");
  u8g2.drawLine(0, 12, 128, 12);

  // 檢查是否有數據以及數據是否超時
  if (!dataReceived || (millis() - lastDataReceivedTime > dataTimeout)) {
    u8g2.drawStr(0, 30, "Waiting for data...");
    return;
  }

  // 顯示RSSI信息
  char buffer[32];
  int signalStrength = rssiToSignalStrength(lastRSSI);

  sprintf(buffer, "RSSI: %d dBm (%d%%)", lastRSSI, signalStrength);
  u8g2.drawStr(0, 24, buffer);

  // 顯示封包統計信息 (移至Page0)
  sprintf(buffer, "Pkts: %lu/%lu", totalPackets, lostPackets); // Combined Total and Lost
  u8g2.drawStr(0, 36, buffer); // Y座標調整為36

  sprintf(buffer, "Loss: %.1f%%", packetLossRate);
  u8g2.drawStr(0, 48, buffer); // Y座標調整為48

  // 繪製封包遺失率進度條
  // Text for Loss Rate is at Y=48. Bar at Y=42
  drawProgressBar(90, 42, 35, 6, (int)(packetLossRate)); // Y座標調整為42

  u8g2.sendBuffer();
  #endif
}

// 更新OLED頁面1（詳細信息）
void updateOLEDPage1() {
  #ifdef DEVICE_ROLE_RECEIVER
  u8g2.clearBuffer();

  // 設置字體
  u8g2.setFont(u8g2_font_6x12_tr);

  // 標題
  u8g2.drawStr(0, 10, "Encoder/Servo Info");
  u8g2.drawLine(0, 12, 128, 12);

  // 檢查是否有數據以及數據是否超時
  if (!dataReceived || (millis() - lastDataReceivedTime > dataTimeout)) {
    u8g2.drawStr(0, 30, "Waiting for data...");
    return;
  }

  // 顯示搖桿數據
  char buffer[40]; // Increased buffer size for longer strings

  // 第1行: 編碼器1值和伺服1角度
  int servoAngle1 = map(joystickData.encoder1_norm, -100, 100, 0, 180);
  sprintf(buffer, "Enc1:%d Srv1:%d°", joystickData.encoder1_norm, servoAngle1);
  u8g2.drawStr(0, 24, buffer);

  // 第2行: 編碼器2值和伺服2角度
  int servoAngle2 = map(joystickData.encoder2_norm, -100, 100, 0, 180);
  sprintf(buffer, "Enc2:%d Srv2:%d°", joystickData.encoder2_norm, servoAngle2);
  u8g2.drawStr(0, 36, buffer);

  // 第3行: 按鈕狀態
  sprintf(buffer, "Btn1:%s Btn2:%s",
          joystickData.button_state ? "ON" : "OFF",
          joystickData.button2_state ? "ON" : "OFF");
  u8g2.drawStr(0, 48, buffer);

  // 第4行: 連接狀態
  unsigned long timeSinceLastData = millis() - lastDataReceivedTime;
  sprintf(buffer, "Link: %s", timeSinceLastData < 2000 ? "Connected" : "Disconnected");
  u8g2.drawStr(0, 60, buffer);

  u8g2.sendBuffer();
  #endif
}

// 發送端OLED顯示函數 - 顯示ESP-NOW訊號強度
void updateSenderOLED() {
  #ifdef DEVICE_ROLE_SENDER
  u8g2.clearBuffer();

  // 設置字體
  u8g2.setFont(u8g2_font_6x12_tr);

  // 顯示標題
  u8g2.drawStr(0, 10, "Dual Encoder Control");
  u8g2.drawLine(0, 12, 128, 12);

  char buffer[32];

  // 顯示旋轉編碼器值
  sprintf(buffer, "Enc1:%d Enc2:%d", 
          joystickData.encoder1_norm, 
          joystickData.encoder2_norm);
  u8g2.drawStr(0, 24, buffer);

  // 顯示最後發送狀態
  if (lastSendSuccess) {
    u8g2.drawStr(0, 36, "Status: OK");
  } else {
    u8g2.drawStr(0, 36, "Status: Fail");
  }

  // 顯示按鈕狀態
  sprintf(buffer, "Btn1:%s Btn2:%s",
          joystickData.button_state ? "ON" : "OFF",
          joystickData.button2_state ? "ON" : "OFF");
  u8g2.drawStr(0, 48, buffer);

  // 檢查是否收到ACK (10秒內)
  unsigned long currentMillis = millis();
  if (ackReceived && (currentMillis - lastAckTime < 10000)) {
    // 顯示RSSI信息
    int signalStrength = rssiToSignalStrength(lastReceivedRSSI);
    sprintf(buffer, "RSSI: %d dBm (%d%%)", lastReceivedRSSI, signalStrength);
    u8g2.drawStr(0, 60, buffer);

    // 繪製訊號強度圖標
    drawSignalStrength(100, 60, signalStrength);
  } else {
    u8g2.drawStr(0, 60, "Waiting for ACK...");
  }

  u8g2.sendBuffer();
  #endif
}

// 更新OLED顯示 - 選擇當前頁面
void updateOLEDDisplay() {
  #ifdef DEVICE_ROLE_RECEIVER
  // 根據當前頁面選擇顯示函數
  if (displayPage == 0) {
    updateOLEDPage0();
  } else {
    updateOLEDPage1();
  }
  #else
  // 發送端更新OLED
  updateSenderOLED();
  #endif
}

// 旋轉編碼器中斷處理函數
void IRAM_ATTR readEncoderISR() {
  rotaryEncoder.readEncoder_ISR();
}

// 第二個旋轉編碼器中斷處理函數
void IRAM_ATTR readEncoder2ISR() {
  rotaryEncoder2.readEncoder_ISR();
}

// 處理旋轉編碼器數據
void handleRotaryEncoders() {
  #ifdef DEVICE_ROLE_SENDER
  // 讀取第一個旋轉編碼器
  int rawValue1 = rotaryEncoder.readEncoder();
  
  // 設置第一個編碼器的值
  joystickData.encoder1_value = rawValue1;
  joystickData.encoder1_norm = rawValue1;
  
  // 讀取第二個旋轉編碼器
  int rawValue2 = rotaryEncoder2.readEncoder();
  
  // 設置第二個編碼器的值
  joystickData.encoder2_value = rawValue2;
  joystickData.encoder2_norm = rawValue2;
  
  // 讀取按鈕狀態
  joystickData.button_state = !rotaryEncoder.isEncoderButtonClicked();
  joystickData.button2_state = !rotaryEncoder2.isEncoderButtonClicked();
  #endif
}

// 接收數據的回調函數
void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int data_len) {
  #ifdef DEVICE_ROLE_SENDER
  // 發送端處理接收到的ACK數據
  const uint8_t *mac_addr = info->src_addr;
  if (data_len == sizeof(ack_message)) {
    memcpy(&receivedAck, data, sizeof(ack_message));
    Serial.print("Received ACK for message ID: ");
    Serial.println(receivedAck.ack_id);
    Serial.print("Receiver RSSI: ");
    Serial.println(receivedAck.rssi);
    // 調試輸出RSSI值以便追蹤問題
    Serial.print("RSSI Strength: ");
    Serial.print(rssiToSignalStrength(receivedAck.rssi));
    Serial.println("%");

    lastReceivedRSSI = receivedAck.rssi;
    ackReceived = true;
    lastAckTime = millis();
  } else {
    Serial.print("Received unknown data of size: ");
    Serial.println(data_len);
  }
  #else
  // 接收端處理邏輯
  // 獲取RSSI值 (訊號強度)
  int receivedRSSI = info->rx_ctrl->rssi;
  Serial.print("RSSI: ");
  Serial.print(receivedRSSI);
  Serial.println(" dBm");

  lastRSSI = receivedRSSI;

  // 更新最大最小RSSI值
  if (lastRSSI > maxRSSI) maxRSSI = lastRSSI;
  if (lastRSSI < minRSSI) minRSSI = lastRSSI;

  // 處理接收到的搖桿數據
  if (data_len == sizeof(joystickData)) {
    memcpy(&joystickData, data, sizeof(joystickData));

    // 更新數據接收時間和狀態
    lastDataReceivedTime = millis();
    dataReceived = true;

    // 更新封包統計
    totalPackets++;

    // 檢查是否有丟失的封包
    if (totalPackets > 1 && joystickData.msg_id > lastMsgId + 1) {
      uint32_t newLostPackets = joystickData.msg_id - lastMsgId - 1;
      lostPackets += newLostPackets;

      Serial.print("Packet(s) lost: ");
      Serial.println(newLostPackets);
    }

    // 更新上次接收的消息ID
    lastMsgId = joystickData.msg_id;

    // 計算封包遺失率
    if (totalPackets > 0) {
      packetLossRate = (float)lostPackets / (totalPackets + lostPackets) * 100.0;
    }

    // 控制伺服馬達
    controlServos(joystickData.encoder1_norm, joystickData.encoder2_norm);

    // 輸出接收到的數據
    Serial.print("Bytes received: ");
    Serial.print(data_len);
    Serial.print(", Msg ID: ");
    Serial.println(joystickData.msg_id);

    // 顯示接收的數據
    Serial.print("Received data - Encoder1: ");
    Serial.print(joystickData.encoder1_norm);
    Serial.print(", Encoder2: ");
    Serial.print(joystickData.encoder2_norm);
    Serial.print(", Button1: ");
    Serial.print(joystickData.button_state ? "Pressed" : "Released");
    Serial.print(", Button2: ");
    Serial.print(joystickData.button2_state ? "Pressed" : "Released");
    Serial.print(", Event: ");
    Serial.print(joystickData.button_event);
    Serial.print(", ID: ");
    Serial.println(joystickData.msg_id);

    // 處理按鈕事件
    ButtonEventType receivedEvent = (ButtonEventType)joystickData.button_event;
    if (receivedEvent != NO_EVENT) {
      Serial.print("Received Button Event: ");
      Serial.println(receivedEvent);
    }

    if (receivedEvent == DOUBLE_CLICK) {
      displayPage = (displayPage + 1) % 2; // 切換顯示頁面
      Serial.print("Receiver: OLED Page switched to ");
      Serial.println(displayPage);
    }

    // 發送ACK回應 - 使用接收到的數據包的RSSI值
    ackData.rssi = info->rx_ctrl->rssi;  // 使用數據包的RSSI而不是WiFi.RSSI()
    ackData.ack_id = joystickData.msg_id;

    // 嘗試添加對等點（如果尚未添加）
    bool peerExists = esp_now_is_peer_exist(info->src_addr);
    if (!peerExists) {
      memset(&peerInfo, 0, sizeof(peerInfo));
      memcpy(peerInfo.peer_addr, info->src_addr, 6);
      peerInfo.channel = ESP_NOW_CHANNEL;
      peerInfo.encrypt = ESP_NOW_ENCRYPT;

      esp_now_add_peer(&peerInfo);
    }

    // 發送ACK
    esp_now_send(info->src_addr, (uint8_t *)&ackData, sizeof(ackData));
  }
  #endif
}

// 檢查WiFi狀態
void printWiFiStatus() {
  Serial.print("WiFi Mode: ");
  switch (WiFi.getMode()) {
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

  Serial.print("RSSI: ");
  Serial.print(WiFi.RSSI());
  Serial.println(" dBm");
}

void setup() {
  // 初始化串口
  Serial.begin(115200);
  delay(1000);

  #ifdef DEVICE_ROLE_SENDER
  Serial.println("\n\n=== ESP32 ESP-NOW - SENDER MODE with Rotary Encoder ===");

  // 初始化第一個旋轉編碼器
  rotaryEncoder.begin();
  rotaryEncoder.setup(readEncoderISR);
  rotaryEncoder.setBoundaries(ROTARY_MIN_VALUE, ROTARY_MAX_VALUE, false); // 不循環
  rotaryEncoder.setAcceleration(250); // 設置加速度
  rotaryEncoder.setEncoderValue(ROTARY_INITIAL_VALUE);

  // 初始化第二個旋轉編碼器
  rotaryEncoder2.begin();
  rotaryEncoder2.setup(readEncoder2ISR);
  rotaryEncoder2.setBoundaries(ROTARY_MIN_VALUE, ROTARY_MAX_VALUE, false); // 不循環
  rotaryEncoder2.setAcceleration(250); // 設置加速度
  rotaryEncoder2.setEncoderValue(ROTARY_INITIAL_VALUE);

  #else
  Serial.println("\n\n=== ESP32 ESP-NOW - RECEIVER MODE with OLED and Servo ===");

  // 初始化第一個伺服馬達
  steeringServo.setPeriodHertz(50);    // 標準50Hz伺服馬達
  steeringServo.attach(SERVO_PIN, 500, 2400); // 附加伺服馬達(引腳, 最小脈寬, 最大脈寬)
  steeringServo.write(90); // 初始位置居中
  lastServoAngle = 90;

  // 初始化第二個伺服馬達
  steeringServo2.setPeriodHertz(50);   // 標準50Hz伺服馬達
  steeringServo2.attach(SERVO2_PIN, 500, 2400); // 附加第二個伺服馬達
  steeringServo2.write(90); // 初始位置居中
  lastServoAngle2 = 90;

  Serial.println("Both servos initialized at center position (90°)");
  #endif

  // 初始化OLED
  u8g2.begin();
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x12_tr);
  u8g2.drawStr(0, 10, "Initializing...");
  u8g2.sendBuffer();

  // 斷開現有WiFi連接
  WiFi.disconnect(true);
  delay(100);

  // 設置WiFi模式
  WiFi.mode(WIFI_STA);
  delay(100);

  // 獲取MAC地址
  String macAddress = WiFi.macAddress();
  Serial.print("MAC Address: ");
  Serial.println(macAddress);

  #ifdef DEVICE_ROLE_RECEIVER
  // 顯示MAC地址以供發送端配置
  Serial.println("*** 請將此MAC地址設定在發送端的receiverMacAddress ***");

  // 以0x格式顯示MAC地址
  byte mac[6];
  WiFi.macAddress(mac);
  Serial.print("MAC地址格式: {0x");
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

  // 在OLED上顯示MAC地址
  u8g2.clearBuffer();
  u8g2.drawStr(0, 10, "MAC地址:");

  char macBuffer[20];
  sprintf(macBuffer, "%02X:%02X:%02X:%02X:%02X:%02X",
          mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  u8g2.drawStr(0, 24, macBuffer);

  u8g2.drawStr(0, 38, "請在發送端設置");
  u8g2.sendBuffer();
  #endif

  // 設置WiFi頻道
  WiFi.channel(ESP_NOW_CHANNEL);
  delay(100);

  // 初始化ESP-NOW
  if (!initESPNow()) {
    Serial.println("ESP-NOW初始化失敗");

    u8g2.clearBuffer();
    u8g2.drawStr(0, 20, "ESP-NOW初始化失敗!");
    u8g2.drawStr(0, 34, "正在重啟...");
    u8g2.sendBuffer();

    delay(2000);
    ESP.restart();
    return;
  }

  // 註冊回調函數
  #ifdef DEVICE_ROLE_SENDER
  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);

  // 添加接收端作為對等點
  memset(&peerInfo, 0, sizeof(peerInfo));
  memcpy(peerInfo.peer_addr, receiverMacAddress, 6);
  peerInfo.channel = ESP_NOW_CHANNEL;
  peerInfo.encrypt = ESP_NOW_ENCRYPT;
  esp_err_t addStatus = esp_now_add_peer(&peerInfo);

  if (addStatus != ESP_OK) {
    Serial.print("添加對等點失敗: ");
    Serial.println(getESPErrorMsg(addStatus));
  } else {
    Serial.println("對等點添加成功");
  }

  #else
  esp_now_register_recv_cb(OnDataRecv);
  esp_now_register_send_cb(OnDataSent);
  #endif

  Serial.println("設置完成");

  delay(1000);

  #ifdef DEVICE_ROLE_SENDER
  updateSenderOLED();
  #else
  updateOLEDPage0();
  #endif
}

void loop() {
  static unsigned long lastWifiCheckTime = 0;
  static unsigned long lastOLEDUpdateTime = 0;
  static unsigned long lastServoUpdateTime = 0;

  // 每10秒檢查一次WiFi狀態
  if (millis() - lastWifiCheckTime > 10000) {
    printWiFiStatus();
    lastWifiCheckTime = millis();
  }

  #ifdef DEVICE_ROLE_SENDER
  // 處理旋轉編碼器數據
  handleRotaryEncoders();

  // 處理第一個編碼器的按鈕事件
  bool buttonPressed = !rotaryEncoder.isEncoderButtonClicked();
  joystickData.button_state = buttonPressed;

  // 處理第二個編碼器的按鈕事件
  bool button2Pressed = !rotaryEncoder2.isEncoderButtonClicked();
  joystickData.button2_state = button2Pressed;

  if (buttonPressed != lastButtonState) {
    unsigned long currentTime = millis();

    if (buttonPressed) {
      // 按鈕按下
      buttonPressTime = currentTime;
      buttonClickCount++;
      longPressActive = false;
    } else {
      // 按鈕釋放
      buttonReleaseTime = currentTime;
    }

    lastButtonState = buttonPressed;
  }

  // 檢測點擊類型
  unsigned long currentTime = millis();

  // 檢測雙擊和單擊
  if (buttonClickCount > 0 && !lastButtonState && (currentTime - buttonReleaseTime > doubleClickGap)) {
    if (buttonClickCount == 1) {
      currentButtonEvent = SINGLE_CLICK;
      Serial.println("Button Event: SINGLE_CLICK");
    } else if (buttonClickCount >= 2) {
      currentButtonEvent = DOUBLE_CLICK;
      Serial.println("Button Event: DOUBLE_CLICK");
    }
    buttonClickCount = 0;
  }

  // 檢測長按
  if (lastButtonState && !longPressActive && (currentTime - buttonPressTime >= longPressDuration)) {
    currentButtonEvent = LONG_PRESS;
    Serial.println("Button Event: LONG_PRESS");
    longPressActive = true;
    buttonClickCount = 0;

    // 長按重置旋轉編碼器
    rotaryEncoder.setEncoderValue(ROTARY_INITIAL_VALUE);
    Serial.println("Reset rotary encoder to center position");

    // Display calibration success on sender's OLED
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB10_tr); // Use a slightly larger font
    u8g2.drawStr(0, 10, "Encoder Reset!");
    u8g2.drawStr(0, 25, "Position: 0");
    u8g2.sendBuffer();
    delay(1000); // Short display time
  }

  // 如果按鈕釋放並且之前是長按，重置長按狀態
  if (!lastButtonState && longPressActive) {
    longPressActive = false;
  }

  // 更新按鈕事件
  joystickData.button_event = (uint8_t)currentButtonEvent;

  // 更新OLED顯示
  if (millis() - lastOLEDUpdateTime > 100) {
    updateSenderOLED();
    lastOLEDUpdateTime = millis();

    // 檢查是否長時間未收到ACK
    if (ackReceived && (millis() - lastAckTime > 30000)) {
      Serial.println("No ACK received for a long time, re-initializing ESP-NOW");
      reinitESPNow();
      ackReceived = false;
    }
  }

  // 發送數據
  if (millis() - lastDataSentTime > dataInterval) {
    // 更新消息ID
    joystickData.msg_id = messageCounter++;

    // 發送數據
    sendESPNowData();

    lastDataSentTime = millis();
  }
  #else
  // 接收端邏輯
  // 定期更新OLED顯示
  if (millis() - lastOLEDUpdateTime > oledUpdateInterval) {
    updateOLEDDisplay();
    lastOLEDUpdateTime = millis();
  }

  // 檢查數據接收超時
  if (dataReceived && (millis() - lastDataReceivedTime > dataTimeout)) {
    Serial.println("Data reception timed out!");
    dataReceived = false;

    // 數據超時時將兩個伺服都重置到中心位置
    steeringServo.write(90);
    steeringServo2.write(90);
    lastServoAngle = 90;
    lastServoAngle2 = 90;
    Serial.println("Timeout - Both servos reset to center position");
  }
  #endif

  // 允許ESP32處理背景任務
  delay(1);
}
