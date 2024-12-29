#include <FastLED.h>

#define LED_PIN     6
#define NUM_LEDS    64
#define BRIGHTNESS  5
#define DELAY_MS    100

CRGB leds[NUM_LEDS];

// Array of different colors to cycle through
CRGB colors[] = {
  CRGB::Red,
  CRGB::Green,
  CRGB::Blue,
  CRGB::Yellow,
  CRGB::Purple,
  CRGB::Orange,
  CRGB::Amethyst,
};
#define NUM_COLORS (sizeof(colors) / sizeof(colors[0]))

void setup() {
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();
}

void loop() {
  // Light each LED with cycling colors
  for(int i = 0; i < NUM_LEDS; i++) {
    leds[i] = colors[i % NUM_COLORS];
    FastLED.show();
    delay(DELAY_MS);
  }
  
  delay(1000);  // Pause when all LEDs are lit
  
  // Turn off each LED one by one
  for(int i = 0; i < NUM_LEDS; i++) {
    leds[i] = CRGB::Black;
    FastLED.show();
    delay(DELAY_MS);
  }
  
  delay(500);  // Pause before starting again
}