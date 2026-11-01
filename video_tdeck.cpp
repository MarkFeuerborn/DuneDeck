/**
 * video_tdeck.cpp - T-Deck video backend for OpenDUNE.
 *
 * Implements src/video/video.h. The engine renders into an 8bpp indexed
 * 320x200 framebuffer (Screen 0) that GFX_Init() gets from us via
 * Video_GetFrameBuffer(). Every place the engine draws to Screen 0 it also
 * records a bounding dirty rectangle (src/gfx.c, src/gui/gui.c, src/wsa.c),
 * which we read back here and use to push only the changed region over SPI
 * instead of the whole 320x200 frame every tick.
 *
 * IMPORTANT, verified from the reference SDL2 driver (video_sdl2.c):
 * the palette bytes handed to Video_SetPalette() are 6-bit VGA DAC values
 * (0-63), NOT 0-255 RGB. Each component must be multiplied by 0x41 (65) to
 * expand it to 8-bit range before it's usable as RGB565.
 */

#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <esp_heap_caps.h>

extern "C" {
#include "../lib/opendune/src/types.h"
#include "../lib/opendune/src/gfx.h"
#include "../lib/opendune/src/video/video.h"
#include "../lib/opendune/src/input/mouse.h"
}

#include "tdeck_pins.h"
#include "tdeck_config.h"

static TFT_eSPI s_tft = TFT_eSPI();

// Screen 0 framebuffer that the engine renders into. Sized like the
// reference drivers (SCREEN_WIDTH * (SCREEN_HEIGHT + 4): a few engine code
// paths touch a handful of rows past the visible area during scroll/blit
// edge cases, so we keep the same safety margin the SDL2 driver uses.
static uint8_t *s_framebuffer = NULL;
static const int32_t FRAMEBUFFER_SIZE = SCREEN_WIDTH * (SCREEN_HEIGHT + 4);

// Palette LUT: index (0-255) -> RGB565, pre-swapped to TFT_eSPI's expected
// byte order for pushPixels().
static uint16_t s_palette565[256];

// One scratch scanline, converted from indexed to RGB565 before each
// pushImage() call.
static uint16_t s_lineBuf[SCREEN_WIDTH];

// Where on the physical 320x240 panel we place the 320x200 game canvas.
// Centered vertically, leaving a 20px strip top and bottom free (unused for
// now; could host a status bar later).
static const int16_t PANEL_WIDTH  = 320;
static const int16_t PANEL_HEIGHT = 240;
static const int16_t OFFSET_X = 0;
static const int16_t OFFSET_Y = (PANEL_HEIGHT - SCREEN_HEIGHT) / 2;

static uint16_t s_videoOffset = 0; // stored, not currently applied (see note in Video_SetOffset)

// Mouse clamp region, mirrored from Video_Mouse_SetRegion(). Our input
// drivers (trackball/touch) read these to keep the cursor in bounds before
// calling Mouse_EventHandler().
uint16_t g_tdeckMouseMinX = 0, g_tdeckMouseMaxX = SCREEN_WIDTH - 1;
uint16_t g_tdeckMouseMinY = 0, g_tdeckMouseMaxY = SCREEN_HEIGHT - 1;
uint16_t g_tdeckMouseX = SCREEN_WIDTH / 2, g_tdeckMouseY = SCREEN_HEIGHT / 2;

