#include <Adafruit_INA3221.h>
#include <U8g2lib.h>
#include <float.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>
#include <math.h>
#include <ESP32Servo.h>
#include "config_secret.h"

//------------------------------------------------------------------------------
// WiFi and VictoriaMetrics Settings
//------------------------------------------------------------------------------
// const char* ssid = "";
// const char* password = "";

// VictoriaMetrics settings
const char* vmHost = "http://192.168.50.10:8428";
const char* vmPath = "/api/v1/import/prometheus";
const char* deviceName = "solar_panel_node1";

// Channel descriptions
const char* channelNames[] = {"solar", "battery1", "battery2"};

// Metrics collection interval
const unsigned long METRICS_INTERVAL = 3000;  // 3 seconds
unsigned long lastMetricsTime = 0;

//------------------------------------------------------------------------------
// NTP Settings
//------------------------------------------------------------------------------
const long gmtOffset_sec = 28800;  // UTC+8 = 8*3600
const int daylightOffset_sec = 0;

//------------------------------------------------------------------------------
// Servo Motor Settings
//------------------------------------------------------------------------------
// 伺服馬達腳位
const int HORIZONTAL_SERVO_PIN = 16;
const int VERTICAL_SERVO_PIN = 17;

// 手動模式針腳定義
const int MANUAL_RESET_POS_PIN = 20;
const int MANUAL_CURRENT_POS_PIN = 19;

// 高雄的經緯度
const double LATITUDE = 22.6273;
const double LONGITUDE = 120.3014;

// 伺服馬達範圍設定
const int HORIZONTAL_SERVO_MIN_ANGLE = 0;
const int HORIZONTAL_SERVO_MAX_ANGLE = 270;
const int VERTICAL_SERVO_MIN_ANGLE = 0;
const int VERTICAL_SERVO_MAX_ANGLE = 180;

// 伺服馬達脈衝範圍（微秒）
const int HORIZONTAL_SERVO_MIN_PULSE = 500;
const int HORIZONTAL_SERVO_MAX_PULSE = 2500;
const int VERTICAL_SERVO_MIN_PULSE = 580;
const int VERTICAL_SERVO_MAX_PULSE = 2480;

// 窗戶方向調整
const double HORIZONTAL_SOUTH_ADD = 22;  // 窗戶正南偏東22度

// 位置更新用的最大步進角度 (度)
const double MAX_STEP = 5.0;

// 建立伺服馬達對象
Servo horizontalServo;
Servo verticalServo;

// 伺服馬達當前位置
double currentHorizontalPosition = 90;  // 初始值为东方向
double currentVerticalPosition = 45;    // 初始值为中间位置

// 夜間模式設定
bool isNightMode = false;
double sunriseHorizontalAngle = 90;  // 預設日出角度
double sunriseVerticalAngle = 15;    // 預設日出仰角

//------------------------------------------------------------------------------
// Global Objects
//------------------------------------------------------------------------------
Adafruit_INA3221 ina3221; // Create instance of INA3221
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);  // OLED display

// Define I2C Address - default is 0x40
#define INA3221_I2C_ADDRESS 0x40

// Define the channels (Adafruit library uses 0-based indexing)
#define SOLAR_PANEL_CHANNEL 0
#define BATTERY1_CHANNEL 1
#define BATTERY2_CHANNEL 2

// Struct to hold measurements for each channel
struct ChannelData {
  float voltage;
  float current;
};

// Array to hold data for all channels
ChannelData channelData[3];

//------------------------------------------------------------------------------
// Sun Position Calculation Class
//------------------------------------------------------------------------------
class SunPosition {
private:
  double latitude;
  double longitude;

  /**
   * 將角度轉換為弧度
   * @param degrees 角度值
   * @return 弧度值
   */
  inline double toRadians(double degrees) {
    return degrees * M_PI / 180.0;
  }

  /**
   * 將弧度轉換為角度
   * @param radians 弧度值
   * @return 角度值
   */
  inline double toDegrees(double radians) {
    return radians * 180.0 / M_PI;
  }

public:
  /**
   * 建構函數，初始化太陽位置計算器
   * @param lat 緯度
   * @param lon 經度
   */
  SunPosition(double lat, double lon)
    : latitude(lat), longitude(lon) {}

