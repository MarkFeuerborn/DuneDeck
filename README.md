# OpenDUNE for the LilyGO T-Deck

A PlatformIO port of [OpenDUNE](https://github.com/OpenDUNE/OpenDUNE) (a GPLv2
reimplementation of the Dune II engine) targeting the LilyGO T-Deck
(ESP32-S3), intended to be flashed/installed as a `.bin` and run under
**bmorcelli Launcher**.

**Status: a from-scratch driver-layer port, built by reading OpenDUNE's
actual source and LilyGO's official pin definitions, but not yet compiled
or run on real hardware** (this was built in a sandboxed environment with
no ESP32 toolchain access). Treat this as a strong, carefully-researched
starting point rather than a guaranteed drop-in binary - see "Known
limitations / things to verify" below for exactly what that means in
practice.

---

## 1. What's in this repo

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

### Patches made to the vendored OpenDUNE source (all marked `TDECK` / searchable)

1. **`os/sleep.h` + `timer.c`** - route the engine's idle loop through a
   `TDECK` branch that also polls trackball/keyboard/touch each iteration
   and yields to FreeRTOS (`delay(1)`) so the watchdog stays happy.
2. **`file.c`** - default data directory is `/sdcard/dune2`, save directory
   is `/sdcard/dune2/save` (see part 2 below).
