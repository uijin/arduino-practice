#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <FastLED.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <string.h>  // Include for strlen, strtok

// WiFi credentials
// const char* ssid = "YOUR_WIFI_SSID";
// const char* password = "YOUR_WIFI_PASSWORD";
#include "config_secret.h"  // Include the header file for secret configuration (WiFi credentials, etc.)

// Pin configuration and LED matrix dimensions
#define LED_PIN 18         // Digital pin connected to the WS2812B data line
#define N_X 16             // Number of LEDs in X direction (width of the matrix)
#define N_Y 16             // Number of LEDs in Y direction (height of the matrix)
#define NUM_LEDS N_X *N_Y  // Total number of LEDs in the matrix (256 LEDs)
#define BRIGHTNESS 10      // LED brightness level (0-255)
#define DELAY_MS 10        // Shorten delay for faster refresh

// Define the LED array that will hold color values for each LED
CRGB leds[NUM_LEDS];

// Button configuration
#define BUTTON_PIN 0       // GPIO0 connected to the button
bool lastButtonState = HIGH;
bool currentButtonState;

// Image list and index
String imageFilenames[256];  // Assuming a maximum of 256 images
int currentImageIndex = 0;
int totalImages = 0;

// Function prototypes
uint8_t getRowMajorIndex(uint8_t x, uint8_t y);
uint8_t getUNShapeIndex(uint8_t x, uint8_t y);  // Define before use
uint8_t get2ShapeIndex(uint8_t x, uint8_t y);
void drawDiagonalLine(CRGB color);
void drawHorizontalLine(uint8_t y, CRGB color);
bool saveImageToLittleFS(const String &filename, uint8_t colors[][3], uint8_t numXPixels, uint8_t numYPixels);
bool loadImageFromLittleFS(const String &filename, uint8_t colors[][3], uint8_t &numXPixels, uint8_t &numYPixels);
String listImagesOnLittleFS();
bool deleteImageFromLittleFS(const String &filename);
String generatePreviewData(uint8_t colors[][3], uint8_t numXPixels, uint8_t numYPixels);
void displayImage(uint8_t colors[][3]);

// Function pointer type for index calculation
typedef uint8_t (*IndexFunction)(uint8_t, uint8_t);