  /**
   * 計算指定時間的太陽方位角和仰角
   * @param timeinfo 時間結構指針
   * @param azimuth 返回計算的方位角
   * @param elevation 返回計算的仰角
   */
  void calculatePosition(struct tm* timeinfo, double& azimuth, double& elevation) {
    // 計算日期相關數據
    int year = timeinfo->tm_year + 1900;
    int month = timeinfo->tm_mon + 1;
    int day = timeinfo->tm_mday;
    int hour = timeinfo->tm_hour;
    int minute = timeinfo->tm_min;
    int second = timeinfo->tm_sec;

    // 計算當天的小時數（包含分和秒的小數部分）
    double hourDecimal = hour + minute / 60.0 + second / 3600.0;

    // 計算當年的天數
    int dayOfYear = 0;
    for (int m = 1; m < month; m++) {
      if (m == 2) {
        dayOfYear += (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) ? 29 : 28;
      } else if (m == 4 || m == 6 || m == 9 || m == 11) {
        dayOfYear += 30;
      } else {
        dayOfYear += 31;
      }
    }
    dayOfYear += day;

    // 計算太陽時角
    double gamma = 2 * M_PI / 365 * (dayOfYear - 1 + (hourDecimal - 12) / 24);
    double eqtime = 229.18 * (0.000075 + 0.001868 * cos(gamma) - 0.032077 * sin(gamma) - 0.014615 * cos(2 * gamma) - 0.040849 * sin(2 * gamma));

    // 計算赤緯角
    double decl = 0.006918 - 0.399912 * cos(gamma) + 0.070257 * sin(gamma)
                  - 0.006758 * cos(2 * gamma) + 0.000907 * sin(2 * gamma)
                  - 0.002697 * cos(3 * gamma) + 0.00148 * sin(3 * gamma);
    decl = toDegrees(decl);

    // 計算真太陽時
    double time_offset = eqtime - 4 * longitude + 60 * 8;  // 東八區
    double tst = hourDecimal * 60 + time_offset;

    // 計算太陽時角
    double ha = (tst / 4 - 180);  // 每分鐘轉動0.25度
    if (ha < -180) ha += 360;

    // 計算太陽高度角
    double lat_rad = toRadians(latitude);
    double decl_rad = toRadians(decl);
    double ha_rad = toRadians(ha);

    double sin_elev = sin(lat_rad) * sin(decl_rad) + cos(lat_rad) * cos(decl_rad) * cos(ha_rad);
    elevation = toDegrees(asin(sin_elev));

    // 計算方位角
    double cos_az = (sin(decl_rad) - sin(lat_rad) * sin_elev) / (cos(lat_rad) * cos(toRadians(elevation)));
    cos_az = constrain(cos_az, -1.0, 1.0);  // 確保在有效範圍內

    azimuth = toDegrees(acos(cos_az));
    if (ha > 0) azimuth = 360 - azimuth;
  }

  /**
   * 使用二分法查找明天的日出時間及其太陽位置
   * @param timeinfo 當前時間結構指針
   * @param azimuth 返回日出時的方位角
   * @param elevation 返回日出時的仰角
   * @return 是否成功找到日出時間
   */
  bool findSunriseTime(struct tm* timeinfo, double& azimuth, double& elevation) {
    // 創建臨時時間結構，設置為明天凌晨
    struct tm tomorrowMorning = *timeinfo;
    tomorrowMorning.tm_mday += 1;
    tomorrowMorning.tm_hour = 4;  // 從凌晨4點開始搜索
    tomorrowMorning.tm_min = 0;
    tomorrowMorning.tm_sec = 0;

    time_t startTime = mktime(&tomorrowMorning);
    if (startTime == -1) return false;

    // 設置結束時間為明天 8 點
    tomorrowMorning.tm_hour = 8;
    time_t endTime = mktime(&tomorrowMorning);
    if (endTime == -1) return false;

    // 二分法搜索日出時間
    while (endTime - startTime > 60) {  // 精確到1分鐘
      time_t midTime = startTime + (endTime - startTime) / 2;
      struct tm midTimeinfo;
      localtime_r(&midTime, &midTimeinfo);

      double midAzimuth, midElevation;
      calculatePosition(&midTimeinfo, midAzimuth, midElevation);

      if (midElevation < 0) {
        startTime = midTime;
      } else {
        endTime = midTime;
      }
    }

    // 使用找到的時間計算最終太陽位置
    struct tm sunriseTimeinfo;
    localtime_r(&endTime, &sunriseTimeinfo);
    calculatePosition(&sunriseTimeinfo, azimuth, elevation);

    // 輸出找到的日出時間
    char timeString[30];
    strftime(timeString, sizeof(timeString), "%Y-%m-%d %H:%M:%S", &sunriseTimeinfo);
    Serial.print("Calculated sunrise time: ");
    Serial.println(timeString);

    return true;
  }
};

