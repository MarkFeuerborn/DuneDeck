#pragma once
// Host stand-in for lewisxhe/SensorLib v0.1.9's TouchDrvGT911. The method names/signatures used by
// input_tdeck.cpp are verified against the REAL v0.1.9 headers by check_sensorlib_api.sh; the
// coordinate transform below copies the real library's order (swap first, then mirror using max).
#include <stdint.h>
#include <Wire.h>
struct HostTouchState {
    bool pressed; int nx, ny;            // the finger, in the controller's NATIVE frame
    bool beginOk; int beginAddr, beginCalls, getPointCalls;
    int maxX, maxY; bool swap, mx, my;
    bool ack5D, ack14; uint8_t lastProbe;
};
extern HostTouchState g_ts;
class TouchDrvGT911 {
public:
    void setPins(int, int) {}
    bool begin(TwoWire&, uint8_t addr, int = -1, int = -1) { g_ts.beginCalls++; g_ts.beginAddr = addr; return g_ts.beginOk; }
    void setMaxCoordinates(uint16_t x, uint16_t y) { g_ts.maxX = x; g_ts.maxY = y; }
    void setSwapXY(bool s) { g_ts.swap = s; }
    void setMirrorXY(bool x, bool y) { g_ts.mx = x; g_ts.my = y; }
    uint8_t getPoint(int16_t* xa, int16_t* ya, uint8_t) {
        g_ts.getPointCalls++;
        if (!g_ts.pressed) return 0;
        int x = g_ts.nx, y = g_ts.ny;
        if (g_ts.swap) { int t = x; x = y; y = t; }
        if (g_ts.mx) x = g_ts.maxX - x;
        if (g_ts.my) y = g_ts.maxY - y;
        xa[0] = (int16_t)x; ya[0] = (int16_t)y; return 1;
    }
};
