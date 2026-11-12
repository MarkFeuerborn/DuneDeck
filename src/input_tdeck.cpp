/**
 * input_tdeck.cpp - T-Deck input backend for OpenDUNE.
 *
 * Feeds the engine through its existing, platform-agnostic entry points:
 *   - Mouse_EventHandler(x, y, leftDown, rightDown)   for pointer + buttons
 *   - Input_EventHandler(scancode)                    for keyboard
 *
 * Input_EventHandler expects standard PC/XT "scancode set 1" values (with
 * bit 0x80 marking key-up, 0xE0 as the extended-key escape prefix) - this
 * was confirmed by reading input.c: it keeps its own s_keyTranslate[] table
 * that maps those XT scancodes to Dune II's internal key codes, and the
 * reference SDL2 driver's keymap tables translate SDL keycodes to the same
 * XT scancode values before calling Input_EventHandler(). We just need to
 * produce XT scancodes from whatever the T-Deck's own peripherals give us.
 *
 * KNOWN LIMITATION - keyboard: the stock T-Deck keyboard co-processor
 * (read over I2C at address 0x55) reports only a single ASCII byte per
 * keypress, with no separate key-up event and no distinct "Shift" keycode
 * of its own (it resolves shift into the ASCII byte itself, e.g. sending
 * 'A' instead of 'a'+Shift). That means:
 *   - We synthesize a down+up scancode pulse per keypress, since there is
 *     no hardware key-up to report. Dune II's UI is built around discrete
 *     keypress events (menus, hotkeys, save slots) so this works fine; the
 *     one behavioural difference is that holding a key down does not
 *     produce the smooth "repeat" some other platforms get from OS-level
 *     key-repeat - you'll get one action per physical press.
 *   - We map the *letter identity* of the incoming ASCII byte back to its
 *     base US-QWERTY scancode (so 'A' and 'a' both produce the same 'a'
 *     key scancode) since Dune II barely uses shifted symbols outside of
 *     save-file naming, where case doesn't matter to the engine.
 *   - Community keyboard firmware replacements (e.g. rgrizzell's or
 *     hreikin's) report richer modifier/hotkey data; if you're running one
 *     of those and want to wire up real Shift/Ctrl/Alt scancodes, extend
 *     TDeck_HandleKeyByte() below - the raw byte is logged over Serial in
 *     TDECK_DEBUG_INPUT builds to make this easy to reverse-engineer.
 *
 * Arrow keys / F-keys / Enter / Backspace / Esc / Tab / Space are handled
 * as ASCII control codes where the stock firmware sends them as such
 * (\r \b \t 0x1B ' '); if your keyboard firmware instead sends distinct
 * non-ASCII byte codes for arrows, add them to s_specialByteMap below once
 * you've confirmed the exact values for your firmware version (see the
 * TDECK_DEBUG_INPUT note above).
 *
 * KEY MAPPING: I/J/K/L move the cursor (up/left/down/right) via
 * TDeck_HandleNavigationKey(), driving the same shared cursor state the
 * trackball uses - see that function's own comment for why IJKL rather
 * than WASD. Enter = left-click (select) only, Backspace = right-click
 * (cancel/deselect) + its normal scancode too. A/M/R/G/B pass through as
 * ordinary scancodes, reaching Dune II's own native hotkeys directly
 * (Attack/Move/Retreat/Guard/Build). O is remapped to F2 (Dune II's
 * actual native "Options" shortcut, confirmed by tracing the engine's
 * own widgetinfo.c - it was never really 'O'). Enter deliberately does
 * NOT also send its raw scancode (0x1C) anymore - that caused hotkeys to
 * require a second Enter press after selecting a unit; see
 * TDeck_HandleKeyByte for details.
 */

#include <Arduino.h>
#include <Wire.h>
#include <driver/rtc_io.h> // rtc_gpio_deinit() / GPIO_NUM_0, used in TDeck_Input_Init

extern "C" {
#include "../lib/opendune/src/types.h"
#include "../lib/opendune/src/gfx.h"
#include "../lib/opendune/src/input/input.h"
#include "../lib/opendune/src/input/mouse.h"
}

#include "tdeck_pins.h"
#include "tdeck_config.h"

#if TDECK_HAS_TOUCH
#include <TouchDrvGT911.hpp>
#endif

// Set to 1 to Serial-print every raw keyboard byte and trackball pulse -
// handy for confirming your keyboard firmware's special-key byte codes.
#ifndef TDECK_DEBUG_INPUT
#define TDECK_DEBUG_INPUT 0
#endif

extern uint16_t g_tdeckMouseMinX, g_tdeckMouseMaxX;
extern uint16_t g_tdeckMouseMinY, g_tdeckMouseMaxY;
extern uint16_t g_tdeckMouseX, g_tdeckMouseY;

