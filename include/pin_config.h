#pragma once

#include <cstdint>

namespace PinConfig {
constexpr uint8_t DISPLAY_MOSI = 13;
constexpr uint8_t DISPLAY_MISO = 12;
constexpr uint8_t DISPLAY_SCLK = 14;
constexpr uint8_t DISPLAY_CS = 15;
constexpr uint8_t DISPLAY_DC = 2;
constexpr uint8_t DISPLAY_RST = 4;
constexpr uint8_t DISPLAY_LED = 21;

constexpr uint8_t TOUCH_MOSI_PIN = 32;
constexpr uint8_t TOUCH_MISO_PIN = 39;
constexpr uint8_t TOUCH_SCLK_PIN = 25;
constexpr uint8_t TOUCH_CS_PIN = 33;
constexpr uint8_t TOUCH_IRQ_PIN = 36;

constexpr uint16_t SCREEN_WIDTH = 240;
constexpr uint16_t SCREEN_HEIGHT = 320;

// Touch calibration for portrait mode.
// These defaults preserve the current proof-of-concept orientation:
// raw Y -> screen X, raw X -> screen Y, with Y inverted.
// If touches are mirrored or rotated, change the swap/invert flags first.
constexpr bool TOUCH_SWAP_XY = false;
constexpr bool TOUCH_INVERT_X = true;
constexpr bool TOUCH_INVERT_Y = true;

// Raw touch bounds are starter values and almost always need tuning per panel.
// Use Serial touch logs while pressing near the screen edges to refine them.
constexpr uint16_t TOUCH_RAW_MIN_X = 600;
constexpr uint16_t TOUCH_RAW_MAX_X = 3500;
constexpr uint16_t TOUCH_RAW_MIN_Y = 500;
constexpr uint16_t TOUCH_RAW_MAX_Y = 3700;

// Pressure floor used to discard floating/noise samples from XPT2046.
// Keep this conservative so light presses still register.
constexpr uint16_t TOUCH_PRESSURE_MIN = 50;

// Temporary diagnostics for touch electrical debugging.
constexpr bool TOUCH_DEBUG_MODE = false;
constexpr uint16_t TOUCH_DEBUG_INTERVAL_MS = 100;
}  // namespace PinConfig
