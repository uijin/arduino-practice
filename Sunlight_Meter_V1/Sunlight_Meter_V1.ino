#include <Adafruit_INA219.h>
#include <U8g2lib.h>
#include "config.h"
#include "thingProperties.h"

Adafruit_INA219 ina219;  // Create sensor object
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

// Function to display measurements on OLED
void displayMeasurements(float current, float voltage, float power) {
  char currentStr[20];
  char voltageStr[20];
  char powerStr[20];
  
  u8g2.clearBuffer();
  
  // Set font and draw title
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(0, 10, "Solar Panel Metrics");
  
  // Draw line under title
  u8g2.drawHLine(0, 13, 128);
  
  // Format measurement strings
  snprintf(currentStr, sizeof(currentStr), "Current: %.3fA", current);
  snprintf(voltageStr, sizeof(voltageStr), "Voltage: %.2fV", voltage);
  snprintf(powerStr, sizeof(powerStr), "Power: %.2fmW", power);
  
  // Display measurements
  u8g2.drawStr(0, 28, currentStr);
  u8g2.drawStr(0, 42, voltageStr);
  u8g2.drawStr(0, 56, powerStr);
  
  u8g2.sendBuffer();
}

void setup() {
  Serial.begin(115200);
  delay(1500);

  // Initialize the INA219
  if (!ina219.begin()) {
    Serial.println("Failed to find INA219 chip");
    while (1) { delay(10); }
  }
  
  Serial.println("INA219 Current/Power Monitor");
  Serial.println("----------------------------");
  
  Serial.println("setCalibration_16V_400mA()");
  ina219.setCalibration_16V_400mA();
  
  // Initialize display
  if (!u8g2.begin()) {
    Serial.println("Display initialization failed!");
    while (1) { delay(10); }
  }
  Serial.println("Display initialized successfully!");
  
  // Defined in thingProperties.h
  initProperties();
  ArduinoCloud.begin(ArduinoIoTPreferredConnection);
  
  setDebugMessageLevel(2);
  ArduinoCloud.printDebugInfo();
}

void loop() {
  ArduinoCloud.update();
  
  // Get measurements from INA219
  float current_mA = ina219.getCurrent_mA();
  float current_A = 0;
  float voltage = 0;
  float power_mW = 0;
  
  if (ina219.success()) {
    current_A = current_mA / 1000.0; // Convert mA to A
    solar_Panel_Current = current_A;
  }
  
  float busVoltage = ina219.getBusVoltage_V();
  float shuntVoltage = ina219.getShuntVoltage_mV() / 1000.0;
  if (ina219.success()) {
    voltage = busVoltage + shuntVoltage;
    solar_Panel_Voltage = voltage;
  }
  
  // Get power directly from INA219
  power_mW = ina219.getPower_mW();
  solar_Panel_Power = power_mW;
  
  // Update display
  displayMeasurements(current_A, voltage, power_mW);
  
  // Debug printing
  Serial.print("Current: ");
  Serial.print(current_A);
  Serial.println(" A");
  
  Serial.print("Voltage: ");
  Serial.print(voltage);
  Serial.println(" V");
  
  Serial.print("Power: ");
  Serial.print(power_mW);
  Serial.println(" mW");
  
  delay(2000);
}

void onSolarPanelCurrentChange() {
  // Add your code here to act upon SolarPanelCurrent change
}

void onSolarPanelVoltageChange() {
  // Add your code here to act upon SolarPanelVoltage change
}