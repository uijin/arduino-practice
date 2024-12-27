# ESP32 WiFi Checker

I purchased two development boards with wireless networking capabilities. One uses the ESP32 chip **ESP32-WROOM-32**, while the other has lower specifications and uses the ESP8266 chip, specifically the **NodeMCU Lua WI-FI V3**. To verify the wireless network functionality of these newly acquired boards, I quickly wrote this code using AI.

## Test Program

### ESP32

The program operation flow is:

1. In setup(), attempt to connect to the WiFi router until successful.
2. Connect to https://ifconfig.me/ip. If the connection is successful, it returns the WiFi router IP and outputs it to the serial port; if unsuccessful, it outputs an error message to the serial port.
3. Wait for ten seconds, then repeat step 2.

### ESP8266

When testing ESP8266, you need to change the imported header files (.h) as shown below:

```c++
1 #include <WiFi.h>        --> 1 #include <ESP8266WiFi.h>
2 #include <HTTPClient.h>  --> 2 #include <ESP8266HTTPClient.h>
```

## Observations

1. The program upload speed to the development board is significantly faster for ESP32 compared to ESP8266.
2. The ESP8266 development board has additional SSL support options, allowing you to choose between a full version (All SSL ciphers) or a basic version (Basic SSL ciphers). This is likely due to ESP8266's smaller storage capacity, making it possible to use only the basic SSL support when necessary.