// Function pointer variable
IndexFunction getLedIndex = getUNShapeIndex;  // Default to getUNShapeIndex

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
  // FastLED.setCorrection(TypicalPixelString);
  FastLED.setTemperature(Halogen);
  FastLED.clear();
  FastLED.show();

  // LED Panel Test
  // Comment out these lines after testing
  // drawDiagonalLine(CRGB::Red);
  drawHorizontalLine(0, CRGB::Red);
  drawHorizontalLine(1, CRGB::Green);
  drawHorizontalLine(2, CRGB::Blue);

  FastLED.show();
  delay(3000);  // Display for 5 seconds
  FastLED.clear();


  drawIpAddress(WiFi.localIP());

  FastLED.show();

  // Route for root / web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/pixelit.html", "text/html");  // Make the root redirect to pixelit.html
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
    uint8_t colors[NUM_LEDS][3];

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
      uint8_t ledIndexDisplay = getRowMajorIndex(img_x, img_y);
      int value = atoi(token);
      colors[ledIndexDisplay][colorComponent] = (uint8_t)value;

      colorComponent++;

      if (colorComponent == 3) {
        colorComponent = 0;
        ledIndex++;
      }
      token = strtok(NULL, ",");
    }


    if (ledIndex != NUM_LEDS) {
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
    displayImage(colors);
    request->send(200, "text/plain", "Image received and displayed!");
  });

  // NEW ROUTE FOR SAVING TO LITTLEFS
  server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request) {
    String pixelData;
    String filename;

    if (request->hasParam("imData", true)) {
      pixelData = request->getParam("imData", true)->value();
      Serial.println(pixelData);
    } else {
      request->send(400, "text/plain", "Missing pixel data for save");
      return;
    }

    if (request->hasParam("filename", true)) {
      filename = request->getParam("filename", true)->value();
      Serial.println(filename);
    } else {
      request->send(400, "text/plain", "Missing filename for save");
      return;
    }

    Serial.print("Saving image data to LittleFS (length after URL decode): ");
    Serial.println(pixelData.length());

    // --- IMAGE DATA PROCESSING (same as in /upload) ---
    uint8_t colors[NUM_LEDS][3];

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
      uint8_t ledIndexDisplay = getRowMajorIndex(img_x, img_y);
      int value = atoi(token);
      colors[ledIndexDisplay][colorComponent] = (uint8_t)value;

      colorComponent++;

      if (colorComponent == 3) {
        colorComponent = 0;
        ledIndex++;
      }
      token = strtok(NULL, ",");
    }


    if (ledIndex != NUM_LEDS) {
      Serial.print("Error: Not enough color data received. Expected data for ");
      Serial.print(NUM_LEDS);
      Serial.print(" LEDs. Received data for ");
      Serial.print(ledIndex);
      Serial.println(" LEDs.");
      request->send(400, "text/plain", "Not enough pixel data");
      return;
    }


    // --- Save the Image to LittleFS

    // Ensure the filename has the .pxl extension.
    if (!filename.endsWith(".pxl")) {
      filename += ".pxl";
    }

    if (!saveImageToLittleFS(filename, colors, N_X, N_Y)) {
      request->send(500, "text/plain", "Failed to save image");
      return;
    }

    request->send(200, "text/plain", "Image saved to LittleFS!");
  });

  // NEW ROUTE:  List files on LittleFS
  server.on("/list", HTTP_GET, [](AsyncWebServerRequest *request) {
    String imageList = listImagesOnLittleFS();
    request->send(200, "application/json", imageList);
  });

  // NEW ROUTE: Serve image data for preview
  server.on("/images/*", HTTP_GET, [](AsyncWebServerRequest *request) {
    String path = request->url();
    String filename = path.substring(8); // Remove "/images/" prefix

    Serial.print("Loading image preview: ");
    Serial.println(filename);

    uint8_t loadedColors[NUM_LEDS][3];
    uint8_t loadedNumXPixels = N_X;
    uint8_t loadedNumYPixels = N_Y;

    if (loadImageFromLittleFS(filename, loadedColors, loadedNumXPixels, loadedNumYPixels)) {
      // Generate a base64 encoded image or JSON data
      String imageData = generatePreviewData(loadedColors, loadedNumXPixels, loadedNumYPixels);
      request->send(200, "application/json", imageData);
    } else {
      Serial.print("Error: Image not found - ");
      Serial.println(filename);
      request->send(404, "text/plain", "Image not found: " + filename);
    }
  });

  // NEW ROUTE: Delete a file from LittleFS
  server.on("/delete", HTTP_POST, [](AsyncWebServerRequest *request) {
    String filename;
    if (request->hasParam("filename")) {
      filename = request->getParam("filename")->value();
    } else {
      request->send(400, "text/plain", "Missing filename parameter");
      return;
    }

    if (deleteImageFromLittleFS(filename)) {
      request->send(200, "text/plain", "File deleted successfully.");
    } else {
      request->send(500, "text/plain", "Failed to delete file.");
    }
  });

  // NEW ROUTE: Load a file from LittleFS
  server.on("/load", HTTP_GET, [](AsyncWebServerRequest *request) {
    String filename;
    if (request->hasParam("filename")) {
      filename = request->getParam("filename")->value();
    } else {
      request->send(400, "text/plain", "Missing filename parameter");
      return;
    }

    uint8_t loadedColors[NUM_LEDS][3];
    uint8_t loadedNumXPixels = N_X;
    uint8_t loadedNumYPixels = N_Y;

    if (loadImageFromLittleFS(filename, loadedColors, loadedNumXPixels, loadedNumYPixels)) {
      Serial.println("Image loaded from LittleFS");
      displayImage(loadedColors);
      request->send(200, "text/plain", "Image loaded and displayed!");

    } else {
      request->send(500, "text/plain", "Failed to load image");
    }
  });

  // Start server
  server.begin();

  // Load image from LittleFS and display
  uint8_t savedColors[NUM_LEDS][3];
  uint8_t savedNumXPixels = N_X;
  uint8_t savedNumYPixels = N_Y;
  if (loadImageFromLittleFS("current_image.pxl", savedColors, savedNumXPixels, savedNumYPixels)) {
    Serial.println("Load image after reboot");
    // --- DISPLAY ON LED PANEL ---
    displayImage(savedColors);
  }

  // Initialize button pin
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // Scan LittleFS for .pxl files
  File root = LittleFS.open("/");
  File file = root.openNextFile();
  while (file) {
    if (String(file.name()).endsWith(".pxl")) {
      imageFilenames[totalImages] = String(file.name());
      totalImages++;
    }
    file = root.openNextFile();
  }

  Serial.print("Total images found: ");
  Serial.println(totalImages);
}

