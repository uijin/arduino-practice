// LED Matrix Effects for Arduino Pro Micro with 16x16 LED matrix
// Including: Fire effects, Matrix Digital Rain, and Shiny Sun Animation
// Supports multiple color options for fire effects

#include <FastLED.h>

// Hardware configuration
#define LED_PIN     3      // Digital pin connected to the LED matrix
#define BUTTON_PIN  2      // Digital pin for color change button
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

// Matrix Rain Parameters
#define NUM_STREAMS 10       // Number of rain streams
#define MIN_SPEED 1          // Minimum stream speed
#define MAX_SPEED 5          // Maximum stream speed
#define TRAIL_LENGTH 5       // Length of the fading trail
#define SPAWN_RATE 40        // Chance of spawning a new drop (higher = less frequent)
#define MATRIX_GREEN_HUE 100 // Base hue for classic Matrix green
#define MATRIX_BLUE_HUE 160  // Base hue for blue Matrix
#define MATRIX_RED_HUE 0     // Base hue for red Matrix
#define HUE_RANGE 30         // How much the hue can vary

// Sun Animation Parameters
#define SUN_CENTER_X 7.5     // X center of the sun (use 7.5 for center of 16x16 matrix)
#define SUN_CENTER_Y 7.5     // Y center of the sun (use 7.5 for center of 16x16 matrix)
#define SUN_CORE_SIZE 3.0    // Size of the sun's core
#define NUM_RAYS 8           // Number of sun rays
#define RAY_LENGTH 8.0       // Maximum length of rays
#define RAY_WIDTH 1.5        // Width of rays
#define RAY_SPEED 10         // Speed of ray animation (higher = faster)
#define SHIMMER_AMOUNT 30    // Amount of shimmer (0-255)
#define CORE_PULSE_SPEED 8   // Speed of core pulsing
#define RAY_HUE 32           // Base hue for rays (32 = yellow-orange)
#define CORE_HUE 30          // Base hue for core (30 = more yellow)

// LED array
CRGB leds[NUM_LEDS];

// Color palette variables
uint8_t paletteIndex = 2;  // Start with blue (0=fire, 1=green, 2=blue, 3,4,5=matrix colors, 6=sun)
#define NUM_PALETTES 7     // Total number of palettes available

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

// Rain stream structure to track falling drops
struct RainStream {
  int x;           // X position
  int y;           // Y position
  int speed;       // How fast it falls (update every N frames)
  int countdown;   // Frames until next movement
  boolean active;  // Whether this stream is currently active
  uint8_t hue;     // Color hue variation
  uint8_t bright;  // Brightness of the lead drop
};

RainStream streams[NUM_STREAMS];

// Sun animation timing variables
uint8_t rayPhase = 0;
uint8_t shimmerPhase = 0;
uint32_t lastSunUpdate = 0;

// Pre-initialize palettes
CRGBPalette16 firePalette;
CRGBPalette16 greenPalette;
CRGBPalette16 bluePalette;

// Animation cycle variables with prime number increments
uint32_t x_offset = 0;
uint32_t y_offset = 0;
uint32_t z_offset = 0;

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

// Initialize all rain streams with proper colors based on current palette
void initializeStreams() {
  for (int i = 0; i < NUM_STREAMS; i++) {
    resetStream(i, true);
  }
}

// Initialize or reset a rain stream
void resetStream(int index, boolean firstRun) {
  streams[index].x = random(WIDTH);

  if (firstRun) {
    // Distribute across the screen for initial setup
    streams[index].y = random(HEIGHT);
  } else {
    // Start new streams at the top
    streams[index].y = 0;

    // Sometimes don't activate right away to avoid too many streams at once
    streams[index].active = (random(100) < 70);  // 70% chance of activation
  }

  // Set random speed (update every N frames)
  streams[index].speed = random(MIN_SPEED, MAX_SPEED + 1);
  streams[index].countdown = streams[index].speed;

  // Set color based on the current palette index
  uint8_t baseHue;
  switch (paletteIndex) {
    case 3: // Green Matrix
      baseHue = MATRIX_GREEN_HUE;
      break;
    case 4: // Blue Matrix
      baseHue = MATRIX_BLUE_HUE;
      break;
    case 5: // Red Matrix
      baseHue = MATRIX_RED_HUE;
      break;
    default:
      baseHue = MATRIX_GREEN_HUE;
  }

  // Add slight variation to the base hue
  streams[index].hue = baseHue + random(-HUE_RANGE/2, HUE_RANGE/2);

  // Lead character is brightest
  streams[index].bright = 255;
}

