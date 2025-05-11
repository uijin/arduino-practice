#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <U8g2lib.h>
#include <Wire.h>

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

// 定義類比搖桿的引腳
#define JOYSTICK_X_PIN A0     // 類比搖桿X軸
#define JOYSTICK_Y_PIN A1     // 類比搖桿Y軸
#define JOYSTICK_BUTTON_PIN D2 // 搖桿按鈕連接到D2

// **重要** 在發送端模式，設置接收端的MAC地址（從接收端的Serial輸出獲取）
// 示例格式: {0x24, 0x0A, 0xC4, 0x9A, 0x58, 0x28}
// uint8_t receiverMacAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}; // 替換為接收端的實際MAC地址
uint8_t receiverMacAddress[] = {0xD8, 0x3B, 0xDA, 0x74, 0x1C, 0xEC}; // 替換為接收端的實際MAC地址

// 搖桿校正值 - 將這些定義移到使用它們的全局變量之前
#define JOYSTICK_CENTER_X 2047  // X軸中心位置 (將被接收端的可變中心值取代)
#define JOYSTICK_CENTER_Y 2047  // Y軸中心位置 (將被接收端的可變中心值取代)

// Sender端使用的固定中心值 (如果需要與接收端校準值不同)
#define JOYSTICK_CENTER_X_SENDER 2047  // Sender X軸中心位置
#define JOYSTICK_CENTER_Y_SENDER 2047  // Sender Y軸中心位置

#ifdef DEVICE_ROLE_SENDER
// 校準後的搖桿中心值 (發送端使用)
int calibrated_joystick_center_x_sender = JOYSTICK_CENTER_X_SENDER; // 初始化為默認中心值
int calibrated_joystick_center_y_sender = JOYSTICK_CENTER_Y_SENDER; // 初始化為默認中心值
#endif

// 在接收端模式，存儲發送端的MAC地址
#ifdef DEVICE_ROLE_RECEIVER
bool senderRegistered = false;         // 發送端是否已註冊為對等設備
uint8_t displayPage = 1;               // 當前顯示頁面 (0: RSSI/Packets, 1: Joystick)

// 校準後的搖桿中心值 (接收端使用)
int calibrated_joystick_center_x = JOYSTICK_CENTER_X; // 初始化為默認中心值
int calibrated_joystick_center_y = JOYSTICK_CENTER_Y; // 初始化為默認中心值
#endif

// 定義按鈕事件類型
typedef enum {
  NO_EVENT = 0,
  SINGLE_CLICK,
  DOUBLE_CLICK,
  LONG_PRESS
} ButtonEventType;

// ESP-NOW通信參數
#define ESP_NOW_CHANNEL 1      // 設置ESP-NOW頻道 (1-14)，兩端必須相同
#define ESP_NOW_ENCRYPT false  // 是否加密傳輸
#define MAX_RETRY_COUNT 3      // 發送失敗時的最大重試次數

#define JOYSTICK_MAX 4095      // ESP32S3的12位ADC最大值
#define JOYSTICK_DEADZONE 50   // 死區，忽略小幅度移動

