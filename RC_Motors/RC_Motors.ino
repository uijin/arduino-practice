#include <espnow.h>
#include <ESP8266WiFi.h>

// ESP-NOW Configuration
#define ESP_NOW_CHANNEL 1      
#define ESP_NOW_ENCRYPT false  
#define DATA_TIMEOUT 3000      // Data reception timeout (ms)

// DRV8833 Motor Driver Pin Definitions
// Motor A (Left Motor)
#define MOTOR_A_PIN1 D1  // GPIO5 - AIN1
#define MOTOR_A_PIN2 D2  // GPIO4 - AIN2

// Motor B (Right Motor)  
#define MOTOR_B_PIN1 D5  // GPIO14 - BIN1
#define MOTOR_B_PIN2 D6  // GPIO12 - BIN2

// Motor Control Parameters
#define PWM_RANGE 1023        // ESP8266 PWM range (10-bit)
#define MIN_PWM_THRESHOLD 100 // Minimum PWM to overcome motor stiction
#define ENCODER_MAX_VALUE 90  // Maximum encoder value from sender

// Status LED
#define STATUS_LED D4  // GPIO2 - Built-in LED (inverted)

// Data structure matching sender (from Sailing_Control.ino)
typedef struct rotary_message {
  int encoder1_value;   
  int encoder1_norm;    // Left motor control (-90 to 90)
  int encoder2_value;   
  int encoder2_norm;    // Right motor control (-90 to 90)
  bool button_state;    
  bool button2_state;   
  uint32_t msg_id;      
  uint8_t button_event; 
  uint8_t button2_event;
} rotary_message;

// ACK message structure
typedef struct ack_message {
  int rssi;            
  uint32_t ack_id;     
} ack_message;

// Global variables
rotary_message receivedData;
ack_message ackData;
unsigned long lastDataReceivedTime = 0;
bool dataReceived = false;
uint32_t totalPackets = 0;
uint32_t lastMsgId = 0;
uint8_t senderMac[6];

// Motor control variables
int currentMotorA_Speed = 0;
int currentMotorB_Speed = 0;
bool motorA_Forward = true;
bool motorB_Forward = true;

// Initialize ESP-NOW
bool initESPNow() {
  if (esp_now_init() != 0) {
    Serial.println("Error initializing ESP-NOW");
    return false;
  }
  return true;
}

// Get ESP error message
const char* getESPErrorMsg(int err) {
  switch (err) {
    case 0: return "Success";
    case 1: return "ESP-NOW not init";
    case 2: return "Invalid argument";
    case 3: return "Internal error";
    case 4: return "Out of memory";
    case 5: return "Peer not found";
    case 6: return "Interface error";
    default: return "Unknown error";
  }
}

// Control Motor A (Left Motor)
void controlMotorA(int encoderValue) {
  int pwmValue = 0;
  
  if (encoderValue == 0) {
    // Stop motor
    digitalWrite(MOTOR_A_PIN1, LOW);
    digitalWrite(MOTOR_A_PIN2, LOW);
    currentMotorA_Speed = 0;
    return;
  }
  
  // Calculate PWM value (0-90 -> MIN_PWM_THRESHOLD to PWM_RANGE)
  pwmValue = map(abs(encoderValue), 0, ENCODER_MAX_VALUE, MIN_PWM_THRESHOLD, PWM_RANGE);
  pwmValue = constrain(pwmValue, MIN_PWM_THRESHOLD, PWM_RANGE);
  
  if (encoderValue > 0) {
    // Forward direction
    analogWrite(MOTOR_A_PIN1, pwmValue);
    digitalWrite(MOTOR_A_PIN2, LOW);
    motorA_Forward = true;
  } else {
    // Backward direction
    digitalWrite(MOTOR_A_PIN1, LOW);
    analogWrite(MOTOR_A_PIN2, pwmValue);
    motorA_Forward = false;
  }
  
  currentMotorA_Speed = pwmValue;
}

