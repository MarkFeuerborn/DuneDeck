// Touchscreen behaviour of the REAL src/input_tdeck.cpp against a scripted GT911 stand-in.
// The stub applies swap-then-mirror like the real library, so we feed "where the finger is on the
// 320x240 landscape screen" and the helper converts that to the controller's native frame.
#include <vector>
#include "../../src/input_tdeck.cpp"
#include <cstdio>
extern uint32_t g_ms; extern int g_clickLevel, g_kbd;
struct Ev { uint32_t t; int l, r; unsigned x, y; };
extern std::vector<Ev> g_events;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); fails++; } else { printf("  ok:   "); printf(__VA_ARGS__); printf("\n"); } } while (0)
static void tick(int n) { for (int i = 0; i < n; i++) { g_ms++; TDeck_PollInput(); } }
static void finger(int sx, int sy) { g_ts.pressed = true; g_ts.nx = 240 - sy; g_ts.ny = sx; }   // native = inverse of (swap, mirrorY)
static void lift() { g_ts.pressed = false; }
static void reinit(bool a5d, bool a14, bool ok = true) {
    g_ts.ack5D = a5d; g_ts.ack14 = a14; g_ts.beginOk = ok; g_ts.beginCalls = 0; g_ts.beginAddr = 0; g_ts.pressed = false;
    s_touchReady = false; s_touchDown = false; s_touchMisses = 0; TDeck_Touch_Init();
}
static int presses() { int n = 0; for (size_t i = 0; i < g_events.size(); i++) if (g_events[i].l && (i == 0 || !g_events[i-1].l)) n++; return n; }
static int releases() { int n = 0; for (size_t i = 1; i < g_events.size(); i++) if (!g_events[i].l && g_events[i-1].l) n++; return n; }
int main() {
    g_clickLevel = 1; tick(40);
    printf("== controller detection ==\n");
    reinit(false, false); CHECK(!s_touchReady && g_ts.beginCalls == 0, "no GT911 on the bus -> touch disabled, library not even initialised");
    int before = g_ts.getPointCalls; finger(100, 100); g_events.clear(); tick(100); lift();
    CHECK(g_ts.getPointCalls == before && g_events.empty(), "with touch disabled the controller is never polled and no events are generated");
    reinit(true, false);  CHECK(s_touchReady && g_ts.beginAddr == 0x5D, "controller at 0x5D (LilyGO's address) is found");
    CHECK(g_ts.maxX == 320 && g_ts.maxY == 240 && g_ts.swap && !g_ts.mx && g_ts.my, "configured like LilyGO's example: 320x240, swapXY, mirrorY");
    reinit(false, true);  CHECK(s_touchReady && g_ts.beginAddr == 0x14, "controller only at 0x14 is found too");
    reinit(true, true);   CHECK(g_ts.beginAddr == 0x5D, "if both answer, 0x5D is preferred");
    reinit(true, false, false); CHECK(!s_touchReady, "controller present but initialisation fails -> touch disabled, no crash");
    reinit(true, false);

    printf("== touch-down: cursor jumps to the finger, THEN the press ==\n");
    g_tdeckMouseX = 10; g_tdeckMouseY = 10; g_events.clear();
    finger(160, 120); tick(30);
    CHECK(g_events.size() >= 2, "touch produced events");
    CHECK(g_events[0].l == 0 && g_events[0].x == 160 && g_events[0].y == 100, "first event is a POSITION-ONLY move to the finger (160,100) - button not yet pressed");
    CHECK(g_events[1].l == 1 && g_events[1].x == 160 && g_events[1].y == 100, "second event is the left-press at that same position (not at the old cursor spot (10,10))");
    CHECK(g_tdeckMouseX == 160 && g_tdeckMouseY == 100, "cursor state moved to the finger");
    CHECK(s_touchDown, "touch registered as down");

    printf("== holding still, then lifting ==\n");
    g_events.clear(); tick(300);
    CHECK(g_events.empty(), "holding a finger still generates NO repeated press events");
    CHECK(g_ts.getPointCalls > 0, "controller is being read");
    lift(); g_events.clear(); tick(120);
    CHECK(g_events.size() == 1 && g_events[0].l == 0 && g_events[0].x == 160 && g_events[0].y == 100, "lifting sends exactly ONE event: the release, at the touch position");
    CHECK(!s_touchDown, "touch registered as up");

    printf("== coordinate mapping (screen -> game canvas, canvas starts 20px down the panel) ==\n");
    struct { int sx, sy, gx, gy; const char* n; } m[] = { {0,20,0,0,"top-left of the canvas"}, {319,219,319,199,"bottom-right of the canvas"}, {320,120,319,100,"x=320 (library max) clamps to 319"}, {40,60,40,40,"arbitrary point"} };
    for (auto& c : m) { g_events.clear(); finger(c.sx, c.sy); tick(30); bool ok = g_events.size() >= 2 && g_events[1].l && g_events[1].x == (unsigned)c.gx && g_events[1].y == (unsigned)c.gy;
        CHECK(ok, "%-34s screen(%d,%d) -> game(%d,%d)", c.n, c.sx, c.sy, c.gx, c.gy); lift(); tick(120); }

    printf("== unused strips above/below the canvas ==\n");
    g_tdeckMouseX = 77; g_tdeckMouseY = 66; g_events.clear();
    finger(100, 5); tick(60); lift(); tick(60); finger(100, 230); tick(60); lift(); tick(60);
    CHECK(g_events.empty() && g_tdeckMouseX == 77 && g_tdeckMouseY == 66, "a NEW touch on the black bars is ignored (no click, cursor untouched)");
    finger(100, 100); tick(30); g_events.clear(); finger(100, 235); tick(30);
    CHECK(!g_events.empty() && g_events.back().l == 1 && g_events.back().y == 199, "a drag that slides into the bar is clamped to the canvas edge and continues");
    lift(); tick(120);

    printf("== drag ==\n");
    finger(50, 80); tick(30); g_events.clear();
    finger(120, 100); tick(14); finger(200, 150); tick(14); finger(260, 180); tick(14);
    bool allHeld = !g_events.empty(); for (auto& e : g_events) if (!e.l) allHeld = false;
    CHECK(allHeld && g_events.size() >= 3 && g_events.back().x == 260 && g_events.back().y == 160, "dragging moves the cursor with the button HELD (drag-select works)");
    lift(); g_events.clear(); tick(120);
    CHECK(g_events.size() == 1 && g_events[0].l == 0 && g_events[0].x == 260 && g_events[0].y == 160, "release is one event at the last drag position");

    printf("== timing: quick taps, flicker, polling rate ==\n");
    g_events.clear(); uint32_t t0 = g_ms; finger(90, 90); tick(13); lift(); tick(150);
    uint32_t tp = 0, tr = 0; for (auto& e : g_events) { if (e.l && !tp) tp = e.t; if (!e.l && tp && !tr) tr = e.t; }
    CHECK(presses() == 1 && releases() == 1 && tr - tp >= 40, "a very quick tap still produces press then release >= 40 ms apart (engine rate-limits faster clicks) [%u ms]", (unsigned)(tr - tp)); (void)t0;
    finger(90, 90); tick(60); g_events.clear(); lift(); tick(30); finger(90, 90); tick(60);
    CHECK(releases() == 0 && presses() == 0, "a short gap of empty reads mid-hold (normal for this chip) does NOT release or re-press");
    lift(); tick(150);
    int c0 = g_ts.getPointCalls; finger(100, 100); tick(1000); int calls = g_ts.getPointCalls - c0; lift(); tick(150);
    CHECK(calls >= 78 && calls <= 88, "controller polled every ~12 ms, not every loop pass (%d reads per second)", calls);

    printf("== interaction with the other inputs ==\n");
    finger(150, 100); tick(30); g_events.clear();
    s_tbDeltaX += 3; tick(5);
    bool tbHeld = false, tbSeen = false; for (auto& e : g_events) { tbSeen = true; tbHeld = e.l; }
    CHECK(tbSeen && tbHeld, "trackball movement while a finger is down keeps reporting the button HELD (no accidental release)");
    g_events.clear(); g_kbd = '\r'; tick(100); g_kbd = 0; tick(100);
    bool leak = false; for (auto& e : g_events) if (!e.l) leak = true;
    CHECK(!g_events.empty() && !leak, "an Enter click during a touch never reports 'released' (the touch stays held)");
    lift(); tick(150); g_events.clear(); s_tbDeltaX += 3; tick(5);
    bool relOk = !g_events.empty(); for (auto& e : g_events) if (e.l) relOk = false;
    CHECK(relOk, "after the finger lifts, trackball movement reports released again");
    g_events.clear(); g_kbd = '\r'; tick(100); g_kbd = 0; tick(100);
    CHECK(presses() == 1 && releases() == 1, "Enter still makes a normal press+release click with touch idle");
    printf("%s\n", fails ? "RESULT: FAILURES" : "RESULT: all touch checks passed"); return fails != 0;
}
