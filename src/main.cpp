/**
 * main.cpp - Arduino entry point for the OpenDUNE T-Deck port.
 *
 * Responsibilities here (everything below this is the engine's own code,
 * untouched apart from the TDECK branches added to sleep.h/timer.c/file.c):
 *   1. Power up the T-Deck's peripheral rail and deselect every SPI device
 *      sharing the bus (TFT/SD/Radio) - see "CS line deselection" below.
 *   2. Bring up the display immediately and show a boot-progress splash,
 *      BEFORE touching SD/I2C/the engine at all. This means: (a) we get
 *      visible proof the panel/backlight/wiring work on their own, and
 *      (b) if anything later in boot hangs or fails, the last message
 *      shown stays on screen and tells you exactly where it got stuck,
 *      rather than a black screen with no information at all.
 *   3. Bring up the shared SPI bus, mount the SD card at /sdcard (matching
 *      DUNE_DATA_DIR in file.c), bring up I2C (keyboard, and touch if
 *      TDECK_HAS_TOUCH).
 *   4. Hand off to OpenDUNE's real main() in a dedicated FreeRTOS task with
 *      a large stack, since it never returns (GameLoop_Main() is a
 *      blocking for(;;) loop - see opendune.c).
 */

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include <TFT_eSPI.h>

#include "tdeck_pins.h"
#include "tdeck_config.h"

// OpenDUNE's actual entry point (src/opendune.c). It ignores argc/argv
// entirely (checked directly in the source), so calling it with a dummy
// argv is safe and exactly matches how every other platform's main() is
// invoked by its OS loader.
extern "C" int main(int argc, char **argv);

// Implemented in input_tdeck.cpp
extern "C" void TDeck_Input_Init(void);

static TFT_eSPI s_bootTft = TFT_eSPI();
static int16_t s_bootLine = 0;

static void BootSplash_Init(void)
{
	pinMode(TDECK_TFT_BACKLIGHT, OUTPUT);
	digitalWrite(TDECK_TFT_BACKLIGHT, LOW);

	s_bootTft.init();
	s_bootTft.setRotation(TDECK_TFT_ROTATION);
	s_bootTft.fillScreen(TFT_BLACK);
	s_bootTft.setTextColor(TFT_GREEN, TFT_BLACK);
	s_bootTft.setTextSize(2);
	s_bootTft.setCursor(4, 4);
	s_bootTft.println("OpenDUNE T-Deck");
	s_bootTft.setTextSize(1);
	s_bootTft.setTextColor(TFT_WHITE, TFT_BLACK);
	s_bootLine = 28;

	digitalWrite(TDECK_TFT_BACKLIGHT, HIGH);
}

// Appends one status line to the boot splash and mirrors it to Serial.
// Deliberately simple/blocking - this only runs a handful of times during
// boot, well before the game's own renderer (video_tdeck.cpp) takes over
// the screen. Exposed with C linkage so TDECK-guarded checkpoints added
// directly into the vendored engine (opendune.c) can call it too - this
// is how we get visibility into *which* engine init step a boot failure
// happens in, not just "we called main() and then nothing".
extern "C" void TDeck_BootLog(const char *msg)
{
	// The TFT and SD card share a bus; make sure the SD card's CS is
	// deselected before we start a TFT SPI transaction; SD library calls
	// made just before this one are the reason a couple of early status
	// lines could come out with their first character corrupted/missing.
	digitalWrite(TDECK_SDCARD_CS, HIGH);

	s_bootTft.setCursor(4, s_bootLine);
	s_bootTft.println(msg);
	s_bootLine += 10;
	Serial.println(msg);
}

static void BootSplash_Log(const char *msg) { TDeck_BootLog(msg); }

static void ShowFatalError(const char *line1, const char *line2)
{
	s_bootTft.setTextColor(TFT_RED, TFT_BLACK);
	s_bootTft.setTextSize(1);
	s_bootLine += 6;
	BootSplash_Log(line1);
	if (line2 != NULL) {
		s_bootTft.setTextColor(TFT_WHITE, TFT_BLACK);
		BootSplash_Log(line2);
	}
	for (;;) { delay(1000); }
}

