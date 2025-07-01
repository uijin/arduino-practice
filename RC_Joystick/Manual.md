# RC Joystick Controller Manual

## Quick Start Guide

### 1. Hardware Setup
**Required Components:**
- XIAO ESP32C6 microcontroller
- KY-023 Dual Axis Joystick Module
- Jumper wires
- USB-C cable for programming/power

**Wiring Connections:**
```
KY-023 Joystick    XIAO ESP32C6
--------------     ------------
VCC            →   3.3V
GND            →   GND
VRX (X-axis)   →   A0
VRY (Y-axis)   →   A1
SW (Button)    →   D2
```

### 2. Software Setup
1. Install Arduino IDE with ESP32 board package
2. Select board: "ESP32C6 Dev Module" (or XIAO ESP32C6 if available)
3. Upload the RC_Joystick.ino sketch
4. Open Serial Monitor at 115200 baud

### 3. First Time Calibration
**⚠️ IMPORTANT: Calibrate before first use!**

1. **Power on while holding joystick button** for 3+ seconds
2. **LED will blink fast** - you're now in calibration mode
3. **Move joystick to all extremes** while holding button:
   - Push fully forward, backward, left, right
   - Move to all four corners
4. **Release button** when done with range mapping
5. **Center joystick** and **press button once** to set neutral position
6. **Calibration complete!** LED returns to normal blinking

## Operating Instructions

### Basic Controls
- **Forward/Backward**: Push/pull joystick Y-axis
- **Left/Right Turn**: Push joystick X-axis left/right
- **Emergency Stop**: Release joystick to center position

### Control Characteristics
- **Tank Drive Mixing**: Independent left/right motor control
- **Steering Speed**: Half-speed differential for smooth turns
- **Reverse Steering**: Automatic steering direction correction when backing up
- **Deadband**: Small neutral zone around center prevents drift

### LED Status Indicators
| LED Pattern | Meaning |
|-------------|---------|
| Solid (slow blink) | Normal operation |
| Fast blink | Calibration mode |
| Medium blink | Communication errors |
| Very fast blink | System error |
| Off | Startup or critical failure |

## Control Examples

### Basic Movements
```
Joystick Position     →    Car Movement
----------------           -------------
Forward only          →    Straight forward
Backward only         →    Straight backward
Left only             →    Spin left (tank turn)
Right only            →    Spin right (tank turn)
```

### Combined Movements
```
Joystick Position     →    Car Movement
----------------           -------------
Forward + Right       →    Forward right turn
Forward + Left        →    Forward left turn
Backward + Right      →    Backward right turn (auto-corrected)
Backward + Left       →    Backward left turn (auto-corrected)
```

## Advanced Features

### Manual Calibration (Anytime)
If control feels off, recalibrate:
1. Power cycle the controller
2. Hold joystick button during startup
3. Follow calibration procedure above

### Communication Status
- **Range**: Typical 100-200 meters line-of-sight
- **Frequency**: 20Hz control updates
- **Target**: Motor Car receiver (MAC: FC:F5:C4:95:97:6C)

### Serial Debug Output
Connect to Serial Monitor to see real-time data:
```
Joy X:2048 Y:2048 | Motors L: 0 R: 0 | Btn:0 | RSSI:-65 | Msg:1247
```
- **Joy X/Y**: Raw joystick readings (0-4095)
- **Motors L/R**: Calculated motor speeds (-90 to +90)
- **Btn**: Button state (0=released, 1=pressed)
- **RSSI**: Signal strength (closer to 0 = better)
- **Msg**: Message counter

## Troubleshooting

### No Response from Car
**Check:**
- ✅ Car receiver is powered on
- ✅ MAC address matches in code
- ✅ Both devices on same ESP-NOW channel
- ✅ Distance within range (< 200m)

**Try:**
- Power cycle both controller and car
- Move closer to car
- Check for interference sources

### Erratic Movement
**Symptoms:** Car moves when joystick is centered
**Solution:** Recalibrate the joystick

**Symptoms:** Wrong directions or oversensitive
**Solution:** Check wiring connections to A0/A1 pins

### Calibration Issues
**Can't Enter Calibration:**
- Hold button longer during power-on (3+ seconds)
- Check button wiring to D2 pin

**Unstable Center:**
- Ensure joystick naturally centers before final calibration step
- Avoid touching joystick during center position setting

### Communication Problems
**LED Blinking Patterns:**
- Fast blink at startup = ESP-NOW init failure
- Medium blink during operation = send/receive errors
- Check Serial Monitor for error messages

## Technical Specifications

### Performance
- **Control Frequency**: 20Hz (50ms updates)
- **Response Time**: < 100ms joystick to motor
- **Resolution**: 12-bit ADC (4096 levels per axis)
- **Range**: 100-200m typical
- **Battery Life**: Depends on power source

### Communication Protocol
- **Technology**: ESP-NOW (peer-to-peer WiFi)
- **Channel**: 1
- **Encryption**: None
- **Compatibility**: RC_Motors receiver project

### Hardware Requirements
- **Operating Voltage**: 3.3V
- **Current Draw**: ~85mA total
- **ADC Resolution**: 12-bit (0-4095)
- **Button Type**: Active low with internal pullup

## Maintenance

### Regular Checks
- Verify joystick returns to center position
- Check for loose wiring connections
- Monitor signal strength (RSSI values)

### Storage
- Power off when not in use
- Store in dry environment
- Avoid extreme temperatures

### Updates
- Check for firmware updates in project repository
- Backup calibration before updating (automatic via NVS)

## Safety Guidelines

### Operating Safety
- Always ensure clear area before testing
- Keep within line-of-sight of RC car
- Have emergency stop method ready
- Respect local RC operation regulations

### Technical Safety
- Do not exceed voltage ratings
- Ensure proper grounding
- Avoid short circuits on power pins
- Use appropriate gauge wires for connections

## Support

### Getting Help
- Check Serial Monitor for debug information
- Verify wiring against pin diagrams
- Test basic joystick readings before full operation
- Consult CLAUDE.md for technical details

### Common Questions
**Q: Can I use different joystick modules?**
A: Yes, any analog joystick with similar pinout should work

**Q: How do I change the target car?**
A: Update the `receiverMacAddress` array in the code

**Q: Can I adjust steering sensitivity?**
A: Yes, modify the `/2` value in the steering calculation

**Q: Does calibration persist after power off?**
A: Yes, calibration is saved to flash memory automatically

---

*For technical details and development information, see CLAUDE.md*