# Pixel Campfire

A realistic fire animation for Arduino Pro Micro with 16x16 LED matrix using Perlin noise.

## Overview

This project creates a mesmerizing, realistic fire effect on a 16x16 LED matrix using an Arduino Pro Micro. The animation uses Perlin noise to generate natural, flowing patterns that mimic the complex movement of flames. The algorithm provides a smooth, organic animation without repeated patterns or jarring transitions.

## Hardware Requirements

- Arduino Pro Micro (or compatible board)
- 16x16 LED Matrix using WS2812B LEDs (256 LEDs total)
- 5V power supply (capable of providing enough current for all LEDs at full brightness)
- Connecting wires
- Momentary push button (for palette switching)

## Wiring

- Connect the LED data input to pin 6 on the Arduino Pro Micro
- Connect power (5V) and ground to the LED matrix power inputs
- Connect the push button between pin 9 and GND
- Ensure proper current capacity for the power supply (each LED can draw up to 60mA at full brightness)

## Features

- Realistic fire animation using Perlin noise algorithm
- Adjustable parameters for customization
- Multiple color palettes (orange/red fire, electric green, electric blue)
- Interactive palette switching via button press
- Serpentine LED layout support
- Optimized noise calculation for smoother animation
- Power management to prevent current spikes during color changes

## Configuration Options

The code includes several parameters you can adjust to customize the animation:

- `BRIGHTNESS`: Overall brightness (0-255)
- `SCALE_XY`: Scale for noise (1-100) - affects the size of the flame patterns
- `SPEED_Y`: Vertical movement speed (1-6) - controls how quickly the flames rise
- `DEBOUNCE_DELAY`: Milliseconds for button debounce handling
- `FRAME_DELAY`: Milliseconds between frames (controls animation speed)

## Code Structure

- **Perlin Noise Implementation**: Uses FastLED's `inoise16()` function with optimized calculations
- **Flame Generation**: Creates vertical fade-out effect with subtle variations
- **Color Palettes**: Three predefined color schemes for different fire effects
- **Matrix Mapping**: Supports serpentine LED layouts common in matrix displays
- **Button Handling**: Debounced input for palette switching

## Optimization

The code includes an optimized Perlin noise calculation system that:
- Caches recent noise values to reduce computational load
- Uses a simple hashing function to identify and reuse calculations
- Maintains consistent noise values across frames for smoother animation
- Power limiting to prevent Arduino resets during palette changes

## Installation

1. Install the FastLED library through the Arduino Library Manager
2. Copy the sketch to your Arduino IDE
3. Select the appropriate board (Arduino Pro Micro)
4. Upload the sketch to your Arduino

## Customization

Feel free to modify the color palettes, animation speeds, or noise parameters to create your own unique fire effects. The code is well-commented to help you understand each parameter's function.

## Changelog

### Version 1.1.0 (Current)
- Added push button support on pin 9 for changing color palettes
- Implemented palette switching between fire (red/orange), green, and blue effects
- Added power management to prevent current spikes during color changes
- Optimized memory usage with pre-initialized palettes
- Improved button debouncing for reliable operation

### Version 1.0.0 (Original)
- Initial implementation with Perlin noise-based fire effect
- Fixed palette selection via code constant
- Optimized noise calculation with caching
- Serpentine LED layout support

## Credits

This code is modified from the original fire effect implementation by Yaroslaw Turbin:
https://www.reddit.com/r/FastLED/comments/hgu16i/my_fire_effect_implementation_based_on_perlin/

## License

This project is open source and available for personal and educational use.
