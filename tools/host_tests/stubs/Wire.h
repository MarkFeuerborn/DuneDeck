#pragma once
#include <stdint.h>
struct TwoWire { void begin(int,int); void beginTransmission(uint8_t); uint8_t endTransmission(bool = true); uint8_t requestFrom(int, uint8_t); int available(); int read(); };
extern TwoWire Wire;