static void GameTask(void *arg)
{
	(void)arg;
	char argv0[] = "opendune";
	char *argv[] = { argv0, NULL };
	main(1, argv);
	/* Never reached: GameLoop_Main() inside main() loops forever. If it
	 * ever DOES return (e.g. a future "quit to launcher" feature is added
	 * to opendune.c), fall through and let the task end cleanly. */
	vTaskDelete(NULL);
}

void setup(void)
{
	Serial.begin(115200);
	delay(200);
	Serial.println("OpenDUNE T-Deck - booting");

	// Peripheral power rail must be up before SPI/I2C devices respond.
	// Per LilyGO's own docs this is required when running on battery and
	// harmless when running on USB, so we always set it.
	pinMode(TDECK_PERI_POWERON, OUTPUT);
	digitalWrite(TDECK_PERI_POWERON, HIGH);
	/* Was 200ms. Raised to 500ms as a test: the keyboard chip and the
	 * trackball's click circuit both failing consistently - on two
	 * independent physical units - while trackball rotation keeps
	 * working, is consistent with those two specifically needing more
	 * time to power up/stabilize than rotation does (if rotation's
	 * simple mechanical/optical contacts don't depend on this rail the
	 * same way an active I2C chip and a switch circuit might). */
	delay(500);

	// --- CS line deselection ---
	// The T-Deck's TFT, SD card, and LoRa radio all share one SPI bus.
	// Only one device may be selected (CS driven low) at a time, and an
	// undriven/floating CS on a device we're not using yet (the radio, in
	// our case - we never use it, but it's still physically on the bus)
	// can otherwise respond to bus traffic and corrupt the TFT/SD
	// transactions. This exact sequence (deselect TFT, SD, and radio CS
	// before doing anything else) is per LilyGO's own official T-Deck
	// documentation.
	pinMode(TDECK_TFT_CS, OUTPUT);
	pinMode(TDECK_SDCARD_CS, OUTPUT);
	pinMode(TDECK_RADIO_CS, OUTPUT);
	digitalWrite(TDECK_TFT_CS, HIGH);
	digitalWrite(TDECK_SDCARD_CS, HIGH);
	digitalWrite(TDECK_RADIO_CS, HIGH);

	// Bring up the display and show visible proof of life before anything
	// else - see the file header comment for why this ordering matters.
	BootSplash_Init();
	BootSplash_Log("Peripheral power: OK");

	// Shared SPI bus for TFT + SD (each has its own CS line, already
	// deselected above).
	SPI.begin(TDECK_SPI_SCK, TDECK_SPI_MISO, TDECK_SPI_MOSI, TDECK_SDCARD_CS);
	BootSplash_Log("SPI bus: OK");

	Wire.begin(TDECK_I2C_SDA, TDECK_I2C_SCL);
	TDeck_Input_Init();
	BootSplash_Log("Keyboard/trackball: OK");

	if (!SD.begin(TDECK_SDCARD_CS, SPI, TDECK_SD_SPI_HZ, "/sdcard")) {
		ShowFatalError(
			"SD card not found",
			"Insert a microSD card with /dune2 data (see README.md) and reboot.");
		/* not reached */
	}
	BootSplash_Log("SD card: mounted");

	// Convenience for first-time setup: create the data folder if the card
	// is blank, so "copy files into /sdcard/dune2" always has somewhere to
	// go. Failures here are fine (folder may already exist) and ignored.
	SD.mkdir("/dune2");

	if (!SD.exists("/dune2/DUNE.PAK") && !SD.exists("/dune2/dune.pak")) {
		ShowFatalError(
			"Dune II data not found",
			"Copy your Dune II 1.07 files to /dune2 on the SD card (see README.md), then reboot.");
		/* not reached */
	}
	BootSplash_Log("Dune II data: found");

	BootSplash_Log("Starting OpenDUNE...");
	delay(400); // brief pause so the message above is actually readable
	BaseType_t taskCreated = xTaskCreatePinnedToCore(GameTask, "opendune", 32768, NULL, 1, NULL, 1);
	if (taskCreated != pdPASS) {
		ShowFatalError(
			"Failed to start engine task",
			"xTaskCreatePinnedToCore failed - likely out of memory. Report this.");
		/* not reached */
	}
}

void loop(void)
{
	// Everything happens in GameTask (and the DSP audio task); nothing to
	// do on Arduino's own loop task.
	vTaskDelay(pdMS_TO_TICKS(1000));
}