//------------------------------------------------------------------------------
// Forward Declarations
//------------------------------------------------------------------------------
int angleToPulse(int angle, int minAngle, int maxAngle, int minPulse, int maxPulse);
int angleToPulseH(double angle);
int angleToPulseV(double angle);

//------------------------------------------------------------------------------
// Solar Tracker Class
//------------------------------------------------------------------------------
class SolarTracker {
private:
  SunPosition sunCalculator;
  double lastHorizontalAngle = 90;  // 初始東方向

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
  void convertToServoAngles(double azimuth, double elevation, double& horizontalAngle, double& verticalAngle) {
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
   * 更新明天日出位置
   * @param timeinfo 當前時間結構
   * @return 是否成功更新日出位置
   */
  bool updateSunrisePosition(struct tm* timeinfo) {
    double azimuth, elevation;
    if (sunCalculator.findSunriseTime(timeinfo, azimuth, elevation)) {
      // 轉換方位角和仰角為伺服馬達角度
      double horizontalAngle, verticalAngle;
      convertToServoAngles(azimuth, elevation, horizontalAngle, verticalAngle);

      // 存儲明天日出位置
      sunriseHorizontalAngle = horizontalAngle;
      sunriseVerticalAngle = verticalAngle;

      Serial.printf("更新明天日出位置 - 方位角: %.1f°, 仰角: %.1f°\n", azimuth, elevation);
      Serial.printf("更新明天日出位置 - 水平角度: %.1f°, 垂直角度: %.1f°\n", horizontalAngle, verticalAngle);

      return true;
    } else {
      Serial.println("無法計算明天的日出時間");
      return false;
    }
  }

  /**
   * 移動到日出位置
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

    // 正常位置追蹤模式
    if (elevation > 0) {
      // 漸進式更新伺服馬達位置
      updateServoPositions(horizontalAngle, verticalAngle);
      Serial.printf("太陽位置 - 方位角: %.1f°, 仰角: %.1f°\n", azimuth, elevation);
    } else {
      // 太陽已經落山，應該進入夜間模式
      if (!isNightMode) {
        // 更新明天的日出位置
        updateSunrisePosition(timeinfo);
        // 移動到日出位置
        moveToSunrisePosition();
      }
    }
  }
};

// Create SolarTracker instance
SolarTracker tracker(LATITUDE, LONGITUDE);

//------------------------------------------------------------------------------
// Function Definitions
//------------------------------------------------------------------------------

/**
 * 將角度轉換為對應的脈衝寬度
 * @param angle 角度值
 * @return 對應的脈衝寬度（微秒）
 */
int angleToPulse(int angle, int minAngle, int maxAngle, int minPulse, int maxPulse) {
  return map(angle, minAngle, maxAngle, minPulse, maxPulse);
}

/**
 * 將水平角度轉換為對應的脈衝寬度
 * @param angle 水平角度值
 * @return 對應的脈衝寬度（微秒）
 */
int angleToPulseH(double angle) {
  return angleToPulse(angle, HORIZONTAL_SERVO_MIN_ANGLE, HORIZONTAL_SERVO_MAX_ANGLE,
                     HORIZONTAL_SERVO_MIN_PULSE, HORIZONTAL_SERVO_MAX_PULSE);
}

/**
 * 將垂直角度轉換為對應的脈衝寬度
 * @param angle 垂直角度值
 * @return 對應的脈衝寬度（微秒）
 */
int angleToPulseV(double angle) {
  return angleToPulse(angle, VERTICAL_SERVO_MIN_ANGLE, VERTICAL_SERVO_MAX_ANGLE,
                     VERTICAL_SERVO_MIN_PULSE, VERTICAL_SERVO_MAX_PULSE);
}

/**
 * Reads data from all INA3221 channels and stores in the global array
 */
void readAllChannels() {
  for (int channel = 0; channel < 3; channel++) {
    channelData[channel].voltage = ina3221.getBusVoltage(channel);
    channelData[channel].current = ina3221.getCurrentAmps(channel);
  }
}

/**
 * Updates the OLED display with current measurements
 */
void displayMeasurements() {
  char line1[24], line2[24], line3[24], line4[24];
  unsigned long uptime = millis() / 1000; // Convert to seconds

  // Calculate days, hours, minutes, seconds
  unsigned long seconds = uptime % 60;
  unsigned long minutes = (uptime / 60) % 60;
  unsigned long hours = (uptime / 3600) % 24;
  unsigned long days = uptime / 86400;

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);

  // Format measurement strings using the pre-collected data
  snprintf(line1, sizeof(line1), "Solar: %.2fV %.3fA",
           channelData[SOLAR_PANEL_CHANNEL].voltage,
           channelData[SOLAR_PANEL_CHANNEL].current);

  snprintf(line2, sizeof(line2), "Bat1: %.2fV %.3fA",
           channelData[BATTERY1_CHANNEL].voltage,
           channelData[BATTERY1_CHANNEL].current);

  snprintf(line3, sizeof(line3), "Bat2: %.2fV %.3fA",
           channelData[BATTERY2_CHANNEL].voltage,
           channelData[BATTERY2_CHANNEL].current);

  snprintf(line4, sizeof(line4), "Up: %lud %luh %lum %lus", days, hours, minutes, seconds);

  // Position strings on display
  u8g2.drawStr(0, 14, line1);
  u8g2.drawStr(0, 28, line2);
  u8g2.drawStr(0, 42, line3);
  u8g2.drawStr(0, 56, line4);

  u8g2.sendBuffer();
}

/**
 * Initializes the INA3221 voltage/current sensor
 * @return true if initialization successful
 */
bool initializeINA3221() {
  Serial.println("Initializing INA3221...");

  // Begin with the default address
  if (!ina3221.begin(INA3221_I2C_ADDRESS)) {
    Serial.println("Failed to find INA3221 chip");
    return false;
  }

  // Configure channels as needed
  ina3221.setShuntResistance(SOLAR_PANEL_CHANNEL, 0.1); // Shunt resistor value in ohms
  ina3221.setShuntResistance(BATTERY1_CHANNEL, 0.1);
  ina3221.setShuntResistance(BATTERY2_CHANNEL, 0.1);

  // Set channel modes - enable all channels
  ina3221.enableChannel(SOLAR_PANEL_CHANNEL);
  ina3221.enableChannel(BATTERY1_CHANNEL);
  ina3221.enableChannel(BATTERY2_CHANNEL);

  return true;
}

/**
 * Initializes the OLED display
 * @return true if initialization successful
 */
bool initializeDisplay() {
  if (!u8g2.begin()) {
    Serial.println("Display initialization failed!");
    return false;
  }
  return true;
}

/**
 * Initialize the servo motors
 */
void initServo() {
  // 允許分配足夠的PWM通道給ESP32的伺服馬達库
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);

