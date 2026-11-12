#pragma once
// NOTE: deliberately does NOT include <stdlib.h> so that our own files must include what they use.
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define IRAM_ATTR
#define INPUT 1
#define OUTPUT 2
#define INPUT_PULLUP 5
#define FALLING 2
#define CHANGE 3
#define LOW 0
#define HIGH 1
struct SerialStub { void printf(const char*, ...) __attribute__((format(printf,2,3))); void println(const char*); };
extern SerialStub Serial;
uint32_t millis(); void delay(uint32_t); int digitalRead(int); void pinMode(int,int);
void attachInterrupt(int, void(*)(), int); int digitalPinToInterrupt(int);
void noInterrupts(); void interrupts();
