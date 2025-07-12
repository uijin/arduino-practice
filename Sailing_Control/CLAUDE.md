# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is an Arduino project for a sailing control system using ESP32 microcontrollers with ESP-NOW wireless communication. The system consists of a sender device with dual rotary encoders and a receiver device with OLED display, dual servo motors, and propeller motor control.

## System Architecture

### Device Roles
The system uses compile-time device role selection via preprocessor directives:
- **DEVICE_ROLE_SENDER**: Controller device with rotary encoders
- **DEVICE_ROLE_RECEIVER**: Receiver device with OLED display and servo motors

Only one role should be defined at a time in `Sailing_Control.ino:12-13`.

### Communication Protocol
- **ESP-NOW**: Peer-to-peer WiFi communication protocol
- **Channel**: Configurable via `ESP_NOW_CHANNEL` (both devices must match)
- **Data Structure**: `rotary_message` struct containing encoder values, button states, event data, and propeller control
- **ACK System**: Receiver sends acknowledgment packets with RSSI information back to sender

### Hardware Components

#### Sender Device
- 2x HW-040 rotary encoders (ai-esp32-rotary-encoder library)
  - Encoder 1: CLK=D8, DT=D9, SW=D10
  - Encoder 2: CLK=D2, DT=D1, SW=D0
- OLED display (I2C, 128x64 SSD1306)

#### Receiver Device  
- 2x servo motors
  - Servo 1: Pin D0
  - Servo 2: Pin D7 (inverted control due to physical mounting)
- 2x propeller motors via DRV8833 motor driver
  - Motor 1: D3 (forward), D4 (reverse)
  - Motor 2: D5 (forward), D6 (reverse)
- OLED display (I2C, 128x64 SSD1306) with dual-page display

### Key Features
- **Dual encoder control**: Independent X/Y axis control via two rotary encoders
- **Propeller mode**: Double-click encoder 1 to toggle propeller control mode
- **Multi-receiver support**: Switch between multiple receivers (e.g., Sailing Car, Motor Car)
- **Receiver switching**: Single-click rotary encoder 2 to cycle between receivers
- **Independent status tracking**: Separate ACK monitoring, RSSI, and timeouts per receiver
- **Button events**: Single-click and double-click detection with debouncing
- **RSSI monitoring**: Signal strength tracking and display per receiver
- **Packet loss detection**: Statistics tracking with loss rate calculation
- **Timeout handling**: Auto-reset servos to center and stop propeller on communication loss
- **Multi-page OLED**: Receiver cycles between signal info and encoder/servo data
- **Switching interface**: Temporary OLED display showing receiver selection with status
- **Gradual ramp-down**: Smooth propeller deceleration when exiting propeller mode

## Development Commands

### Arduino IDE Setup
```bash
# Install required libraries:
# - ai-esp32-rotary-encoder
# - U8g2lib
# - ESP32Servo
# Tools > Manage Libraries > Search and install each library
```

### Multi-Receiver Configuration
The sender now supports switching between multiple receivers. MAC addresses are configured in the sender code at `Sailing_Control.ino:62-65`.

Current receiver configuration:
```cpp
uint8_t receiverMacAddresses[NUM_RECEIVERS][6] = {
  {0xD8, 0x3B, 0xDA, 0x74, 0x1C, 0xEC}, // Sailing Car
  {0xFC, 0xF5, 0xC4, 0x95, 0x97, 0x6C}  // Motor Car
};
```

To add/change receivers:
1. Upload code to receiver with `DEVICE_ROLE_RECEIVER` defined
2. Open Serial Monitor to get MAC address
3. Update `receiverMacAddresses[]` array in sender code
4. Update `receiverNames[]` array with descriptive names

### Device Role Configuration
In `Sailing_Control.ino`, uncomment only one role:
```cpp
#define DEVICE_ROLE_SENDER     // For controller device
// #define DEVICE_ROLE_RECEIVER // For receiver device
```

## Code Structure

### Core Functions
- `setup()`: Device initialization, ESP-NOW setup, hardware configuration
- `loop()`: Main execution loop with role-specific logic
- `OnDataRecv()`: ESP-NOW receive callback (handles data packets and ACK)
- `OnDataSent()`: ESP-NOW send callback (handles send status and retries)

### Sender-Specific Functions
- `handleRotaryEncoders()`: Read encoder values, handle propeller mode, and button states
- `sendESPNowData()`: Transmit data to current receiver with retry logic
- `switchReceiver()`: Switch between receivers with peer management
- `updateSenderOLED()`: Display encoder/propeller values and current receiver status
- `updateReceiverSwitchOLED()`: Display receiver selection interface