// Every individual call site below already has its own logging, and yet
// none of it has ever appeared in a log showing genuine phantom clicks -
// which shouldn't be possible if any of them were the source. As a
// last-resort, foolproof diagnostic: route EVERY call to
// Mouse_EventHandler through this one traced wrapper instead, tagged
// with which internal function triggered it. This cannot be bypassed by
// any code path, unlike the individual per-site logging.
static void TDeck_TracedMouseEvent(const char *source, uint16_t x, uint16_t y, bool left, bool right)
{
	Serial.printf("[MOUSE-EVENT from %s] x=%u y=%u left=%d right=%d\n", source, x, y, left, right);
	Mouse_EventHandler(x, y, left, right);
}

// ---------------------------------------------------------------------
// Trackball
// ---------------------------------------------------------------------

static volatile int32_t s_tbDeltaX = 0;
static volatile int32_t s_tbDeltaY = 0;

static void IRAM_ATTR tbIsrUp(void)    { s_tbDeltaY -= 1; }
static void IRAM_ATTR tbIsrDown(void)  { s_tbDeltaY += 1; }
static void IRAM_ATTR tbIsrLeft(void)  { s_tbDeltaX -= 1; }
static void IRAM_ATTR tbIsrRight(void) { s_tbDeltaX += 1; }

static bool s_leftDown = false;   // trackball click (or touch-down) => left mouse button
static bool s_rightDown = false;  // no second physical button; reserved for a keyboard-modified click (see TDeck_PollInput)
static bool s_clickPrev = false;

// Synthetic (keyboard-initiated) click state. A key press sends the button
// DOWN immediately and the matching UP on a later poll ~50ms afterwards
// (non-blocking), so the engine sees a press and a release separated by
// real time across separate game-loop iterations, like a real click.
// While one is pending, movement events must keep reporting the button as
// held, otherwise they'd inject a stray release in the middle of it.
static bool s_synthLeft = false;
static bool s_synthRight = false;
static uint32_t s_synthReleaseAt = 0;
#define TDECK_SYNTH_CLICK_HOLD_MS 50

// A finger is on the touchscreen. Kept separate from s_leftDown (the
// trackball button) so the two can never cancel each other out; every path
// that reports button state to the engine ORs this in.
static bool s_touchDown = false;
static uint32_t s_clickDebounceAt = 0;

// ---------------------------------------------------------------------
// Keyboard
// ---------------------------------------------------------------------

// ASCII (letter identity, case-insensitive) -> base US-QWERTY XT scancode.
// 0 entries are handled specially below (control chars) or unmapped.
static uint8_t AsciiToScancode(uint8_t c)
{
	if (c >= 'a' && c <= 'z') c -= 32; /* fold to uppercase for the table below */

	switch (c) {
		case 'Q': return 0x10; case 'W': return 0x11; case 'E': return 0x12;
		case 'R': return 0x13; case 'T': return 0x14; case 'Y': return 0x15;
		case 'U': return 0x16; case 'I': return 0x17; case 'O': return 0x18;
		case 'P': return 0x19;
		case 'A': return 0x1E; case 'S': return 0x1F; case 'D': return 0x20;
		case 'F': return 0x21; case 'G': return 0x22; case 'H': return 0x23;
		case 'J': return 0x24; case 'K': return 0x25; case 'L': return 0x26;
		case 'Z': return 0x2C; case 'X': return 0x2D; case 'C': return 0x2E;
		case 'V': return 0x2F; case 'B': return 0x30; case 'N': return 0x31;
		case 'M': return 0x32;
		case '1': return 0x02; case '2': return 0x03; case '3': return 0x04;
		case '4': return 0x05; case '5': return 0x06; case '6': return 0x07;
		case '7': return 0x08; case '8': return 0x09; case '9': return 0x0A;
		case '0': return 0x0B;
		case '-': case '_': return 0x0C;
		case '=': case '+': return 0x0D;
		case '[': case '{': return 0x1A;
		case ']': case '}': return 0x1B;
		case ';': case ':': return 0x27;
		case '\'': case '"': return 0x28;
		case '`': case '~': return 0x29;
		case '\\': case '|': return 0x2B;
		case ',': case '<': return 0x33;
		case '.': case '>': return 0x34;
		case '/': case '?': return 0x35;
		case ' ': return 0x39;
		case '\t': return 0x0F;
		case '\r': case '\n': return 0x1C;
		case 0x08: case 0x7F: return 0x0E; /* backspace / delete-as-backspace */
		case 0x1B: return 0x01;            /* escape */
		default: return 0x00;
	}
}

