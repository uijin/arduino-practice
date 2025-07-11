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
// U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);  // OLED display
// 如果使用SPI連接，請用以下設置（調整引腳）:
// U8G2_SSD1306_128X64_NONAME_F_4W_SW_SPI u8g2(U8G2_R0, /* clock=*/ 13, /* data=*/ 11, /* cs=*/ 10, /* dc=*/ 9, /* reset=*/ 8);

// 定義第一個旋轉編碼器的引腳
#define ROTARY_ENCODER_CLK_PIN D8     // 旋轉編碼器CLK連接到D2
#define ROTARY_ENCODER_DT_PIN D9      // 旋轉編碼器DT連接到D1
#define ROTARY_ENCODER_SW_PIN D10      // 旋轉編碼器SW連接到D0
#define ROTARY_ENCODER_VCC_PIN -1    // 如果您使用自己的電源，設置為-1
#define ROTARY_ENCODER_STEPS 2       // 旋轉步數配置，一般使用4

// 定義第二個旋轉編碼器的引腳
#define ROTARY_ENCODER2_CLK_PIN D2    // 第二個旋轉編碼器CLK連接到D8
#define ROTARY_ENCODER2_DT_PIN D1     // 第二個旋轉編碼器DT連接到D9
#define ROTARY_ENCODER2_SW_PIN D0    // 第二個旋轉編碼器SW連接到D10
#define ROTARY_ENCODER2_VCC_PIN -1   // 如果您使用自己的電源，設置為-1
#define ROTARY_ENCODER2_STEPS 2      // 旋轉步數配置，一般使用4

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

// **重要** 多接收端配置
#define NUM_RECEIVERS 2
uint8_t receiverMacAddresses[NUM_RECEIVERS][6] = {
  {0xD8, 0x3B, 0xDA, 0x74, 0x1C, 0xEC}, // Sailing Car
  {0xFC, 0xF5, 0xC4, 0x95, 0x97, 0x6C}  // Motor Car
};

const char* receiverNames[NUM_RECEIVERS] = {
  "Sailing Car",
  "Motor Car"
};

// 當前選擇的接收端
int currentReceiver = 0;  // 默認選擇第一個接收端

// 旋轉編碼器範圍設定
#define ROTARY_MIN_VALUE -90
#define ROTARY_MAX_VALUE 90
#define ROTARY_INITIAL_VALUE 0

// 定義按鈕事件類型
typedef enum {
  NO_EVENT = 0,
  SINGLE_CLICK
} ButtonEventType;

#define ESP_NOW_CHANNEL 1      // 設置ESP-NOW頻道 (1-14)，兩端必須相同
#define ESP_NOW_ENCRYPT false  // 是否加密傳輸
#define MAX_RETRY_COUNT 3      // 發送失敗時的最大重試次數

// 定義數據結構
typedef struct rotary_message {
  int encoder1_value;   // 第一個編碼器值
  int encoder1_norm;    // 第一個編碼器正規化值 (-200 到 200)
  int encoder2_value;   // 第二個編碼器值
  int encoder2_norm;    // 第二個編碼器正規化值 (-200 到 200)
  bool button_state;    // 第一個按鈕當前物理狀態 (Pressed/Released)
  bool button2_state;   // 第二個按鈕當前物理狀態 (Pressed/Released)
  uint32_t msg_id;      // 消息ID，用於追蹤
  uint8_t button_event; // 按鈕觸發的事件類型 (0:NO_EVENT, 1:SINGLE_CLICK)
  uint8_t button2_event; // 第二個按鈕觸發的事件類型 (0:NO_EVENT, 1:SINGLE_CLICK)
  int8_t propeller;     // 螺旋槳值 (-90 到 90)
} rotary_message;

// ACK封包結構體 - 用於發送確認和RSSI值
typedef struct ack_message {
  int rssi;            // 接收訊號強度 (dBm)
  uint32_t ack_id;     // 確認回應的消息ID
} ack_message;

// 創建數據結構實例
rotary_message rotaryData;
ack_message ackData;          // ACK封包數據結構

// Sender端的特定變數
#ifdef DEVICE_ROLE_SENDER
uint32_t messageCounter = 0;  // 消息計數器