extern "C" bool Video_Init(int screen_magnification, VideoScaleFilter filter)
{
	(void)screen_magnification; /* We always run at native 320x200 -> centered on the 320x240 panel; no integer scaling. */
	(void)filter;               /* No scale2x/hqx on this target - not worth the RAM/CPU here. */

	pinMode(TDECK_TFT_BACKLIGHT, OUTPUT);
	digitalWrite(TDECK_TFT_BACKLIGHT, LOW);

	s_tft.init();
	s_tft.setRotation(TDECK_TFT_ROTATION); // see tdeck_config.h if the image is upside-down
#if TDECK_TFT_SWAP_BYTES
	s_tft.setSwapBytes(true);
#endif
	s_tft.fillScreen(TFT_BLACK);
	digitalWrite(TDECK_TFT_BACKLIGHT, HIGH);

	s_framebuffer = (uint8_t *)heap_caps_malloc(FRAMEBUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
	if (s_framebuffer == NULL) {
		/* Fall back to internal RAM if PSRAM isn't available/initialised
		 * for some reason - 64000 bytes is still affordable there. */
		s_framebuffer = (uint8_t *)malloc(FRAMEBUFFER_SIZE);
	}
	if (s_framebuffer == NULL) return false;
	memset(s_framebuffer, 0, FRAMEBUFFER_SIZE);

	memset(s_palette565, 0, sizeof(s_palette565));

	return true;
}

extern "C" void Video_Uninit(void)
{
	if (s_framebuffer != NULL) {
		heap_caps_free(s_framebuffer);
		s_framebuffer = NULL;
	}
	digitalWrite(TDECK_TFT_BACKLIGHT, LOW);
}

extern "C" void Video_SetPalette(void *palette, int from, int length)
{
	const uint8_t *p = (const uint8_t *)palette;
	int i;

	for (i = 0; i < length; i++) {
		const int idx = from + i;
		const uint8_t r6 = p[i * 3 + 0] & 0x3F;
		const uint8_t g6 = p[i * 3 + 1] & 0x3F;
		const uint8_t b6 = p[i * 3 + 2] & 0x3F;

		/* Expand 6-bit VGA DAC value to 8-bit. This is the exact formula
		 * used by the reference video_sdl2.c driver: (v6*0x41)>>4, which
		 * for v6 in [0,63] maps to r8 in [0,255] (63*0x41=4095, >>4=255). */
		const uint8_t r8 = (uint8_t)((r6 * 0x41) >> 4);
		const uint8_t g8 = (uint8_t)((g6 * 0x41) >> 4);
		const uint8_t b8 = (uint8_t)((b6 * 0x41) >> 4);

		if (idx >= 0 && idx < 256) {
#if TDECK_TFT_SWAP_RB
			s_palette565[idx] = s_tft.color565(b8, g8, r8);
#else
			s_palette565[idx] = s_tft.color565(r8, g8, b8);
#endif
		}
	}
}

extern "C" void Video_Mouse_SetPosition(uint16 x, uint16 y)
{
	g_tdeckMouseX = x;
	g_tdeckMouseY = y;
}

extern "C" void Video_Mouse_SetRegion(uint16 minX, uint16 maxX, uint16 minY, uint16 maxY)
{
	g_tdeckMouseMinX = minX;
	g_tdeckMouseMaxX = maxX;
	g_tdeckMouseMinY = minY;
	g_tdeckMouseMaxY = maxY;
}

extern "C" void Video_SetOffset(uint16 offset)
{
	/* Legacy VGA bank-switch hook from the DOS/mode-X days. None of the
	 * modern desktop backends (SDL/SDL2/Win32) actually act on this either
	 * - it's stored and otherwise unused - so we do the same here. */
	s_videoOffset = offset;
}

extern "C" void *Video_GetFrameBuffer(uint16 size)
{
	(void)size; /* Always SCREEN_WIDTH*SCREEN_HEIGHT for SCREEN_0; we sized generously above. */
	return s_framebuffer;
}

extern "C" void Video_Tick(void)
{
	if (s_framebuffer == NULL) return;

	if (!GFX_Screen_IsDirty(SCREEN_0)) return;

	struct dirty_area *area = GFX_Screen_GetDirtyArea(SCREEN_0);
	int16_t left, top, right, bottom;

#if TDECK_VIDEO_ALWAYS_FULL_REDRAW
	(void)area;
	left = 0; top = 0; right = SCREEN_WIDTH; bottom = SCREEN_HEIGHT;
#else
	if (area != NULL) {
		left   = area->left;
		top    = area->top;
		right  = area->right;
		bottom = area->bottom;
	} else {
		left = 0; top = 0; right = SCREEN_WIDTH; bottom = SCREEN_HEIGHT;
	}
#endif

	if (left < 0) left = 0;
	if (top < 0) top = 0;
	if (right > SCREEN_WIDTH) right = SCREEN_WIDTH;
	if (bottom > SCREEN_HEIGHT) bottom = SCREEN_HEIGHT;
	if (left >= right || top >= bottom) {
		GFX_Screen_SetClean(SCREEN_0);
		return;
	}

	const int16_t w = right - left;
	const int16_t h = bottom - top;

	s_tft.startWrite();
	s_tft.setAddrWindow(OFFSET_X + left, OFFSET_Y + top, w, h);
	for (int16_t y = 0; y < h; y++) {
		const uint8_t *srcRow = s_framebuffer + (size_t)(top + y) * SCREEN_WIDTH + left;
		for (int16_t x = 0; x < w; x++) {
			s_lineBuf[x] = s_palette565[srcRow[x]];
		}
		s_tft.pushPixels(s_lineBuf, w);
	}
	s_tft.endWrite();

	GFX_Screen_SetClean(SCREEN_0);
}
