#pragma once
// Fuller Arduino surface than stubs/Arduino.h: just enough for the REAL SensorLib headers to compile.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stddef.h>
typedef bool boolean; typedef uint8_t byte;
#define IRAM_ATTR
#define INPUT 1
#define OUTPUT 2
#define INPUT_PULLUP 5
#define LOW 0
#define HIGH 1
#define FALLING 2
#define RISING 1
#define CHANGE 3
#define LSBFIRST 0
#define MSBFIRST 1
#define HEX 16
#define DEC 10
#define log_i(...) do{}while(0)
#define log_e(...) do{}while(0)
#define log_d(...) do{}while(0)
#define log_w(...) do{}while(0)
struct Print { size_t print(const char*){return 0;} size_t println(const char*){return 0;} size_t print(int,int=10){return 0;} size_t println(int,int=10){return 0;} size_t println(){return 0;} size_t printf(const char*,...){return 0;} };
struct SerialStub : Print {}; extern SerialStub Serial;
uint32_t millis(); void delay(uint32_t); void delayMicroseconds(unsigned int);
int digitalRead(int); void digitalWrite(int,int); void pinMode(int,int);
void attachInterrupt(int, void(*)(), int); int digitalPinToInterrupt(int);
void noInterrupts(); void interrupts();
static const uint8_t SDA = 8; static const uint8_t SCL = 9;   // esp32-s3-devkitc-1 variant defaults
