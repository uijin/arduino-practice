#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <FastLED.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <string.h>  // Include for strlen, strtok
#include <ArduinoJson.h>

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
#define BUTTON_PIN 0  // GPIO0 connected to the button
bool lastButtonState = HIGH;
bool currentButtonState;

// Image list and index
String imageFilenames[256];  // Assuming a maximum of 256 images
int currentImageIndex = 0;
int totalImages = 0;

// Slide show feature variables
unsigned long buttonPressStartTime = 0;
unsigned long longPressThreshold = 2000;  // 1 second for long press
bool buttonLongPressed = false;           // Track if long press action was already taken
bool slideshowActive = false;
unsigned long lastSlideChangeTime = 0;
unsigned long slideInterval = 5000;  // Change image every 5 seconds

// Function prototypes
uint8_t getRowMajorIndex(uint8_t x, uint8_t y);
uint8_t getVerticalZigzagIndex(uint8_t x, uint8_t y);  // Renamed from getUNShapeIndex
uint8_t getHorizontalZigzagIndex(uint8_t x, uint8_t y); // Renamed from get2ShapeIndex
void drawDiagonalTestPattern(CRGB color); // Renamed from drawDiagonalLine
void drawHorizontalTestPattern(uint8_t y, CRGB color); // Renamed from drawHorizontalLine
bool saveImageToLittleFS(const String &filename, uint8_t colors[][3], uint8_t numXPixels, uint8_t numYPixels);
bool loadImageFromLittleFS(const String &filename, uint8_t colors[][3], uint8_t &numXPixels, uint8_t &numYPixels);
String listImagesOnLittleFS();
bool deleteImageFromLittleFS(const String &filename);
String generatePreviewData(uint8_t colors[][3], uint8_t numXPixels, uint8_t numYPixels);
void renderImageToLedMatrix(uint8_t colors[][3]); // Renamed from displayImage
bool parseRgbStringToColorArray(const String &pixelData, uint8_t colors[][3]); // Renamed from processPixelData
void nextImage();
inline void applyNoRotation(uint8_t &x, uint8_t &y) {} // Renamed from rotate0
void rotateUpsideDown(uint8_t &x, uint8_t &y); // Renamed from rotate180degree
void rotate90Clockwise(uint8_t &x, uint8_t &y);
void rotate270Clockwise(uint8_t &x, uint8_t &y);

// Function pointer type for index calculation
typedef uint8_t (*IndexFunction)(uint8_t, uint8_t);
typedef void (*RotateFunction)(uint8_t &, uint8_t &);

// Function pointer variable
IndexFunction getLedPanelIndex = getVerticalZigzagIndex;  // Default to getVerticalZigzagIndex
RotateFunction rotateCoordinates = rotate270Clockwise;

uint8_t mapToPhysicalLedIndex(uint8_t x, uint8_t y) {  // Renamed from getLedIndex
  // Apply rotation to coordinates before mapping to physical LEDs
  uint8_t xDisplay = x, yDisplay = y;
  rotateCoordinates(xDisplay, yDisplay);
  return getLedPanelIndex(xDisplay, yDisplay);
}

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

