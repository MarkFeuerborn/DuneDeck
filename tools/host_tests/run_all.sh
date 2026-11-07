#!/bin/bash
# Host-side verification suite: runs the REAL src/*.cpp on a PC against mock hardware.
# No ESP32, PlatformIO or Arduino needed - just a C++11 compiler.   usage: ./run_all.sh
# NOT run on a real T-Deck: it can show the logic is right, not how it sounds or feels.
set -e
cd "$(dirname "$0")"
CXX=${CXX:-g++}
F="-std=gnu++11 -Istubs -I../../src -DTDECK -DTDECK_HAS_TOUCH=0"
mkdir -p build
echo "[1/7] strict standalone compile of our drivers (-Wall -Wextra)"
for f in input_tdeck dsp_tdeck midi_tdeck; do $CXX $F -Wall -Wextra -Wno-unused-parameter -fsyntax-only ../../src/$f.cpp; echo "      $f.cpp: clean"; done
echo "[2/7] synthesizer";                 $CXX $F -O2 -Wall -Wextra test_synth.cpp -o build/synth && build/synth | tail -1
echo "[3/7] audio mixer / MIDI backend / I2S pins"; $CXX $F -w test_audio.cpp mocks_audio.cpp -o build/audio && build/audio | tail -1
echo "[4/7] clicks, pin stuck LOW at boot";  $CXX $F -w test_input_clicks.cpp mocks_input.cpp -o build/clicks && build/clicks stuckLow | tail -1
echo "[5/7] clicks, healthy pin idling HIGH"; build/clicks normalHigh | tail -1
echo "[6/7] text entry vs normal bindings";  $CXX $F -w test_text_entry.cpp mocks_input.cpp -o build/text && build/text | tail -1
echo "[7/7] music-off fallback";             $CXX $F -w -DTDECK_ENABLE_MUSIC=0 test_music_off.cpp -o build/off && build/off
echo "ALL HOST TESTS PASSED"