// Extend this table if you confirm non-ASCII byte codes from your specific
// keyboard firmware for arrows / Fn combos (see file header comment).
struct SpecialByte { uint8_t raw; uint8_t scancode; };
static const SpecialByte s_specialByteMap[] = {
	/* { 0xB4, 0x4B }, // example: LEFT  - UNCONFIRMED, verify for your firmware */
	/* { 0xB5, 0x48 }, // example: UP    - UNCONFIRMED, verify for your firmware */
	/* { 0xB6, 0x50 }, // example: DOWN  - UNCONFIRMED, verify for your firmware */
	/* { 0xB7, 0x4D }, // example: RIGHT - UNCONFIRMED, verify for your firmware */
};

// IJKL + Enter + Backspace navigation, driving the SAME shared mouse
// cursor state that the trackball uses (g_tdeckMouseX/Y, s_leftDown,
// s_rightDown) via Mouse_EventHandler - not the keyboard scancode path.
// This is deliberate: it's what lets these keys work *simultaneously*
// alongside the trackball rather than fighting over two different input
// systems (see the file header comment on why we don't use the engine's
// own built-in numpad mouse-emulation mode here).
//
// IJKL rather than WASD so that A/M/R/G/B/O stay free to pass through
// as ordinary keypresses to Dune II's own hotkeys (Attack/Move/Retreat/
// Guard/Build/Options) - see TDeck_HandleKeyByte below.
//
// Returns true if the byte was handled as a navigation key (caller
// should not also process it as a plain scancode).
static bool TDeck_HandleNavigationKey(uint8_t raw)
{
	int32_t dx = 0, dy = 0;
	bool isClick = false, clickIsLeft = false;

	switch (raw) {
		case 'i': case 'I': dy = -1; break; /* up */
		case 'k': case 'K': dy = 1; break;  /* down */
		case 'j': case 'J': dx = -1; break; /* left */
		case 'l': case 'L': dx = 1; break;  /* right */
		case '\r': case '\n': case ' ': isClick = true; clickIsLeft = true; break;  /* Enter = select */
		case 0x08: case 0x7F: isClick = true; clickIsLeft = false; break; /* Backspace = cancel/deselect */
		default: return false;
	}

	if (dx != 0 || dy != 0) {
		int32_t nx = (int32_t)g_tdeckMouseX + dx * TDECK_IJKL_STEP;
		int32_t ny = (int32_t)g_tdeckMouseY + dy * TDECK_IJKL_STEP;
		if (nx < g_tdeckMouseMinX) nx = g_tdeckMouseMinX;
		if (nx > g_tdeckMouseMaxX) nx = g_tdeckMouseMaxX;
		if (ny < g_tdeckMouseMinY) ny = g_tdeckMouseMinY;
		if (ny > g_tdeckMouseMaxY) ny = g_tdeckMouseMaxY;
		g_tdeckMouseX = (uint16_t)nx;
		g_tdeckMouseY = (uint16_t)ny;
		TDeck_TracedMouseEvent("IJKL-move", g_tdeckMouseX, g_tdeckMouseY, s_leftDown || s_synthLeft || s_touchDown, s_rightDown || s_synthRight);
	}

	if (isClick) {
		/* Non-blocking synthetic click: DOWN now, UP from TDeck_PollInput()
		 * once TDECK_SYNTH_CLICK_HOLD_MS has elapsed. If one is already in
		 * flight, ignore this press rather than stacking events. (The
		 * actual cause of the old "press Enter twice" bug was a stale
		 * "left button held" state - see TDeck_PollInput.) */
		if (!s_synthLeft && !s_synthRight) {
			Serial.printf("[nav] %s-click at (%u,%u)\n", clickIsLeft ? "left" : "right", g_tdeckMouseX, g_tdeckMouseY);
			if (clickIsLeft) s_synthLeft = true; else s_synthRight = true;
			s_synthReleaseAt = millis() + TDECK_SYNTH_CLICK_HOLD_MS;
			TDeck_TracedMouseEvent(clickIsLeft ? "Enter-click-down" : "Backspace-click-down",
				g_tdeckMouseX, g_tdeckMouseY, s_leftDown || s_synthLeft || s_touchDown, s_rightDown || s_synthRight);
		}
	}

	return true;
}

// ---------------------------------------------------------------------
// Text-entry mode (save-game name, high-score name, copy-protection prompt)
// ---------------------------------------------------------------------
//
// Our normal key bindings (IJKL = cursor, Enter/Space = click, Backspace =
// right-click, O = F2) are wrong while the player is TYPING: letters I/J/K/L
// never reached the text box, Space/Enter could not be typed or confirm the
// entry (the high-score name screen soft-locked the game), and 'O' typed
// nothing. All three of the engine's text prompts go through one function,
// GUI_EditBox() (gui/editbox.c), which sets INPUT_FLAG_NO_TRANSLATE for
// exactly as long as it is open and clears it on exit (the flag is cleared
// at startup in opendune.c, so it is a reliable "typing right now" signal).
// While it's set we bypass every remap and behave like an ordinary keyboard.
static bool TDeck_TextEntryActive(void)
{
	return (g_inputFlags & INPUT_FLAG_NO_TRANSLATE) != 0;
}