// Update stream position
void updateStream(int index) {
  // Only process active streams
  if (!streams[index].active) {
    // Occasionally activate inactive streams
    if (random(100) < 5) {  // 5% chance each frame
      streams[index].active = true;
    }
    return;
  }

  // Count down to next movement
  streams[index].countdown--;

  // Time to update position
  if (streams[index].countdown <= 0) {
    // Reset countdown
    streams[index].countdown = streams[index].speed;

    // Move down one row
    streams[index].y++;

    // Reset if we've gone off the bottom
    if (streams[index].y >= HEIGHT) {
      resetStream(index, false);
    }
  }
}

// Draw the stream on the matrix
void drawStream(int index) {
  // Only draw active streams
  if (!streams[index].active) {
    return;
  }

  int x = streams[index].x;
  int y = streams[index].y;

  // Draw the lead dot (brightest)
  if (y >= 0 && y < HEIGHT) {
    leds[XY(x, y)] = CHSV(streams[index].hue, 255, streams[index].bright);
  }

  // Draw the trailing dots (fading)
  for (int i = 1; i <= TRAIL_LENGTH; i++) {
    if (y - i >= 0 && y - i < HEIGHT) {
      // Calculate fading brightness for trail
      uint8_t trailBrightness = streams[index].bright * (TRAIL_LENGTH - i + 1) / (TRAIL_LENGTH + 2);

      // Set the LED with fading brightness and slightly shifted hue
      leds[XY(x, y - i)] += CHSV(streams[index].hue + i * 2, 255, trailBrightness);
    }
  }

  // Randomly change streams occasionally
  if (random(100) < 2) {  // 2% chance each frame
    // Adjust color, brightness or create branching effect
    if (random(100) < 40) {  // 40% of those changes
      // Create a "branch" by starting a new stream nearby if one is available
      for (int i = 0; i < NUM_STREAMS; i++) {
        if (!streams[i].active) {
          streams[i].active = true;
          streams[i].x = constrain(x + random(-1, 2), 0, WIDTH - 1);
          streams[i].y = y - random(1, 3);
          streams[i].speed = streams[index].speed + random(-1, 2);
          streams[i].countdown = streams[i].speed;
          streams[i].hue = streams[index].hue + random(-10, 11);
          streams[i].bright = 220;
          break;
        }
      }
    }
  }

  // Occasionally spawn a new stream
  if (random(100) < 100/SPAWN_RATE) {
    for (int i = 0; i < NUM_STREAMS; i++) {
      if (!streams[i].active) {
        resetStream(i, false);
        streams[i].active = true;
        break;
      }
    }
  }
}

// Draw the sun's core with pulsing effect
void drawSunCore() {
  // Calculate core radius with pulsing effect
  float coreRadius = SUN_CORE_SIZE + sin(radians(rayPhase * CORE_PULSE_SPEED)) * 0.5;

  // Calculate core brightness variation
  uint8_t coreBrightness = 255 - SHIMMER_AMOUNT + (SHIMMER_AMOUNT * sin(radians(shimmerPhase * 2)));

  // Draw the core as a filled circle
  for (int y = 0; y < HEIGHT; y++) {
    for (int x = 0; x < WIDTH; x++) {
      // Calculate distance from center
      float distance = sqrt(pow(x - SUN_CENTER_X, 2) + pow(y - SUN_CENTER_Y, 2));

      // If inside core radius
      if (distance <= coreRadius) {
        // Brightest in the center, dimming toward edges with smooth falloff
        float brightness = 255 * (1.0 - pow(distance / coreRadius, 2));

        // Add shimmer effect
        brightness = constrain(brightness + random(-10, 11), 0, 255);

        // Apply core brightness variation
        brightness = brightness * coreBrightness / 255;

        // Determine color - yellowish but shifting toward white in center
        uint8_t sat = map(distance, 0, coreRadius, 130, 240);  // Less saturation in center (more white)

        // Set pixel color
        leds[XY(x, y)] = CHSV(CORE_HUE, sat, brightness);
      }
    }
  }
}

// Draw sun's rays
void drawSunRays() {
  // Animation phase for ray pulsing
  float rayPulse = sin(radians(rayPhase * RAY_SPEED)) * 0.3 + 0.7;  // Range 0.4 to 1.0

  // Draw each ray
  for (int i = 0; i < NUM_RAYS; i++) {
    // Calculate angle for this ray
    float angle = (i * 360.0 / NUM_RAYS) + sin(radians(rayPhase)) * 5.0;  // Slight rotation effect

    // Calculate current ray length (different rays pulse differently)
    float rayLengthModifier = 0.7 + 0.3 * sin(radians(rayPhase * RAY_SPEED + i * 45));
    float currentRayLength = RAY_LENGTH * rayLengthModifier * rayPulse;

    // Draw this ray
    drawRay(angle, currentRayLength);
  }
}

