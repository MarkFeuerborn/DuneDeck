#pragma once
#include <stdint.h>
#include <stddef.h>
struct TwoWire {
  bool begin(); bool begin(int sda, int scl, uint32_t freq = 0); void end();
  void beginTransmission(uint8_t); uint8_t endTransmission(bool = true);
  size_t write(uint8_t); size_t write(const uint8_t*, size_t);
  uint8_t requestFrom(uint8_t, uint8_t, uint8_t = true); uint8_t requestFrom(int, uint8_t); int available(); int read();
  void setClock(uint32_t); size_t readBytes(uint8_t*, size_t); size_t readBytes(char*, size_t);
};
extern TwoWire Wire;
