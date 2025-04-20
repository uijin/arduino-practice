// Perlin noise fire effect for Arduino Pro Micro with 16x16 LED matrix
// Modified from original by Yaroslaw Turbin
// https://www.reddit.com/r/FastLED/comments/hgu16i/my_fire_effect_implementation_based_on_perlin/
// With optimized Perlin noise calculation for smoother animation

#include <FastLED.h>

// Hardware configuration
#define LED_PIN     6      // Digital pin connected to the LED matrix
#define BUTTON_PIN  9      // Digital pin for color change button
#define HEIGHT      16     // Matrix height
#define WIDTH       16     // Matrix width
#define NUM_LEDS    (HEIGHT * WIDTH)
#define SERPENTINE  true   // Set to true if your matrix alternates direction every row
#define BRIGHTNESS  150    // Overall brightness (0-255)

// Animation parameters
#define SCALE_XY     20    // Scale for noise (1-100)
#define SPEED_Y      1     // Vertical movement speed
#define DEBOUNCE_DELAY 100 // Debounce interval in milliseconds
#define X_SPEED      1     // X dimension drift - horizontal variation
#define Y_SPEED_MULT 3     // Y dimension multiplier - vertical flame movement
#define Z_SPEED      2     // Z dimension drift - flame wavering
#define FRAME_DELAY  50    // Milliseconds between frames

// LED array
CRGB leds[NUM_LEDS];

// Color palette variables
uint8_t paletteIndex = 2;  // Start with blue (0=fire, 1=green, 2=blue)
#define NUM_PALETTES 3     // Total number of palettes available

// Button debounce variables
uint8_t lastButtonState = HIGH;    // Last state of the button (assuming pull-up resistor)
uint8_t buttonState = HIGH;        // Current state of the button
uint32_t lastDebounceTime = 0;     // Last time the button state changed

// Define original fire palette (keeping the full intensity for visual quality)
DEFINE_GRADIENT_PALETTE(firepal){
    0,   0,   0,   0,      // black
    32,  255, 0,   0,      // red
    190, 255, 255, 0,      // yellow
    255, 255, 255, 255     // white
};

// Electric green fire palette
DEFINE_GRADIENT_PALETTE(electricGreenFirePal){
    0,   0,   0,   0,      // black
    32,  0,   70,  0,      // dark green
    190, 57,  255, 20,     // electric neon green
    255, 255, 255, 255     // white
};

// Electric blue fire palette
DEFINE_GRADIENT_PALETTE(electricBlueFirePal) {
    0,   0,   0,   0,      // Black
    32,  0,   0,  70,      // Dark blue
    128, 20, 57, 255,      // Electric blue
    255, 255, 255, 255     // White
};

// Pre-initialize palettes
CRGBPalette16 firePalette;
CRGBPalette16 greenPalette;
CRGBPalette16 bluePalette;

// Function to map x,y coordinates to LED index accounting for serpentine layout
uint16_t XY(uint8_t x, uint8_t y) {
  uint16_t i;

  if (y & 0x01) {  // Odd rows run backwards
    if (SERPENTINE == true) {
      uint8_t reverseX = (WIDTH - 1) - x;
      i = (y * WIDTH) + reverseX;
    } else {
      i = (y * WIDTH) + x;
    }
  } else {         // Even rows run forwards
    i = (y * WIDTH) + x;
  }

  if (i >= NUM_LEDS) return 0;
  return i;
}

// Get palette based on selected type
CRGBPalette16& getPalette() {
    switch (paletteIndex) {
    case 0:
        return firePalette;
    case 1:
        return greenPalette;
    case 2:
        return bluePalette;
    default:
        return bluePalette;
    }
}

// Cache to store recent noise calculations for smoother transitions
#define NOISE_CACHE_SIZE 8
uint16_t noiseCache[NOISE_CACHE_SIZE][2];  // Store [hash, value] pairs
uint8_t cacheIndex = 0;

// Simple hash function to identify noise calculation inputs
uint16_t hashNoiseInputs(uint32_t x, uint32_t y, uint32_t z) {
    // Mix the bits of x, y, z to create a simple hash
    return ((x & 0xFF) << 8) | ((y & 0xF0) << 0) | ((z & 0xF0) >> 4);
}