void loop() {
  // Read the current state of the button
  currentButtonState = digitalRead(BUTTON_PIN);

  // Check if the button was pressed (transition from HIGH to LOW)
  if (currentButtonState == LOW && lastButtonState == HIGH) {
    delay(200);  // Debounce delay

    // Load and display the next image
    if (totalImages > 0) {
      currentImageIndex = (currentImageIndex + 1) % totalImages;
      String filename = imageFilenames[currentImageIndex];
      uint8_t colors[NUM_LEDS][3];
      uint8_t numXPixels = N_X;
      uint8_t numYPixels = N_Y;

      if (loadImageFromLittleFS(filename, colors, numXPixels, numYPixels)) {
        displayImage(colors);
      }
    }
  }

  // Update the last button state
  lastButtonState = currentButtonState;
}

uint8_t getRowMajorIndex(uint8_t x, uint8_t y) {
    return y * N_X + x;
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

void draw3x3Digit(uint8_t offset_x, uint8_t offset_y, int digit, CRGB color) {
  const uint8_t DIGIT_WIDTH = 3;
  for (uint8_t i = 0; i < digit; i++) {
    uint8_t index = getLedIndex(i % DIGIT_WIDTH + offset_x, i / DIGIT_WIDTH + offset_y);
    leds[index] = color;
  }
}

/**
 * Show IP address on LED panel, using draw3x3Digit().
 * e.g. 192.168.50.14
 * -> draw3x3Digit(0, 0, 1, Red), draw3x3Digit(3, 0, 9, Green), draw3x3Digit(6, 0, 2, Blue)
 * -> draw3x3Digit(0, 3, 1, Red), draw3x3Digit(3, 3, 6, Green), draw3x3Digit(6, 3, 8, Blue)
 * -> draw3x3Digit(0, 6, 0, Red), draw3x3Digit(3, 3, 5, Green), draw3x3Digit(6, 3, 0, Blue)
 * -> draw3x3Digit(0, 9, 0, Red), draw3x3Digit(3, 3, 1, Green), draw3x3Digit(6, 3, 4, Blue)
 */
void drawIpAddress(IPAddress ip) {
  // CRGB colors[] = {CRGB::Red, CRGB::Green, CRGB::Blue}; // Example colors
  // uint8_t colorIndex = 0;
  uint8_t xOffset = 0;
  uint8_t yOffset = 0;

  for (int i = 0; i < 4; i++) {
    int digit1 = ip[i] / 100;
    int digit2 = (ip[i] % 100) / 10;
    int digit3 = ip[i] % 10;

    draw3x3Digit(xOffset, yOffset, digit1, CRGB::Red);
    xOffset += 3;
    draw3x3Digit(xOffset, yOffset, digit2, CRGB::Green);
    xOffset += 3;
    draw3x3Digit(xOffset, yOffset, digit3, CRGB::Blue);
    xOffset += 3;

    xOffset = 0;
    yOffset += 4;
    // colorIndex += 1;
  }
}

/**
 * Saves the processed image data to LittleFS in a binary format, along with metadata about the image dimensions.
 */
bool saveImageToLittleFS(const String &filename, uint8_t colors[][3], uint8_t numXPixels, uint8_t numYPixels) {
  File file = LittleFS.open("/" + filename, "w");

  if (!file) {
    Serial.println("Failed to open file for writing");
    return false;
  }

  // Write 6-byte header
  file.write(0x50);  // 'P'
  file.write(0x58);  // 'X'
  file.write(0x4C);  // 'L'
  file.write(0x01);  // File Schema Version 1
  file.write(numXPixels);
  file.write(numYPixels);

  uint16_t num_leds = numXPixels * numYPixels;

  // Write pixel data (R, G, B for each pixel)
  for (uint16_t i = 0; i < num_leds; i++) {
    file.write(colors[i][0]);  // Red
    file.write(colors[i][1]);  // Green
    file.write(colors[i][2]);  // Blue
  }

  file.close();
  Serial.print("Image saved to LittleFS: ");
  Serial.println(filename);

  return true;
}

/**
 * Loads image data from a LittleFS file (in the defined binary format) into the colors array and retrieves the image dimensions.
 */
bool loadImageFromLittleFS(const String &filename, uint8_t colors[][3], uint8_t &numXPixels, uint8_t &numYPixels) {
  File file = LittleFS.open("/" + filename, "r");

  if (!file) {
    Serial.println("Failed to open file for reading");
    return false;
  }

  // Read 6-byte header
  if (file.available() < 6) {
    Serial.println("File is too small to contain header");
    file.close();
    return false;
  }

  // Check signature
  if (file.read() != 0x50 || file.read() != 0x58 || file.read() != 0x4C) {
    Serial.println("Invalid file signature");
    file.close();
    return false;
  }

  // Read file schema version (discard)
  file.read();

  // Read image dimensions
  numXPixels = file.read();
  numYPixels = file.read();

  uint16_t num_leds = numXPixels * numYPixels;
  if (num_leds != NUM_LEDS) {
    Serial.print("Loaded data (X=");
    Serial.print(numXPixels);
    Serial.print(", Y=");
    Serial.print(numYPixels);
    Serial.print(") does not match current LED data (X=");
    Serial.print(N_X);
    Serial.print(", Y=");
    Serial.print(N_Y);
    Serial.println(")");
    // TODO: Realloc the memory of color and leds array if needed.
  }

  // Read pixel data
  for (uint16_t i = 0; i < num_leds; i++) {
    if (file.available() < 3) {
      Serial.println("File ended prematurely while reading pixel data");
      file.close();
      return false;
    }

    colors[i][0] = file.read();  // Red
    colors[i][1] = file.read();  // Green
    colors[i][2] = file.read();  // Blue
  }

  file.close();
  Serial.print("Image loaded from LittleFS: ");
  Serial.println(filename);
  return true;
}


/**
 * Lists all files with ".pxl" extension on LittleFS with metadata.
 */
String listImagesOnLittleFS() {
  String imageList = "[";
  File root = LittleFS.open("/");
  File file = root.openNextFile();

  bool first = true;
  while (file) {
    if (String(file.name()).endsWith(".pxl")) {
      if (!first) {
        imageList += ",";
      }

      // Get file size
      size_t fileSize = file.size();
      String filename = String(file.name());

      // Create a JSON object for each file
      imageList += "{\"name\":\"" + filename + "\",";
      imageList += "\"size\":" + String(fileSize) + ",";
      imageList += "\"path\":\"/images/" + filename + "\"}";

      first = false;
    }
    file = root.openNextFile();
  }

  imageList += "]";
  return imageList;
}

/**
 * Generates JSON data with image preview information
 */
String generatePreviewData(uint8_t colors[][3], uint8_t numXPixels, uint8_t numYPixels) {
  String data = "{\"width\":" + String(numXPixels) + ",";
  data += "\"height\":" + String(numYPixels) + ",";
  data += "\"pixels\":[";

  for (int i = 0; i < numXPixels * numYPixels; i++) {
    if (i > 0) data += ",";

    data += "[" + String(colors[i][0]) + ","
               + String(colors[i][1]) + ","
               + String(colors[i][2]) + "]";
  }

  data += "]}";
  return data;
}

/**
 * Deletes an image file from LittleFS.
 */
bool deleteImageFromLittleFS(const String &filename) {
  String fullPath = "/" + filename;

  if (LittleFS.remove(fullPath.c_str())) {
    Serial.print("Image deleted from LittleFS: ");
    Serial.println(filename);
    return true;
  } else {
    Serial.print("Failed to delete image from LittleFS: ");
    Serial.println(filename);
    return false;
  }
}

void displayImage(uint8_t colors[][3]) {
  for (uint8_t y = 0; y < N_Y; y++) {
    for (uint8_t x = 0; x < N_X; x++) {
      uint8_t ledIndexFile = getRowMajorIndex(x, y);
      uint8_t ledIndexDisplay = getLedIndex(x, y);

      leds[ledIndexDisplay].r = colors[ledIndexFile][0];
      leds[ledIndexDisplay].g = colors[ledIndexFile][1];
      leds[ledIndexDisplay].b = colors[ledIndexFile][2];
    }
  }
  FastLED.show();
}
