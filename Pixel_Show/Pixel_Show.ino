#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <FastLED.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <string.h> // Include for strlen, strtok

// WiFi credentials
// const char* ssid = "YOUR_WIFI_SSID";
// const char* password = "YOUR_WIFI_PASSWORD";
#include "config_secret.h" // Include the header file for secret configuration (WiFi credentials, etc.)

// Pin configuration and LED matrix dimensions
#define LED_PIN 18         // Digital pin connected to the WS2812B data line
#define N_X 16            // Number of LEDs in X direction (width of the matrix)
#define N_Y 16            // Number of LEDs in Y direction (height of the matrix)
#define NUM_LEDS N_X * N_Y // Total number of LEDs in the matrix (256 LEDs)
#define BRIGHTNESS 5      // LED brightness level (0-255)
#define DELAY_MS 10       // Shorten delay for faster refresh

// Define the LED array that will hold color values for each LED
CRGB leds[NUM_LEDS];

// Function prototypes
uint8_t getUNShapeIndex(uint8_t x, uint8_t y); // Define before use
uint8_t get2ShapeIndex(uint8_t x, uint8_t y);
void drawDiagonalLine(CRGB color);
void drawHorizontalLine(uint8_t y, CRGB color);

// Function pointer type for index calculation
typedef uint8_t (*IndexFunction)(uint8_t, uint8_t);

// Function pointer variable
IndexFunction getLedIndex = getUNShapeIndex; // Default to getUNShapeIndex

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

void setup() {
  Serial.begin(115200);

  // Initialize LittleFS
  if (!LittleFS.begin(true)) {
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

  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.clear();
  FastLED.show();

  // LED Panel Test
  // Comment out these lines after testing
  drawDiagonalLine(CRGB::Red);
  drawHorizontalLine(8, CRGB::Blue);
  FastLED.show();
  delay(5000); // Display for 5 seconds
  FastLED.clear();
  FastLED.show();

  // Route for root / web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/pixelit.html", "text/html"); // Make the root redirect to pixelit.html
  });

  // Route for pixelit.html
  server.on("/pixelit.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/pixelit.html", "text/html");
  });

  // NEW: Route for handling image upload (POST request)
  server.on("/upload", HTTP_POST, [](AsyncWebServerRequest *request) {
    String pixelData;
    if (request->hasParam("imData", true)) {
      pixelData = request->getParam("imData", true)->value();
      Serial.println(pixelData);
    } else {
      request->send(400, "text/plain", "Missing pixel data");
      return;
    }

    Serial.print("Received pixel data (length after URL decode): ");
    Serial.println(pixelData.length());

    // --- IMAGE DATA PROCESSING ---
    int colors[NUM_LEDS][3];

    char *str;
    char *token;

    // Create a non-const copy of pixelData for strtok
    char pixelDataCopy[pixelData.length() + 1];
    strcpy(pixelDataCopy, pixelData.c_str());

    str = pixelDataCopy;

    int ledIndex = 0;
    int colorComponent = 0;

    token = strtok(str, ",");
    while (token != NULL && ledIndex < NUM_LEDS) {
      int img_x = ledIndex % N_X;
      int img_y = ledIndex / N_X;
      uint8_t ledIndexDisplay = getLedIndex(img_x, img_y);
      int value = atoi(token);
      colors[ledIndexDisplay][colorComponent] = value;

      colorComponent++;

      if (colorComponent == 3) {
        colorComponent = 0;
        ledIndex++;
      }
      token = strtok(NULL, ",");
    }


    if(ledIndex != NUM_LEDS) {
      Serial.print("Error: Not enough color data received. Expected data for ");
      Serial.print(NUM_LEDS);
      Serial.print(" LEDs. Received data for ");
      Serial.print(ledIndex);
      Serial.println(" LEDs.");
      request->send(400, "text/plain", "Not enough pixel data");
      return;
    }

    Serial.print("colors[0][0] = ");
    Serial.println(colors[0][0]);
    Serial.print("colors[0][1] = ");
    Serial.println(colors[0][1]);
    Serial.print("colors[0][2] = ");
    Serial.println(colors[0][2]);

    // --- DISPLAY ON LED PANEL ---
    for (uint8_t y = 0; y < N_Y; y++) {
      for (uint8_t x = 0; x < N_X; x++) {
        uint8_t ledIndexDisplay = getLedIndex(x, y);

        // Calculate the colors[ledIndex][0], colors[ledIndex][1] and colors[ledIndex][2] from the color array.
        leds[ledIndexDisplay].r = colors[ledIndexDisplay][0];
        leds[ledIndexDisplay].g = colors[ledIndexDisplay][1];
        leds[ledIndexDisplay].b = colors[ledIndexDisplay][2];
      }
    }

    FastLED.show(); // Send the data to the LEDs
    request->send(200, "text/plain", "Image received and displayed!");
  });

  // Route for style.css (ADDED)
  server.on("/styles.css", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/styles.css", "text/css");
  });

  // Route for pixelit.js (ADDED)
  server.on("/pixelit.js", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/pixelit.js", "application/javascript");
  });

  // Start server
  server.begin();
}

void loop() {
  // Nothing to do here - the display is updated in the /upload handler
}

/**
 * Converts X,Y coordinates to LED index for a U/N-shaped serpentine pattern
 * For counterclockwise rotated 90 degrees S-shape panel.
 *
 * In this pattern, even columns run bottom to top, odd columns run top to bottom:
 *
 * 15,15 14,15 13,15 12,15 ... 1,15  0,15
 *   ↑     ↓     ↑     ↓         ↓     ↑
 * 15,0  14,0  13,0  12,0  ... 1,0   0,0
 *
 * @param x X-coordinate (column, 0-based, 0-15)
 * @param y Y-coordinate (row, 0-based, 0-15)
 * @return The LED index in the strip (0-255)
 */
uint8_t getUNShapeIndex(uint8_t x, uint8_t y) {
  // Check if x is even or odd to determine direction
  uint8_t odd_x = x % 2;
  if (odd_x == 0) {
    // Even columns (0, 2, 4...) run bottom to top
    return x * N_Y + y;
  } else {
    // Odd columns (1, 3, 5...) run top to bottom
    return x * N_Y + (N_Y - 1 - y);
  }
}

void drawDiagonalLine(CRGB color) {
  for (uint8_t i = 0; i < min(N_X, N_Y); i++) {
    leds[getLedIndex(i, i)] = color;
  }
}

void drawHorizontalLine(uint8_t y, CRGB color) {
  for (uint8_t x = 0; x < N_X; x++) {
    leds[getLedIndex(x, y)] = color;
  }
}

/**
 * Converts X,Y coordinates to LED index for a horizontally oriented S-shape pattern
 *
 * In this pattern, even rows run left to right, odd rows run right to left:
 *
 * 0,2  1,2  2,2  ... 14,2  15,2
 *  ↑     ↑     ↑         ↑     ↑
 * 15,1 14,1 13,1 ...  1,1   0,1
 *  ↑     ↑     ↑         ↑     ↑
 * 0,0  1,0  2,0  ... 14,0  15,0
 *
 * @param x X-coordinate (column, 0-based)
 * @param y Y-coordinate (row, 0-based)
 * @return The LED index in the strip
 */
uint8_t get2ShapeIndex(uint8_t x, uint8_t y) {
  // Check if y is even or odd to determine direction
  uint8_t odd_y = y % 2;
  if (odd_y == 0) {
    // Even rows (0, 2, 4...) run left to right
    return y * N_X + x;
  } else {
    // Odd rows (1, 3, 5...) run right to left
    return y * N_X + (N_X - 1 - x);
  }
}