// Optimized Perlin noise calculation with smoothing
uint16_t getOptimizedNoise(uint32_t x, uint32_t y, uint32_t z) {
    uint16_t inputHash = hashNoiseInputs(x, y, z);

    // Check if we already have this calculation in cache
    for (uint8_t i = 0; i < NOISE_CACHE_SIZE; i++) {
        if (noiseCache[i][0] == inputHash) {
            // Found in cache, return the cached value
            return noiseCache[i][1];
        }
    }

    // Not found in cache, calculate new value
    uint16_t noiseValue = inoise16(
        (x * 313) % 65521 << 8,
        (y * 457) % 65521 << 8,
        (z * 631) % 65521 << 8
    );

    // Store in cache, replacing oldest entry
    noiseCache[cacheIndex][0] = inputHash;
    noiseCache[cacheIndex][1] = noiseValue;
    cacheIndex = (cacheIndex + 1) % NOISE_CACHE_SIZE;

    return noiseValue;
}

void setup() {
    // Initialize palettes only once to avoid memory fragmentation
    firePalette = firepal;
    greenPalette = electricGreenFirePal;
    bluePalette = electricBlueFirePal;

    // Initialize FastLED with power limiting
    FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
    FastLED.setCorrection(Typical8mmPixel);
    FastLED.setBrightness(BRIGHTNESS);

    // Power limiting for stability
    FastLED.setMaxPowerInVoltsAndMilliamps(5, 800);

    // Initialize button pin with internal pull-up resistor
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    // Initialize noise cache
    for (uint8_t i = 0; i < NOISE_CACHE_SIZE; i++) {
        noiseCache[i][0] = 0xFFFF;  // Invalid hash
        noiseCache[i][1] = 0;
    }
}

// Animation cycle variables with prime number increments
uint32_t x_offset = 0;
uint32_t y_offset = 0;
uint32_t z_offset = 0;

void checkButtonAndChangePalette() {
    // Read the button state (LOW when pressed since we're using pull-up)
    uint8_t reading = digitalRead(BUTTON_PIN);

    // Check if the button state has changed
    if (reading != lastButtonState) {
        // Reset the debounce timer
        lastDebounceTime = millis();
    }

    // If enough time has passed since the last button state change
    if ((millis() - lastDebounceTime) > DEBOUNCE_DELAY) {
        // If the button was just pressed (button is LOW when pressed)
        if (reading != buttonState) {
            buttonState = reading;

            if (buttonState == LOW) {
                // Change to the next palette with special handling for fire
                paletteIndex = (paletteIndex + 1) % NUM_PALETTES;
            }
        }
    }

    // Save the button state for the next loop
    lastButtonState = reading;
}

void loop() {
    // Check if the button was pressed to change the color palette
    checkButtonAndChangePalette();

    // Get the current palette
    CRGBPalette16& myPal = getPalette();

    // Set the brightness for this frame
    FastLED.setBrightness(BRIGHTNESS);

    // Increment offsets using prime numbers and varying rates
    x_offset = (x_offset + X_SPEED) % 65521;  // Very slow x drift
    y_offset = (y_offset + (SPEED_Y * Y_SPEED_MULT)) % 65521;  // Slowed vertical movement
    z_offset = (z_offset + Z_SPEED) % 65521;  // Slowed z movement

    // Generate the fire effect for each pixel
    for (uint8_t i = 0; i < HEIGHT; i++) {
        for (uint8_t j = 0; j < WIDTH; j++) {
            // Create 3D noise coordinates with multiple prime-based offsets
            uint32_t noise_x = (i * SCALE_XY + x_offset) % 65521;
            uint32_t noise_y = (j * SCALE_XY + y_offset) % 65521;
            uint32_t noise_z = z_offset;

            // Get noise value using optimized noise function
            uint16_t noise16 = getOptimizedNoise(noise_x, noise_y, noise_z);
            uint8_t noise_val = noise16 >> 8;

            // Apply vertical fade-out - increased subtraction for more gradual fade
            int8_t subtraction_factor = abs8(j - (WIDTH - 1)) * 255 / (WIDTH - 1);
            uint8_t palette_index = qsub8(noise_val, subtraction_factor);

            // Get color from palette and set LED
            CRGB color = ColorFromPalette(myPal, palette_index, 255);

            // Map coordinates to LED index (rotated 180 degrees clockwise)
            int index = XY(i, j);
            leds[index] = color;
        }
    }

    FastLED.show();

    // Delay for animation speed control
    delay(FRAME_DELAY);
}
