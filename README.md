# DuneDeck - OpenDUNE for the LilyGO T-Deck

A PlatformIO port of [OpenDUNE](https://github.com/OpenDUNE/OpenDUNE) (a GPLv2
reimplementation of the Dune II engine) targeting the LilyGO T-Deck and T-Deck Plus
(ESP32-S3). The .bin release works with [bmorcelli Launcher](https://bmorcelli.github.io/Launcher/), or can directly flashed to the T-Deck.

## Disclaimer

This port was created using Claude Sonnet 5.5, but also went through extensive testing on multiple real T-Decks. The T-Deck's trackball lends itself as an interesting novelty for an RTS, and it's a surprisingly fun way to play the game if you also familiarize yourself with keyboard shortcuts for commands. A full playthrough of the game has not been completed with DuneDeck yet. However, input controls for the game menus and missions, advancing through multiple missions, and functions like saving and loading a game have all been tested and confirmed to work on real hardware.

## What's in this repo

```
platformio.ini              Build configuration
partitions_tdeck.csv        Flash partition table (16MB flash)
opendune.ini                Ready-made config - copy to /dune2/ on the SD card
src/
  main.cpp                  Arduino entry point: power-up, SPI/SD/I2C bring-up, launches the engine
  tdeck_pins.h               T-Deck GPIO map (from LilyGO's own repo)
  tdeck_config.h             Compile-time options (touch on/off, SPI/I2S speeds, etc.)
  User_Setup_TDeck.h         TFT_eSPI panel configuration
  video_tdeck.cpp            video.h implementation (ST7789 over SPI)
  input_tdeck.cpp            input.h/mouse.h implementation (trackball + keyboard + optional touch)
  dsp_tdeck.cpp              dsp.h implementation (sound effects over I2S)
lib/opendune/
  library.json               Scopes the engine's one needed include path to just its own compilation
  src/                       The OpenDUNE engine itself, vendored and trimmed
                             to the platform-independent files plus several
                             small patches (see below) - this is the actual
                             upstream game logic, essentially untouched.
```

## Where to put your Dune II data files

Dune II's original game data is not included. You
need your own legally-obtained copy of the original Dune II v1.07
release.

### The files you need

Copy these into a folder named `dune2` at the root directory of the SD card that will be inserted into the T-Deck
(so you end up with `/dune2/DUNE.PAK`, `/dune2/INTRO.PAK`, etc.). If your files are not all caps (.pak instead of .PAK) it might be a good idea to change them to mirror what's listed below.

| File | Required? | Notes |
|---|---|---|
| `DUNE.PAK` | **Required** | Core shared assets (units, buildings, UI) |
| `SCENARIO.PAK` | **Required** | Mission/level data |
| `MENTAT.PAK` | **Required** | Mentat advisor screens |
| `VOC.PAK` | **Required** | Sound effects |
| `SOUND.PAK` | **Required** | More sound effects / voice |
| `INTRO.PAK` | **Required** | Intro cutscene assets |
| `FINALE.PAK` | **Required** | Ending cutscene assets |
| `MERC.PAK` (or `HERC.PAK`) | **Required** | Mercenary/tileset data - some releases name this `HERC.PAK`, same content |
| `HARK.PAK` | **Required** | Harkonnen house-specific assets |
| `ATRE.PAK` | **Required** | Atreides house-specific assets |
| `ORDOS.PAK` | **Required** | Ordos house-specific assets |
| `ENGLISH.PAK` | Required for English | Language/text data - use the one matching your language |
| `GERMAN.PAK` | Optional | German language (needs EU or HitSquad-version data - see below) |
| `FRENCH.PAK` | Optional | French language (needs EU or HitSquad-version data - see below) |
| `INTROVOC.PAK` | Optional | Present on some releases; extra intro voice-over |
| `dune2.exe` | Optional | Not used by the engine at all - harmless to include, upstream OpenDUNE's own instructions say you can leave it in |

Any of the four known original releases work (US v1.0, US v1.07,
EU v1.07, HitSquad v1.07) - but per OpenDUNE's own documentation, **French
only works with EU or HitSquad data, and German only works with EU
data.** If you get a version mismatch or corrupted files, exact filenames
can vary slightly release-to-release (e.g. `MERC.PAK` vs `HERC.PAK`); if
in doubt, copy everything from your original install rather than
cherry-picking from the table above.

### Optional: config and saves

- `opendune.ini` (optional) - a ready-to-use copy for this port is
  included at the project root (`opendune.ini`); copy it to `/dune2/` on
  the SD card. It's pre-set with the correct `datadir`/`savedir` for this
  port and notes on which upstream options don't apply here (scaling,
  fullscreen, MIDI). See upstream OpenDUNE's own `README.txt` for the full
  option reference if you want to change anything else.