// 定義數據結構
typedef struct joystick_message {
  int x_value;         // 原始X值
  int y_value;         // 原始Y值
  int x_normalized;    // 正規化後的X值 (-100 到 100)
  int y_normalized;    // 正規化後的Y值 (-100 到 100)
  bool button_state;   // 按鈕當前物理狀態 (Pressed/Released)
  uint32_t msg_id;     // 消息ID，用於追蹤
  uint8_t button_event; // 按鈕觸發的事件類型 (0:NO_EVENT, 1:SINGLE, 2:DOUBLE, 3:LONG)
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
char positionText[20] = "Position: None"; // 位置文本

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
// uint8_t displayPage = 0; // 已在 #ifdef DEVICE_ROLE_RECEIVER 內部定義，此處移除
unsigned long lastPageSwitchTime = 0;  // 上次切換顯示頁面的時間
const unsigned long pageSwitchInterval = 5000; // 自動切換顯示頁面的間隔
bool buttonPressedLast_receiver = false; // 用於接收端按鈕邏輯 (之前叫buttonPressedLast，為避免混淆改名)
#endif

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

  // 註冊對等設備
  // 設置對等設備信息
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

#ifdef DEVICE_ROLE_SENDER
// 發送狀態回調函數 - 僅發送端使用
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac_addr[0], mac_addr[1], mac_addr[2],
           mac_addr[3], mac_addr[4], mac_addr[5]);

  Serial.print("Last Packet Send Status to ");
  Serial.print(macStr);
  Serial.print(": ");

  if (status == ESP_NOW_SEND_SUCCESS) {
    Serial.println("Delivery Success");
    lastSendSuccess = true;
    retryCount = 0; // 成功後重置重試計數
  } else {
    Serial.println("Delivery Failed");
    lastSendSuccess = false;

    // 發送失敗時嘗試重新註冊對等設備
    #ifdef DEVICE_ROLE_RECEIVER
    // 如果是接收端發送ACK失敗
    Serial.println("Trying to re-register sender as peer");
    #endif
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

// 繪製水平進度條
void drawProgressBar(int x, int y, int width, int height, int percentage) {
  // 繪製邊框
  u8g2.drawFrame(x, y, width, height);

  // 繪製填充部分
  int fillWidth = map(percentage, 0, 100, 0, width-2);
  if (fillWidth > 0) {
    u8g2.drawBox(x+1, y+1, fillWidth, height-2);
  }
}

// 更新OLED顯示 - 第一頁：RSSI和SNR信息
void updateOLEDPage0() {
  u8g2.clearBuffer();

  // 設置字體
  u8g2.setFont(u8g2_font_6x12_tr);

  // 標題
  u8g2.drawStr(0, 10, "Signal & Packets"); // Changed title
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
}

// 更新OLED顯示 - 第二頁：封包統計和搖桿數據
void updateOLEDPage1() {
  u8g2.clearBuffer();

  // 設置字體
  u8g2.setFont(u8g2_font_6x12_tr);

  // 標題
  u8g2.drawStr(0, 10, "Joystick Info"); // Changed title
  u8g2.drawLine(0, 12, 128, 12);

  // 檢查是否有數據以及數據是否超時
  if (!dataReceived || (millis() - lastDataReceivedTime > dataTimeout)) {
    u8g2.drawStr(0, 30, "Waiting for data...");
    return;
  }

  // 顯示搖桿數據
  char buffer[40]; // Increased buffer size for longer strings

  // 第1行: Raw X 和 Normalized X
  sprintf(buffer, "X:%4d NX:%4d", joystickData.x_value, joystickData.x_normalized);
  u8g2.drawStr(0, 24, buffer);

  // 第2行: Raw Y 和 Normalized Y
  sprintf(buffer, "Y:%4d NY:%4d", joystickData.y_value, joystickData.y_normalized);
  u8g2.drawStr(0, 36, buffer);

  // 第3行: 按鈕狀態
  sprintf(buffer, "Button: %s", joystickData.button_state ? "Pressed" : "Released");
  u8g2.drawStr(0, 48, buffer);

  // 第4行: 位置
  String position = "";
  if (abs(joystickData.x_normalized) < 10 && abs(joystickData.y_normalized) < 10) {
    position = "Center";
  } else {
    if (joystickData.y_normalized < -30) position += "Up";
    else if (joystickData.y_normalized > 30) position += "Down";

    if (joystickData.x_normalized < -30) {
      if (position.length() > 0) position += " ";
      position += "Left";
    }
    else if (joystickData.x_normalized > 30) {
      if (position.length() > 0) position += " ";
      position += "Right";
    }
  }
  sprintf(buffer, "Position: %s", position.c_str());
  u8g2.drawStr(0, 60, buffer);
}
#endif // DEVICE_ROLE_RECEIVER

// 發送端OLED顯示函數 - 顯示ESP-NOW訊號強度
#ifdef DEVICE_ROLE_SENDER
void updateSenderOLED() {
  u8g2.clearBuffer();

  // 設置字體
  u8g2.setFont(u8g2_font_6x12_tr);

  // 顯示標題
  u8g2.drawStr(0, 10, "ESP-NOW Signal Strength");
  u8g2.drawLine(0, 12, 128, 12);

  char buffer[32];

  // 顯示消息計數器
  sprintf(buffer, "Msg Count: %lu", messageCounter);
  u8g2.drawStr(0, 24, buffer);

  // 顯示最後發送狀態
  if (lastSendSuccess) {
    u8g2.drawStr(0, 36, "Status: Success");
  } else {
    u8g2.drawStr(0, 36, "Status: Failed");
  }

  // 檢查是否收到ACK (10秒內)
  unsigned long currentMillis = millis();
  if (ackReceived && (currentMillis - lastAckTime < 10000)) {
    // 顯示RSSI信息
    int signalStrength = rssiToSignalStrength(lastReceivedRSSI);
    sprintf(buffer, "RSSI: %d dBm (%d%%)", lastReceivedRSSI, signalStrength);
    u8g2.drawStr(0, 48, buffer);

    // 繪製訊號強度圖標
    // drawSignalStrength(100, 48, signalStrength);

    sprintf(buffer, "ACK ID: %lu", receivedAck.ack_id);
    u8g2.drawStr(0, 60, buffer);
  } else {
    u8g2.drawStr(0, 48, "Waiting for ACK...");
    if (ackReceived) {
      sprintf(buffer, "Last ACK: %lu", receivedAck.ack_id);
      u8g2.drawStr(0, 60, buffer);
    }
  }

  u8g2.sendBuffer();
}
#endif

#ifdef DEVICE_ROLE_RECEIVER
// 更新OLED顯示 - 選擇當前頁面
void updateOLEDDisplay() {
  // 根據當前頁面選擇顯示函數
  if (displayPage == 0) {
    updateOLEDPage0();
  } else {
    updateOLEDPage1();
  }

  // 發送緩衝區內容到顯示器
  u8g2.sendBuffer();
}
#endif

// 數據接收回調函數 - 僅接收端使用
void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *incomingData, int len) {
  // 獲取發送者的MAC地址
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
           recv_info->src_addr[0], recv_info->src_addr[1], recv_info->src_addr[2],
           recv_info->src_addr[3], recv_info->src_addr[4], recv_info->src_addr[5]);

  #ifdef DEVICE_ROLE_RECEIVER
  // 不在這裡保存MAC地址，我們會在發送ACK時保存
  #endif

  Serial.print("Received from: ");
  Serial.println(macStr);

  // 獲取RSSI值 (訊號強度)
  int receivedRSSI = recv_info->rx_ctrl->rssi;
  Serial.print("RSSI: ");
  Serial.print(receivedRSSI);
  Serial.println(" dBm");

  #ifdef DEVICE_ROLE_RECEIVER
  // 接收端處理邏輯
  lastRSSI = receivedRSSI;

  // 更新最大最小RSSI值
  if (lastRSSI > maxRSSI) maxRSSI = lastRSSI;
  if (lastRSSI < minRSSI) minRSSI = lastRSSI;

  // 處理接收到的搖桿數據
  if (len == sizeof(joystickData)) {
    memcpy(&joystickData, incomingData, sizeof(joystickData));

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

    // 輸出接收到的數據
    Serial.print("Bytes received: ");
    Serial.print(len);
    Serial.print(", Msg ID: ");
    Serial.println(joystickData.msg_id);

    Serial.print("Raw - X: ");
    Serial.print(joystickData.x_value);
    Serial.print(", Y: ");
    Serial.print(joystickData.y_value);
    Serial.print(", Button: ");
    Serial.println(joystickData.button_state ? "Pressed" : "Released");

    // 更新位置文本並在串口顯示
    updatePositionText();
    Serial.println(positionText);

    // 輸出封包統計
      Serial.print("Packet Stats - Total: ");
      Serial.print(totalPackets);
      Serial.print(", Lost: ");
      Serial.print(lostPackets);
      Serial.print(", Loss Rate: ");
      Serial.print(packetLossRate, 1);
      Serial.println("%");

      // --- 處理按鈕事件 ---
      ButtonEventType receivedEvent = (ButtonEventType)joystickData.button_event;
      if (receivedEvent != NO_EVENT) {
        Serial.print("Received Button Event: ");
        Serial.println(receivedEvent);
      }

      if (receivedEvent == DOUBLE_CLICK) {
        displayPage = (displayPage + 1) % 2; // 切換顯示頁面
        lastPageSwitchTime = millis(); // 重置自動切換計時器，讓手動切換優先
        Serial.print("Receiver: OLED Page switched to ");
        Serial.println(displayPage);
      } else if (receivedEvent == LONG_PRESS) {
        // Calibration is now done on the sender side.
        // Receiver will still log this event if Serial output for receivedEvent is active.
        Serial.println("Receiver: Received LONG_PRESS event. Calibration is handled by sender.");
      }
      // --- 按鈕事件處理結束 ---

      // 每隔至少2秒發送一次ACK封包
      unsigned long currentTime = millis();
    if (currentTime - lastAckSentTime >= 2000) { // 至少2秒發送一次
      // 準備ACK數據
      ackData.rssi = lastRSSI;
      ackData.ack_id = joystickData.msg_id;

      // 將發送者的MAC地址保存為接收端的目標地址
      static uint8_t senderMacAddress[6];
      memcpy(senderMacAddress, recv_info->src_addr, 6);

      // 直接使用當前發送者的MAC地址

      // 顯示發送者MAC地址以便調試
      Serial.print("Sending ACK to: ");
      char macStr2[32];
      snprintf(macStr2, sizeof(macStr2), "%02X:%02X:%02X:%02X:%02X:%02X",
               senderMacAddress[0], senderMacAddress[1], senderMacAddress[2],
               senderMacAddress[3], senderMacAddress[4], senderMacAddress[5]);
      Serial.println(macStr2);

      // 註冊發送端為對等設備
      esp_now_peer_info_t peerInfo;
      memset(&peerInfo, 0, sizeof(peerInfo));
      memcpy(peerInfo.peer_addr, senderMacAddress, 6);
      peerInfo.channel = ESP_NOW_CHANNEL;
      peerInfo.encrypt = ESP_NOW_ENCRYPT;

      // 總是先嘗試刪除再重新添加以確保正確註冊
      esp_now_del_peer(senderMacAddress);
      Serial.println("Registering sender as peer");

      // 添加發送端為對等設備
      Serial.println("Adding sender as peer");
      esp_err_t addStatus = esp_now_add_peer(&peerInfo);
      if (addStatus != ESP_OK) {
        Serial.print("Failed to add peer: ");
        Serial.println(getESPErrorMsg(addStatus));
      } else {
        Serial.println("Sender added as peer successfully");
        senderRegistered = true;
      }

      // 發送ACK封包
      esp_err_t result = esp_now_send(senderMacAddress, (uint8_t*)&ackData, sizeof(ackData));
      if (result == ESP_OK) {
        Serial.println("ACK packet sent with RSSI information");
        Serial.print("ACK contains RSSI: ");
        Serial.print(ackData.rssi);
        Serial.print(" dBm, Msg ID: ");
        Serial.println(ackData.ack_id);
        Serial.print("Sent to MAC: ");
        Serial.println(macStr2);
        lastAckSentTime = currentTime;
        senderRegistered = true;
      } else {
        Serial.print("Error sending ACK packet: ");
        Serial.println(getESPErrorMsg(result));

        // 如果發送失敗，嘗試再次發送
        if (result == ESP_ERR_ESPNOW_NOT_FOUND) {
          Serial.println("Peer not found, trying one more time");
          // 重新嘗試發送
          result = esp_now_send(senderMacAddress, (uint8_t*)&ackData, sizeof(ackData));
          if (result == ESP_OK) {
            Serial.println("ACK sent successfully on second attempt");
            lastAckSentTime = currentTime;
            senderRegistered = true;
          } else {
            Serial.print("Still failed on second attempt: ");
            Serial.println(getESPErrorMsg(result));
          }
        }
      }
    }
  } else {
    Serial.print("Error: Received data size doesn't match. Expected ");
    Serial.print(sizeof(joystickData));
    Serial.print(" but got ");
    Serial.println(len);
  }
  #endif

  #ifdef DEVICE_ROLE_SENDER
  // 發送端處理接收到的ACK封包
  if (len == sizeof(ack_message)) {
    memcpy(&receivedAck, incomingData, sizeof(ack_message));
    lastReceivedRSSI = receivedAck.rssi;
    ackReceived = true;
    lastAckTime = millis();

    Serial.print("ACK received for message ID: ");
    Serial.print(receivedAck.ack_id);
    Serial.print(", RSSI: ");
    Serial.print(receivedAck.rssi);
    Serial.println(" dBm");
  } else {
    Serial.print("Error: Received unexpected data size. Expected ");
    Serial.print(sizeof(ack_message));
    Serial.print(" but got ");
    Serial.println(len);
  }
  #endif
}

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
  Serial.println("Joystick Calibration Info (Initial):");
  Serial.print("Center X: ");
  Serial.print(calibrated_joystick_center_x_sender);
  Serial.print(", Center Y: ");
  Serial.print(calibrated_joystick_center_y_sender);
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

  // 初始化OLED顯示模組
  if (!u8g2.begin()) {
    Serial.println("OLED初始化失敗!");
  }
  u8g2.setFont(u8g2_font_6x12_tr);
  u8g2.clearBuffer();
  u8g2.drawStr(0, 24, "Initializing...");
  u8g2.sendBuffer();

  // 初始化ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");

    u8g2.clearBuffer();
    u8g2.drawStr(0, 20, "ESP-NOW Init Error!");
    u8g2.drawStr(0, 34, "Restarting...");
    u8g2.sendBuffer();

    delay(2000);
    ESP.restart(); // 如果初始化失敗，重啟設備
    return;
  } else {
      Serial.println("ESP-NOW initialized successfully");

      u8g2.drawStr(0, 58, "ESP-NOW Ready!");
      u8g2.sendBuffer();
  }

  #ifdef DEVICE_ROLE_SENDER
  // 發送端初始化
  // 註冊回調函數
  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv); // 註冊接收回調以接收ACK封包

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
  // esp_now_register_send_cb(OnDataSent); // 接收端也需要註冊發送回調來處理ACK
  Serial.println("Receiver Ready");

  // 初始化無線訊號相關變數
  minRSSI = 0;
  maxRSSI = -100;
  // 發送端未初始註冊
  senderRegistered = false;
  #endif

  // 打印WiFi詳細狀態
  printWiFiStatus();

  Serial.println("Setup Complete");

  // 讓用戶有時間看到初始化信息
  delay(2000);

  #ifdef DEVICE_ROLE_SENDER
  updateSenderOLED(); // 顯示初始OLED信息
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
  static unsigned long lastOLEDUpdateTime = 0;  // 追蹤上次更新OLED的時間

  // 讀取搖桿數據
  joystickData.x_value = analogRead(JOYSTICK_X_PIN);
  joystickData.y_value = analogRead(JOYSTICK_Y_PIN);
  bool currentButtonState = !digitalRead(JOYSTICK_BUTTON_PIN);  // 按鈕通常是低電平觸發
  joystickData.button_state = currentButtonState;

  // --- 按鈕事件檢測邏輯 ---
  currentButtonEvent = NO_EVENT; // 重置當前事件
  unsigned long currentTime = millis();

  if (currentButtonState != lastButtonState) {
    if (currentButtonState) { // 按鈕被按下
      buttonPressTime = currentTime;
      buttonClickCount++;
      longPressActive = false; // 重置長按標記
    } else { // 按鈕被釋放
      buttonReleaseTime = currentTime;
      if (!longPressActive) { // 只有在長按未被觸發時才判斷單擊/雙擊
        if (currentTime - buttonPressTime < doubleClickGap) {
          // 這是短按的一部分，等待看是否是雙擊
        } else {
          // 如果超過雙擊間隔但不足以構成雙擊的第二次點擊，則可能是單擊
          // 但我們在檢測到第二次按下時或超時後處理單擊/雙擊
        }
      }
    }
    lastButtonState = currentButtonState;
  }

  // 檢測雙擊和超時的單擊
  if (buttonClickCount > 0 && !currentButtonState && (currentTime - buttonPressTime > doubleClickGap)) {
    if (buttonClickCount == 1) {
      currentButtonEvent = SINGLE_CLICK;
      Serial.println("Button Event: SINGLE_CLICK");
    } else if (buttonClickCount >= 2) { // 處理雙擊及更多次點擊（通常只關心雙擊）
      currentButtonEvent = DOUBLE_CLICK;
      Serial.println("Button Event: DOUBLE_CLICK");
    }
    buttonClickCount = 0; // 重置點擊計數
  }

  // 檢測長按
  if (currentButtonState && !longPressActive && (currentTime - buttonPressTime >= longPressDuration)) {
    currentButtonEvent = LONG_PRESS;
    Serial.println("Button Event: LONG_PRESS");
    longPressActive = true; // 標記長按已觸發，避免重複觸發或觸發單擊/雙擊
    buttonClickCount = 0; // 長按後不應再觸發單擊/雙擊

    // Sender-side joystick calibration
    calibrated_joystick_center_x_sender = joystickData.x_value;
    calibrated_joystick_center_y_sender = joystickData.y_value;
    Serial.println("Sender: Joystick center recalibrated!");
    Serial.print("New Center X: ");
    Serial.print(calibrated_joystick_center_x_sender);
    Serial.print(", New Center Y: ");
    Serial.println(calibrated_joystick_center_y_sender);

    // Display calibration success on sender's OLED
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB10_tr); // Use a slightly larger font
    u8g2.drawStr(0, 10, "Center Calibrated!");
    char calibInfoSender[30];
    sprintf(calibInfoSender, "X:%d Y:%d", calibrated_joystick_center_x_sender, calibrated_joystick_center_y_sender);
    u8g2.drawStr(0, 25, calibInfoSender);
    u8g2.sendBuffer();
    // Note: A long delay here might affect responsiveness.
    // Consider making this a timed display handled by updateSenderOLED()
    // For now, keep it simple like the receiver's previous behavior.
    // unsigned long calibDisplayStartTime = millis();
    // while(millis() - calibDisplayStartTime < 2000) { /* allow ESP-NOW to process */ delay(1); }
    // Instead of a blocking delay, we could set a flag for updateSenderOLED to show this message for a duration.
    // For simplicity now, we'll just update OLED and continue. The main OLED update will overwrite it.
    // Or, if a short display is acceptable:
    // delay(2000); // Display for 2 seconds, then it will be overwritten by regular OLED update
    // To avoid blocking, this message will be shown until the next OLED update.
  }

  // 如果按鈕釋放了，並且之前是長按狀態，則重置 longPressActive
  if (!currentButtonState && longPressActive) {
      longPressActive = false;
  }

  joystickData.button_event = (uint8_t)currentButtonEvent;
  // --- 按鈕事件檢測邏輯結束 ---

  // 更新OLED顯示
  unsigned long currentMillis = millis();
  if (currentMillis - lastOLEDUpdateTime > 100) { // 每100毫秒更新一次OLED
    updateSenderOLED();
    lastOLEDUpdateTime = currentMillis;

    // 如果長時間沒有收到ACK，嘗試重新註冊接收端
    if (ackReceived && (currentMillis - lastAckTime > 30000)) { // 30秒無ACK
      Serial.println("No ACK received for a long time, re-initializing ESP-NOW");
      reinitESPNow();
      ackReceived = false; // 重置狀態以避免重複嘗試
    }
  }

  // 正規化搖桿值
  #if defined(DEVICE_ROLE_SENDER)
    // Sender 端使用校準後的中心值
    joystickData.x_normalized = normalizeJoystick(joystickData.x_value, calibrated_joystick_center_x_sender);
    joystickData.y_normalized = normalizeJoystick(joystickData.y_value, calibrated_joystick_center_y_sender);
  // #elif defined(DEVICE_ROLE_RECEIVER)
  //   // Receiver 端使用其自身的校準後的中心值 (這些值在接收端初始化，不再通過長按事件更新)
  //   joystickData.x_normalized = normalizeJoystick(joystickData.x_value, calibrated_joystick_center_x);
  //   joystickData.y_normalized = normalizeJoystick(joystickData.y_value, calibrated_joystick_center_y);
  #endif

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
  // 檢查搖桿按鈕狀態變化，用於手動切換顯示頁面 - 已移除，由Sender發送的事件觸發頁面切換
  // if (dataReceived && (millis() - lastDataReceivedTime < dataTimeout)) {
  //   bool buttonPressed = joystickData.button_state;
  //
  //   // 檢測按鈕狀態變化：從未按下到已按下
  //   if (buttonPressed && !buttonPressedLast_receiver) {
  //     // 切換顯示頁面
  //     // displayPage = (displayPage + 1) % 2; // 已由sender發送的雙擊事件處理
  //     // lastPageSwitchTime = millis(); // 重置自動切換計時器
  //
  //     // Serial.print("Display page changed to: ");
  //     // Serial.println(displayPage);
  //   }
  //   buttonPressedLast_receiver = buttonPressed;
  // }

  // 自動切換顯示頁面 - 如果需要保留，可以取消註釋，但手動切換優先級更高
  /*
  if (millis() - lastPageSwitchTime > pageSwitchInterval) {
    displayPage = (displayPage + 1) % 2;
    lastPageSwitchTime = millis();

    Serial.print("Auto-switching display page to: ");
    Serial.println(displayPage);
  }
  */

  // 定期更新OLED顯示
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
