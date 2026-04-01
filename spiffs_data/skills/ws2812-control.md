# WS2812 LED Control

Control WS2812 RGB LED strip with individual pixel control.

## When to use
When user asks to:
- Control RGB LED colors and effects
- Set specific LED pixels to different colors
- Create custom lighting patterns
- Turn on/off LED strip
- Display status indicators with colors

## How to use
1. **Initialize the strip first**: Always call ws2812_init before using other functions
   - Default: GPIO 46, LED count 3 (use these unless specified otherwise)
   - Specify GPIO pin and LED count if different from default
   - Example: ws2812_init {"gpio": 46, "led_count": 3}

2. **Set all LEDs to same color**: use ws2812_set_all
   - RGB values range from 0-255
   - Example: ws2812_set_all {"r": 255, "g": 0, "b": 0} (red)

3. **Set individual LED pixel**: use ws2812_set_pixel
   - Index is 0-based (first LED is index 0)
   - Example: ws2812_set_pixel {"index": 0, "r": 255, "g": 0, "b": 0}

4. **Clear all LEDs**: use ws2812_clear to turn off all LEDs
   - Example: ws2812_clear {}

5. **Flush buffer**: use ws2812_flush (note: colors are applied immediately in this implementation)
   - Example: ws2812_flush {}

## Common RGB colors
- Red: {"r": 255, "g": 0, "b": 0}
- Green: {"r": 0, "g": 255, "b": 0}
- Blue: {"r": 0, "g": 0, "b": 255}
- White: {"r": 255, "g": 255, "b": 255}
- Yellow: {"r": 255, "g": 255, "b": 0}
- Cyan: {"r": 0, "g": 255, "b": 255}
- Magenta: {"r": 255, "g": 0, "b": 255}
- Black (off): {"r": 0, "g": 0, "b": 0}

## Example
User: "Turn on the LED strip with red color"
→ ws2812_init {"gpio": 46, "led_count": 3}
→ "OK: WS2812 initialized on GPIO 46 with 3 LEDs"
→ ws2812_set_all {"r": 255, "g": 0, "b": 0}
→ "OK: set all pixels to RGB(255, 0, 0)"
→ "LED strip is now red."

User: "Set the first LED to green and the second to blue"
→ ws2812_set_pixel {"index": 0, "r": 0, "g": 255, "b": 0}
→ "OK: set pixel 0 to RGB(0, 255, 0)"
→ ws2812_set_pixel {"index": 1, "r": 0, "g": 0, "b": 255}
→ "OK: set pixel 1 to RGB(0, 0, 255)"
→ "First LED is green, second LED is blue."

User: "Turn off all LEDs"
→ ws2812_clear {}
→ "OK: cleared all LEDs"
→ "All LEDs are now off."