void setup() {
  Serial.begin(115200);

  // Initialize LittleFS
  if (!LittleFS.begin(true)) {
    Serial.println("An Error has occurred while mounting LittleFS");
    return;
  }

  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);
  // FastLED.setCorrection(TypicalPixelString);
  FastLED.setTemperature(Halogen);

  // LED Panel Test
  drawHorizontalTestPattern(0, CRGB::Red);    // Updated function name
  drawHorizontalTestPattern(1, CRGB::Green);  // Updated function name
  drawHorizontalTestPattern(2, CRGB::Blue);   // Updated function name
  drawHorizontalTestPattern(3, CRGB::Purple); // Updated function name
  FastLED.show();

  // Connect to Wi-Fi
  WiFi.begin(ssid, password);
  for (uint8_t i = 0; i < 10 && WiFi.status() != WL_CONNECTED; i++) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }

  FastLED.clear();
  if (WiFi.status() == WL_CONNECTED) {
    // Print ESP32 IP address
    Serial.println(WiFi.localIP());
    drawIpAddress(WiFi.localIP());
  } else {
    Serial.println("Draw diagonal line when not connected to Wi-Fi");
    drawDiagonalTestPattern(CRGB::White);  // Updated function name
    slideshowActive = true;
  }
  FastLED.show();
  delay(1000);

  // // Load image from LittleFS and display
  // uint8_t savedColors[NUM_LEDS][3];
  // uint8_t savedNumXPixels = N_X;
  // uint8_t savedNumYPixels = N_Y;
  // if (loadImageFromLittleFS("current_image.pxl", savedColors, savedNumXPixels, savedNumYPixels)) {
  //   Serial.println("Load image after reboot");
  //   // --- DISPLAY ON LED PANEL ---
  //   renderImageToLedMatrix(savedColors); // Updated function name
  // }

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
    } else {
      request->send(400, "text/plain", "Missing pixel data");
      return;
    }

    Serial.print("Received pixel data (length after URL decode): ");
    Serial.println(pixelData.length());

    // Process the pixel data
    uint8_t colors[NUM_LEDS][3];
    if (!parseRgbStringToColorArray(pixelData, colors)) {  // Updated function name
      request->send(400, "text/plain", "Invalid pixel data format");
      return;
    }

    // Display on LED panel
    renderImageToLedMatrix(colors);  // Updated function name
    request->send(200, "text/plain", "Image received and displayed!");
  });

  // NEW ROUTE FOR SAVING TO LITTLEFS
  server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request) {
    String pixelData;
    String filename;

    if (request->hasParam("imData", true)) {
      pixelData = request->getParam("imData", true)->value();
    } else {
      request->send(400, "text/plain", "Missing pixel data for save");
      return;
    }

    if (request->hasParam("filename", true)) {
      filename = request->getParam("filename", true)->value();
    } else {
      request->send(400, "text/plain", "Missing filename for save");
      return;
    }

    Serial.print("Saving image data to LittleFS (length after URL decode): ");
    Serial.println(pixelData.length());

    // Process the pixel data
    uint8_t colors[NUM_LEDS][3];
    if (!parseRgbStringToColorArray(pixelData, colors)) {  // Updated function name
      request->send(400, "text/plain", "Invalid pixel data format");
      return;
    }

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

  // List files on LittleFS
  server.on("/list", HTTP_GET, [](AsyncWebServerRequest *request) {
    String imageList = listImagesOnLittleFS();
    request->send(200, "application/json", imageList);
  });

  // NEW: Route to download files from LittleFS
  server.on("/download", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("filename")) {
      request->send(400, "text/plain", "Missing filename parameter");
      return;
    }

    String filename = request->getParam("filename")->value();
    String filePath = "/" + filename;

    if (!LittleFS.exists(filePath)) {
      request->send(404, "text/plain", "File not found");
      return;
    }

    File file = LittleFS.open(filePath, "r");
    if (!file) {
      request->send(500, "text/plain", "Failed to open file");
      return;
    }

    // Set content disposition to attachment to trigger download
    AsyncWebServerResponse *response = request->beginResponse(LittleFS, filePath,
                                                              "application/octet-stream");
    response->addHeader("Content-Disposition", "attachment; filename=" + filename);
    request->send(response);
  });

  // Serve image data for preview
  server.on("/images/*", HTTP_GET, [](AsyncWebServerRequest *request) {
    String path = request->url();
    String filename = path.substring(8);  // Remove "/images/" prefix

    Serial.print("Loading image preview: ");
    Serial.println(filename);

    uint8_t loadedColors[NUM_LEDS][3];
    uint8_t loadedNumXPixels = N_X;
    uint8_t loadedNumYPixels = N_Y;

    if (loadImageFromLittleFS(filename, loadedColors, loadedNumXPixels, loadedNumYPixels)) {
      // Generate json data using the improved function
      String imageData = generatePreviewData(loadedColors, loadedNumXPixels, loadedNumYPixels);
      request->send(200, "application/json", imageData);
    } else {
      Serial.print("Error: Image not found - ");
      Serial.println(filename);
      request->send(404, "text/plain", "Image not found: " + filename);
    }
  });

  // Delete a file from LittleFS
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

  // Load a file from LittleFS
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
      renderImageToLedMatrix(loadedColors);  // Updated function name
      request->send(200, "text/plain", "Image loaded and displayed!");

    } else {
      request->send(500, "text/plain", "Failed to load image");
    }
  });

  // NEW: List all files in LittleFS (not just images)
  server.on("/listall", HTTP_GET, [](AsyncWebServerRequest *request) {
    String fileList = listAllFilesOnLittleFS();
    request->send(200, "application/json", fileList);
  });

  // Start server
  server.begin();
}