// 每個接收端的獨立連接狀態
ack_message receivedAck[NUM_RECEIVERS];      // 每個接收端的ACK封包
int lastReceivedRSSI[NUM_RECEIVERS];         // 每個接收端的RSSI值
bool ackReceived[NUM_RECEIVERS];             // 每個接收端的ACK狀態
unsigned long lastAckTime[NUM_RECEIVERS];    // 每個接收端的上次ACK時間

int retryCount = 0;           // 重試計數器
bool lastSendSuccess = true;  // 上次發送是否成功

// 接收端切換相關變量
bool receiverSwitchMode = false;       // 是否處於接收端切換模式
unsigned long switchModeStartTime = 0; // 切換模式開始時間
#define SWITCH_MODE_TIMEOUT 3000       // 切換模式超時時間 (3秒)

// 按鈕事件檢測相關變量
ButtonEventType currentButtonEvent = NO_EVENT; // 當前檢測到的按鈕事件
ButtonEventType currentButton2Event = NO_EVENT; // 第二個按鈕事件

// 螺旋槳模式相關變量
bool propellerMode = false;           // 是否處於螺旋槳模式
int8_t propellerValue = 0;            // 螺旋槳值 (-90 到 90)
bool propellerRampingDown = false;    // 是否正在漸減螺旋槳值
unsigned long propellerRampStartTime = 0; // 漸減開始時間
int8_t propellerRampStartValue = 0;   // 漸減開始值
const unsigned long PROPELLER_RAMP_DURATION = 2000; // 漸減持續時間 (2秒)

// 螺旋槳模式下凍結的編碼器值
int frozenEncoder1Value = 0;
int frozenEncoder1Norm = 0;

// 雙擊檢測相關變量
unsigned long lastClickTime = 0;      // 上次點擊時間
bool waitingForDoubleClick = false;   // 是否等待第二次點擊
const unsigned long DOUBLE_CLICK_TIMEOUT = 500; // 雙擊超時時間 (500ms)
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

// DRV8833螺旋槳馬達控制
#define MOTOR1_PIN1 D3                 // 馬達1正轉
#define MOTOR1_PIN2 D4                 // 馬達1反轉
#define MOTOR2_PIN1 D5                 // 馬達2正轉
#define MOTOR2_PIN2 D6                 // 馬達2反轉
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

  // 重新添加當前接收端作為對等點
  #ifdef DEVICE_ROLE_SENDER
  // 添加當前選擇的接收端作為對等點
  memset(&peerInfo, 0, sizeof(peerInfo));
  memcpy(peerInfo.peer_addr, receiverMacAddresses[currentReceiver], 6);
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

// 切換接收端函數
bool switchReceiver(int newReceiver) {
  #ifdef DEVICE_ROLE_SENDER
  if (newReceiver < 0 || newReceiver >= NUM_RECEIVERS) {
    Serial.println("Invalid receiver index");
    return false;
  }
  
  if (newReceiver == currentReceiver) {
    Serial.println("Already using this receiver");
    return true;
  }
  
  // 移除當前對等點
  esp_err_t removeStatus = esp_now_del_peer(receiverMacAddresses[currentReceiver]);
  if (removeStatus != ESP_OK) {
    Serial.print("Failed to remove current peer: ");
    Serial.println(getESPErrorMsg(removeStatus));
  }
  
  // 添加新的對等點
  memset(&peerInfo, 0, sizeof(peerInfo));
  memcpy(peerInfo.peer_addr, receiverMacAddresses[newReceiver], 6);
  peerInfo.channel = ESP_NOW_CHANNEL;
  peerInfo.encrypt = ESP_NOW_ENCRYPT;
  
  esp_err_t addStatus = esp_now_add_peer(&peerInfo);
  if (addStatus != ESP_OK) {
    Serial.print("Failed to add new peer: ");
    Serial.println(getESPErrorMsg(addStatus));
    Serial.print("Staying on current receiver: ");
    Serial.println(receiverNames[currentReceiver]);
    
    // 重新添加原來的對等點
    memcpy(peerInfo.peer_addr, receiverMacAddresses[currentReceiver], 6);
    esp_now_add_peer(&peerInfo);
    return false;
  }
  
  // 成功切換
  currentReceiver = newReceiver;
  Serial.print("Switched to receiver: ");
  Serial.println(receiverNames[currentReceiver]);
  
  // 重置新接收端的連接狀態
  ackReceived[currentReceiver] = false;
  lastAckTime[currentReceiver] = 0;
  lastReceivedRSSI[currentReceiver] = -100;
  
  return true;
  #endif
  return false;
}

