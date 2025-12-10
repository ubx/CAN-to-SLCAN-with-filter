// Simple WS2812 LED control interface
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize WS2812 (creates and enables RMT TX channel/encoder when RGB_LED_PIN is defined).
void ws2812_init(void);

// Set WS2812 color (R, G, B). No-op when RGB_LED_PIN is not defined.
void ws2812_set_color(uint8_t r, uint8_t g, uint8_t b);

#ifdef __cplusplus
}
#endif
