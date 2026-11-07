// Mocks for the input driver tests: time, GPIO, I2C keyboard, and the engine's mouse/keyboard entry points.
#include <Arduino.h>
#include <Wire.h>
#include <driver/rtc_io.h>
#include <vector>
SerialStub Serial; TwoWire Wire;
uint32_t g_ms = 0; int g_clickLevel = 0; int g_kbd = 0; int g_kbdAvail = 0; int g_rtcCalls = 0;
void SerialStub::printf(const char*, ...) {} void SerialStub::println(const char*) {}
uint32_t millis() { return g_ms; } void delay(uint32_t) {}
int digitalRead(int pin) { return pin == 0 ? g_clickLevel : 1; }     // pin 0 = trackball click
void pinMode(int,int){} void attachInterrupt(int, void(*)(), int){} int digitalPinToInterrupt(int p){return p;}
void noInterrupts(){} void interrupts(){}
void TwoWire::begin(int,int){}
uint8_t TwoWire::requestFrom(int, uint8_t){ g_kbdAvail = 1; return 1; }
int TwoWire::available(){ int a = g_kbdAvail; g_kbdAvail = 0; return a; }
int TwoWire::read(){ return g_kbd; }
int rtc_gpio_deinit(gpio_num_t){ g_rtcCalls++; return 0; }
uint16_t g_tdeckMouseMinX=0,g_tdeckMouseMaxX=319,g_tdeckMouseMinY=0,g_tdeckMouseMaxY=199,g_tdeckMouseX=160,g_tdeckMouseY=100;
struct Ev { uint32_t t; int l, r; unsigned x, y; };
std::vector<Ev> g_events; std::vector<int> g_keys;
extern "C" uint16_t g_inputFlags = 0;
extern "C" void Mouse_EventHandler(uint16_t x, uint16_t y, bool l, bool r) { g_events.push_back({g_ms, l, r, x, y}); }
extern "C" void Input_EventHandler(uint8_t k) { g_keys.push_back(k); }