// Map a raw ASCII byte from the T-Deck keyboard to an XT scancode plus
// whether Shift must be held. The T-Deck chip resolves Shift itself and
// sends e.g. 'A' or '?', but the engine decides case/symbols from a
// Right-Shift key state (Input_EventHandler: 0x36 -> engine code 0x39),
// so we synthesize Shift around such characters. Returns 0 if unmappable.
static uint8_t TDeck_TextScancode(uint8_t raw, bool *needShift)
{
	*needShift = false;
	if (raw >= 'A' && raw <= 'Z') { *needShift = true; return AsciiToScancode(raw); }
	static const char shiftedDigits[] = ")!@#$%^&*(";   // index = digit key 0..9
	for (int d = 0; d < 10; d++) {
		if (raw == (uint8_t)shiftedDigits[d]) {
			*needShift = true;
			return AsciiToScancode((uint8_t)('0' + d));
		}
	}
	switch (raw) {   // shifted punctuation: same physical key as the unshifted one
		case '_': case '+': case '{': case '}': case ':': case '"':
		case '~': case '|': case '<': case '>': case '?':
			*needShift = true;
			break;
		default: break;
	}
	return AsciiToScancode(raw);   // letters(lower), digits, space, enter, backspace, punctuation
}

static void TDeck_HandleTextEntryByte(uint8_t raw)
{
	bool shift = false;
	uint8_t sc = TDeck_TextScancode(raw, &shift);
	if (sc == 0) return;
	if (shift) Input_EventHandler(0x36);          // Right Shift down
	Input_EventHandler(sc);
	Input_EventHandler(sc | 0x80);
	if (shift) Input_EventHandler(0x36 | 0x80);   // Right Shift up
}

static void TDeck_HandleKeyByte(uint8_t raw)
{
#if TDECK_DEBUG_INPUT
	Serial.printf("[kbd] raw=0x%02X ('%c')\n", raw, (raw >= 0x20 && raw < 0x7F) ? raw : '.');
#endif
	if (raw == 0) return;

	/* While a text prompt is open, act as a plain keyboard (see above). */
	if (TDeck_TextEntryActive()) {
		TDeck_HandleTextEntryByte(raw);
		return;
	}

	/* IJKL is pure cursor navigation - never also send it as a letter
	 * keypress. A/M/R/G/B/O and everything else fall through below to
	 * also be sent as their normal scancode, since those are meant to
	 * reach Dune II's own hotkeys (Attack/Move/Retreat/Guard/Build/
	 * Options) directly. Backspace also falls through to send its
	 * scancode in addition to its click pulse; Enter does not anymore -
	 * see the dedicated comment further down in this function. */
	switch (raw) {
		case 'i': case 'I': case 'j': case 'J': case 'k': case 'K': case 'l': case 'L':
			TDeck_HandleNavigationKey(raw);
			return;
		default:
			TDeck_HandleNavigationKey(raw);
			break;
	}

	uint8_t scancode;
	if (raw == 'o' || raw == 'O') {
		/* Dune II's native "Options" shortcut is NOT the letter O - traced
		 * via the engine's own table/widgetinfo.c (GUI_Widget_Options_Click,
		 * shortcut=113) and its scancode translation table
		 * (s_keyTranslate[0x3C] == 113), which resolves to XT scancode
		 * 0x3C: F2. Remapped here so the O key does what was asked for,
		 * since the game itself was never going to respond to a plain 'O'. */
		scancode = 0x3C;
	} else {
		scancode = AsciiToScancode(raw);
	}

	if (scancode == 0) {
		for (unsigned i = 0; i < sizeof(s_specialByteMap) / sizeof(s_specialByteMap[0]); i++) {
			if (s_specialByteMap[i].raw == raw) { scancode = s_specialByteMap[i].scancode; break; }
		}
	}
	if (scancode == 0) return; /* unmapped byte - ignore rather than guess */

	if (raw == '\r' || raw == '\n' || raw == ' ') {
		/* Enter previously also sent its normal scancode (0x1C) here, in
		 * addition to the click pulse in TDeck_HandleNavigationKey above.
		 * Removed as an experiment: confirmed via testing that hotkeys
		 * (Attack/Move/etc.) only worked after pressing Enter a SECOND
		 * time following a unit selection, which is consistent with this
		 * extra scancode confusing the engine's input queue/state right
		 * after a click. The click alone already performs selection
		 * correctly (confirmed working), so there's no clear need for
		 * this pass-through - if some other menu turns out to actually
		 * need a raw Enter keypress, we'll need a more targeted fix
		 * instead of reverting this blindly. */
		return;
	}

	/* No real key-up from this keyboard: synthesize a down+up pulse.
	 * Backspace still sends its normal scancode too (0x0E), in addition
	 * to the mouse-click pulse above, in case a menu listens for the raw
	 * keyboard event directly rather than a mouse click. */
	Input_EventHandler(scancode);
	Input_EventHandler(scancode | 0x80);
}

