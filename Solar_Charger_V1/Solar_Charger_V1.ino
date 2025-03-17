#include <Adafruit_INA3221.h>
#include <U8g2lib.h>
#include <float.h>
#include <WiFi.h>        // ESP32 WiFi library
#include <HTTPClient.h>  // ESP32 HTTP client library
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
// Functions
//------------------------------------------------------------------------------
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

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("\n=== Initial Setup ===");

  // Initialize sensors and display
  if (!initializeINA3221())
    while (1) delay(10);
  if (!initializeDisplay())
    while (1) delay(10);

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
  } else {
    Serial.println("\nWiFi connection failed, continuing without network");
  }
}

/**
 * Sends metrics to VictoriaMetrics using pre-collected data
 */
void sendMetricsToVM() {
  if (WiFi.status() == WL_CONNECTED) {
    // Format metrics in Prometheus format
    String metrics = "";
    char valueStr[20];

    // Use the pre-collected data for all three channels
    for (int channel = 0; channel <= 2; channel++) {
      // Add metrics for this channel
      snprintf(valueStr, sizeof(valueStr), "%.3f", channelData[channel].current);
      metrics += "solar_panel_current{device=\"" + String(deviceName) + "\",channel=\"" + String(channelNames[channel]) + "\"} " + String(valueStr) + "\n";

      snprintf(valueStr, sizeof(valueStr), "%.3f", channelData[channel].voltage);
      metrics += "solar_panel_voltage{device=\"" + String(deviceName) + "\",channel=\"" + String(channelNames[channel]) + "\"} " + String(valueStr) + "\n";
    }

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

void loop() {
  unsigned long currentTime = millis();

  Serial.print("currentTime: ");
  Serial.println(currentTime);

  // Read all channel data once per loop cycle
  readAllChannels();

  // Update the display with current readings
  displayMeasurements();

  // Send metrics at the specified interval
  if (currentTime - lastMetricsTime >= METRICS_INTERVAL) {
    if (WiFi.status() == WL_CONNECTED) {
      sendMetricsToVM();
      lastMetricsTime = currentTime;
    } else {
      Serial.println("WiFi not connected");
    }
  }

  delay(1000);  // Short delay to prevent CPU hogging
}