3. **`os/strings.h`** - excluded `TDECK` from a glibc-only `snprintf`
   re-declaration block (it uses `__THROW`/`__restrict`, which don't exist
   in ESP32's newlib and would otherwise fail the build).
4. **`types.h`** - guarded the C89 `bool`/`true`/`false` shim with
   `#if !defined(__cplusplus)`. The original codebase never needed this
   guard because every existing platform driver is plain C; this port's
   T-Deck drivers are C++ (they need `Arduino.h`/`TFT_eSPI`/etc.), and
   without the guard `typedef unsigned char bool;` is a hard error in C++
   (you can't `typedef` over a language keyword).
5. **Renamed four engine headers that collided with real system/C++
   standard headers of the same name**: `string.h` -> `dune_string.h`,
   `os/math.h` -> `os/dune_math.h`, `os/strings.h` -> `os/dune_strings.h`,
   `os/error.h` -> `os/dune_error.h`. This was found via an actual build
   failure (see "Troubleshooting" below) - the original desktop build
   never hits this because its Makefile never exposes these directories to
   unrelated third-party code, but PlatformIO's global `build_flags`
   apply to every library in the project, so `os/math.h` was shadowing
   `<math.h>`/C++'s `<algorithm>`, breaking Arduino-ESP32 core files that
   have nothing to do with OpenDUNE. The include-path setup was also
   reworked: only one path (`lib/opendune/src`) is actually needed
   anywhere (confirmed by auditing every `#include` in the tree), and it's
   now scoped to just the engine's own compilation via
   `lib/opendune/library.json`'s `includeDir`, instead of being a global
   `platformio.ini` flag that leaks into every other library.
6. **`timer.c`** - added `TDECK` to the branches that skip the POSIX
   `sigaction`/`setitimer`/`SIGALRM`-based real async timer (used to
   drive `Timer_Handler` on desktop platforms), matching the existing
   DOS/TOS/Haiku precedent - none of those APIs exist on ESP32, and we
   don't need them since everything is driven cooperatively through
   `Timer_InterruptRun(0)` polling instead (see patch #1).
7. **`inifile.c`** - added a `TDECK` branch to `Load_IniFile()` pointing
   directly at `/sdcard/dune2/opendune.ini`; without it, the function fell
   into the generic `getenv("HOME")`-based branch, which can never
   succeed on ESP32, silently making the documented `opendune.ini`
   override feature non-functional.
8. **`opendune.c`** - added `TDECK`-guarded boot-progress checkpoints
   (`TDeck_BootLog()`, implemented in `src/main.cpp`) at every major step
   of `main()`'s init sequence - `File_Init`, `Config_Read`/`Write`,
   `Drivers_All_Init`, `Font_Init`, `Video_Init`. Pure diagnostics, no
   behavioural change - see "Troubleshooting" below.
9. **`os/readdir.c`** - the actual root cause of the `File_Init` failure,
   confirmed via hardware testing: ESP-IDF's VFS/FATFS `opendir()`
   returns `EINVAL` for a path with a trailing slash, unlike most POSIX
   systems. The data-directory path is `/sdcard/dune2/` (trailing slash
   intentional - the code right after `opendir()` concatenates each
   filename directly onto it without inserting a separator), so
   `ReadDir_ProcessAllFiles()` now opens a slash-stripped *copy* of the
   path for the `opendir()` call only, leaving the original untouched for
   that concatenation. Also added `errno`/`strerror()` reporting (via
   `Error()`, which prints to Serial) on any future directory-open
   failure, so it explains *why* instead of just *that* it failed.

Everything else under `lib/opendune/src/` is the real engine, not
reconstructed or rewritten.

---

## 2. Where to put your Dune II data files

OpenDUNE is **only the engine** - it does not include Dune II's original
graphics/sound/level data, which is still copyrighted (Westwood/EA). You
need your own legally-obtained copy of the original **Dune II v1.07**
release. This is the exact same requirement as running OpenDUNE on a PC;
only the folder location changes.

**How file lookup actually works** (confirmed by reading `file.c`):
OpenDUNE doesn't hardcode which `.PAK` archives should exist - when it
looks up an asset (e.g. `MENTAT.SHP`), it transparently checks both loose
files and the contents of every `.PAK` file present in the data
directory. Practically, that means the instruction really is "copy your
entire original Dune II 1.07 installation in here", not "these exact N
files and no others."

### The files you need

Copy these into a folder named `dune2` at the **root of the SD card**
(so you end up with `/dune2/DUNE.PAK`, `/dune2/INTRO.PAK`, etc. - not
nested any deeper). This list is cross-referenced from OpenDUNE's own
wiki, the Dune Legacy project (which documents identical data
requirements), and a Raspberry Pi community thread specifically about
running OpenDUNE:

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
Check the speaker/I2S wiring and `dsp_tdeck.cpp` first.

---

## 3. Building

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

---

## 4. Installing via bmorcelli Launcher

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

**Returning to the Launcher**: I don't have confirmed details on
bmorcelli Launcher's convention for an installed app to exit back to the
launcher menu (button combo, reset, a specific API call, etc.). This isn't
wired up in `main.cpp` - check the Launcher's own example/template app for
the expected mechanism and add it to `main.cpp`'s `loop()` or `GameTask`
once you know it.

---

## 5. Controls

- **Trackball** moves the mouse cursor; **trackball click** is the left
  mouse button (Dune II's primary input). See Troubleshooting below - as
  of this version, clicks not registering in menus is an open issue with
  added diagnostics, not yet a confirmed fix.
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
  Attack.)
- **O** opens **O**ptions - but unlike the five above, this one *is* a
  deliberate remap: Dune II's actual native Options shortcut is F2, not
  'O' (confirmed by tracing `table/widgetinfo.c`), so pressing O sends
  F2's scancode directly rather than relying on a binding that doesn't
  exist in the original game.
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
`CHANGE` instead - exact polarity can vary by trackball module revision
and I didn't have hardware to confirm this against.

---

## 6. Troubleshooting

### Fixed: black screen on first boot

Two confirmed bugs, found by cross-checking against LilyGO's own official
T-Deck documentation:

1. **Wrong screen rotation.** `video_tdeck.cpp` called
   `tft.setRotation(0)`, which gives a *portrait* 240x320 active area on
   this panel - but every addressing calculation in that file (and the
   whole rest of the port) assumes *landscape* 320x240. LilyGO's own
   example code uses `setRotation(1)`; this project now does too
   (`TDECK_TFT_ROTATION` in `tdeck_config.h` - try `3` instead if the
   image comes up upside-down on your unit).
2. **Missing CS-line deselection.** The T-Deck's TFT, SD card, and LoRa
   radio all share one SPI bus. LilyGO's docs are explicit that every
   device's CS line must be driven high (deselected) before doing any SPI
   transaction, including ones you never intend to use (the radio, in our
   case) - an undriven CS can otherwise respond to bus traffic and
   corrupt the TFT/SD transactions. `main.cpp` now does this immediately
   after the peripheral power-on step.

### New: boot-progress splash screen

`main.cpp` now brings up the display and shows status text (peripheral
power, SPI bus, keyboard/trackball, SD mount, data files found, starting
engine) at each step of boot, *before* touching SD/I2C or handing off to
the engine at all. This serves two purposes: it proves the panel and
backlight work independently of everything else, and if a *future* build
hangs or fails somewhere, the last message left on screen tells you
exactly where - rather than an undifferentiated black screen. If you hit
a new hardware issue, please include a description (or photo) of what the
splash screen shows when reporting it.

### Fixed: missing first character on status lines right after SD access

The T-Deck's TFT and SD card share one SPI bus. The `S` in "SD card:
mounted" and the `D` in "Dune II data: found" coming out missing/garbled
was the SD library leaving its CS line asserted momentarily after a call
returns, corrupting the very first byte of the next TFT transaction.
`TDeck_BootLog()` (the function behind every boot-splash status line) now
explicitly forces the SD card's CS line high before drawing text.

### Music (V18)

Dune II's score now plays alongside voices and sound effects.

**How it works.** The engine's own sequencer (`mt32mpu.c`, already part of this
port) turns the music tracks into standard MIDI messages at 120 Hz and hands
them to a "MIDI backend". The old backend (`midi_none.c`) answered "no music
device", which switched the whole music path off. It is replaced by
`src/midi_tdeck.cpp`, which queues those messages (lock-free, game task ->
audio task) for a small software synthesizer (`src/tdeck_synth.h`: 24 voices,
16 channels, one oscillator+filter+envelope timbre per General-MIDI instrument
family, a synthesized drum kit on channel 10, volume/expression/pitch-bend/
sustain-pedal support). `src/dsp_tdeck.cpp` is now a continuous mixer: synth +
the current voice/effect clip are summed, soft-limited and streamed to the
speaker, which also removes any chance of the old stutter-after-a-clip bug.

**Honest expectations.** This is *not* a sampled General-MIDI synth: the tunes
are the right tunes, in time, but the sound is chiptune/FM-like rather than
Roland SC-55. A sample-based upgrade (e.g. TinySoundFont plus a small,
freely-licensed SoundFont on the SD card) is possible later; it needs a
SoundFont chosen for size (samples are held in RAM) and licensing, and would
plug into the same backend seam.

**Needs from your game data:** Dune II's General-MIDI tracks (`*.C55`, the
default) must be present in your PAK files. If music stays silent while
voices/effects work, your data may lack them; as a fallback you can set
`mt32midi=1` in `opendune.ini` to use the MT-32 (`.XMI`) tracks, but those use
MT-32 instrument numbering and will sound wrong on this synthesizer.

**Settings** (`src/tdeck_config.h`): `TDECK_ENABLE_MUSIC` (1/0; 0 restores
silent music exactly as before), `TDECK_MUSIC_VOLUME` (default 70),
`TDECK_AUDIO_SAMPLE_RATE` (raised 16000 -> 22050).

**Verification and its limits.** Run `tools/host_tests/run_all.sh` on any PC
with a C++ compiler: it builds the *real* source files against mock hardware.
The synth passes pitch-accuracy (zero-crossing) tests, envelope/release,
velocity/volume/expression, voice stealing under a 44-note pile-up, 5000
random MIDI messages, all 16 instrument families audible, pitch bend + RPN
range, sustain pedal, CC120/121/123 and reset; the mixer reproduces clip
resampling bit-exactly, returns to exact silence after a clip, keeps music
playing across clips, stays within 16-bit range, and the queue survives a
flood past its size. **Not verified:** how it *sounds* on the T-Deck speaker,
real-time CPU load on the ESP32 (about 4 ns per voice-sample on a PC, so
expected to be a modest share of one core, but unmeasured), and the engine's
music-file loading with your data.

### V18: trackball click root cause (probably), text entry, music

- **Trackball click: our own audio driver was hijacking GPIO0.** The I2S setup
  used `i2s_pin_config_t pins = {}`, which zero-fills the struct's *MCLK* pin
  field - and 0 is a real pin number. In master mode the ESP-IDF driver then
  routes the I2S master clock out to **GPIO0, the trackball click button**
  (Espressif's docs/example use `.mck_io_num = 0`, and a GitHub issue shows the
  driver logging "I2S0, MCLK output by GPIO0"). A pad driven by the I2S
  peripheral reads LOW whatever the switch does - exactly "100% LOW, never
  changes, on every unit". Fixed with `pins.mck_io_num = I2S_PIN_NO_CHANGE`;
  the input driver also re-claims the pin as a pulled-up input right before
  taking its idle baseline. **Unverified on hardware** - the next log should
  show `[trackball] raw click pin` changing level when you press it. (If it
  still doesn't, send that log; the earlier RTC-mode idea stays in as a
  harmless extra.) The host test asserts no I2S signal is mapped to GPIO0.
- **Typing in the save-game / high-score / copy-protection prompts.** Our remaps
  (IJKL = cursor, Enter/Space = click, Backspace = right-click, O = F2)
  swallowed I/J/K/L/O, Space and Enter in text boxes, so a high-score name
  couldn't be finished (soft-lock). All three prompts use one engine function,
  `GUI_EditBox`, which sets `INPUT_FLAG_NO_TRANSLATE` only while open; while
  that flag is set the driver behaves as a plain keyboard (every letter,
  digits, a real Enter/Backspace/Space) and synthesizes Right-Shift around
  capitals and shifted symbols so case is correct. The normal bindings return
  as soon as the prompt closes. Simulated on a PC; not tried on hardware.
  (Typing the same letter twice very quickly can be missed: the keyboard chip
  reports a byte, not key-up.)
- **Host tests ship with the project** in `tools/host_tests/` (PlatformIO
  ignores that folder) so the verification above can be re-run by anyone.

### V17: radar drag + "press Enter twice" fixed (one root cause); Space = click; RTC experiment

**Root cause (my own bug from V15), confirmed in the V16 log:** all 261
trackball-movement events carried `left=1`, i.e. the engine was told the
left button was held down for the whole session. V15's "suppress phantom
clicks" change stopped the repeating pulses but left `s_leftDown` latched
`true` from the single boot-time "click -> DOWN" (the click pin reads LOW
on its very first sample, which the old logic took as a press).
- **Radar:** hovering it looked like a click-and-drag, so the view
  scrolled with the cursor and kept doing so after leaving the radar.
- **Enter twice:** the log shows `trackball-normal left=1` ->
  `Enter-click-down left=1` (no state change, so no press event - swallowed)
  -> `Enter-click-up left=0` (a lone release). Only the second Enter, with
  the engine back at "released", was a real press+release.

**Fix (`TDeck_PollInput` in `src/input_tdeck.cpp`):**
- The first reading of the click pin is a *baseline* ("idle"), not a
  press; a press is any deviation from it (so it also works whichever way
  the switch is wired). The repeating "stuck pin" pulse generator is
  removed (it would have turned any genuine hold >400ms, e.g. drag-select,
  into repeated clicks).
- Keyboard clicks are non-blocking: DOWN immediately, UP ~50ms later from
  a later poll, and movement events during that window keep reporting the
  button as held (so they can't inject a stray release mid-click).
- **Space** is now the same left click as Enter (no raw keypress sent).

**Verification, and its limits:** the patched file compiles cleanly
(-Wall -Wextra) against stub Arduino/ESP-IDF headers, and a host-side
simulation that includes the real file and mocks time/GPIO/I2C/the engine
confirms: with the pin stuck LOW at boot, no movement event reports the
button held; Enter/Space = exactly one press + one release ~50ms apart; a
second Enter is a complete click; Backspace = right click only; a genuine
pin-level change = press then release. The same simulation run against V16
reproduces the bug (58 of 61 movement events `left=1`). It has **not** been
run on hardware.

**Trackball press still unverified:** the pin has read LOW 100% of the time
on both tested units regardless of the button. References checked: the
MeshPunk Doom log shows Doom is a guest ELF using the *launcher's own*
trackball driver (`[elf_host] trackball: ...`) - not something a standalone
firmware can borrow; retro-go's T-Deck target reads the same button as
`GPIO_NUM_0, pullup, active-low` with a 2-sample/10ms debounce - same pin
and polarity as ours, so neither pin choice nor debouncing explains it.
One lead (ESP32-S3 forum reports of GPIO0 not acting as a normal input
after boot): `TDeck_Input_Init()` now calls `rtc_gpio_deinit(GPIO_NUM_0)`
before configuring the pin. **Unverified hypothesis.** Check the next log's
`[trackball] raw click pin` and `click idle baseline` lines to see if the
pin ever changes level when pressed.

### Trackball click: power-delay theory disproven; phantom clicks fixed, real clicks still don't register

The longer peripheral power-up delay didn't help - the raw click pin read
`LOW(pressed)` in **100% of ~70+ samples across the entire session**,
with zero variation whether you were pressing it or not. That's stronger
than "stuck" - it means the physical signal isn't responding to the
button at all, which also rules out a simple polarity inversion fix (that
would still show *some* change, just interpreted backwards). The
"never seen HIGH" suppression added last round worked as intended -
phantom clicking stopped - but as an unavoidable side effect, genuine
presses can't register either while the signal itself never moves. I
don't have a further software fix to offer here without more data; this
may need physical investigation on your end (continuity-checking the
actual click switch/pin, or comparing against LilyGO's current reference
schematic for your specific board revision) since it's now confirmed
consistent across two independent units, ruling out a one-off defect.

### Fixed: O key now opens Options; investigating the Enter-twice issue

Traced Dune II's actual native shortcuts in `table/widgetinfo.c`:
Options is bound to shortcut value 113, which resolves (via the engine's
own scancode translation table) to **F2** - never 'O'. Your request made
sense, the game just doesn't agree. Remapped O to send F2's scancode
directly, so it now does what you asked for.

For hotkeys needing a second Enter after selecting a unit: Enter
previously sent both a click pulse *and* its raw scancode (0x1C) - the
scancode was speculative "in case some menu wants it," added without
confirmed need. Removed it as a low-risk experiment, since the click
alone already performs selection correctly. If hotkeys work normally on
the first press now, that confirms the extra scancode was interfering
with the engine's input state right after a click; if the issue persists,
it's something else and we'll need to look further.

### Confirmed with real data: trackball click pin reads LOW from the very first read, on two separate physical units

The USB CDC fix worked - full diagnostic visibility at last. The log
showed `[trackball] raw click pin = LOW(pressed)` immediately, before any
user interaction, and `[MOUSE-EVENT from trackball-normal]` /
`[MOUSE-EVENT from stuck-pin-pulse-down/up]` confirmed the phantom clicks
are 100% trackball-sourced - not keyboard, not touch, not the engine's
own native key-to-click mechanism (which was a real but, it turns out,
inactive finding from investigating this). The debounce/stuck-pin code
has been behaving exactly as designed the whole time; the GPIO is
genuinely reporting "pressed" from the start.

Getting the same result on a **second, independent T-Deck** is important
information: it argues against a one-off defective unit and toward
either a systematic firmware issue or a universal characteristic of how
the T-Deck's trackball click circuit behaves that this port hasn't
accounted for correctly. Two changes this round, aimed at a unified
explanation (keyboard input also failing completely is the other half of
this - see below): both the keyboard chip and the trackball's click
circuit failing together, on two separate units, while trackball
*rotation* keeps working, is consistent with those two needing more time
to power up/stabilize than rotation does.
- Raised the peripheral power-on settling delay from 200ms to 500ms in
  `main.cpp` - a cheap, direct test of that theory.
- `TDeck_PollInput()` now tracks whether the click pin has *ever* read
  HIGH since boot. If it never has, the repeating stuck-pin auto-click
  pulse is suppressed entirely (logged once, then silent) rather than
  generating disruptive phantom clicks every 600ms - still not a real
  fix, but a much less disruptive state to be in while this gets
  narrowed down further. A single spurious click right at boot is still
  expected and currently unavoidable (the first LOW reading is
  indistinguishable from a genuine press), but it should stop repeating
  after that.

### Fixed (reverted a bad fix of my own): keyboard stopped registering input entirely

Caused by me: last round's keyboard "hardening" (requiring `requestFrom()`
to return exactly 1, and the same byte confirmed on 2 consecutive polls)
was built on the theory that keyboard read noise was causing the phantom
clicks. That's now disproven by the trackball-focused data above - the
clicks were never keyboard-related - and that same hardening apparently
made it impossible to ever confirm a genuine keypress on this keyboard
chip, breaking input entirely. Reverted to the simpler approach proven
working several rounds ago (trust `Wire.available()`, de-duplicate only
on "byte differs from the last one processed"). Also added always-on,
rate-limited raw I2C logging (`[kbd] raw I2C state: ...`) so if this is
still broken, the next log will show definitively whether the I2C
transaction itself is failing, succeeding but always reading 0, or
reading something unexpected.

**Please test again and send the full log** - specifically interested in
whether `[kbd] raw I2C state` ever shows a nonzero `raw=` value when you
press a key, and whether `[trackball] raw click pin` ever flips to
`HIGH(released)` at any point after the longer power-up delay.

### Likely explanation found: main.cpp's own Serial output never appearing

The tracing wrapper from last round should have made the phantom-click
source undeniable - but the resulting log showed zero `[MOUSE-EVENT ...]`
lines at all, *and*, on closer inspection, zero trace of any of
`main.cpp`'s own boot messages either ("Peripheral power: OK" etc.),
despite those being unconditional and present in every build for many
rounds. Confirmed this wasn't a stale-build issue (fresh extraction every
time). The actual explanation: `Warning()`/`Error()` (which do show up,
via `vfprintf(stderr, ...)`) and `main.cpp`'s own `Serial.println()`/
`Serial.printf()` calls are two *different* output mechanisms. On an
ESP32-S3 board with native USB and no separate UART bridge chip (like the
T-Deck), Arduino's `Serial` object defaults to a hardware UART that may
not be connected to anything reachable, while the engine's console output
goes out over the native USB port your monitor is actually reading -
unless `ARDUINO_USB_CDC_ON_BOOT` is explicitly set. It never was. Added
`-DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=1` to `platformio.ini`.