static uint32_t s_lastKeyboardPollAt = 0;

static uint8_t s_lastKeyboardByte = 0;

static uint8_t s_pendingKeyboardByte = 0;
static uint8_t s_pendingKeyboardCount = 0;

static void TDeck_PollKeyboard(void)
{
	/* Previously this required digitalRead(TDECK_KEYBOARD_INT) == LOW
	 * before ever attempting a read - confirmed via hardware testing to
	 * result in ZERO keyboard input ever registering, meaning that
	 * assumption about the INT pin's behavior was wrong. Poll
	 * unconditionally instead (rate-limited so we don't hammer the I2C
	 * bus every ~1ms idle-loop iteration); if TDECK_KEYBOARD_INT does
	 * go low when a key is pending, this still benefits from that as a
	 * fast-path, it just isn't required anymore. */
	uint32_t now = millis();
	bool intLow = (digitalRead(TDECK_KEYBOARD_INT) == LOW);
	if (!intLow && (now - s_lastKeyboardPollAt) < 30) return;
	s_lastKeyboardPollAt = now;

	/* NOTE: this used to additionally require requestFrom() to return
	 * exactly 1 and the same byte to be read on 2 consecutive polls,
	 * added while chasing the phantom-click bug on the (now disproven)
	 * theory that it was keyboard-read noise. Hardware testing has since
	 * proven the phantom clicks are 100% trackball-sourced (see
	 * TDeck_PollInput below) - and that same hardening apparently made
	 * genuine keypresses on this keyboard chip impossible to ever
	 * confirm, breaking keyboard input entirely. Back to the simpler,
	 * previously-proven-working approach: trust Wire.available(), and
	 * only treat a byte as a NEW keypress if it differs from the last
	 * one processed (this keyboard chip keeps returning the same
	 * non-zero byte for as long as a key is held, rather than clearing
	 * to 0 once read, so without this a held key would re-fire every
	 * ~30ms; a genuine second press of the same key still works
	 * correctly, since the chip returns 0 while the key is up in
	 * between, which resets s_lastKeyboardByte). */
	uint8_t received = Wire.requestFrom((int)TDECK_KEYBOARD_ADDR, (uint8_t)1);
	bool avail = Wire.available();
	uint8_t raw = avail ? Wire.read() : 0xFF; /* 0xFF is not a valid ASCII byte from this chip - used purely as a "no data" marker in the log below */

	/* Always-on, rate-limited (every 500ms) raw I2C state, regardless of
	 * whether anything "interesting" happened - this is the missing
	 * visibility needed to tell apart "the I2C transaction itself is
	 * failing" (received != 1 or !avail) from "it succeeds but always
	 * reads 0" (chip responding, no key pressed) from "reads something
	 * that isn't being recognized" (raw is some unexpected value). */
	static uint32_t s_lastKbdRawLogAt = 0;
	if (now - s_lastKbdRawLogAt > 500) {
		s_lastKbdRawLogAt = now;
		Serial.printf("[kbd] raw I2C state: requestFrom()=%u available=%d raw=0x%02X\n", received, avail, raw);
	}

	if (avail && raw != 0 && raw != 0xFF && raw != s_lastKeyboardByte) {
		s_lastKeyboardByte = raw;
		Serial.printf("[kbd] confirmed raw=0x%02X ('%c')\n", raw, (raw >= 0x20 && raw < 0x7F) ? raw : '.');
		TDeck_HandleKeyByte(raw);
	} else if (avail && raw == 0) {
		s_lastKeyboardByte = 0;
	}
}

// ---------------------------------------------------------------------
// GT911 capacitive touchscreen (present on T-Deck units generally - not just
// the "Plus"; an earlier note here claiming otherwise was an assumption that
// real units disproved). If no controller answers on the I2C bus, touch is
// simply disabled and everything else runs as before.
// ---------------------------------------------------------------------
//
// Behaviour: a touch moves the on-screen cursor to the finger and presses the
// LEFT mouse button there; dragging moves the cursor with the button held
// (so drag-select works); lifting releases. It is the same input as a
// trackball press, Enter or Space, just at an absolute position.
//
// ORDER MATTERS on touch-down: the engine's Mouse_EventHandler() applies the
// BUTTON change first and the POSITION second, and Input_HandleInput() records
// a click's coordinates from the cursor position at that moment. So a press
// delivered in the same call as a big cursor jump would click where the cursor
// USED to be. We therefore send a position-only event first (cursor teleports
// to the finger, no button change), then the press at that same position.

