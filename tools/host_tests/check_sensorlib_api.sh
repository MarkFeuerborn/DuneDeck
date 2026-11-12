#!/bin/bash
# Compiles src/input_tdeck.cpp against the REAL lewisxhe/SensorLib v0.1.9 headers - the exact version
# platformio.ini's "^0.1.9" resolves to - instead of the host stand-in used by the behaviour tests.
# That proves the library calls we make (setPins / begin / setMaxCoordinates / setSwapXY /
# setMirrorXY / getPoint) exist with those signatures. Needs git and network access.
set -e
cd "$(dirname "$0")"
mkdir -p build
[ -d build/SensorLib-0.1.9 ] || git -c advice.detachedHead=false clone -q --branch v0.1.9 --depth 1 https://github.com/lewisxhe/SensorLib.git build/SensorLib-0.1.9
${CXX:-g++} -std=gnu++11 -fsyntax-only -DARDUINO -DTDECK -DTDECK_HAS_TOUCH=1 \
    -Istubs_sensorlib -Ibuild/SensorLib-0.1.9/src -I../../src ../../src/input_tdeck.cpp
echo "      input_tdeck.cpp compiles against the REAL SensorLib v0.1.9 headers"
