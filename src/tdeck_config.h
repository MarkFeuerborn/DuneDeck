#pragma once
/**
 * Compile-time knobs for the OpenDUNE T-Deck port.
 * Most of these can also be overridden from platformio.ini via -D flags.
 */

// Set to 1 only on a T-Deck PLUS (GT911 capacitive touch panel present).
// The standard T-Deck has NO touchscreen - only trackball + keyboard - and
// enabling this on a standard T-Deck will simply find no device at the GT911
// I2C address and silently do nothing (harmless, but wastes a bit of boot
// time probing for it).
#ifndef TDECK_HAS_TOUCH
#define TDECK_HAS_TOUCH 0
#endif

// SPI clock for the ST7789 panel. 40 MHz is a safe, widely-used value for
// T-Deck community projects; some go to 80 MHz successfully.
#ifndef TDECK_TFT_SPI_HZ
#define TDECK_TFT_SPI_HZ 40000000
#endif

// SPI clock for the SD card. 20 MHz is conservative/reliable; many cards
// tolerate 40 MHz too.
#ifndef TDECK_SD_SPI_HZ
#define TDECK_SD_SPI_HZ 20000000
#endif

// I2S sample rate used for sound-effect playback (see dsp_tdeck.c). Dune II's
// original VOC clips are natively ~6-11kHz; we resample up to this rate.
#ifndef TDECK_AUDIO_SAMPLE_RATE
#define TDECK_AUDIO_SAMPLE_RATE 22050   // was 16000; raised for the music synth's sake
#endif

// Music: 1 = play Dune II's score through the built-in software synthesizer
// (src/tdeck_synth.h, fed by midi_tdeck.cpp). 0 = silent music, exactly as
// before (sound effects and voices are unaffected either way).
#ifndef TDECK_ENABLE_MUSIC
#define TDECK_ENABLE_MUSIC 1
#endif

// Music loudness relative to the voice/sound-effect channel, 0..100+.
// (Values above ~100 can clip on dense passages; the mixer soft-clips.)
#ifndef TDECK_MUSIC_VOLUME
#define TDECK_MUSIC_VOLUME 70
#endif

// Screen rotation, passed to tft.setRotation(). LilyGO's own official T-Deck
// example code uses 1 (landscape, 320x240 active area on the native
// 240x320 ST7789 panel). If the image appears upside-down, try 3 instead
// (the other landscape option) - this depends on which way your unit's
// flex cable/panel revision is mounted.
#ifndef TDECK_TFT_ROTATION
#define TDECK_TFT_ROTATION 1
#endif

// If 1, Video_Tick() redraws the ENTIRE screen every time anything is
// dirty, instead of only the specific sub-rectangle the engine reports
// as changed. This trades away a dirty-rect optimization for
// correctness: some scene transitions apparently don't mark the full
// screen dirty even though the old content needs fully replacing (seen
// as stale pixels from a previous scene remaining visible under new
// content). Full-screen 320x200 redraws are still comfortably fast
// enough for this game at the configured SPI clock - if you want to
// restore the partial-redraw optimization later once this is confirmed
// fixed some other way, set this to 0.
#ifndef TDECK_VIDEO_ALWAYS_FULL_REDRAW
#define TDECK_VIDEO_ALWAYS_FULL_REDRAW 1
#endif

// Color correction for pushPixels(). Both of these are common,
// well-documented TFT_eSPI pitfalls when feeding it manually-computed
// RGB565 values (as opposed to using its own drawing primitives, which
// handle this internally): panels wired as BGR instead of RGB report
// reds as blues and vice versa, and pushPixels() specifically needs an
// explicit endianness hint that other TFT_eSPI calls don't. Defaults
// below are a best-guess starting point based on a real reported symptom
// (desert oranges/tans rendering as blues/purples/greens strongly
// suggests a red/blue channel swap) - if colors are still wrong, try
// flipping TDECK_TFT_SWAP_RB first, then TDECK_TFT_SWAP_BYTES, and
// report back which combination (if any) fixes it.
#ifndef TDECK_TFT_SWAP_RB
#define TDECK_TFT_SWAP_RB 1
#endif
#ifndef TDECK_TFT_SWAP_BYTES
#define TDECK_TFT_SWAP_BYTES 1
#endif

// Trackball sensitivity: logical pixels moved per raw pulse. Was 1, then
// 2; raised again to 4 on request for faster cursor travel.
#ifndef TDECK_TRACKBALL_STEP
#define TDECK_TRACKBALL_STEP 4
#endif

// IJKL keyboard-navigation step size: logical pixels moved per keypress.
// Larger than the trackball step since each key event is a single
// discrete "nudge" (no hold/repeat - see input_tdeck.cpp's file header
// comment on keyboard limitations), not continuous motion.
#ifndef TDECK_IJKL_STEP
#define TDECK_IJKL_STEP 8
#endif

// Root of the game's data files on the SD card. Must match file.c's
// DUNE_DATA_DIR for the TDECK branch.
#define TDECK_DATA_DIR   "/sdcard/dune2"
#define TDECK_SAVE_DIR   "/sdcard/dune2/save"
