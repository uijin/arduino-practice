#include <Adafruit_INA219.h>
#include <U8g2lib.h>
#include <Stepper.h>
#include <float.h>
#include "thingProperties.h"

//------------------------------------------------------------------------------
// Constants and Pin Definitions
//------------------------------------------------------------------------------
// Stepper motor configuration
const int STEPS_PER_REV = 2048;              // 32 steps/rev * 64:1 gear ratio
const float STEPS_PER_DEGREE = 5.68;         // 2048/360 steps per degree

// ESP32 Pin assignments for stepper motor
const int IN1 = 27;                          // Blue wire   - GPIO27
const int IN2 = 26;                          // Pink wire   - GPIO26
const int IN3 = 25;                          // Yellow wire - GPIO25
const int IN4 = 33;                          // Orange wire - GPIO33

//------------------------------------------------------------------------------
// Global Objects
//------------------------------------------------------------------------------
Adafruit_INA219 ina219;                     // Current/voltage sensor
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);  // OLED display
Stepper myStepper(STEPS_PER_REV, IN1, IN3, IN2, IN4);  // Note: IN3/IN2 swapped for correct sequence

// Track panel angle - Initialize to INFINITY to detect first cloud sync
float currentAngle = INFINITY;

//------------------------------------------------------------------------------
// Display Functions
//------------------------------------------------------------------------------
/**
 * Updates the OLED display with current measurements
 * @param current Current in Amperes
 * @param voltage Voltage in Volts
 * @param power Power in milliWatts
 * @param angle Panel angle in degrees
 */
void displayMeasurements(float current, float voltage, float power, float angle) {
  char currentStr[20], voltageStr[20], powerStr[20], angleStr[20];
  
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  
  // Format measurement strings
  snprintf(currentStr, sizeof(currentStr), "Current: %.3fA", current);
  snprintf(voltageStr, sizeof(voltageStr), "Voltage: %.2fV", voltage);
  snprintf(powerStr, sizeof(powerStr), "Power: %.2fmW", power);
  snprintf(angleStr, sizeof(angleStr), "Angle: %.1f", angle);
  
  // Position strings on display
  u8g2.drawStr(0, 14, currentStr);
  u8g2.drawStr(0, 28, voltageStr);
  u8g2.drawStr(0, 42, powerStr);
  u8g2.drawStr(0, 56, angleStr);
  
  u8g2.sendBuffer();
}

//------------------------------------------------------------------------------
// Motor Control Functions
//------------------------------------------------------------------------------
/**
 * Tests stepper motor pins individually
 * Useful for debugging pin/wire connections
 */
void testPins() {
  Serial.println("\nTesting stepper pins individually:");
  
  // Test each pin with HIGH signal for 500ms
  const int testPins[] = {IN1, IN2, IN3, IN4};
  const char* pinNames[] = {"IN1 (GPIO27)", "IN2 (GPIO26)", "IN3 (GPIO25)", "IN4 (GPIO33)"};
  
  for(int i = 0; i < 4; i++) {
    Serial.print("Testing "); Serial.println(pinNames[i]);
    digitalWrite(testPins[i], HIGH);
    delay(500);
    digitalWrite(testPins[i], LOW);
  }
}

/**
 * Disables all stepper motor pins
 * Called after movement to reduce power consumption
 */
void disableMotor() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
}

//------------------------------------------------------------------------------
// Setup and Initialization
//------------------------------------------------------------------------------
void initializeStepperPins() {
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);
  disableMotor();
}

/**
 * Initializes the INA219 current/voltage sensor
 * @return true if initialization successful
 */
bool initializeINA219() {
  if (!ina219.begin()) {
    Serial.println("Failed to find INA219 chip");
    return false;
  }
  ina219.setCalibration_16V_400mA();
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
 * Waits for cloud connection and initial property sync
 * @param timeout Maximum wait time in milliseconds
 * @return true if connection successful
 */
bool waitForCloudConnection(unsigned long timeout) {
  unsigned long startTime = millis();
  bool connected = false;
  
  Serial.println("Waiting for cloud connection and property sync...");
  while (!connected && (millis() - startTime) < timeout) {
    ArduinoCloud.update();
    if (ArduinoCloud.connected()) {
      delay(1000);  // Allow time for property sync
      ArduinoCloud.update();
      connected = true;
    }
    delay(500);
  }
  return connected;
}

void setup() {
  Serial.begin(115200);
  delay(500);
  
  // Initialize stepper motor pins
  Serial.println("\n=== Initial Setup ===");
  initializeStepperPins();
  testPins();
  
  // Initialize sensors and display
  if (!initializeINA219()) while(1) delay(10);
  if (!initializeDisplay()) while(1) delay(10);
  
  // Configure stepper motor
  myStepper.setSpeed(1);  // 1 RPM for smooth movement
  testPins();
  
  // Initialize cloud connection
  Serial.println("\n=== Starting IoT Cloud Connection ===");
  initProperties();
  ArduinoCloud.begin(ArduinoIoTPreferredConnection);
  setDebugMessageLevel(2);
  ArduinoCloud.printDebugInfo();
  
  if (!waitForCloudConnection(30000)) {  // 30 second timeout
    Serial.println("Warning: Cloud connection timeout");
  }
  
  // Ensure pins are still properly configured after cloud connection
  initializeStepperPins();
  testPins();
}

//------------------------------------------------------------------------------
// Main Loop and Cloud Callbacks
//------------------------------------------------------------------------------
void loop() {
  ArduinoCloud.update();
  
  // Read current sensor values
  float current_mA = ina219.getCurrent_mA();
  float current_A = 0;
  float voltage = 0;
  float power_mW = 0;
  
  // Update measurements if sensor read successful
  if (ina219.success()) {
    current_A = current_mA / 1000.0;
    solar_Panel_Current = current_A;
    
    float busVoltage = ina219.getBusVoltage_V();
    float shuntVoltage = ina219.getShuntVoltage_mV() / 1000.0;
    voltage = busVoltage + shuntVoltage;
    solar_Panel_Voltage = voltage;
    
    power_mW = ina219.getPower_mW();
    solar_Panel_Power = power_mW / 1000.0;  // Convert to Watts for cloud
  }
  
  displayMeasurements(current_A, voltage, power_mW, currentAngle);
  delay(1000);
}

/**
 * Callback function for panel angle changes from cloud
 * Handles both initial angle sync and subsequent movements
 */
void onPanelHorizontalAngleChange() {
  // Handle initial angle sync from cloud
  if (currentAngle == INFINITY) {
    currentAngle = panel_Horizontal_Angle;
    Serial.print("Initial angle from cloud: ");
    Serial.println(currentAngle);
    return;
  }
  
  // Handle angle change request
  float newAngle = panel_Horizontal_Angle;
  float angleDiff = newAngle - currentAngle;
  
  Serial.printf("Angle change: Current=%.1f New=%.1f Diff=%.1f\n", 
                currentAngle, newAngle, angleDiff);
  
  // Move motor
  int steps = angleDiff * STEPS_PER_DEGREE;
  myStepper.step(steps);
  currentAngle = newAngle;
  
  Serial.println("Motor movement completed");
  disableMotor();  // Reduce power consumption when not moving
}