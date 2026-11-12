#pragma once
#include <stdint.h>
struct SPISettings { SPISettings(uint32_t=0,uint8_t=0,uint8_t=0){} };
struct SPIClass { void begin(int=-1,int=-1,int=-1,int=-1); void end(); void beginTransaction(SPISettings); void endTransaction(); uint8_t transfer(uint8_t); void transfer(void*,uint32_t); void setBitOrder(uint8_t); void setDataMode(uint8_t); void setFrequency(uint32_t); };
extern SPIClass SPI;
#define SPI_MODE0 0
#define SPI_MSBFIRST 1
#define SPI_LSBFIRST 0
