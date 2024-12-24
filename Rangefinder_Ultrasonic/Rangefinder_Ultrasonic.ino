#include <SoftwareSerial.h>
#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>

SoftwareSerial bt(6, 7);  // RX, TX
// Define HC-SR04 pins
const int trigPin = A0;      // Trig pin connected to Arduino A0
const int echoPin = A1;      // Echo pin connected to Arduino A1
const int waitFailure = 50;  // 50ms
const int laserPin = 4;      // Define the pin connected to laser module

// SH1106 configuration
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/U8X8_PIN_NONE);
const int sclPin = 3;
const int sdaPin = 2;

// Declare variables
long duration;  // Store the time from ultrasonic wave emission to reception
int distance;   // Store the calculated distance
bool u8g2Ready;

void setup() {
  // Add delay at startup to stabilize power
  delay(100);
  // Initialize serial communication with baud rate 9600
  Serial.begin(9600);
  bt.begin(9600);  // Start Bluetooth communication at 9600 baud

  // Initialize OLED first
  u8g2Ready = u8g2.begin();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  delay(50);  // Give OLED time to stabilize

  // Set pin modes
  pinMode(trigPin, OUTPUT);      // Set Trig as output
  pinMode(echoPin, INPUT);       // Set Echo as input
  pinMode(LED_BUILTIN, OUTPUT);  // Set LED as output
  digitalWrite(LED_BUILTIN, LOW);

  pinMode(laserPin, OUTPUT);  // Set the laser pin as output
}

void loop() {
  // Turn on LED and laser
  digitalWrite(LED_BUILTIN, HIGH);
  digitalWrite(laserPin, HIGH);  // Turn laser ON

  // Clear HC-SR04 Trig pin
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);

  // Trigger HC-SR04 ultrasonic emission
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  // Read Echo pin duration with timeout
  duration = pulseIn(echoPin, HIGH, 23529);  // Timeout after ~400cm

  // Validate reading results
  if (duration == 0) {
    char* errTimeout = "Timeout - No echo received";
    if (u8g2Ready) {
      u8g2.drawStr(0, 50, errTimeout);
      u8g2.sendBuffer();
    }
    if (Serial) {
      Serial.println(errTimeout);
    }
    if (bt) {
      bt.println(errTimeout);
    }
    delay(waitFailure);
  } else {
    // Calculate distance. Duration is round-trip time, so divide by 2
    distance = duration * 0.034 / 2;

    // Verify if distance is within valid range
    if (distance < 2 || distance > 400) {
      char* errInvalidDistance = "Invalid distance reading";
      if (u8g2Ready) {
        u8g2.drawStr(0, 50, errInvalidDistance);
        u8g2.sendBuffer();
      }
      if (Serial) {
        Serial.println(errInvalidDistance);
      }
      if (bt) {
        bt.println(errInvalidDistance);
      }
      delay(waitFailure);
    } else {
      // Calculate delay time
      int delayTime = calculateDelay(distance);
      if (u8g2Ready) {
        drawDistance(distance, delayTime);
      }
      // Only output information when serial port is available
      if (Serial) {
        Serial.print("Distance: ");
        Serial.print(distance);
        Serial.print(" cm, Delay: ");
        Serial.print(delayTime);
        Serial.println(" ms");
      }
      if (bt) {
        // Serial.println("Output to BT");
        bt.print("Distance: ");
        bt.print(distance);
        bt.print(" cm, Delay: ");
        bt.print(delayTime);
        bt.println(" ms");
      }
      // Turn off LED and laser
      digitalWrite(LED_BUILTIN, LOW);
      digitalWrite(laserPin, LOW);  // Turn laser OFF
      // Delay
      delay(delayTime);
    }
  }
}

/**
 * Takes a distance (likely measured in centimeters) as input and returns a delayTime (in milliseconds).
 * The delay time is not directly proportional to the distance; instead, it's scaled using an exponential 
 * curve, making it non-linear. This means that changes in distance will have a more significant effect
 * on the delay time at higher distances than at lower distances.
 */
int calculateDelay(int distance) {
  const int MIN_DISTANCE = 5;    // cm
  const int MAX_DISTANCE = 300;  // cm
  const int MIN_DELAY = 100;     // ms
  const int MAX_DELAY = 1000;    // ms

  // Constrain distance to valid range
  int constrainedDistance = constrain(distance, MIN_DISTANCE, MAX_DISTANCE);
  // Calculate normalized distance (0 to 1)
  float normalizedDist = (float)(constrainedDistance - MIN_DISTANCE) / (MAX_DISTANCE - MIN_DISTANCE);
  // Apply exponential curve (you can adjust the exponent 2.0 to change the curve)
  float curve = pow(normalizedDist, 2.0);
  // Calculate final delay
  int delayTime = MIN_DELAY + curve * (MAX_DELAY - MIN_DELAY);
  return delayTime;
}

/**
 * Show metrics on OLED display
 */
void drawDistance(int distance, int delayVal) {
  char distStr[5];
  char delayStr[5];
  // Convert integers to strings
  itoa(distance, distStr, 10);
  itoa(delayVal, delayStr, 10);

  u8g2.clearBuffer();
  u8g2.drawStr(0, 10, "Distance:");
  u8g2.drawStr(60, 10, distStr);
  u8g2.drawStr(80, 10, "cm");
  u8g2.drawStr(0, 30, "Delay:");
  u8g2.drawStr(60, 30, delayStr);
  u8g2.drawStr(80, 30, "ms");
  u8g2.sendBuffer();
}
