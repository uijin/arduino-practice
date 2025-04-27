#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>
#include <math.h>
#include <ESP32Servo.h>
#include <esp_sleep.h>
#include "config_secret.h"
#include "SunPosition.h"

// #define DEBUG_MODE
// #define ENABLE_METRIC_COLLECT

// WiFi 設定
// const char* ssid = "YOUR_WIFI_SSID";
// const char* password = "YOUR_WIFI_PASSWORD";

// NTP 設定
const long gmtOffset_sec = 28800;  // UTC+8 = 8*3600
const int daylightOffset_sec = 0;

// VictoriaMetrics 設定
const char* vmHost = "http://192.168.50.10:8428";  // 替換為您的VictoriaMetrics服務器地址
const char* vmPath = "/api/v1/import/prometheus";
const char* deviceName = "solar_tracker_node1";  // 設備名稱

// VictoriaMetrics 指標收集間隔
const unsigned long METRICS_INTERVAL = 60000;  // 60秒
unsigned long lastMetricsTime = 0;

// 伺服馬達腳位 - 調整為ESP32C6針腳
const int HORIZONTAL_SERVO_PIN = 16;  // 更新為ESP32C6針腳
const int VERTICAL_SERVO_PIN = 17;    // 更新為ESP32C6針腳

// 手動模式針腳定義
const int MANUAL_RESET_POS_PIN = 20;    // 更新為ESP32C6針腳
const int MANUAL_CURRENT_POS_PIN = 19;  // 更新為ESP32C6針腳

// 高雄的經緯度
const double LATITUDE = 22.6273;
const double LONGITUDE = 120.3014;

// 伺服馬達範圍設定
const int HORIZONTAL_SERVO_MIN_ANGLE = 0;    // 水平伺服馬達最大角度為270度
const int HORIZONTAL_SERVO_MAX_ANGLE = 270;  // 水平伺服馬達最大角度為270度
const int VERTICAL_SERVO_MIN_ANGLE = 0;      // 垂直伺服馬達最大角度為180度
const int VERTICAL_SERVO_MAX_ANGLE = 180;    // 垂直伺服馬達最大角度為180度

// 伺服馬達脈衝範圍（微秒）
const int HORIZONTAL_SERVO_MIN_PULSE = 500;   // 對應0度位置
const int HORIZONTAL_SERVO_MAX_PULSE = 2500;  // 對應270度位置
const int VERTICAL_SERVO_MIN_PULSE = 580;     // 對應0度位置
const int VERTICAL_SERVO_MAX_PULSE = 2480;    // 對應180度位置

// 建立伺服馬達對象
Servo horizontalServo;
Servo verticalServo;

// 夜間模式設定
bool isNightMode = false;
double sunriseHorizontalAngle = 90;  // 預設日出角度
double sunriseVerticalAngle = 0;     // 預設日出仰角

// 伺服馬達位置追蹤變數
double currentHorizontalPosition = (HORIZONTAL_SERVO_MIN_ANGLE + HORIZONTAL_SERVO_MIN_ANGLE) / 2;  // 默認初始位置
double currentVerticalPosition = 0;                                                                // 默認初始位置
const double MAX_STEP = 5.0;                                                                       // 每次最大移動角度

// 南方水平角度: SolarTracker 假設水平 135 度正對南方，但機座貼著牆壁放置，很難正對南方。
const double HORIZONTAL_SOUTH_ANGLE = 113;  // 機座前方偏向東邊 22 度角
const double HORIZONTAL_SOUTH_ADD = HORIZONTAL_SOUTH_ANGLE - 135;

/**
 * 將角度轉換為微秒脈衝寬度
 * @param angle 要轉換的角度
 * @param minAngle 角度的最小值
 * @param maxAngle 角度的最大值
 * @param minPulse 最小脈衝寬度(微秒)
 * @param maxPulse 最大脈衝寬度(微秒)
 * @return 轉換後的脈衝寬度(微秒)
 */