// Control Motor B (Right Motor)
void controlMotorB(int encoderValue) {
  int pwmValue = 0;
  
  if (encoderValue == 0) {
    // Stop motor
    digitalWrite(MOTOR_B_PIN1, LOW);
    digitalWrite(MOTOR_B_PIN2, LOW);
    currentMotorB_Speed = 0;
    return;
  }
  
  // Calculate PWM value (0-90 -> MIN_PWM_THRESHOLD to PWM_RANGE)
  pwmValue = map(abs(encoderValue), 0, ENCODER_MAX_VALUE, MIN_PWM_THRESHOLD, PWM_RANGE);
  pwmValue = constrain(pwmValue, MIN_PWM_THRESHOLD, PWM_RANGE);
  
  if (encoderValue > 0) {
    // Forward direction
    analogWrite(MOTOR_B_PIN1, pwmValue);
    digitalWrite(MOTOR_B_PIN2, LOW);
    motorB_Forward = true;
  } else {
    // Backward direction
    digitalWrite(MOTOR_B_PIN1, LOW);
    analogWrite(MOTOR_B_PIN2, pwmValue);
    motorB_Forward = false;
  }
  
  currentMotorB_Speed = pwmValue;
}

// Emergency stop - stop both motors immediately
void emergencyStop() {
  digitalWrite(MOTOR_A_PIN1, LOW);
  digitalWrite(MOTOR_A_PIN2, LOW);
  digitalWrite(MOTOR_B_PIN1, LOW);
  digitalWrite(MOTOR_B_PIN2, LOW);
  currentMotorA_Speed = 0;
  currentMotorB_Speed = 0;
  Serial.println("EMERGENCY STOP - All motors stopped");
}

// Data receive callback
void OnDataRecv(uint8_t *mac, uint8_t *data, uint8_t data_len) {
  // Get RSSI value
  int receivedRSSI = WiFi.RSSI();
  
  // Process received data
  if (data_len == sizeof(rotary_message)) {
    memcpy(&receivedData, data, sizeof(rotary_message));
    
    // Update data reception status
    lastDataReceivedTime = millis();
    dataReceived = true;
    totalPackets++;
    
    // Control motors based on encoder values
    controlMotorA(receivedData.encoder1_norm);  // Left motor
    controlMotorB(receivedData.encoder2_norm);  // Right motor
    
    // Debug output
    Serial.print("Received - ID: ");
    Serial.print(receivedData.msg_id);
    Serial.print(", Enc1: ");
    Serial.print(receivedData.encoder1_norm);
    Serial.print(", Enc2: ");
    Serial.print(receivedData.encoder2_norm);
    Serial.print(", RSSI: ");
    Serial.print(receivedRSSI);
    Serial.println(" dBm");
    
    // Prepare ACK response
    ackData.rssi = receivedRSSI;
    ackData.ack_id = receivedData.msg_id;
    
    // Store sender MAC address for response
    memcpy(senderMac, mac, 6);
    
    // Send ACK
    int result = esp_now_send(mac, (uint8_t *)&ackData, sizeof(ackData));
    if (result != 0) {
      Serial.print("ACK send failed: ");
      Serial.println(getESPErrorMsg(result));
    }
  }
}

// Data send callback
void OnDataSent(uint8_t *mac_addr, uint8_t status) {
  if (status == 0) {
    Serial.println("ACK sent successfully");
  } else {
    Serial.println("ACK send failed");
  }
}