#if TDECK_HAS_TOUCH
static TouchDrvGT911 s_touch;
static bool s_touchReady = false;
static uint8_t s_touchMisses = 0;          // consecutive empty polls while a touch is down
static uint32_t s_touchPressedAt = 0;
static uint32_t s_touchLastPollAt = 0;
static uint32_t s_touchLastIgnoredLogAt = 0;

static void TDeck_Touch_Init(void)
{
	/* The GT911 answers at 0x5D or 0x14 depending on how it latched at
	 * power-up. LilyGO's own T-Deck example uses 0x5D (SensorLib's
	 * GT911_SLAVE_ADDRESS_L); probe both rather than assume. */
	static const uint8_t kAddrs[2] = { TDECK_TOUCH_ADDR, 0x14 };
	uint8_t addr = 0;
	for (int i = 0; i < 2 && addr == 0; i++) {
		Wire.beginTransmission(kAddrs[i]);
		if (Wire.endTransmission() == 0) addr = kAddrs[i];
	}
	if (addr == 0) {
		Serial.println("[touch] no GT911 answered at 0x5D or 0x14 - touchscreen disabled");
		return;
	}

	pinMode(TDECK_TOUCH_INT, INPUT);
	s_touch.setPins(-1, TDECK_TOUCH_INT);    /* no reset pin wired; INT on GPIO16 (as in LilyGO's example) */
	if (!s_touch.begin(Wire, addr, TDECK_I2C_SDA, TDECK_I2C_SCL)) {
		Serial.printf("[touch] GT911 at 0x%02X did not initialise - touchscreen disabled\n", addr);
		return;
	}
	/* Orientation exactly as LilyGO's own T-Deck touch example: the panel's
	 * native frame is rotated relative to the landscape display, so swap
	 * XY then mirror Y (SensorLib applies swap first, then mirrors using the
	 * max value). Adjustable in tdeck_config.h if a unit disagrees. */
	s_touch.setMaxCoordinates(320, 240);
	s_touch.setSwapXY(TDECK_TOUCH_SWAP_XY);
	s_touch.setMirrorXY(TDECK_TOUCH_MIRROR_X, TDECK_TOUCH_MIRROR_Y);
	s_touchReady = true;
	Serial.printf("[touch] GT911 ready at 0x%02X (swapXY=%d mirrorX=%d mirrorY=%d)\n",
				  addr, TDECK_TOUCH_SWAP_XY, TDECK_TOUCH_MIRROR_X, TDECK_TOUCH_MIRROR_Y);
}

/* Panel coordinates (320x240, already swapped/mirrored by the library) ->
 * game canvas coordinates (320x200, centred vertically: see video_tdeck.cpp
 * OFFSET_Y). `strict` is used for a NEW touch: one landing in the unused
 * strip above/below the canvas is rejected rather than clamped, so a stray
 * touch on the black bars can't click the nearest game pixel. A touch that is
 * already down and drags into a strip is clamped instead, so the drag goes on. */
static bool TDeck_TouchToGame(int32_t px, int32_t py, bool strict, int32_t *gx, int32_t *gy)
{
	const int32_t offY = (240 - SCREEN_HEIGHT) / 2;
	const int32_t minX = (int32_t)g_tdeckMouseMinX, maxX = (int32_t)g_tdeckMouseMaxX;
	const int32_t minY = (int32_t)g_tdeckMouseMinY, maxY = (int32_t)g_tdeckMouseMaxY;
	int32_t x = px, y = py - offY;
	if (x < minX) x = minX;
	if (x > maxX) x = maxX;                  /* the library can return the max value itself (e.g. 320) */
	if (y < minY || y > maxY) {
		if (strict) return false;
		y = (y < minY) ? minY : maxY;
	}
	*gx = x;
	*gy = y;
	return true;
}

