#pragma once
/**
 * T-Deck pin map, taken verbatim from LilyGO's official repo:
 *   https://github.com/Xinyuan-LilyGO/T-Deck
 *   examples/LvglArduinoVNC_VGA/TDECK_PINS.h
 *
 * If you're on a T-Deck Plus, these are the same pins; the Plus additionally
 * populates the GT911 touch controller on the same I2C bus / INT line.
 */

// Must be driven HIGH before touching SD/TFT/keyboard/trackball/audio.
#define TDECK_PERI_POWERON      10

// Shared SPI bus (TFT + SD + LoRa)
#define TDECK_SPI_MOSI          41
#define TDECK_SPI_MISO          38
#define TDECK_SPI_SCK           40

#define TDECK_SDCARD_CS         39

#define TDECK_TFT_CS            12
#define TDECK_TFT_DC            11
#define TDECK_TFT_BACKLIGHT     42
// T-Deck ties TFT reset to the system reset line - no dedicated RST pin.
#define TDECK_TFT_RST           -1

#define TDECK_RADIO_CS          9
#define TDECK_RADIO_BUSY        13
#define TDECK_RADIO_RST         17
#define TDECK_RADIO_DIO1        45

// I2C bus (keyboard + touch controller, when present)
#define TDECK_I2C_SDA           18
#define TDECK_I2C_SCL           8

#define TDECK_TOUCH_INT         16   // GT911 - T-Deck Plus only

#define TDECK_KEYBOARD_INT      46
#define TDECK_KEYBOARD_ADDR     0x55

// Trackball: four direction GPIOs (interrupt-capable) + center click button
#define TDECK_TRACKBALL_UP      3
#define TDECK_TRACKBALL_DOWN    15
#define TDECK_TRACKBALL_LEFT    1
#define TDECK_TRACKBALL_RIGHT   2
#define TDECK_TRACKBALL_CLICK   0

// Mic array codec (ES7210) - not used by this port
#define TDECK_ES7210_MCLK       48
#define TDECK_ES7210_LRCK       21
#define TDECK_ES7210_SCK        47
#define TDECK_ES7210_DIN        14

// Speaker I2S output - used by dsp_tdeck.c
#define TDECK_I2S_WS            5
#define TDECK_I2S_BCK           7
#define TDECK_I2S_DOUT          6

#define TDECK_BAT_ADC           4

// GT911 default I2C address (only meaningful if TDECK_HAS_TOUCH is set)
#define TDECK_TOUCH_ADDR        0x5D