**This should make every diagnostic added in every previous round finally
visible** - the trackball/keyboard/nav tags, and especially last round's
`[MOUSE-EVENT from ...]` tracing wrapper, which should now finally reveal
the true source of the phantom clicks. Please test again and send the
full log.

**Known tradeoff**: some ESP32-S3 boards/core versions delay booting
until a USB host actually connects when this flag is set. If OpenDUNE
stops starting standalone (only boots once a PC/monitor is attached),
that's why - flag it and we'll revisit once we're done needing
guaranteed Serial visibility for debugging.

### Investigating: phantom clicks - previous fix didn't help, deeper trace in progress

Your latest log ruled out both of the previous two suspects definitively:
zero `[trackball]` lines and zero `[kbd]`/`[nav]` lines anywhere, despite
404 `Mouse_CheckButtons` calls showing the same repeating click pattern.
Since every one of *our* code's calls into the engine's mouse handling
already had its own logging, and none of it ever fired, the previous
fixes (keyboard read hardening, stuck-pin pulse) were addressing real
bugs, but not *this* one.

Along the way this traced into a genuinely interesting piece of the
engine's own design: `INPUT_FLAG_KBD_MOUSE_CLK` is a built-in,
intentional mechanism (active by default on every platform, not
something we caused) where certain keys - Enter among them, confirmed by
following the engine's own scancode translation table - act as a left
click automatically. That turned out not to be the active mechanism
here (the log data shows all 144 real click events are immediately
preceded by a matching `Mouse_CheckButtons` call, meaning they
genuinely come from the mouse path, not this keyboard mechanism) - but
it's a real behavior worth knowing about regardless, and confirms our
own explicit Enter-click handling in `TDeck_HandleNavigationKey` is
redundant with the engine's own native handling (harmless, just
duplicated).