int angleToPulse(double angle, double minAngle, double maxAngle, int minPulse, int maxPulse) {
  // 確保角度在範圍內
  angle = constrain(angle, minAngle, maxAngle);
  // 線性映射：將角度轉換為脈衝寬度
  return map(angle * 10, minAngle * 10, maxAngle * 10, minPulse, maxPulse);
}

inline int angleToPulseH(double angle) {
  return angleToPulse(angle, HORIZONTAL_SERVO_MIN_ANGLE, HORIZONTAL_SERVO_MAX_ANGLE,
                      HORIZONTAL_SERVO_MIN_PULSE, HORIZONTAL_SERVO_MAX_PULSE);
}

inline int angleToPulseV(double angle) {
  return angleToPulse(angle, VERTICAL_SERVO_MIN_ANGLE, VERTICAL_SERVO_MAX_ANGLE,
                      VERTICAL_SERVO_MIN_PULSE, VERTICAL_SERVO_MAX_PULSE);
}

/**
 * 發送太陽追蹤器數據到VictoriaMetrics
 * @param sunAzimuth 太陽方位角
 * @param sunElevation 太陽仰角
 * @param servoHorizontal 水平伺服馬達角度
 * @param servoVertical 垂直伺服馬達角度
 */
void sendMetricsToVM(double sunAzimuth, double sunElevation,
                     double servoHorizontal, double servoVertical) {

#ifndef ENABLE_METRIC_COLLECT
  return;
#endif

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi未連接，無法發送指標");
    WiFi.begin(ssid, password);
  } else {
    // 格式化Prometheus格式的指標
    String metrics = "";
    char valueStr[20];

    // 添加太陽位置指標
    snprintf(valueStr, sizeof(valueStr), "%.3f", sunAzimuth);
    metrics += "solar_tracker_sun_azimuth{device=\"" + String(deviceName) + "\"} " + String(valueStr) + "\n";
    snprintf(valueStr, sizeof(valueStr), "%.3f", sunElevation);
    metrics += "solar_tracker_sun_elevation{device=\"" + String(deviceName) + "\"} " + String(valueStr) + "\n";

    // 添加伺服馬達位置指標
    snprintf(valueStr, sizeof(valueStr), "%.3f", servoHorizontal);
    metrics += "solar_tracker_servo_horizontal{device=\"" + String(deviceName) + "\"} " + String(valueStr) + "\n";
    snprintf(valueStr, sizeof(valueStr), "%.3f", servoVertical);
    metrics += "solar_tracker_servo_vertical{device=\"" + String(deviceName) + "\"} " + String(valueStr) + "\n";

    // 添加夜間模式狀態指標
    snprintf(valueStr, sizeof(valueStr), "%d", isNightMode ? 1 : 0);
    metrics += "solar_tracker_night_mode{device=\"" + String(deviceName) + "\"} " + String(valueStr) + "\n";

    Serial.println("發送以下指標到VictoriaMetrics:");
    Serial.println(metrics);

    // 發送指標到VictoriaMetrics
    HTTPClient http;
    String url = String(vmHost) + vmPath;
    http.begin(url);
    http.addHeader("Content-Type", "text/plain");

    int httpCode = http.POST(metrics);
    if (httpCode > 0) {
      Serial.printf("HTTP響應代碼: %d\n", httpCode);
      if (httpCode == 204) {
        Serial.println("數據成功寫入!");
      }
    } else {
      Serial.printf("發送指標時出錯: %s\n", http.errorToString(httpCode).c_str());
    }
    http.end();
  }
}


class SolarTracker {
private:
  SunPosition sunCalculator;
  double lastHorizontalAngle = 90;  // 修改為有效的初始值，如90度（東方）