- `/dune2/save/` - created automatically on first boot; save games go
  here.

**If the game seems to hang on the house-selection screen after boot**:
this is a documented upstream OpenDUNE behaviour, not specific to this
port - it plays a voice clip when you click a house and waits for it to
finish, so it can look "stuck" if the audio driver isn't outputting sound.
Check the speaker/I2S wiring and `dsp_tdeck.cpp` first. I have found scrolling the trackball usually moves the game along.

## Controls

- **Trackball** moves the mouse cursor; **trackball click** is the left
  mouse button (Dune II's primary input). See Troubleshooting below - as
  of this version, clicks not registering in menus is an open issue with
  added diagnostics, not yet a confirmed fix.
- **Enter and Space** act as additional inputs for the left mouse button, so you can also use them instead of the trackball.  
- **Backspace** acts as a right-click for deselect, or canceling a command or building placement.
- **I/J/K/L** move the mouse cursor up/left/down/right (a fixed step per
  keypress, since this keyboard has no key-repeat - see the fidelity
  limitation below). **Enter** or **Space** performs a left-click (select) at the
  current cursor position (same input as a trackball press), **Backspace** performs a right-click (cancel/
  deselect). All of these drive the exact same cursor as the trackball
  and work simultaneously alongside it - press I and the cursor moves the
  same as if you'd rolled the trackball up.
- **A/M/R/G/B** pass through as ordinary keypresses to Dune II's own
  native hotkeys: **A**ttack, **M**ove, **R**etreat, **G**uard,
  **B**uild. These aren't remapped or intercepted by this port - they're
  just Dune II's existing PC keyboard shortcuts, which work because the
  letter reaches the engine unchanged. (IJKL was chosen for cursor
  movement specifically so these five stay free, unlike an earlier
  version of this port that used WASD, which would have conflicted with
  Attack.) You can also use the on-screen buttons for these commands, but the keyboard shortcuts are faster.
- **O** opens **O**ptions - but unlike the five above, this one *is* a
  deliberate remap: Dune II's actual native Options shortcut is F2, not
  'O' (confirmed by tracing `table/widgetinfo.c`), so pressing O sends
  F2's scancode directly rather than relying on a binding that doesn't
  exist in the original game.
- **P** cues placement of a building or concrete that has finished construction.  
- **Keyboard** also sends any other hotkeys/shortcuts you'd use on PC
  (Q for quit, F1-F3 for Mentat/options/structure menus, etc.).
- **Touch** (T-Deck **Plus** only - the standard T-Deck has no
  touchscreen; see `tdeck_config.h`'s `TDECK_HAS_TOUCH`) acts as an
  absolute-position left-click pointer alongside the trackball.

### Keyboard fidelity limitation (please read)

The stock T-Deck keyboard co-processor reports a single ASCII byte per
keypress over I2C, with **no separate key-up event and no distinct
Shift/Ctrl/Alt keycodes of their own** (it resolves Shift into the ASCII
byte itself, e.g. sends `'A'` rather than `'a'`+Shift). `input_tdeck.cpp`:

- Synthesizes a down+up scancode pulse per keypress (no true "held key"
  state) - fine for Dune II's menu/hotkey-driven UI, but you won't get
  OS-style key-repeat from holding a key down.
- Maps a key's *letter identity* back to its base US-QWERTY scancode
  regardless of case, since Dune II barely uses shifted symbols outside
  save-file naming (where case doesn't matter to the engine).
- Does **not** currently send scancodes for arrow keys / Fn combinations,
  because I could not confirm the exact non-ASCII byte values the stock
  keyboard firmware uses for them without hardware to test against. There's
  a `s_specialByteMap` table with commented-out placeholder entries in
  `input_tdeck.cpp` - build with `TDECK_DEBUG_INPUT` defined to `1`,
  watch the serial monitor while pressing those keys, and fill in the real
  byte values you observe.
- If you flash a community keyboard firmware replacement (e.g.
  rgrizzell's or hreikin's) that reports richer modifier data, you can get
  real Shift/Ctrl/Alt scancodes - extend `TDeck_HandleKeyByte()` for it.

### Trackball polarity

`TDeck_Input_Init()` wires the four direction GPIOs with `attachInterrupt`
on `FALLING`. If movement feels inverted, jumpy, or entirely absent, try
`CHANGE` instead - exact polarity can vary by trackball module revision.

## Building

### Toolchain: this is a PlatformIO project, not native Visual Studio

ESP32-S3 firmware is cross-compiled with the Xtensa GCC toolchain - native
MSVC/Visual Studio cannot target this chip directly. Your real options:

- **VS Code + PlatformIO extension** (the standard, best-supported path).
  Open this folder, let PlatformIO fetch the toolchain/libraries, then
  `PlatformIO: Build` / `PlatformIO: Upload`.
- **Actual Visual Studio + [VisualGDB](https://visualgdb.com/)**, if you
  specifically want it inside Visual Studio's IDE. VisualGDB can import a
  PlatformIO project directly; the `platformio.ini` here is the source of
  truth either way.
- **CLI**: `pio run` to build, `pio run -t upload` to build+flash over USB.

### Steps

```
pio run                 # builds .pio/build/tdeck/firmware.bin
pio run -t upload       # builds and flashes over USB (T-Deck in normal mode)
pio device monitor       # serial log at 115200 baud
```

The finished firmware is `.pio/build/tdeck/firmware.bin`.

### Toolchain notes / version pinning

`platformio.ini` pins `platform = espressif32@6.5.0`. This was a deliberate
choice: `dsp_tdeck.cpp` uses the legacy `driver/i2s.h` API, which is stable
on the arduino-esp32 2.0.x core that this platform version bundles.
Newer `espressif32` platform releases move to arduino-esp32 3.x, which
restructures the I2S API (`ESP_I2S.h`) - if you bump the platform version,
expect to rewrite the I2S calls in `dsp_tdeck.cpp` to match.

## Installing via bmorcelli Launcher

1. Build `firmware.bin` as above.
2. Copy it to the T-Deck's SD card (or wherever your bmorcelli Launcher
   build expects installable apps - check its own docs/UI for this).
3. Install/launch it from the Launcher's app list.

**Partition table caveat**: `partitions_tdeck.csv` uses the standard
Arduino-ESP32 dual-OTA-slot layout (`nvs`/`otadata`/`app0`/`app1`/`spiffs`),
since that's the safest default and what most flashing tools expect. I was
not able to confirm from bmorcelli Launcher's own documentation whether it
specifically wants a single unversioned `factory` partition instead of
`ota_0`/`ota_1`. If the Launcher rejects the binary or won't boot it,
open `partitions_tdeck.csv` and switch `app0`'s SubType from `ota_0` to
`factory`, then delete the `otadata` and `app1` lines (give app0 the freed
space back) - a small, well-understood ESP32 partition-table edit.

**Returning to the Launcher**: No functionality exists within this firmware to properly exit back to bmorcelli Launcher, so you will need to power-cycle the T-Deck to do so.



## Legal

OpenDUNE itself is GPLv2. **Dune II's original data files are not
included here and remain copyrighted** (Westwood Studios/Electronic Arts).
You must supply your own legally-obtained copy, exactly as with any other
OpenDUNE platform build.
