#include <FastLED.h>
#include <stdint.h>  // For uint8_t type

// Pin configuration and LED matrix dimensions
#define LED_PIN 6         // Digital pin connected to the WS2812B data line
#define N_X 16            // Number of LEDs in X direction (width of the matrix)
#define N_Y 16            // Number of LEDs in Y direction (height of the matrix) 
#define NUM_LEDS N_X* N_Y // Total number of LEDs in the matrix (256 LEDs)
#define BRIGHTNESS 5      // LED brightness level (0-255, low value to prevent eye strain and reduce power consumption)
#define DELAY_MS 100      // Delay between lighting each LED (in milliseconds)

// Define the LED array that will hold color values for each LED
CRGB leds[NUM_LEDS];

// Array of different colors to cycle through when lighting up the LEDs
// Each color is a predefined constant from the FastLED library
CRGB colors[] = {
  CRGB::Red,      // Pure red
  CRGB::Green,    // Pure green
  CRGB::Blue,     // Pure blue
  CRGB::Yellow,   // Yellow (red + green)
  CRGB::Purple,   // Purple (red + blue)
  CRGB::Orange,   // Orange
  CRGB::Amethyst, // Amethyst (light purple)
};
#define NUM_COLORS (sizeof(colors) / sizeof(colors[0]))  // Calculate number of colors in the array

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

/**
 * Converts X,Y coordinates to LED index for a standard row-by-row pattern (Z-shape)
 * 
 * In this pattern, all rows run left to right:
 * 
 * 0,2  1,2  2,2  ... 14,2  15,2
 *  ↑     ↑     ↑         ↑     ↑
 * 0,1  1,1  2,1  ... 14,1  15,1
 *  ↑     ↑     ↑         ↑     ↑
 * 0,0  1,0  2,0  ... 14,0  15,0
 * 
 * @param x X-coordinate (column, 0-based)
 * @param y Y-coordinate (row, 0-based)
 * @return The LED index in the strip
 */
uint8_t getIndex(uint8_t x, uint8_t y) {
  // Simple row-major ordering
  return y * N_X + x;
}

/**
 * Setup function runs once when the Arduino starts
 */
void setup() {
  // Initialize the FastLED library with the correct LED type, data pin, and color order
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
  // Set the brightness level of the LEDs
  FastLED.setBrightness(BRIGHTNESS);
  // Turn off all LEDs by setting them to black
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  // Update the physical LED strip with the current values
  FastLED.show();
}

/**
 * Main loop function that runs repeatedly
 */
void loop() {
  // Light each LED one by one with cycling colors
  for (uint8_t y = 0; y < N_Y; y++) {
    for (uint8_t x = 0; x < N_X; x++) {
      // Print x and y as integers since Serial.println() doesn't handle uint8_t correctly
      Serial.print("X: "); Serial.print((int)x); 
      Serial.print(", Y: "); Serial.println((int)y);  // Debug output of current x,y coordinates
      // Convert x,y coordinates to LED index using the horizontal S-shape pattern
      uint8_t i = getUNShapeIndex(x, y);
      // Set the LED to a color based on its position in the matrix
      leds[i] = colors[i % NUM_COLORS];
      // Update the physical LED strip
      FastLED.show();
      // Wait before moving to the next LED
      delay(DELAY_MS);
    }
  }

  // Pause when all LEDs are lit
  delay(1000);

  // Turn off each LED one by one
  for (uint8_t y = 0; y < N_Y; y++) {
    for (uint8_t x = 0; x < N_X; x++) {
      // Convert x,y coordinates to LED index using the horizontal S-shape pattern
      uint8_t i = getUNShapeIndex(x, y);
      // Turn off the LED by setting it to black
      leds[i] = CRGB::Black;
      // Update the physical LED strip
      FastLED.show();
      // Wait before moving to the next LED
      delay(DELAY_MS);
    }
  }
  // Pause before starting the cycle again
  delay(500);
}