// Draw a single ray at the specified angle and length
void drawRay(float angle, float length) {
  // Start from outside the core
  float startDistance = SUN_CORE_SIZE;
  float endDistance = SUN_CORE_SIZE + length;

  // Draw ray as a series of points
  for (float dist = startDistance; dist <= endDistance; dist += 0.3) {
    // Make rays taper at the ends
    float relativePos = (dist - startDistance) / (endDistance - startDistance);
    float width = RAY_WIDTH * (1.0 - pow(relativePos - 0.5, 2) * 2.0);

    // Draw a circle at this position along the ray to create width
    for (float w = -width; w <= width; w += 0.5) {
      // Calculate perpendicular angle for width
      float perpAngle = angle + 90;

      // Calculate coordinates
      float rayX = SUN_CENTER_X + cos(radians(angle)) * dist + cos(radians(perpAngle)) * w;
      float rayY = SUN_CENTER_Y + sin(radians(angle)) * dist + sin(radians(perpAngle)) * w;

      // Check if within bounds
      if (rayX >= 0 && rayX < WIDTH && rayY >= 0 && rayY < HEIGHT) {
        int x = int(rayX);
        int y = int(rayY);

        // Brightness falls off along length and width
        float brightness = 255 * (1.0 - relativePos) * (1.0 - pow(abs(w) / width, 2) * 0.8);

        // Add shimmer effect
        float shimmerEffect = SHIMMER_AMOUNT * sin(radians(shimmerPhase + dist * 20 + angle));
        brightness = constrain(brightness + shimmerEffect, 0, 255);

        // Color shifts slightly from center to tip
        uint8_t hue = RAY_HUE + int(relativePos * 15);

        // Set pixel, blending with existing value for smoother appearance
        leds[XY(x, y)] += CHSV(hue, 180 - relativePos * 70, brightness);
      }
    }
  }
}

// Draws the sun with core and rays
void drawSun() {
  // Update animation phases
  uint32_t currentMillis = millis();
  if (currentMillis - lastSunUpdate > 50) {  // 20 fps update rate
    lastSunUpdate = currentMillis;
    rayPhase += 3;          // Controls ray pulsation
    shimmerPhase += 7;      // Controls shimmer effect
  }

  // Draw the sun's rays first (so core draws on top)
  drawSunRays();

  // Draw the sun's core
  drawSunCore();
}

void setup() {
    // Initialize serial for debugging
    Serial.begin(9600);

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

    // Initialize all rain streams
    initializeStreams();

    // Initialize Sun animation variables
    lastSunUpdate = millis();
    rayPhase = 0;
    shimmerPhase = 0;

    // Seed the random number generator
    randomSeed(analogRead(0));

    Serial.println("Setup complete");
}

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
                // Store previous palette for transition
                uint8_t previousPalette = paletteIndex;

                // Change to the next palette
                paletteIndex = (paletteIndex + 1) % NUM_PALETTES;

                // Initialize matrix streams if switching to a matrix effect
                if (paletteIndex >= 3 && paletteIndex <= 5) {
                    initializeStreams();
                }

                Serial.print("Changed to effect: ");
                Serial.println(paletteIndex);
            }
        }
    }

    // Save the button state for the next loop
    lastButtonState = reading;
}

void drawFireEffect() {
    // Get the current palette
    CRGBPalette16& myPal = getPalette();

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
}

void drawMatrixRain() {
    // Clear the display (but don't show yet)
    fadeToBlackBy(leds, NUM_LEDS, 40);  // Fade existing LEDs for trail effect

    // Update and draw each rain stream
    for (int i = 0; i < NUM_STREAMS; i++) {
        updateStream(i);
        drawStream(i);
    }
}

void loop() {
    // Reset random seed to avoid pattern issues
    randomSeed(millis());

    // Check if the button was pressed to change the color palette
    checkButtonAndChangePalette();

    // Set the brightness for this frame
    FastLED.setBrightness(BRIGHTNESS);

    // Handle display clearing based on effect type
    if (paletteIndex >= 3 && paletteIndex <= 5) { // Matrix effects have their own fade-to-black
        // Don't clear for Matrix effect
    } else {
        // Clear for fire and sun effects
        FastLED.clear();
    }

    // Draw the selected effect
    switch (paletteIndex) {
        case 0:
        case 1:
        case 2:
            // Fire effect with different color palettes
            drawFireEffect();
            break;
        case 3:
        case 4:
        case 5:
            // Matrix Digital Rain effect with different colors (green, blue, red)
            drawMatrixRain();
            break;
        case 6:
            // Sun animation effect
            drawSun();
            break;
        default:
            // Failsafe - reset to a known good state
            paletteIndex = 0;
            Serial.println("Reset to fire effect");
            break;
    }

    FastLED.show();

    // Delay for animation speed control
    delay(FRAME_DELAY);
}