  /**
     * 漸進式更新伺服馬達位置，避免突然大幅度移動
     * @param targetH 目標水平角度
     * @param targetV 目標垂直角度
     */
  void updateServoPositions(double targetH, double targetV) {
    // 窗戶沒有正對南方，而是偏東方 22 度
    targetH += HORIZONTAL_SOUTH_ADD;
    while (currentHorizontalPosition != targetH || currentVerticalPosition != targetV) {

      // 計算水平方向移動差值
      double diffH = targetH - currentHorizontalPosition;
      // 標準化到 -180到+180範圍
      while (diffH > 180) diffH -= 360;
      while (diffH < -180) diffH += 360;

      // 限制單次移動量
      if (abs(diffH) > MAX_STEP) {
        diffH = (diffH > 0) ? MAX_STEP : -MAX_STEP;
      }
      // 更新當前位置
      currentHorizontalPosition += diffH;
      currentHorizontalPosition = constrain(currentHorizontalPosition, HORIZONTAL_SERVO_MIN_ANGLE, HORIZONTAL_SERVO_MAX_ANGLE);

      // 垂直方向同理
      double diffV = targetV - currentVerticalPosition;
      if (abs(diffV) > MAX_STEP) {
        diffV = (diffV > 0) ? MAX_STEP : -MAX_STEP;
      }
      currentVerticalPosition += diffV;
      currentVerticalPosition = constrain(currentVerticalPosition, VERTICAL_SERVO_MIN_ANGLE, VERTICAL_SERVO_MAX_ANGLE);

      // 使用微秒控制伺服馬達
      int horizontalPulse = angleToPulseH(currentHorizontalPosition);
      int verticalPulse = angleToPulseV(currentVerticalPosition);

      horizontalServo.writeMicroseconds(horizontalPulse);
      delay(500);
      verticalServo.writeMicroseconds(verticalPulse);
      delay(500);
      Serial.printf("伺服馬達位置：水平 %.1f°(%dµs) 垂直 %.1f°(%dµs)\n",
                    currentHorizontalPosition, horizontalPulse,
                    currentVerticalPosition, verticalPulse);
    }
  }

public:
  /**
     * 建構函數，初始化太陽追蹤器
     * @param latitude 緯度
     * @param longitude 經度
     */
  SolarTracker(double latitude, double longitude)
    : sunCalculator(latitude, longitude) {}

  /**
     * 將太陽方位角和仰角轉換為伺服馬達控制角度
     * @param azimuth 太陽方位角
     * @param elevation 太陽仰角
     * @param horizontalAngle 返回水平伺服馬達角度
     * @param verticalAngle 返回垂直伺服馬達角度
     */
  void convertToServoAngles(double azimuth, double elevation,
                            double& horizontalAngle, double& verticalAngle) {
    Serial.printf("原始方位角: %.1f°, 上次馬達水平角度: %.1f°\n",
                  azimuth, lastHorizontalAngle);

    // 將方位角轉換為以東北方為0度的角度
    horizontalAngle = fmod(azimuth - 45, 360);
    if (horizontalAngle < 0) horizontalAngle += 360;

    // 確保水平角度在0-270度範圍內 (因為水平伺服馬達是270度的)
    if (horizontalAngle > 270) {
      horizontalAngle = -1;  // 無效角度
    }

    // 當仰角超過85度時，固定指向正南方
    if (elevation > 85) {
      horizontalAngle = 135;
    }
    // 當仰角在80-85度間，逐漸過渡到正南方
    else if (elevation > 80) {
      double transitionFactor = (elevation - 80) / 5;
      horizontalAngle = horizontalAngle * (1 - transitionFactor) + 135 * transitionFactor;
    }

    // 修正垂直角度計算：太陽仰角0度對應伺服馬達180度，仰角90度對應伺服馬達90度
    verticalAngle = elevation;

    // 在函數結束前添加更嚴格的範圍限制
    horizontalAngle = constrain(horizontalAngle, HORIZONTAL_SERVO_MIN_ANGLE, HORIZONTAL_SERVO_MAX_ANGLE);
    verticalAngle = constrain(verticalAngle, VERTICAL_SERVO_MIN_ANGLE, VERTICAL_SERVO_MAX_ANGLE);

    // 添加額外安全檢查
    if (horizontalAngle < 0 || horizontalAngle > HORIZONTAL_SERVO_MAX_ANGLE) {
      Serial.printf("警告：水平角度超出範圍！計算值為: %.1f°\n", horizontalAngle);
      horizontalAngle = constrain(horizontalAngle, 0, HORIZONTAL_SERVO_MAX_ANGLE);
    }

    horizontalAngle = 270 - horizontalAngle;

    Serial.printf("轉換後馬達水平角度: %.1f°, 重直角度: %.1f°\n", horizontalAngle, verticalAngle);
  }