// 將RSSI轉換為信號強度級別(0-4)
// 將RSSI值轉換為訊號強度百分比 (0-100%)
int rssiToSignalStrength(int rssi) {
  // RSSI 通常在 -30 (很強) 到 -90 (很弱) 之間
  // 轉換為0-100%的訊號強度
  int strength = constrain(map(rssi, -90, -30, 0, 100), 0, 100);
  return strength;
}

// 發送數據時的回調函數
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  #ifdef DEVICE_ROLE_SENDER
  lastSendSuccess = (status == ESP_NOW_SEND_SUCCESS);
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);

  if (status == ESP_NOW_SEND_SUCCESS) {
    retryCount = 0; // 成功後重置重試計數器
  } else {

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

  // ACK send status (removed verbose logging)
  #endif
}

// 發送ESP-NOW數據
bool sendESPNowData() {
  #ifdef DEVICE_ROLE_SENDER
  if (retryCount > 0 && retryCount <= MAX_RETRY_COUNT) {
    // 這是一個重試發送
    Serial.println("Retrying send...");
  }

  esp_err_t result = esp_now_send(receiverMacAddresses[currentReceiver], (uint8_t *)&rotaryData, sizeof(rotaryData));

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
  int servoAngle1 = map(value1, ROTARY_MIN_VALUE, ROTARY_MAX_VALUE, 0, 180);
  int servoAngle2 = map(value2, ROTARY_MIN_VALUE, ROTARY_MAX_VALUE, 0, 180);
  servoAngle2 = 180 - servoAngle2; // 因為上下顛倒安裝，所以轉動方向相反。

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

// 控制螺旋槳馬達
void controlPropeller(int8_t propellerValue) {
  #ifdef DEVICE_ROLE_RECEIVER
  // 螺旋槳值範圍 -90 到 90
  // 0 = 停止，正值 = 前進，負值 = 後退
  
  if (propellerValue == 0) {
    // 停止所有馬達
    analogWrite(MOTOR1_PIN1, 0);
    analogWrite(MOTOR1_PIN2, 0);
    analogWrite(MOTOR2_PIN1, 0);
    analogWrite(MOTOR2_PIN2, 0);
  } else if (propellerValue > 0) {
    // 前進：兩個馬達都向前轉
    int pwmValue = map(propellerValue, 0, 90, 0, 255);
    analogWrite(MOTOR1_PIN1, pwmValue);
    analogWrite(MOTOR1_PIN2, 0);
    analogWrite(MOTOR2_PIN1, pwmValue);
    analogWrite(MOTOR2_PIN2, 0);
    
    Serial.print("Propeller forward: ");
    Serial.print(propellerValue);
    Serial.print(" (PWM: ");
    Serial.print(pwmValue);
    Serial.println(")");
  } else {
    // 後退：兩個馬達都向後轉
    int pwmValue = map(-propellerValue, 0, 90, 0, 255);
    analogWrite(MOTOR1_PIN1, 0);
    analogWrite(MOTOR1_PIN2, pwmValue);
    analogWrite(MOTOR2_PIN1, 0);
    analogWrite(MOTOR2_PIN2, pwmValue);
    
    Serial.print("Propeller reverse: ");
    Serial.print(propellerValue);
    Serial.print(" (PWM: ");
    Serial.print(pwmValue);
    Serial.println(")");
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
  int servoAngle1 = map(rotaryData.encoder1_norm, ROTARY_MIN_VALUE, ROTARY_MAX_VALUE, 0, 180);
  sprintf(buffer, "Enc1:%d Srv1:%d°", rotaryData.encoder1_norm, servoAngle1);
  u8g2.drawStr(0, 24, buffer);

  // 第2行: 編碼器2值和伺服2角度
  int servoAngle2 = map(rotaryData.encoder2_norm, ROTARY_MIN_VALUE, ROTARY_MAX_VALUE, 0, 180);
  sprintf(buffer, "Enc2:%d Srv2:%d°", rotaryData.encoder2_norm, servoAngle2);
  u8g2.drawStr(0, 36, buffer);

  // 第3行: 按鈕狀態
  sprintf(buffer, "Btn1:%s Btn2:%s",
          rotaryData.button_state ? "ON" : "OFF",
          rotaryData.button2_state ? "ON" : "OFF");
  u8g2.drawStr(0, 48, buffer);

  // 第4行: 連接狀態
  unsigned long timeSinceLastData = millis() - lastDataReceivedTime;
  sprintf(buffer, "Link: %s", timeSinceLastData < 2000 ? "Connected" : "Disconnected");
  u8g2.drawStr(0, 60, buffer);

  u8g2.sendBuffer();
  #endif
}

// 接收端切換顯示函數
void updateReceiverSwitchOLED() {
  #ifdef DEVICE_ROLE_SENDER
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x12_tr);
  
  // 標題
  u8g2.drawStr(0, 10, "Select Receiver:");
  u8g2.drawLine(0, 12, 128, 12);
  
  char buffer[32];
  
  // 顯示接收端列表
  for (int i = 0; i < NUM_RECEIVERS; i++) {
    int yPos = 26 + (i * 12);
    
    // 當前選中的接收端用箭頭標示
    if (i == currentReceiver) {
      u8g2.drawStr(0, yPos, ">");
    }
    
    // 接收端名稱
    u8g2.drawStr(10, yPos, receiverNames[i]);
    
    // 連接狀態指示
    unsigned long currentMillis = millis();
    if (ackReceived[i] && (currentMillis - lastAckTime[i] < 10000)) {
      sprintf(buffer, "OK(%d)", rssiToSignalStrength(lastReceivedRSSI[i]));
      u8g2.drawStr(80, yPos, buffer);
    } else {
      u8g2.drawStr(80, yPos, "---");
    }
  }
  
  // 顯示剩餘時間
  unsigned long remainingTime = SWITCH_MODE_TIMEOUT - (millis() - switchModeStartTime);
  sprintf(buffer, "Auto exit: %lus", remainingTime / 1000);
  u8g2.drawStr(0, 60, buffer);
  
  u8g2.sendBuffer();
  #endif
}

// 發送端OLED顯示函數 - 顯示ESP-NOW訊號強度
void updateSenderOLED() {
  #ifdef DEVICE_ROLE_SENDER
  
  // 如果處於接收端切換模式，顯示切換界面
  if (receiverSwitchMode) {
    updateReceiverSwitchOLED();
    return;
  }
  
  u8g2.clearBuffer();

  // 設置字體
  u8g2.setFont(u8g2_font_6x12_tr);

  // 顯示標題和當前接收端
  char titleBuffer[32];
  sprintf(titleBuffer, "Control->%s", receiverNames[currentReceiver]);
  u8g2.drawStr(0, 10, titleBuffer);
  u8g2.drawLine(0, 12, 128, 12);

  char buffer[32];

  // 顯示旋轉編碼器值或螺旋槳值
  if (propellerMode) {
    sprintf(buffer, "PROP:%d Enc2:%d",
            rotaryData.propeller,
            rotaryData.encoder2_norm);
  } else {
    sprintf(buffer, "Enc1:%d Enc2:%d",
            rotaryData.encoder1_norm,
            rotaryData.encoder2_norm);
  }
  u8g2.drawStr(0, 24, buffer);

  // 顯示最後發送狀態
  if (lastSendSuccess) {
    u8g2.drawStr(0, 36, "Status: OK");
  } else {
    u8g2.drawStr(0, 36, "Status: Fail");
  }

  // 顯示按鈕狀態
  sprintf(buffer, "Btn1:%s Btn2:%s",
          rotaryData.button_state ? "ON" : "OFF",
          rotaryData.button2_state ? "ON" : "OFF");
  u8g2.drawStr(0, 48, buffer);

  // 檢查當前接收端的ACK狀態 (10秒內)
  unsigned long currentMillis = millis();
  if (ackReceived[currentReceiver] && (currentMillis - lastAckTime[currentReceiver] < 10000)) {
    // 顯示RSSI信息
    int signalStrength = rssiToSignalStrength(lastReceivedRSSI[currentReceiver]);
    sprintf(buffer, "RSSI: %d dBm (%d%%)", lastReceivedRSSI[currentReceiver], signalStrength);
    u8g2.drawStr(0, 60, buffer);

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

  if (propellerMode || propellerRampingDown) {
    // 螺旋槳模式或漸減期間：編碼器值保持凍結
    if (propellerMode && !propellerRampingDown) {
      // 只有在螺旋槳模式且不在漸減時才從編碼器讀取螺旋槳值
      propellerValue = constrain(rawValue1, -90, 90);
    }
    // 螺旋槳值在漸減時由主循環處理
    rotaryData.encoder1_value = frozenEncoder1Value;
    rotaryData.encoder1_norm = frozenEncoder1Norm;
  } else {
    // 正常模式：編碼器1控制普通值
    rotaryData.encoder1_value = rawValue1;
    rotaryData.encoder1_norm = rawValue1;
  }

  // 讀取第二個旋轉編碼器
  int rawValue2 = rotaryEncoder2.readEncoder();

  // 設置第二個編碼器的值
  rotaryData.encoder2_value = rawValue2;
  rotaryData.encoder2_norm = rawValue2;

  // 讀取當前按鈕狀態
  rotaryData.button_state = rotaryEncoder.isEncoderButtonDown();
  rotaryData.button2_state = rotaryEncoder2.isEncoderButtonDown();
  
  // 設置螺旋槳值
  rotaryData.propeller = propellerValue;
  #endif
}

// 接收數據的回調函數
void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int data_len) {
  #ifdef DEVICE_ROLE_SENDER
  // 發送端處理接收到的ACK數據
  const uint8_t *mac_addr = info->src_addr;
  if (data_len == sizeof(ack_message)) {
    ack_message tempAck;
    memcpy(&tempAck, data, sizeof(ack_message));
    
    // 找到對應的接收端
    int receiverIndex = -1;
    for (int i = 0; i < NUM_RECEIVERS; i++) {
      if (memcmp(mac_addr, receiverMacAddresses[i], 6) == 0) {
        receiverIndex = i;
        break;
      }
    }
    
    if (receiverIndex >= 0) {
      // 更新對應接收端的狀態
      memcpy(&receivedAck[receiverIndex], &tempAck, sizeof(ack_message));
      lastReceivedRSSI[receiverIndex] = tempAck.rssi;
      ackReceived[receiverIndex] = true;
      lastAckTime[receiverIndex] = millis();
      
      // ACK received (removed verbose logging)
    } else {
      Serial.println("Received ACK from unknown receiver");
    }
  } else {
    Serial.print("Received unknown data of size: ");
    Serial.println(data_len);
  }
  #else
  // 接收端處理邏輯
  // 獲取RSSI值 (訊號強度)
  int receivedRSSI = info->rx_ctrl->rssi;
  lastRSSI = receivedRSSI;

  // 更新最大最小RSSI值
  if (lastRSSI > maxRSSI) maxRSSI = lastRSSI;
  if (lastRSSI < minRSSI) minRSSI = lastRSSI;

  // 處理接收到的搖桿數據
  static rotary_message rotaryData;
  if (data_len == sizeof(rotary_message)) {
    memcpy(&rotaryData, data, sizeof(rotary_message));

    // 更新數據接收時間和狀態
    lastDataReceivedTime = millis();
    dataReceived = true;

    // 更新封包統計
    totalPackets++;

    // 檢查是否有丟失的封包
    if (totalPackets > 1 && rotaryData.msg_id > lastMsgId + 1) {
      uint32_t newLostPackets = rotaryData.msg_id - lastMsgId - 1;
      lostPackets += newLostPackets;

      Serial.print("Packet(s) lost: ");
      Serial.println(newLostPackets);
    }

    // 更新上次接收的消息ID
    lastMsgId = rotaryData.msg_id;

    // 計算封包遺失率
    if (totalPackets > 0) {
      packetLossRate = (float)lostPackets / (totalPackets + lostPackets) * 100.0;
    }

    // 控制伺服馬達
    controlServos(rotaryData.encoder1_norm, rotaryData.encoder2_norm);
    
    // 控制螺旋槳馬達
    controlPropeller(rotaryData.propeller);

    // Data received (removed verbose logging)

    // 處理按鈕事件
    ButtonEventType receivedEvent = (ButtonEventType)rotaryData.button_event;
    ButtonEventType receivedEvent2 = (ButtonEventType)rotaryData.button2_event;
    if (receivedEvent != NO_EVENT) {
      Serial.print("Received Button1 Event: ");
      Serial.println(receivedEvent);
    }
    if (receivedEvent2 != NO_EVENT) {
      Serial.print("Received Button2 Event: ");
      Serial.println(receivedEvent2);
    }



    // 發送ACK回應 - 使用接收到的數據包的RSSI值
    ackData.rssi = info->rx_ctrl->rssi;  // 使用數據包的RSSI而不是WiFi.RSSI()
    ackData.ack_id = rotaryData.msg_id;

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
  rotaryEncoder.setAcceleration(50); // 設置加速度
  rotaryEncoder.setEncoderValue(ROTARY_INITIAL_VALUE);
  // 長按檢測將使用自定義邏輯實現

  // 初始化第二個旋轉編碼器
  rotaryEncoder2.begin();
  rotaryEncoder2.setup(readEncoder2ISR);
  rotaryEncoder2.setBoundaries(ROTARY_MIN_VALUE, ROTARY_MAX_VALUE, false); // 不循環
  rotaryEncoder2.setAcceleration(50); // 設置加速度
  rotaryEncoder2.setEncoderValue(ROTARY_INITIAL_VALUE);

  #else
  Serial.println("\n\n=== ESP32 ESP-NOW - RECEIVER MODE with OLED and Servo ===");

  // 初始化第一個伺服馬達
  steeringServo.setPeriodHertz(50);    // 標準50Hz伺服馬達
  steeringServo.attach(SERVO_PIN, 500, 2500); // 附加伺服馬達(引腳, 最小脈寬, 最大脈寬)
  steeringServo.write(90); // 初始位置居中
  lastServoAngle = 90;

  // 初始化第二個伺服馬達
  steeringServo2.setPeriodHertz(50);   // 標準50Hz伺服馬達
  steeringServo2.attach(SERVO2_PIN, 500, 2500); // 附加第二個伺服馬達
  steeringServo2.write(90); // 初始位置居中
  lastServoAngle2 = 90;

  // 初始化螺旋槳馬達控制引腳
  pinMode(MOTOR1_PIN1, OUTPUT);
  pinMode(MOTOR1_PIN2, OUTPUT);
  pinMode(MOTOR2_PIN1, OUTPUT);
  pinMode(MOTOR2_PIN2, OUTPUT);
  
  // 確保馬達初始時停止
  controlPropeller(0);

  Serial.println("Both servos initialized at center position (90°), propeller motors initialized");
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

  // 添加默認接收端作為對等點
  memset(&peerInfo, 0, sizeof(peerInfo));
  memcpy(peerInfo.peer_addr, receiverMacAddresses[currentReceiver], 6);
  peerInfo.channel = ESP_NOW_CHANNEL;
  peerInfo.encrypt = ESP_NOW_ENCRYPT;
  esp_err_t addStatus = esp_now_add_peer(&peerInfo);

  if (addStatus != ESP_OK) {
    Serial.print("添加對等點失敗: ");
    Serial.println(getESPErrorMsg(addStatus));
  } else {
    Serial.print("對等點添加成功: ");
    Serial.println(receiverNames[currentReceiver]);
  }
  
  // 初始化所有接收端的連接狀態
  for (int i = 0; i < NUM_RECEIVERS; i++) {
    ackReceived[i] = false;
    lastAckTime[i] = 0;
    lastReceivedRSSI[i] = -100;
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
  // 處理螺旋槳漸減邏輯
  if (propellerRampingDown) {
    unsigned long elapsedTime = millis() - propellerRampStartTime;
    if (elapsedTime >= PROPELLER_RAMP_DURATION) {
      // 漸減完成，恢復編碼器位置
      propellerValue = 0;
      propellerRampingDown = false;
      rotaryEncoder.setEncoderValue(frozenEncoder1Value);
      Serial.println("Propeller ramp down completed, encoder position restored");
    } else {
      // 計算當前漸減值
      float progress = (float)elapsedTime / PROPELLER_RAMP_DURATION;
      propellerValue = propellerRampStartValue * (1.0 - progress);
    }
  }
  
  // 處理旋轉編碼器數據
  handleRotaryEncoders();

  // 雙擊檢測邏輯
  if (rotaryEncoder.isEncoderButtonClicked()) {
    unsigned long currentTime = millis();
    
    if (waitingForDoubleClick && (currentTime - lastClickTime <= DOUBLE_CLICK_TIMEOUT)) {
      // 雙擊事件
      waitingForDoubleClick = false;
      
      if (propellerMode) {
        // 退出螺旋槳模式，開始漸減
        propellerMode = false;
        propellerRampingDown = true;
        propellerRampStartTime = millis();
        propellerRampStartValue = propellerValue;
        Serial.println("Exiting propeller mode - ramping down");
      } else {
        // 進入螺旋槳模式
        propellerMode = true;
        propellerValue = 0;
        propellerRampingDown = false;
        // 凍結當前編碼器值
        frozenEncoder1Value = rotaryData.encoder1_value;
        frozenEncoder1Norm = rotaryData.encoder1_norm;
        // 將編碼器位置設為0以開始螺旋槳控制
        rotaryEncoder.setEncoderValue(0);
        Serial.println("Entering propeller mode");
      }
    } else {
      // 第一次點擊，開始等待雙擊
      waitingForDoubleClick = true;
      lastClickTime = currentTime;
    }
  }
  
  // 檢查雙擊超時
  if (waitingForDoubleClick && (millis() - lastClickTime > DOUBLE_CLICK_TIMEOUT)) {
    // 單擊事件（雙擊超時）
    waitingForDoubleClick = false;
    
    if (propellerMode) {
      propellerValue = 0;
      Serial.println("Propeller value reset to 0");
    }
    currentButtonEvent = SINGLE_CLICK;
  }

  if (rotaryEncoder2.isEncoderButtonClicked()) {
    // 處理接收端切換邏輯
    if (receiverSwitchMode) {
      // 在切換模式中，切換到下一個接收端
      int nextReceiver = (currentReceiver + 1) % NUM_RECEIVERS;
      if (switchReceiver(nextReceiver)) {
        receiverSwitchMode = false; // 切換成功，退出切換模式
      }
    } else {
      // 進入接收端切換模式
      receiverSwitchMode = true;
      switchModeStartTime = millis();
      Serial.println("Entering receiver switch mode");
    }
    currentButton2Event = SINGLE_CLICK;
    Serial.println("Button2 Event: SINGLE_CLICK");
  }
  
  // 處理接收端切換模式超時
  if (receiverSwitchMode && (millis() - switchModeStartTime > SWITCH_MODE_TIMEOUT)) {
    receiverSwitchMode = false;
    Serial.println("Receiver switch mode timeout, returning to normal mode");
  }

  // 更新按鈕事件
  rotaryData.button_event = (uint8_t)currentButtonEvent;
  rotaryData.button2_event = (uint8_t)currentButton2Event;

  // 更新OLED顯示
  if (millis() - lastOLEDUpdateTime > 100) {
    updateSenderOLED();
    lastOLEDUpdateTime = millis();

    // 檢查當前接收端是否長時間未收到ACK
    if (ackReceived[currentReceiver] && (millis() - lastAckTime[currentReceiver] > 30000)) {
      Serial.print("No ACK received from ");
      Serial.print(receiverNames[currentReceiver]);
      Serial.println(" for a long time, re-initializing ESP-NOW");
      reinitESPNow();
      ackReceived[currentReceiver] = false;
    }
  }

  // 發送數據
  if (millis() - lastDataSentTime > dataInterval) {
    // 更新消息ID
    rotaryData.msg_id = messageCounter++;

    // 發送數據
    sendESPNowData();

    // 重置按鈕事件狀態，避免重複發送
    currentButtonEvent = NO_EVENT;
    currentButton2Event = NO_EVENT;

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

    // 數據超時時將兩個伺服都重置到中心位置，螺旋槳停止
    steeringServo.write(90);
    steeringServo2.write(90);
    lastServoAngle = 90;
    lastServoAngle2 = 90;
    controlPropeller(0); // 停止螺旋槳
    Serial.println("Timeout - Both servos reset to center position, propeller stopped");
  }
  #endif

  // 允許ESP32處理背景任務
  delay(1);
}