Since exhaustive per-call-site logging still didn't reveal the source,
this version takes a different, foolproof approach: every single call
into `Mouse_EventHandler` anywhere in `input_tdeck.cpp` - all 9 of
them, covering trackball, WASD, Enter/Backspace, touch, and the
stuck-pin safety net - now goes through one traced wrapper function
that logs which specific one fired, every time, with no possibility of
a gap in coverage. **Please test again and send the log** - a line like
`[MOUSE-EVENT from trackball-normal] ...` or
`[MOUSE-EVENT from Enter-click-down] ...` will tell us definitively,
for the first time, exactly which code path is generating these clicks.

### Fixed (hopefully) again: phantom clicks persisted after the last fix

Your last log showed the previous fix (de-dup on "byte changed since last
read") wasn't enough - the pattern was a clean, repeating press/release
cycle, and critically, you reported it happening *even without touching
the keyboard at all*. That points to I2C read instability/noise, not a
genuinely held key: a read that flickers between a byte value and 0 on
alternating polls defeats simple "changed since last time" de-duplication,
since each flicker looks like a fresh value. Also found: `Wire.available()`
alone doesn't guarantee the keyboard chip actually acknowledged and sent
real data - `requestFrom()`'s return value was never checked. Two
hardening measures in `TDeck_PollKeyboard()` now: it validates that
`requestFrom()` actually got the requested byte back, and requires the
same non-zero byte on 2 consecutive polls (~30ms apart) before trusting
it as a real keypress - random noise is very unlikely to repeat the exact
same byte twice in a row, while a genuine press easily holds that long.
Added logging (`[kbd] confirmed raw=...` and `[nav] left/right-click
pulse...`) so the next test gives definitive proof either way.

### Changed: trackball sensitivity increased

`TDECK_TRACKBALL_STEP` in `tdeck_config.h` doubled (1 -> 2) per request -
easy to tune further in that one spot if still not enough.

### Fixed (from real log evidence): phantom continuous clicks + audio stutter after voice lines

Two separate, now well-evidenced bugs:

1. **Phantom clicks/selections happening on their own.** Your log showed
   rapid, continuous press/release cycles with zero `[trackball]`
   diagnostic lines anywhere alongside them - proof the trackball's own
   code wasn't the source (it always logs before acting). The real cause:
   last round's keyboard fix (polling unconditionally instead of gating
   on the INT pin) uncovered a different problem - this keyboard chip
   apparently keeps returning the same non-zero byte on every I2C read
   for as long as (or longer than) a key is held, rather than clearing
   to 0 once consumed. Combined with polling every ~30ms, a single
   keypress (likely Enter, from your earlier WASD/Enter test) was being
   processed as a brand-new click roughly 30 times a second, forever.
   Fixed with edge-detection on the byte value itself in
   `TDeck_PollKeyboard()`: a byte is only treated as a new keypress if it
   differs from the last one seen. A genuine repeat of the same key still
   works correctly, since the chip presumably returns 0 while the key is
   up in between, which resets the tracking.