void loop() {
  // Read the current state of the button
  currentButtonState = digitalRead(BUTTON_PIN);

  // Button press started (transition from HIGH to LOW)
  if (currentButtonState == LOW && lastButtonState == HIGH) {
    buttonPressStartTime = millis();
    buttonLongPressed = false;  // Reset long press flag
    delay(50);                  // Debounce delay
  }

  // Button is being held down
  if (currentButtonState == LOW && lastButtonState == LOW) {
    // Check for long press threshold
    unsigned long pressDuration = millis() - buttonPressStartTime;
    if (pressDuration >= longPressThreshold && !buttonLongPressed) {
      // Long press detected, toggle slideshow
      slideshowActive = !slideshowActive;
      buttonLongPressed = true;  // Mark that we've handled this long press

      if (slideshowActive) {
        Serial.println("Slideshow started");
        // Initialize slideshow timing
        lastSlideChangeTime = millis();
      } else {
        Serial.println("Slideshow stopped");
      }
      FastLED.showColor(CRGB::Black);
      // Small delay to debounce
      delay(100);
    }
  }

  // Button released (transition from LOW to HIGH)
  if (currentButtonState == HIGH && lastButtonState == LOW) {
    // Only handle as short press if the long press action wasn't taken
    unsigned long pressDuration = millis() - buttonPressStartTime;
    if (pressDuration < longPressThreshold && !buttonLongPressed && !slideshowActive) {
      // This was a short press, and we're not in slideshow mode
      nextImage();
    }
    delay(50);  // Debounce delay
  }

  // Run slideshow if active
  if (slideshowActive) {
    unsigned long currentTime = millis();
    if (currentTime - lastSlideChangeTime >= slideInterval) {
      nextImage();
      lastSlideChangeTime = currentTime;
    }
  }

  // Update the last button state
  lastButtonState = currentButtonState;
  delay(100);
}

// NEW: List all files in LittleFS, not just image files
String listAllFilesOnLittleFS() {
  String fileList = "[";
  File root = LittleFS.open("/");
  File file = root.openNextFile();

  bool first = true;
  while (file) {
    if (!first) {
      fileList += ",";
    }

    // Get file size
    size_t fileSize = file.size();
    String filename = String(file.name());

    // Create a JSON object for each file
    fileList += "{\"name\":\"" + filename + "\",";
    fileList += "\"size\":" + String(fileSize) + "}";

    first = false;
    file = root.openNextFile();
  }

  fileList += "]";
  return fileList;
}

uint8_t getRowMajorIndex(uint8_t x, uint8_t y) {
  return y * N_X + x;
}

