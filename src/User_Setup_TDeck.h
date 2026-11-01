// User_Setup_TDeck.h
// TFT_eSPI configuration for the LilyGO T-Deck's 320x240 ST7789 panel.
// Wired in via platformio.ini's build_flags: -DUSER_SETUP_LOADED=1 -include User_Setup_TDeck.h
// This avoids having to edit anything inside the TFT_eSPI library itself.

#define ST7789_DRIVER

#define TFT_WIDTH  240
#define TFT_HEIGHT 320

// Pins from tdeck_pins.h (kept as plain numbers here since TFT_eSPI's
// User_Setup convention wants raw macros, evaluated before tdeck_pins.h's
// #pragma once header would normally be pulled in by other translation
// units - duplicated here intentionally, keep in sync with tdeck_pins.h).
#define TFT_MOSI   41
#define TFT_MISO   38
#define TFT_SCLK   40
#define TFT_CS     12
#define TFT_DC     11
#define TFT_RST    -1   // tied to system reset on T-Deck, no dedicated pin
#define TFT_BL     42
#define TFT_BACKLIGHT_ON HIGH

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4

#define SPI_FREQUENCY       40000000
#define SPI_READ_FREQUENCY  16000000

// We manage the shared SPI bus ourselves (TFT + SD both on it) via
// SPI.begin() in main.cpp before TFT_eSPI's own init() runs; this is fine -
// re-beginning the same pins is harmless on arduino-esp32.