static void TDeck_PollTouch(void)
{
	if (!s_touchReady) return;
	uint32_t now = millis();
	if ((now - s_touchLastPollAt) < TDECK_TOUCH_POLL_MS) return;
	s_touchLastPollAt = now;

	int16_t x[1] = { 0 }, y[1] = { 0 };
	uint8_t n = s_touch.getPoint(x, y, 1);   /* first finger only; GT911 clears its buffer on every read */

	if (n > 0) {
		s_touchMisses = 0;
		int32_t gx = 0, gy = 0;
		if (!s_touchDown) {
			if (!TDeck_TouchToGame(x[0], y[0], true, &gx, &gy)) {
				if (now - s_touchLastIgnoredLogAt > 500) {
					s_touchLastIgnoredLogAt = now;
					Serial.printf("[touch] ignored touch outside the game canvas: raw=(%d,%d)\n", (int)x[0], (int)y[0]);
				}
				return;
			}
			g_tdeckMouseX = (uint16_t)gx;
			g_tdeckMouseY = (uint16_t)gy;
			/* (1) cursor jumps to the finger - position only, no button change... */
			TDeck_TracedMouseEvent("touch-move", g_tdeckMouseX, g_tdeckMouseY,
				s_leftDown || s_synthLeft, s_rightDown || s_synthRight);
			/* (2) ...then the press lands at that position. */
			s_touchDown = true;
			s_touchPressedAt = now;
			Serial.printf("[touch] down raw=(%d,%d) -> game=(%d,%d)\n", (int)x[0], (int)y[0], (int)gx, (int)gy);
			TDeck_TracedMouseEvent("touch-down", g_tdeckMouseX, g_tdeckMouseY, true, s_rightDown || s_synthRight);
		} else {
			TDeck_TouchToGame(x[0], y[0], false, &gx, &gy);
			if (gx != (int32_t)g_tdeckMouseX || gy != (int32_t)g_tdeckMouseY) {
				g_tdeckMouseX = (uint16_t)gx;
				g_tdeckMouseY = (uint16_t)gy;
				TDeck_TracedMouseEvent("touch-drag", g_tdeckMouseX, g_tdeckMouseY, true, s_rightDown || s_synthRight);
			}
		}
	} else if (s_touchDown) {
		/* A single empty read mid-touch is normal for this chip (it clears
		 * its status on every read and takes a moment to repost), so release
		 * only after several in a row - and never sooner than a minimum hold,
		 * so the engine always sees a real press-then-release. */
		if (s_touchMisses < 255) s_touchMisses++;
		if (s_touchMisses >= TDECK_TOUCH_RELEASE_POLLS && (now - s_touchPressedAt) >= TDECK_TOUCH_MIN_HOLD_MS) {
			s_touchDown = false;
			s_touchMisses = 0;
			TDeck_TracedMouseEvent("touch-up", g_tdeckMouseX, g_tdeckMouseY,
				s_leftDown || s_synthLeft, s_rightDown || s_synthRight);
			Serial.printf("[touch] up at game=(%u,%u)\n", (unsigned)g_tdeckMouseX, (unsigned)g_tdeckMouseY);
		}
	}
}
#endif /* TDECK_HAS_TOUCH */

// ---------------------------------------------------------------------
// Public entry points
// ---------------------------------------------------------------------

extern "C" void TDeck_Input_Init(void)
{
	/* GPIO0 is a boot-strapping pin, and ESP32-S3 can leave such pins in
	 * "RTC GPIO" mode after boot, where the normal digital pull-up that
	 * pinMode(INPUT_PULLUP) sets does not take effect. rtc_gpio_deinit()
	 * forces it back to a normal digital GPIO first. UNVERIFIED
	 * hypothesis (found while comparing against retro-go's T-Deck
	 * handling of the same pin) - check the serial log's
	 * "[trackball] raw click pin" lines to see if the pin finally
	 * changes level when the trackball is pressed. */
	rtc_gpio_deinit(GPIO_NUM_0);

	pinMode(TDECK_TRACKBALL_UP, INPUT_PULLUP);
	pinMode(TDECK_TRACKBALL_DOWN, INPUT_PULLUP);
	pinMode(TDECK_TRACKBALL_LEFT, INPUT_PULLUP);
	pinMode(TDECK_TRACKBALL_RIGHT, INPUT_PULLUP);
	pinMode(TDECK_TRACKBALL_CLICK, INPUT_PULLUP);

	attachInterrupt(digitalPinToInterrupt(TDECK_TRACKBALL_UP), tbIsrUp, CHANGE);
	attachInterrupt(digitalPinToInterrupt(TDECK_TRACKBALL_DOWN), tbIsrDown, CHANGE);
	attachInterrupt(digitalPinToInterrupt(TDECK_TRACKBALL_LEFT), tbIsrLeft, CHANGE);
	attachInterrupt(digitalPinToInterrupt(TDECK_TRACKBALL_RIGHT), tbIsrRight, CHANGE);
	/* CHANGE (not FALLING) matches LilyGO's own official T-Deck example
	 * code for this exact trackball module. TDECK_TRACKBALL_STEP in
	 * tdeck_config.h is halved accordingly, since CHANGE fires on both
	 * edges (~2x the interrupts per physical rotation vs FALLING-only). */

	pinMode(TDECK_KEYBOARD_INT, INPUT_PULLUP);

#if TDECK_HAS_TOUCH
	TDeck_Touch_Init();
#endif
}