  // 將伺服馬達連接到指定针腳
  horizontalServo.setPeriodHertz(50);  // 标准PWM频率为50Hz
  verticalServo.setPeriodHertz(50);

  horizontalServo.attach(HORIZONTAL_SERVO_PIN, HORIZONTAL_SERVO_MIN_PULSE, HORIZONTAL_SERVO_MAX_PULSE);
  verticalServo.attach(VERTICAL_SERVO_PIN, VERTICAL_SERVO_MIN_PULSE, VERTICAL_SERVO_MAX_PULSE);

  // 先移动到初始位置
  horizontalServo.writeMicroseconds(angleToPulseH(currentHorizontalPosition));
  verticalServo.writeMicroseconds(angleToPulseV(currentVerticalPosition));

  Serial.println("伺服馬達已初始化");
}

/**
 * Sends metrics to VictoriaMetrics using pre-collected data
 */
void sendMetricsToVM() {
  if (WiFi.status() == WL_CONNECTED) {
    // Format metrics in Prometheus format
    String metrics = "";
    char valueStr[20];

    // Add solar/battery metrics
    for (int channel = 0; channel <= 2; channel++) {
      snprintf(valueStr, sizeof(valueStr), "%.3f", channelData[channel].current);
      metrics += "solar_panel_current{device=\"" + String(deviceName) + "\",channel=\"" + String(channelNames[channel]) + "\"} " + String(valueStr) + "\n";

      snprintf(valueStr, sizeof(valueStr), "%.3f", channelData[channel].voltage);
      metrics += "solar_panel_voltage{device=\"" + String(deviceName) + "\",channel=\"" + String(channelNames[channel]) + "\"} " + String(valueStr) + "\n";
    }

    // Add solar tracker metrics
    metrics += "solar_tracker_position{device=\"" + String(deviceName) + "\",type=\"horizontal\"} " + String(currentHorizontalPosition) + "\n";
    metrics += "solar_tracker_position{device=\"" + String(deviceName) + "\",type=\"vertical\"} " + String(currentVerticalPosition) + "\n";

    Serial.println(metrics);

    // Send metrics to VictoriaMetrics
    HTTPClient http;

    String url = String(vmHost) + vmPath;
    http.begin(url);
    http.addHeader("Content-Type", "text/plain");

    int httpCode = http.POST(metrics);

    if (httpCode > 0) {
      Serial.printf("HTTP Response code: %d\n", httpCode);
    } else {
      Serial.printf("Error sending metrics: %s\n", http.errorToString(httpCode).c_str());
    }
    http.end();
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("\n=== Initial Setup ===");

  // Initialize sensors and display
  if (!initializeINA3221())
    while (1) delay(10);
  if (!initializeDisplay())
    while (1) delay(10);

  // Initialize servo motors
  initServo();

  // Setup manual control pins
  pinMode(MANUAL_RESET_POS_PIN, INPUT_PULLUP);
  pinMode(MANUAL_CURRENT_POS_PIN, INPUT_PULLUP);
  pinMode(LED_BUILTIN, OUTPUT);

  // Initialize WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  int wifiAttempts = 0;
  while (WiFi.status() != WL_CONNECTED && wifiAttempts < 20) {
    delay(500);
    Serial.print(".");
    wifiAttempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected to WiFi");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    // Configure time using NTP
    configTime(gmtOffset_sec, daylightOffset_sec, "pool.ntp.org", "time.nist.gov");
    Serial.println("Waiting for NTP time sync...");

    // Wait for time to be set
    time_t now = time(nullptr);
    while (now < 8 * 3600 * 2) {
      delay(500);
      Serial.print(".");
      now = time(nullptr);
    }
    Serial.println("");

    // Get and display current time
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      char timeStringBuff[50];
      strftime(timeStringBuff, sizeof(timeStringBuff), "%A, %B %d %Y %H:%M:%S", &timeinfo);
      Serial.print("Time initialized: ");
      Serial.println(timeStringBuff);

      // Initialize solar tracker with current time
      tracker.updateSunrisePosition(&timeinfo);
    }
  } else {
    Serial.println("\nWiFi connection failed, continuing without network");
  }
}

