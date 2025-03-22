#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

// WiFi credentials
// const char* ssid = "YOUR_WIFI_SSID";
// const char* password = "YOUR_WIFI_PASSWORD";
#include "config_secret.h" // Include the header file for secret configuration (WiFi credentials, etc.)

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

void setup() {
  Serial.begin(115200);

  // Initialize LittleFS
  if(!LittleFS.begin(true)){
    Serial.println("An Error has occurred while mounting LittleFS");
    return;
  }

  // Connect to Wi-Fi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }

  // Print ESP32 IP address
  Serial.println(WiFi.localIP());

  // Route for root / web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/index.html", "text/html");
  });

  // Route for root / web page
  server.on("/index.html", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/index.html", "text/html");
  });

  // Route for style.css
  server.on("/styles.css", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/styles.css", "text/css");
  });

  // Route for pixelit.js
  server.on("/pixelit.js", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/pixelit.js", "application/javascript");
  });

  // Add routes for any other assets PixelIt needs

  // Start server
  server.begin();
}

void loop() {
  // Nothing to do here
}
