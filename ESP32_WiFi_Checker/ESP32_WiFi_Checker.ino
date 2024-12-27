#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

// Replace with your network credentials
const char* ssid = "YOUR_WIFI_NAME";
const char* password = "YOUR_WIFI_PASSWORD";

void setup() {
  Serial.begin(115200);
  
  // Connect to Wi-Fi
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClientSecure client;
    // Skip SSL certificate verification
    client.setInsecure();
    
    HTTPClient https;
    
    Serial.println("[HTTPS] begin...");
    if (https.begin(client, "https://ifconfig.me/ip")) {
      Serial.println("[HTTPS] GET...");
      // Start connection and send HTTP header
      int httpCode = https.GET();
      
      // httpCode will be negative on error
      if (httpCode > 0) {
        // HTTP header has been sent and Server response header has been handled
        Serial.printf("[HTTPS] GET... code: %d\n", httpCode);
        
        // File found at server
        if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
          String payload = https.getString();
          Serial.println("Your public IP is:");
          Serial.println(payload);
        }
      } else {
        Serial.printf("[HTTPS] GET... failed, error: %s\n", https.errorToString(httpCode).c_str());
      }
      
      https.end();
    } else {
      Serial.println("[HTTPS] Unable to connect");
    }
    
    delay(10000); // Wait 10 seconds before next request
  }
}