  /**
     * 更新明天日出時的太陽位置
     * @param timeinfo 當前時間結構指針
     * @return 是否成功更新日出位置
     */
  bool updateSunrisePosition(struct tm* timeinfo) {
    double azimuth, elevation;

    // 使用二分法精確查找日出時間和位置
    if (sunCalculator.findSunriseTime(timeinfo, azimuth, elevation)) {
      convertToServoAngles(azimuth, elevation, sunriseHorizontalAngle, sunriseVerticalAngle);

      // 確保角度在有效範圍內
      if (sunriseHorizontalAngle >= HORIZONTAL_SERVO_MIN_ANGLE && sunriseHorizontalAngle <= HORIZONTAL_SERVO_MAX_ANGLE) {
        Serial.printf("更新日出位置: 水平角度: %.1f° 垂直角度: %.1f°\n",
                      sunriseHorizontalAngle, sunriseVerticalAngle);
        return true;
      }
    }

    // 如果找不到有效的日出位置，使用預設值
    sunriseHorizontalAngle = 90;  // 東方
    sunriseVerticalAngle = 5;     // 稍微抬頭
    Serial.println("無法計算日出位置，使用預設值");
    return false;
  }

  /**
     * 移動伺服馬達到日出位置，用於夜間模式
     */
  void moveToSunrisePosition() {
    if (!isNightMode) {
      Serial.println("進入夜間模式，移動到明日日出位置");

      // 確保角度在有效範圍內
      double horizontalAngle = constrain(sunriseHorizontalAngle, 0, HORIZONTAL_SERVO_MAX_ANGLE);
      double verticalAngle = constrain(sunriseVerticalAngle, 0, VERTICAL_SERVO_MAX_ANGLE);

      // 漸進式更新伺服馬達位置
      updateServoPositions(horizontalAngle, verticalAngle);

      Serial.printf("已設置為日出位置: 水平角度: %.1f° 垂直角度: %.1f°\n",
                    horizontalAngle, verticalAngle);

      isNightMode = true;
    }
  }

  /**
     * 根據當前時間更新太陽追蹤器位置
     * @param timeinfo 當前時間結構指針
     */
  void updatePosition(struct tm* timeinfo) {
    double azimuth, elevation;
    sunCalculator.calculatePosition(timeinfo, azimuth, elevation);

    // 如果太陽升起，退出夜間模式
    if (elevation > 0) {
      isNightMode = false;
    }

    double horizontalAngle, verticalAngle;
    convertToServoAngles(azimuth, elevation, horizontalAngle, verticalAngle);

    // 檢查是否需要發送指標
    unsigned long currentTime = millis();
    if (currentTime - lastMetricsTime >= METRICS_INTERVAL) {
      // 發送當前狀態到VictoriaMetrics
      sendMetricsToVM(azimuth, elevation,
                      currentHorizontalPosition, currentVerticalPosition);
      lastMetricsTime = currentTime;
    }

    // 只在太陽升起時（仰角為正）且水平角度有效時更新伺服馬達
    if (elevation > 0 && horizontalAngle >= 0) {
      // 設定水平伺服馬達
      if (lastHorizontalAngle < 0) {
        lastHorizontalAngle = horizontalAngle;
      } else {
        // 計算最短路徑
        double diff = horizontalAngle - lastHorizontalAngle;
        // 標準化到 -180到+180範圍
        while (diff > 180) diff -= 360;
        while (diff < -180) diff += 360;

        // 使用增量而非絕對值
        horizontalAngle = lastHorizontalAngle + diff;

        // 確保仍在有效範圍內
        horizontalAngle = constrain(horizontalAngle, 0, HORIZONTAL_SERVO_MAX_ANGLE);
      }
      lastHorizontalAngle = horizontalAngle;
      // 輸出目前角度（用於除錯）
      Serial.printf("時間: %02d:%02d 目標角度: 水平 %.1f° 垂直 %.1f°\n",
                    timeinfo->tm_hour, timeinfo->tm_min,
                    horizontalAngle, verticalAngle);
      // 漸進式更新伺服馬達位置
      updateServoPositions(horizontalAngle, verticalAngle);


    } else {
      Serial.println("太陽未升起或角度超出範圍");
      Serial.printf("時間: %02d:%02d 目標角度: 水平 %.1f° 垂直 %.1f°\n",
                    timeinfo->tm_hour, timeinfo->tm_min,
                    horizontalAngle, verticalAngle);

      // 在太陽落下後，更新日出位置並移動到該位置
      if (!isNightMode) {
        updateServoPositions(135, 0);  // 先移到 135 度，以避免日落角度 > 235 度時，會往 270 度方向轉動。
        updateSunrisePosition(timeinfo);
        moveToSunrisePosition();
      } else {
        // 已經在夜間模式，每小時更新一次日出位置
        if (timeinfo->tm_min == 0) {
          updateSunrisePosition(timeinfo);
          moveToSunrisePosition();
        }
      }
    }
  }
};