/**
 * Converts X,Y coordinates to LED index for a vertical zigzag serpentine pattern
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
uint8_t getVerticalZigzagIndex(uint8_t x, uint8_t y) {  // Renamed from getUNShapeIndex
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

void drawDiagonalTestPattern(CRGB color) {  // Renamed from drawDiagonalLine
  for (uint8_t i = 0; i < min(N_X, N_Y); i++) {
    leds[mapToPhysicalLedIndex(i, i)] = color;  // Updated function call
  }
}

void drawHorizontalTestPattern(uint8_t y, CRGB color) {  // Renamed from drawHorizontalLine
  for (uint8_t x = 0; x < N_X; x++) {
    leds[mapToPhysicalLedIndex(x, y)] = color;  // Updated function call
  }
}

/**
 * Converts X,Y coordinates to LED index for a horizontally oriented zigzag pattern
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
uint8_t getHorizontalZigzagIndex(uint8_t x, uint8_t y) {  // Renamed from get2ShapeIndex
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
    uint8_t index = mapToPhysicalLedIndex(i % DIGIT_WIDTH + offset_x, i / DIGIT_WIDTH + offset_y);  // Updated function call
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
  // Calculate the size needed for the JSON document
  // Each pixel needs 3 integers + brackets and commas
  const size_t pixelCount = numXPixels * numYPixels;
  const size_t capacity = JSON_OBJECT_SIZE(3) +             // width, height, pixels array
                          JSON_ARRAY_SIZE(pixelCount) +     // pixels array
                          pixelCount * JSON_ARRAY_SIZE(3);  // RGB values for each pixel

  DynamicJsonDocument doc(capacity);

  doc["width"] = numXPixels;
  doc["height"] = numYPixels;

  JsonArray pixels = doc.createNestedArray("pixels");
  for (size_t i = 0; i < pixelCount; i++) {
    JsonArray pixel = pixels.createNestedArray();
    pixel.add(colors[i][0]);  // R
    pixel.add(colors[i][1]);  // G
    pixel.add(colors[i][2]);  // B
  }

  String result;
  serializeJson(doc, result);
  return result;
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

/**
 * Renders an image to the LED matrix with rotation applied
 * This function maps the stored pixel data to the physical LED matrix,
 * applying the current rotation function before calculating the final LED index.
 *
 * The process:
 * 1. Read RGB values from the colors array (stored in row-major order)
 * 2. Apply rotation to the coordinates using the rotateCoordinates function
 * 3. Map the rotated coordinates to physical LEDs using mapToPhysicalLedIndex
 * 4. Set the LED color and show the result
 *
 * @param colors 2D array containing RGB values for each pixel [LED_index][RGB]
 */
void renderImageToLedMatrix(uint8_t colors[][3]) {  // Renamed from displayImage
  for (uint8_t y = 0; y < N_Y; y++) {
    for (uint8_t x = 0; x < N_X; x++) {
      uint8_t ledIndexFile = getRowMajorIndex(x, y);
      // Map to the physical LED index based on wiring pattern
      uint8_t ledIndexDisplay = mapToPhysicalLedIndex(x, y);  // Updated function call

      leds[ledIndexDisplay].r = colors[ledIndexFile][0];
      leds[ledIndexDisplay].g = colors[ledIndexFile][1];
      leds[ledIndexDisplay].b = colors[ledIndexFile][2];
    }
  }
  FastLED.show();
}

/**
 * Parses a comma-separated string of RGB values into a color array
 * Uses efficient parsing method to handle the incoming data from HTTP requests
 *
 * @param pixelData String containing comma-separated RGB values
 * @param colors Output array where the parsed RGB values will be stored
 * @return true if parsing was successful, false otherwise
 */