void loop() {
  unsigned long currentTime = millis();
  static unsigned long lastUpdateTime = 0;
  const unsigned long updateInterval = 10000; // 10 seconds between tracker updates

  // Check manual control buttons
  if (digitalRead(MANUAL_RESET_POS_PIN) == LOW) {
    Serial.println("Manual mode activated - Moving to default position");

    // Target position - middle of horizontal range, vertical at 0
    double targetHorizontal = (HORIZONTAL_SERVO_MIN_ANGLE + HORIZONTAL_SERVO_MAX_ANGLE) / 2;
    double targetVertical = 0.0;

    // Calculate servo pulses
    int horizontalPulse = angleToPulseH(targetHorizontal);
    int verticalPulse = angleToPulseV(targetVertical);

    // Move servos with delay between movements
    horizontalServo.writeMicroseconds(horizontalPulse);
    delay(500);
    verticalServo.writeMicroseconds(verticalPulse);

    // Update current position variables
    currentHorizontalPosition = targetHorizontal;
    currentVerticalPosition = targetVertical;

    Serial.printf("Set to manual position: Horizontal: %.1f° Vertical: %.1f°\n",
                 targetHorizontal, targetVertical);

    // Wait to avoid continuous triggering
    delay(3000);
  }

  if (digitalRead(MANUAL_CURRENT_POS_PIN) == LOW) {
    // Get current time and force immediate position update
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      Serial.println("Manual update - Moving to position for current time");
      tracker.updatePosition(&timeinfo);

      // Wait to avoid continuous triggering
      delay(3000);
    } else {
      Serial.println("Failed to get current time for manual update");
      delay(1000);
    }
  }

  // Read all channel data once per loop cycle
  readAllChannels();

  // Update the display with current readings
  displayMeasurements();

  // Update solar tracker position at the specified interval
  if (currentTime - lastUpdateTime >= updateInterval) {
    // Get current time
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      // Update solar tracker position
      tracker.updatePosition(&timeinfo);
      lastUpdateTime = currentTime;

      // Print current time for debugging
      char timeString[30];
      strftime(timeString, sizeof(timeString), "%Y-%m-%d %H:%M:%S", &timeinfo);
      Serial.print("Current time: ");
      Serial.println(timeString);
      Serial.println("------------------------------------");
    } else {
      Serial.println("Failed to get current time for scheduled update");
    }
  }

  // Send metrics at the specified interval
  if (currentTime - lastMetricsTime >= METRICS_INTERVAL) {
    if (WiFi.status() == WL_CONNECTED) {
      sendMetricsToVM();
      lastMetricsTime = currentTime;
    } else {
      Serial.println("WiFi not connected");
    }
  }

  // Flash the built-in LED to indicate the system is running
  digitalWrite(LED_BUILTIN, HIGH);
  delay(900);  // LED on time
  digitalWrite(LED_BUILTIN, LOW);
  delay(100);  // LED off time
}