SolarTracker tracker(LATITUDE, LONGITUDE);

/**
 * 初始化伺服馬達
 * 連接伺服馬達到指定的GPIO引腳
 */
void initServo() {
  // 連接伺服馬達到GPIO
  horizontalServo.setPeriodHertz(50);  // 標準50Hz伺服
  horizontalServo.attach(HORIZONTAL_SERVO_PIN, HORIZONTAL_SERVO_MIN_PULSE, HORIZONTAL_SERVO_MAX_PULSE);
  delay(1000);
  verticalServo.setPeriodHertz(50);
  verticalServo.attach(VERTICAL_SERVO_PIN, VERTICAL_SERVO_MIN_PULSE, VERTICAL_SERVO_MAX_PULSE);

  // 如果需要設定初始位置為1500微秒
  verticalServo.writeMicroseconds(1500);
}

/**
 * Arduino設置函數，程序啟動時執行一次
 * 初始化系統環境、WiFi連接、時間同步、伺服馬達等
 */
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n太陽追蹤器啟動中...");

  int maxAttempts = 3;
  for (int attempt = 0; attempt < maxAttempts; attempt++) {
    Serial.print("連接嘗試 ");
    Serial.print(attempt + 1);
    Serial.print("/");
    Serial.println(maxAttempts);

    WiFi.begin(ssid, password);

    // 等待 15 秒看是否連接成功
    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startTime < 15000) {
      delay(500);
      Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
      IPAddress ip = WiFi.localIP();
      // 檢查是否取得有效 IP (非 APIPA)
      if (!(ip[0] == 169 && ip[1] == 254)) {
        Serial.println("\n連接成功!");
        Serial.print("IP: ");
        Serial.println(ip);
        break;
      }
    }

    // 如果連接失敗或獲得 APIPA 地址，斷開連接並等待一段時間再試
    Serial.println("\n連接不成功，重試...");
    WiFi.disconnect(true);
    delay(3000);  // 稍微休息一下再嘗試
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi 連接成功!");
    Serial.print("IP地址: ");
    Serial.println(WiFi.localIP());
    Serial.print("MAC地址: ");
    Serial.println(WiFi.macAddress());
    Serial.print("SSID: ");
    Serial.println(WiFi.SSID());
    Serial.print("信號強度: ");
    Serial.println(WiFi.RSSI());
  } else {
    Serial.println("\nWiFi 連接失敗! 繼續執行程序...");
    // 選擇性地重置ESP32
    // ESP.restart();
  }

  // 初始化時間，使用多個NTP服務器
  configTime(gmtOffset_sec, daylightOffset_sec, "pool.ntp.org", "time.nist.gov", "time.google.com");
  Serial.println("同步時間中...");

  // 等待取得時間
  time_t now;
  struct tm timeinfo;
  time(&now);
  localtime_r(&now, &timeinfo);

  // 初始化時間後立即檢查
  char timeString[50];
  strftime(timeString, sizeof(timeString), "%Y-%m-%d %H:%M:%S", &timeinfo);
  Serial.print("初始化時間: ");
  Serial.println(timeString);
  Serial.print("年份: ");
  Serial.println(timeinfo.tm_year + 1900);

  while (timeinfo.tm_year < (2016 - 1900)) {
    Serial.println("Waiting for fetch time...");
    delay(1000);
    time(&now);
    localtime_r(&now, &timeinfo);
  }

  // 初始化伺服馬達
  initServo();

  // 初始化指標時間
  lastMetricsTime = millis();

  // 初始位置
  // tracker.updatePosition(&timeinfo);
  // 輸出地理位置信息
  Serial.printf("地理位置: 緯度 %.4f°N, 經度 %.4f°E\n", LATITUDE, LONGITUDE);

  // 初始化手動模式針腳為輸入，並啟用內部上拉電阻
  pinMode(MANUAL_RESET_POS_PIN, INPUT_PULLUP);
  pinMode(MANUAL_CURRENT_POS_PIN, INPUT_PULLUP);
  pinMode(LED_BUILTIN, OUTPUT);