bool parseRgbStringToColorArray(const String &pixelData, uint8_t colors[][3]) {  // Renamed from processPixelData
  int dataLen = pixelData.length();
  int startIdx = 0;
  int endIdx = 0;
  int ledIndex = 0;
  int colorComponent = 0;

  // Process each value in the comma-separated list
  while (startIdx < dataLen && ledIndex < NUM_LEDS) {
    // Find the next comma or end of string
    endIdx = pixelData.indexOf(',', startIdx);
    if (endIdx == -1) {
      endIdx = dataLen;  // Last value
    }

    // Extract and convert the value
    int value = pixelData.substring(startIdx, endIdx).toInt();

    // Calculate the target index for the LED
    int img_x = ledIndex % N_X;
    int img_y = ledIndex / N_X;
    uint8_t ledIndexDisplay = getRowMajorIndex(img_x, img_y);

    // Store the color component
    colors[ledIndexDisplay][colorComponent] = (uint8_t)value;

    // Move to next color component or LED
    colorComponent++;
    if (colorComponent == 3) {
      colorComponent = 0;
      ledIndex++;
    }
    // Move to next value
    startIdx = endIdx + 1;
  }

  // Verify we got all the expected data
  if (ledIndex != NUM_LEDS) {
    Serial.printf("Error: Not enough color data. Expected %d LEDs, got %d LEDs.\n",
                  NUM_LEDS, ledIndex);
    return false;
  }
  return true;
}

// Create a separate function to show the next image (extracted from the existing code)
void nextImage() {
  if (totalImages > 0) {
    currentImageIndex = (currentImageIndex + 1) % totalImages;
    String filename = imageFilenames[currentImageIndex];
    uint8_t colors[NUM_LEDS][3];
    uint8_t numXPixels = N_X;
    uint8_t numYPixels = N_Y;

    if (loadImageFromLittleFS(filename, colors, numXPixels, numYPixels)) {
      renderImageToLedMatrix(colors);  // Updated function call
      Serial.print("Showing image: ");
      Serial.println(filename);
    }
  }
}

/**
 * Rotates coordinates 180 degrees for display on an upside-down panel
 * This function transforms x,y coordinates by flipping both horizontally and vertically
 * so that images appear correct on a panel mounted upside-down.
 *
 * For example, with a 16x16 panel:
 * - (0,0) becomes (15,15)
 * - (1,0) becomes (14,15)
 * - (0,1) becomes (15,14)
 *
 * @param x X-coordinate (passed by reference, will be modified)
 * @param y Y-coordinate (passed by reference, will be modified)
 */
inline void rotateUpsideDown(uint8_t &x, uint8_t &y) {  // Renamed from rotate180degree
  // Flip the coordinates (rotate 180 degrees)
  x = N_X - 1 - x;
  y = N_Y - 1 - y;
}

/**
 * Rotates coordinates 90 degrees clockwise for display
 * This function transforms x,y coordinates for a 90-degree clockwise rotation.
 * The origin (0,0) moves to the top-right corner (N_X-1, 0).
 *
 * For example, with a 16x16 panel:
 * - (0,0) becomes (15, 0)
 * - (1,0) becomes (15, 1)
 * - (0,1) becomes (14, 0)
 *
 * @param x X-coordinate (passed by reference, will be modified)
 * @param y Y-coordinate (passed by reference, will be modified)
 */
inline void rotate90Clockwise(uint8_t &x, uint8_t &y) {
  uint8_t temp_x = x;
  x = N_Y - 1 - y; // New x depends on old y and matrix height (N_Y)
  y = temp_x;      // New y is the old x
}

/**
 * Rotates coordinates 270 degrees clockwise (or 90 degrees counter-clockwise)
 * This function transforms x,y coordinates for a 270-degree clockwise rotation.
 * The origin (0,0) moves to the bottom-left corner (0, N_Y-1).
 *
 * For example, with a 16x16 panel:
 * - (0,0) becomes (0, 15)
 * - (1,0) becomes (0, 14)
 * - (0,1) becomes (1, 15)
 *
 * @param x X-coordinate (passed by reference, will be modified)
 * @param y Y-coordinate (passed by reference, will be modified)
 */
inline void rotate270Clockwise(uint8_t &x, uint8_t &y) {
  uint8_t temp_x = x;
  x = y;             // New x is the old y
  y = N_X - 1 - temp_x; // New y depends on old x and matrix width (N_X)
}