extern "C" void TDeck_PollInput(void)
{
	/* Complete a pending keyboard-initiated click: send the matching UP. */
	if ((s_synthLeft || s_synthRight) && (int32_t)(millis() - s_synthReleaseAt) >= 0) {
		bool wasLeft = s_synthLeft;
		s_synthLeft = false;
		s_synthRight = false;
		TDeck_TracedMouseEvent(wasLeft ? "Enter-click-up" : "Backspace-click-up",
			g_tdeckMouseX, g_tdeckMouseY, s_leftDown || s_touchDown, s_rightDown);
	}

	/* --- Trackball click (left button) --- */
	bool rawLow = (digitalRead(TDECK_TRACKBALL_CLICK) == LOW);
	uint32_t now = millis();
	bool leftChanged = false;

	/* Periodic raw-state log (every 500ms) - lets a serial log show
	 * whether the pin level ever changes in response to the button. */
	static uint32_t s_lastRawLogAt = 0;
	if (now - s_lastRawLogAt > 500) {
		s_lastRawLogAt = now;
		Serial.printf("[trackball] raw click pin = %s\n", rawLow ? "LOW" : "HIGH");
	}

	/* BASELINE, NOT A PRESS: whatever level the pin reads on the first
	 * poll is treated as its IDLE level, and a "press" is any deviation
	 * from it. This replaced two earlier assumptions that both caused
	 * real bugs:
	 *   1. "LOW means pressed". On both tested T-Decks the pin reads LOW
	 *      from the very first sample, so that was seen as a press
	 *      transition at boot and latched s_leftDown = true forever -
	 *      every later trackball-movement event told the engine the left
	 *      button was held (261 of 261 events in a real log carried
	 *      left=1). That made hovering the radar behave like a drag, and
	 *      made the next Enter's press a no-op (engine already thought
	 *      the button was down, so only a lone release got through - the
	 *      "press Enter twice" bug).
	 *   2. A repeating "stuck pin" click-pulse generator, which would also
	 *      turn any genuine hold longer than 400ms (e.g. drag-selecting)
	 *      into repeated clicks. Removed.
	 * Deviation-from-baseline also works regardless of the switch's
	 * polarity. Caveat: if the button is physically held down during the
	 * very first poll, its polarity is inverted until reboot. */
	/* Re-claim the pin as a plain pulled-up input right before taking the
	 * baseline. This runs after the engine has initialised its drivers
	 * (including I2S audio), and nothing else should be driving this pad -
	 * but if any peripheral ever routes a signal out to GPIO0 again (as
	 * the I2S MCLK once did), this restores it to an input. */
	static bool s_baselineTaken = false;
	static bool s_idleIsLow = false;
	if (!s_baselineTaken) {
		s_baselineTaken = true;
		pinMode(TDECK_TRACKBALL_CLICK, INPUT_PULLUP);
		delay(5);
		rawLow = (digitalRead(TDECK_TRACKBALL_CLICK) == LOW);
		s_idleIsLow = rawLow;
		s_clickPrev = false;
		s_leftDown = false;
		Serial.printf("[trackball] click idle baseline = %s (no event generated)\n", rawLow ? "LOW" : "HIGH");
	}

	bool pressedNow = (rawLow != s_idleIsLow);

	if (pressedNow != s_clickPrev && (now - s_clickDebounceAt) > 15) {
		s_clickDebounceAt = now;
		s_clickPrev = pressedNow;
		if (s_leftDown != pressedNow) {
			s_leftDown = pressedNow;
			leftChanged = true;
			Serial.printf("[trackball] click -> %s\n", s_leftDown ? "DOWN" : "UP");
		}
	}

	/* --- Trackball movement --- */
	int32_t dx, dy;
	noInterrupts();
	dx = s_tbDeltaX; s_tbDeltaX = 0;
	dy = s_tbDeltaY; s_tbDeltaY = 0;
	interrupts();

	bool moved = false;
	if (dx != 0 || dy != 0) {
#if TDECK_DEBUG_INPUT
		Serial.printf("[trackball] dx=%d dy=%d\n", (int)dx, (int)dy);
#endif
		int32_t nx = (int32_t)g_tdeckMouseX + dx * TDECK_TRACKBALL_STEP;
		int32_t ny = (int32_t)g_tdeckMouseY + dy * TDECK_TRACKBALL_STEP;
		if (nx < g_tdeckMouseMinX) nx = g_tdeckMouseMinX;
		if (nx > g_tdeckMouseMaxX) nx = g_tdeckMouseMaxX;
		if (ny < g_tdeckMouseMinY) ny = g_tdeckMouseMinY;
		if (ny > g_tdeckMouseMaxY) ny = g_tdeckMouseMaxY;
		g_tdeckMouseX = (uint16_t)nx;
		g_tdeckMouseY = (uint16_t)ny;
		moved = true;
	}

	if (moved || leftChanged) {
		TDeck_TracedMouseEvent("trackball-normal", g_tdeckMouseX, g_tdeckMouseY, s_leftDown || s_synthLeft || s_touchDown, s_rightDown || s_synthRight);
	}

	/* --- Keyboard (I2C, interrupt-gated so we don't hammer the bus) --- */
	TDeck_PollKeyboard();

#if TDECK_HAS_TOUCH
	TDeck_PollTouch();
#endif
}