#ifndef ENABLE_METRIC_COLLECT
  // Ensure WiFi is off to save power
  WiFi.mode(WIFI_OFF);
  btStop();  // Turn off Bluetooth
#endif

#ifdef DEBUG_MODE
  testHorizontalServoMove();
  testVerticalServoMove();
#endif
}

/**
 * Arduino主循環函數，重複執行
 * 獲取當前時間並更新太陽追蹤器位置
 */
void loop() {
#ifdef DEBUG_MODE
  simulation();
  return;
#endif

  static unsigned long lastUpdateTime = 0;
  unsigned long currentTime = millis();

  // 轉動至初始位置 H:135, V:0
  if (digitalRead(MANUAL_RESET_POS_PIN) == LOW) {  // 如果接到地(LOW)，則啟動手動模式
    Serial.println("手動模式啟動 - 移動到指定位置");

    // 目標位置
    double targetHorizontal = (HORIZONTAL_SERVO_MIN_ANGLE + HORIZONTAL_SERVO_MAX_ANGLE) / 2;  // 135.0;
    double targetVertical = 0.0;

    // 計算伺服馬達脈衝
    int horizontalPulse = angleToPulseH(targetHorizontal);
    int verticalPulse = angleToPulseV(targetVertical);

    // 移動伺服馬達
    horizontalServo.writeMicroseconds(horizontalPulse);
    delay(500);
    verticalServo.writeMicroseconds(verticalPulse);

    Serial.printf("已設置為手動位置: 水平角度: %.1f° 垂直角度: %.1f°\n",
                  targetHorizontal, targetVertical);

    // 更新當前位置變數
    currentHorizontalPosition = targetHorizontal;
    currentVerticalPosition = targetVertical;

    // 等待一段時間再繼續，避免連續觸發
    delay(3000);
  } else {
    // 每1000ms執行一次
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    // 輸出當前時間到Serial
    char timeString[30];
    strftime(timeString, sizeof(timeString), "%Y-%m-%d %H:%M:%S", &timeinfo);
    Serial.print("Current time: ");
    Serial.print(timeString);
    Serial.print(" (Unix timestamp: ");
    Serial.print(now);
    Serial.println(")");

    digitalWrite(LED_BUILTIN, HIGH);
    // 只有當經過了10分鐘(10*60*1000ms)才更新位置
    if (currentTime - lastUpdateTime >= 10 * 60 * 1000) {
      tracker.updatePosition(&timeinfo);
      Serial.println("------------------------------------");
      lastUpdateTime = currentTime;
    }
    if (digitalRead(MANUAL_CURRENT_POS_PIN) == LOW) {  // 如果接到地(LOW)，則啟動手動模式
      Serial.println("Move to position for current time");
      tracker.updatePosition(&timeinfo);
    }

    delay(1900);  // 每1000ms執行一次loop
    digitalWrite(LED_BUILTIN, LOW);
    delay(100);
  }
}

#ifdef DEBUG_MODE