### Receiver-Specific Functions  
- `controlServos()`: Map encoder values to servo positions
- `controlPropeller()`: Control dual motors via DRV8833 based on propeller value
- `updateOLEDPage0()`: Display signal strength and packet statistics
- `updateOLEDPage1()`: Display encoder/servo values and connection status

### Utility Functions
- `rssiToSignalStrength()`: Convert RSSI dBm to percentage
- `drawProgressBar()`: OLED progress bar rendering
- `reinitESPNow()`: Recovery function for communication failures

## Configuration Constants

### Communication
- `ESP_NOW_CHANNEL`: WiFi channel (1-14, default 1)
- `dataInterval`: Send interval in ms (default 50ms = 20Hz)
- `MAX_RETRY_COUNT`: Send retry attempts (default 3)

### Hardware
- `ROTARY_MIN_VALUE`/`ROTARY_MAX_VALUE`: Encoder range (-90 to 90)
- `SERVO_PIN`/`SERVO2_PIN`: Servo control pins
- Encoder pin definitions for both devices

### Display
- `oledUpdateInterval`: OLED refresh rate (default 100ms)
- `dataTimeout`: Communication timeout (default 3000ms)

### Multi-Receiver Support
- `NUM_RECEIVERS`: Number of supported receivers (default 2)
- `currentReceiver`: Index of currently selected receiver (0-based)
- `receiverSwitchMode`: Boolean flag for receiver switching mode
- `SWITCH_MODE_TIMEOUT`: Auto-exit timeout for switching mode (default 3000ms)

## User Interface

### Propeller Mode Operation

#### Entering Propeller Mode
1. **Double-click rotary encoder 1** (within 500ms)
   - Encoder 1 values freeze at current position
   - Propeller value starts at 0
   - OLED displays "PROP: 0" instead of "Enc1: X"
   - Encoder 1 position resets to 0 for propeller control

#### Using Propeller Mode
- **Rotate encoder 1**: Controls propeller value (-90 to +90)
  - Positive values: Forward thrust (both motors forward)
  - Negative values: Reverse thrust (both motors reverse)
  - Zero: Motors stopped
- **Single-click encoder 1**: Immediately resets propeller value to 0
- **Encoder 2**: Continues normal operation for servo control
- **Receiver switching**: Still works normally via encoder 2

#### Exiting Propeller Mode
1. **Double-click rotary encoder 1** again
   - Propeller value gradually ramps down to 0 over 2 seconds
   - During ramp-down: Encoder 1 values remain frozen
   - After ramp-down: Encoder 1 position restored to original value
   - OLED returns to showing "Enc1: X"

### Receiver Switching Operation
1. **Enter Switch Mode**: Single-click rotary encoder 2
   - OLED shows receiver selection interface
   - Current receiver highlighted with arrow (>)
   - Connection status shown for each receiver (OK/---)

2. **Select Receiver**: Single-click rotary encoder 2 again
   - Cycles to next receiver in list
   - Automatically exits switch mode on successful switch
   - ESP-NOW peer updated to new receiver

3. **Auto-Exit**: Wait 3 seconds
   - Returns to normal mode without changing receiver
   - Countdown timer displayed on OLED

### OLED Display Modes

#### Normal Mode (Sender)
- Title shows current receiver: "Control->Sailing Car"
- Encoder/propeller values: "Enc1: X Enc2: Y" or "PROP: X Enc2: Y"
- Button states, send status
- RSSI and signal strength for current receiver

#### Switch Mode (Sender)  
- Title: "Select Receiver:"
- List of receivers with connection status
- Arrow (>) indicates current selection
- Auto-exit countdown timer

#### Receiver Display
- Page 0: Signal strength and packet statistics
- Page 1: Encoder/servo values and connection status
- Automatic page cycling every 5 seconds

## Error Handling

### Receiver Switching Errors
- If peer addition fails, stays on current receiver
- Error message displayed in serial output
- Original peer connection restored automatically

### Communication Errors
- Independent ACK timeout monitoring per receiver
- Automatic ESP-NOW re-initialization on timeout
- Per-receiver retry logic with exponential backoff
- Auto-stop propeller motors on communication timeout

### Propeller Mode Errors
- Gradual ramp-down prevents abrupt motor stopping
- Encoder position restoration maintains control continuity
- Double-click timeout (500ms) prevents accidental mode changes

## Related Projects

### RC Motors Integration
The sender is compatible with the RC_Motors project receiver:
- ESP8266-based motor controller
- DRV8833 dual motor driver
- Same ESP-NOW protocol and message format
- Motor control via encoder values (forward/backward/speed)