2. **Stuttering audio after voice lines finish.** I2S peripherals in
   continuous DMA mode keep cycling/repeating whatever was last in their
   buffer once new samples stop being written - `i2s_write()` only ever
   adds data, it doesn't silence anything on its own. `dsp_tdeck.cpp` now
   calls `i2s_zero_dma_buffer()` immediately after a clip finishes
   playing, which explains why the stutter "changed slightly depending on
   what was last played" - it was quite literally still playing (a
   repeating loop of) the tail end of it.

The trackball click issue from last round (stuck reporting "held down")
is unrelated to both of the above and still needs its own fix/more data -
now that phantom keyboard clicks are out of the way, the next test should
show trackball click behavior more clearly on its own.

### Fixed: stale frame content ("ghost" of previous scene visible under new one)

`Video_Tick()` was only redrawing the specific sub-rectangle the engine
reports as changed (a performance optimization). Apparently some scene
transitions don't mark the full screen dirty even though the old content
needs fully replacing, leaving stale pixels on screen (e.g. the top half
still showing the Westwood Studios logo under the main menu). Fixed by
having `Video_Tick()` always redraw the entire screen whenever anything
is dirty, trading away that optimization for correctness -
`TDECK_VIDEO_ALWAYS_FULL_REDRAW` in `tdeck_config.h` if you want to
experiment with reverting it later once this is solved some other way.
Full 320x200 redraws are still comfortably fast at the configured SPI
clock.