/**
 * 模擬太陽運行的函數
 * 用於測試太陽追蹤器在不同時間的表現，無需等待實際時間流逝
 * 時間從早上5點開始，每次增加10分鐘，直到晚上7點後重置
 */
void simulation() {
  static struct tm simTimeinfo = { 0 };
  static time_t simTime = 0;

  // Initialize only once when simTime is 0
  if (simTime == 0) {
    simTimeinfo.tm_year = 2025 - 1900;
    simTimeinfo.tm_mon = 3 - 1;
    simTimeinfo.tm_mday = 9;
    simTimeinfo.tm_hour = 5;
    simTimeinfo.tm_min = 0;

    simTime = mktime(&simTimeinfo);
  }

  // 輸出模擬時間到Serial
  char timeString[30];
  strftime(timeString, sizeof(timeString), "%Y-%m-%d %H:%M:%S", &simTimeinfo);
  Serial.print("Simulated time: ");
  Serial.print(timeString);
  Serial.print(" (Unix timestamp: ");
  Serial.print(simTime);
  Serial.println(")");

  tracker.updatePosition(&simTimeinfo);

  // 增加10分鐘
  simTime += 10 * 60;
  localtime_r(&simTime, &simTimeinfo);

  // 模擬時間到達 19:00 時重置為 05:00
  if (simTimeinfo.tm_hour >= 19) {
    simTimeinfo.tm_hour = 6;
    simTimeinfo.tm_min = 0;
    simTimeinfo.tm_sec = 0;
    simTime = mktime(&simTimeinfo);
  }

  // 延遲5秒
  Serial.println("------------------------------------");
  delay(100);
}

/**
 * 測試水平伺服馬達運動的函數
 * 該函數會固定垂直伺服馬達於0度位置，並讓水平伺服馬達
 * 在不同角度（0°、45°、135°、225°、270°）之間循環移動
 * 主要用於調試和校準水平伺服馬達
 */
void testHorizontalServoMove() {
  // 初始化當前位置
  int horizontalPulse = HORIZONTAL_SERVO_MIN_PULSE;
  int verticalPulse = VERTICAL_SERVO_MIN_PULSE;

  // 設定垂直伺服馬達為初始位置
  verticalPulse = angleToPulseV(0);
  verticalServo.writeMicroseconds(verticalPulse);

  // 水平角度測試值陣列
  int horizontalAngles[] = { 0, 45, 135, 225, 270 };

  // while (true) {
  // 使用for迴圈遍歷所有測試角度
  for (int i = 0; i < 5; i++) {
    horizontalPulse = angleToPulseH(horizontalAngles[i]);
    Serial.printf("HPulse: %d, VPulse: %d (angle: %d°)\n", horizontalPulse, verticalPulse, horizontalAngles[i]);
    horizontalServo.writeMicroseconds(horizontalPulse);
    delay(3000);
  }
  // }
}

/**
 * 測試垂直伺服馬達運動的函數
 * 該函數會固定水平伺服馬達於0度位置，並讓垂直伺服馬達
 * 在不同角度（0°、45°、90°、135°、180°）之間循環移動
 * 主要用於調試和校準垂直伺服馬達
 */
void testVerticalServoMove() {
  // 初始化當前位置
  int horizontalPulse = HORIZONTAL_SERVO_MIN_PULSE;
  int verticalPulse = VERTICAL_SERVO_MIN_PULSE;

  // 設定水平伺服馬達為初始位置
  horizontalPulse = angleToPulseH(0);
  horizontalServo.writeMicroseconds(horizontalPulse);

  // while (true) {
  // 垂直角度測試值陣列
  int verticalAngles[] = { 0, 45, 90, 135, 180 };

  // 使用for迴圈遍歷所有測試角度
  for (int i = 0; i < 5; i++) {
    verticalPulse = angleToPulseV(verticalAngles[i]);
    Serial.printf("HPulse: %d, VPulse: %d (angle: %d°)\n", horizontalPulse, verticalPulse, verticalAngles[i]);
    verticalServo.writeMicroseconds(verticalPulse);
    delay(3000);
  }
  // }
}

#endif