void setup() {
  // Initialize serial communication
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n=== ESP8266 RC Motors Receiver ===");
  
  // Initialize motor control pins
  pinMode(MOTOR_A_PIN1, OUTPUT);
  pinMode(MOTOR_A_PIN2, OUTPUT);
  pinMode(MOTOR_B_PIN1, OUTPUT);
  pinMode(MOTOR_B_PIN2, OUTPUT);
  
  // Initialize status LED
  pinMode(STATUS_LED, OUTPUT);
  digitalWrite(STATUS_LED, HIGH); // Turn off LED (inverted)
  
  // Set PWM range for ESP8266
  analogWriteRange(PWM_RANGE);
  
  // Stop all motors initially
  emergencyStop();
  
  // Disconnect existing WiFi connection
  WiFi.disconnect(true);
  delay(100);
  
  // Set WiFi mode to Station
  WiFi.mode(WIFI_STA);
  delay(100);
  
  // Display MAC address
  String macAddress = WiFi.macAddress();
  Serial.print("MAC Address: ");
  Serial.println(macAddress);
  
  // Convert MAC to hex format for sender configuration
  uint8_t mac[6];
  WiFi.macAddress(mac);
  Serial.print("MAC for sender config: {0x");
  for (int i = 0; i < 6; i++) {
    Serial.print(mac[i], HEX);
    if (i < 5) Serial.print(", 0x");
  }
  Serial.println("}");
  
  // Set WiFi channel
  WiFi.channel(ESP_NOW_CHANNEL);
  delay(100);
  
  // Initialize ESP-NOW
  if (!initESPNow()) {
    Serial.println("ESP-NOW initialization failed");
    while (1) {
      digitalWrite(STATUS_LED, LOW);  // Turn on LED (error indicator)
      delay(500);
      digitalWrite(STATUS_LED, HIGH); // Turn off LED
      delay(500);
    }
  }
  
  // Register callbacks
  esp_now_register_recv_cb(OnDataRecv);
  esp_now_register_send_cb(OnDataSent);
  
  Serial.println("Setup complete - Ready to receive motor commands");
  Serial.println("Motor A (Left): D1(AIN1), D2(AIN2)");
  Serial.println("Motor B (Right): D5(BIN1), D6(BIN2)");
  
  // Indicate ready status
  for (int i = 0; i < 3; i++) {
    digitalWrite(STATUS_LED, LOW);  // Turn on LED
    delay(200);
    digitalWrite(STATUS_LED, HIGH); // Turn off LED
    delay(200);
  }
}

void loop() {
  static unsigned long lastStatusUpdate = 0;
  
  // Check for data timeout
  if (dataReceived && (millis() - lastDataReceivedTime > DATA_TIMEOUT)) {
    Serial.println("Data timeout - Emergency stop activated");
    emergencyStop();
    dataReceived = false;
    
    // Flash LED to indicate timeout
    digitalWrite(STATUS_LED, LOW);
    delay(100);
    digitalWrite(STATUS_LED, HIGH);
  }
  
  // Status LED - breathing effect when receiving data
  if (dataReceived && (millis() - lastDataReceivedTime < DATA_TIMEOUT)) {
    static unsigned long lastLedUpdate = 0;
    static int ledBrightness = 0;
    static int ledDirection = 5;
    
    if (millis() - lastLedUpdate > 50) {
      ledBrightness += ledDirection;
      if (ledBrightness >= 255 || ledBrightness <= 0) {
        ledDirection = -ledDirection;
      }
      ledBrightness = constrain(ledBrightness, 0, 255);
      analogWrite(STATUS_LED, 255 - ledBrightness); // Inverted LED
      lastLedUpdate = millis();
    }
  } else {
    // No data - solid LED
    digitalWrite(STATUS_LED, HIGH); // Off
  }
  
  // Print status every 5 seconds
  if (millis() - lastStatusUpdate > 5000) {
    if (!dataReceived) {
      // Print MAC address when no data received (for sender configuration)
      uint8_t mac[6];
      WiFi.macAddress(mac);
      Serial.print("Waiting for sender... MAC: {0x");
      for (int i = 0; i < 6; i++) {
        if (mac[i] < 16) Serial.print("0");
        Serial.print(mac[i], HEX);
        if (i < 5) Serial.print(", 0x");
      }
      Serial.println("}");
    } else {
      Serial.print("Status - Packets: ");
      Serial.print(totalPackets);
      Serial.print(", Motor A: ");
      Serial.print(currentMotorA_Speed);
      Serial.print(motorA_Forward ? " FWD" : " REV");
      Serial.print(", Motor B: ");
      Serial.print(currentMotorB_Speed);
      Serial.println(motorB_Forward ? " FWD" : " REV");
    }
    lastStatusUpdate = millis();
  }
  
  // Allow ESP8266 to handle background tasks
  yield();
  delay(1);
}