### Status: intro cutscene not playing

Not investigated further per your direction (not a priority right now).
Worth noting it may be related to the frame-artifact bug just above,
since cutscenes are WSA-animation-driven and rely on the same dirty-rect
reporting - if it starts happening differently after this fix, that's
why. Flag it again if it matters once we're past the input issue.

### In progress: menu selection still not working (trackball OR keyboard)

Important new data point from your last report: since WASD/Enter -
implemented completely independently of the trackball's GPIO hardware,
driving the exact same `Mouse_EventHandler()` call - *also* don't select
anything, the bug is not trackball/hardware-specific. It's somewhere in
how click events flow through to the menu system once they reach the
engine, which is shared code across every input path.

Traced deep into the actual mechanism (`Mouse_CheckButtons` ->
`Input_HandleInput` -> a bit-packed input-history queue ->
`GUI_Widget_HandleEvents`) - this is genuinely complex, cryptic,
DOS-era code, and I found a specific dedup/"repeat" check in
`Input_HandleInput` that can silently discard a queued click event under
certain conditions, plus a separate mechanism (`g_mouseClickX`/`Y`) that
captures the click position through an indirect path rather than using
the live cursor position directly. I couldn't conclusively confirm
either is the actual bug by reading code alone, so rather than keep
guessing, this version adds detailed `Warning()`-based diagnostic
logging (visible on Serial) at every relevant step: the button state
change computed by `Mouse_CheckButtons`, whether the dedup/rollback check
fires, and the mouse position captured for the click.

**Please test and send the Serial log covering a single trackball-click
attempt on a menu item** (or a WASD+Enter attempt) - the
`[Mouse_CheckButtons]` and `[Input_HandleInput]` lines will show exactly
what the engine computed, which should make the actual bug obvious
instead of requiring more speculation.

### Fixed (probably): wrong colors during cutscenes/menus

Purple/blue/green hues where Dune II's actual palette is mostly desert
oranges/tans strongly suggests a red/blue channel swap - a well-documented
TFT_eSPI pitfall when feeding it manually-computed RGB565 values via
`pushPixels()` (its own drawing functions handle this internally; raw
pixel pushes need it set explicitly). Two independent fixes are now
applied by default in `tdeck_config.h`:
- `TDECK_TFT_SWAP_RB` - swaps red/blue when building the palette LUT
  (for panels wired BGR instead of RGB)
- `TDECK_TFT_SWAP_BYTES` - calls `setSwapBytes(true)`, which TFT_eSPI's
  own header explicitly documents as needed for `pushPixels()`

I can't verify color output without hardware, so **if colors are still
wrong, try flipping one or both of these flags** (0/1) and rebuilding -
that's a config change, not a code change, so it's a fast thing to try
a few combinations of.

### Superseded: earlier trackball-click investigation

(Kept for history - see "In progress: menu selection still not working"
above for the current state.) Ruled out: the click button's
`pinMode`/polarity setup (`INPUT_PULLUP`, active-low) exactly matches
LilyGO's own official T-Deck example code, and switched the trackball's
movement interrupts from `FALLING` to `CHANGE` to match their examples
too (a latent difference, not necessarily related to the click issue).
The `[trackball] click -> DOWN`/`UP` Serial line from that round confirms
whether the GPIO-level press is detected at all; the new
`[Mouse_CheckButtons]`/`[Input_HandleInput]` lines added this round go
further and trace what happens to that click once it reaches the engine.

### Added: WASD + Enter + Backspace navigation

W/A/S/D move the cursor, Enter left-clicks, Backspace right-clicks - all
implemented by driving the same shared cursor state the trackball uses
(not the engine's own built-in numpad-based mouse-emulation mode, which
would have disabled real mouse/trackball clicks entirely while active -
see the comment above `TDeck_HandleNavigationKey()` in
`input_tdeck.cpp` for why). These work simultaneously alongside the
trackball, not as an alternative to it.

### (History) Status: no music - superseded by V18, see "Music"

This remains unimplemented, and I want to be upfront about scope before
attempting it: Dune II's music goes through a real MIDI sequencer
(`mt32mpu.c`, which this port already restored and uses - see patch #6)
that expects an actual synthesizer to turn MIDI events into audio. On
desktop platforms this leans on the OS's own MIDI services (ALSA/CoreMIDI
sequencer, a hardware MT-32, etc.); there's no equivalent to lean on
here. Making music work means writing a software synthesizer from
scratch and feeding its output into `dsp_tdeck.cpp`'s I2S pipeline - a
substantially bigger undertaking than anything else in this project so
far, with real risk of needing several hardware-test iterations to get
right (I have no way to verify audio quality without your testing).
Sound effects and voice lines are unaffected by this and already work,
confirmed by your report. Let me know if you'd like me to take this on
as a dedicated next task, or hold off while we finish the input/display
fixes above.

### Fixed: File_Init failure / freeze after "Starting OpenDUNE..."

Root cause confirmed via hardware testing (thank you for the exact
`errno` output - it was decisive here): ESP-IDF's VFS/FATFS `opendir()`
returns `EINVAL` (errno 22) for a directory path ending in `/`, which is
different from how most POSIX systems behave. The engine's data
directory path is constructed as `/sdcard/dune2/` - with a trailing
slash by design, since the very next lines of code concatenate each
filename directly onto that path without inserting a separator. Fixed in
`os/readdir.c`: `opendir()` now receives a slash-stripped copy of the
path, while the original (with its trailing slash) is left untouched for
that later concatenation to keep working correctly.

### Diagnosed: freeze at "Starting OpenDUNE..."

The boot splash only covered `main.cpp`'s own setup - once
`xTaskCreatePinnedToCore` hands off into the engine's real `main()`
(opendune.c), there was no visibility at all into what happens next, and
several steps in there involve file I/O that behaves differently on
ESP32's VFS than on a desktop OS. This version adds status checkpoints
(mirrored to both the splash screen *and* Serial) at every major step
between entering `main()` and the first frame actually rendering:
`File_Init`, `Config_Read`/`Write`, `Drivers_All_Init`, and - the most
likely actual culprit - `Font_Init()` inside `OpenDune_Init()`, which
loads `new8p.fnt` from inside a `.PAK` archive and is the very first
thing that touches your actual game data. If this fails, the engine
calls `exit(1)`, which can behave like a fast, silent reboot loop on
ESP32/Arduino - visually indistinguishable from a hang, since the splash
redraws identically each time, too fast to notice.

**Please test again and report back two things**: what the splash screen
shows now (it should get further than "Starting OpenDUNE..." and show
one of the new checkpoint lines), and the Serial monitor output
(`pio device monitor`, 115200 baud) from power-on through the freeze -
the checkpoints print there too, and if `File_Init` or `Font_Init` fail,
the underlying `Error()` message (e.g. "Cannot initialise files" or
similar) will appear on Serial even if it scrolls off the small screen.

### Fixed: header collisions breaking TFT_eSPI/LittleFS compilation

An earlier version of this project's `platformio.ini` exposed the engine's
subdirectories (`lib/opendune/src/os`, `.../audio`, etc.) as global
`-I` flags. PlatformIO applies global `build_flags` to *every* library in
the project, and the engine has files literally named `math.h`,
`string.h`, and `strings.h` - these shadowed the real C/C++ standard
headers for unrelated code (TFT_eSPI, LittleFS), causing errors like
`'constexpr' static data member 'min' must have an initializer` deep in
`<limits>`/`<algorithm>`, or `'uint16' does not name a type` inside
`string.h`. This is fixed as of the version in this archive (see patch #5
above) - if you're working from an older copy, re-download or apply the
same fix: rename those four files and remove the subdirectory `-I` flags.

### Possible next issue: I2S API version mismatch

`dsp_tdeck.cpp` uses the legacy `driver/i2s.h` API (`i2s_driver_install`,
`i2s_set_pin`, `i2s_write`). This is stable on arduino-esp32 2.0.x
(bundled with older `espressif32` platform releases) but restructured in
3.x/IDF5 cores into `ESP_I2S.h`. If your build resolves a 3.x core (check
the framework path in your build log, e.g.
`framework-arduinoespressif32@3.x.x`) and fails inside `dsp_tdeck.cpp`
with `driver/i2s.h` errors, that's why - either pin an older
`espressif32` platform version that resolves a 2.0.x core, or share the
error and `dsp_tdeck.cpp` can be rewritten against the newer `ESP_I2S.h`
API.

## 7. Known limitations / things to verify on real hardware

This was built by reading OpenDUNE's actual source in detail (verified:
the 320x200 8bpp framebuffer + dirty-rect model, the 6-bit VGA palette
format, the VOC sound format, the XT-scancode input format, the
single-thread engine loop) and LilyGO's official pin definitions - but it
has **not been compiled or run on a real T-Deck**. Specific things to
watch for on first boot:

- **General compile sanity.** This is a large, from-scratch integration;
  budget time for the normal first-build iteration of fixing missed
  include paths or minor API mismatches against whatever exact
  arduino-esp32/TFT_eSPI/SensorLib versions PlatformIO resolves.
- **SPI bus sharing.** TFT_eSPI performs its own internal `SPI.begin()`
  using the pins in `User_Setup_TDeck.h`; `main.cpp` also begins the same
  bus first (for the SD card's benefit). This should be harmless
  (re-beginning the same pins is a no-op in arduino-esp32) but hasn't been
  hardware-verified.
- **Trackball interrupt polarity** (see above).
- **Keyboard special keys** (see above) - arrows/F-keys are unmapped by
  default until you confirm the byte codes.
- **Screen orientation** - fixed in this version (see Troubleshooting
  above); `TDECK_TFT_ROTATION` in `tdeck_config.h` defaults to `1`
  (LilyGO's own documented value). Try `3` if your unit shows the image
  upside-down.
- **Possible ST7789 init-sequence quirk** - LilyGO's own T-Deck
  documentation states "T-Deck updated the TFT_eSPI ST7789 initialization
  sequence on 2024-07-26. If you encounter incorrect screen display,
  check that the initialization sequence in the repo matches," implying
  some production units may need panel-specific init commands beyond
  what the stock `bodmer/TFT_eSPI` library sends via the standard
  `ST7789_DRIVER` setting. This project uses the stock library. If the
  boot splash still doesn't appear after the fixes in this version (with
  correct wiring/rotation), try LilyGO's own bundled TFT_eSPI fork/example
  from https://github.com/Xinyuan-LilyGO/T-Deck as a comparison point.
- **Audio quality** - `dsp_tdeck.cpp` does simple nearest-neighbour
  resampling from Dune II's native ~6-11kHz VOC clips up to 16kHz for I2S
  output. It's meant to be correct and audible, not hi-fi.
- **Music is a software approximation, not the original sound.** See the
  "Music" section: the notes/timing are Dune II's own, but the instruments
  are simple synthesized timbres rather than Roland SC-55 samples. It is
  tested on a PC (synth, mixer, queue) but **has never been heard on real
  hardware** - audio quality and CPU headroom on the device are unverified.
- **Partition/launcher integration** (see section 4) - the exact
  partition subtype and "return to launcher" convention bmorcelli expects
  weren't confirmed against its source/docs.

None of the above are architectural problems - they're exactly the kind of
first-boot bring-up issues you'd expect for any new embedded target, and
the engine integration underneath them (file I/O, timing, rendering,
input plumbing into the actual game code) is grounded directly in
OpenDUNE's real source rather than guesswork.

---

## 8. Legal

OpenDUNE itself is GPLv2. **Dune II's original data files are not
included here and remain copyrighted** (Westwood Studios/Electronic Arts).
You must supply your own legally-obtained copy, exactly as with any other
OpenDUNE platform build.
