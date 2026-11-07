// Replays the situation from the real serial logs against the REAL src/input_tdeck.cpp.
// usage: ./test_input_clicks stuckLow|normalHigh
#include <vector>
#include "../../src/input_tdeck.cpp"
#include <cstdio>
#include <cstring>
extern uint32_t g_ms; extern int g_clickLevel, g_kbd, g_rtcCalls;
struct Ev { uint32_t t; int l, r; unsigned x, y; };
extern std::vector<Ev> g_events;
static int fails = 0;
#define CHECK(c, msg) do { if (!(c)) { printf("  FAIL: %s\n", msg); fails++; } else printf("  ok:   %s\n", msg); } while (0)
static void tick(int n, int move_every = 0) { for (int i = 0; i < n; i++) { g_ms++; if (move_every && g_ms % move_every == 0) s_tbDeltaX += 3; TDeck_PollInput(); } }
static void press_key(int ch, int hold_ms) { g_kbd = ch; tick(hold_ms, 7); g_kbd = 0; tick(80, 7); }
int main(int argc, char** argv) {
    bool stuckLow = argc > 1 && !strcmp(argv[1], "stuckLow");
    g_clickLevel = stuckLow ? 0 : 1;
    TDeck_Input_Init();
    printf("== click pin %s at boot ==\n", stuckLow ? "STUCK LOW (as seen on both real units)" : "idle HIGH (healthy)");
    CHECK(g_rtcCalls == 1, "rtc_gpio_deinit(GPIO_NUM_0) called once during init");
    tick(300, 5);
    size_t n1 = g_events.size(); int held = 0; for (auto& e : g_events) if (e.l) held++;
    CHECK(n1 > 10, "trackball movement produces events");
    CHECK(held == 0, "NO movement event reports the left button held (the radar-drag / Enter-twice bug)");
    g_events.clear(); press_key('\r', 120);
    int downs = 0, ups = 0; uint32_t tDown = 0, tUp = 0;
    for (auto& e : g_events) { if (e.l && !downs) { downs++; tDown = e.t; } else if (!e.l && downs && !ups) { ups++; tUp = e.t; } }
    CHECK(downs == 1 && ups == 1, "Enter = exactly ONE press and ONE release (chip keeps returning the byte; no repeats)");
    CHECK(tUp - tDown >= 45 && tUp - tDown <= 70, "release comes ~50ms after the press (non-blocking, separate poll)");
    bool heldBetween = true; for (auto& e : g_events) if (e.t > tDown && e.t < tUp && !e.l) heldBetween = false;
    CHECK(heldBetween, "movement during the click keeps reporting held (no stray release mid-click)");
    CHECK(g_events.back().l == 0, "after the click, movement reports released again");
    g_events.clear(); press_key('\r', 120); bool sd = false, su = false; for (auto& e : g_events) { if (e.l) sd = true; else if (sd) su = true; }
    CHECK(sd && su, "a second Enter is also a complete press+release");
    g_events.clear(); press_key(' ', 120); sd = su = false; for (auto& e : g_events) { if (e.l) sd = true; else if (sd) su = true; }
    CHECK(sd && su, "Space = the same left click as Enter");
    g_events.clear(); press_key(0x08, 120); bool rd = false, ru = false, anyL = false; for (auto& e : g_events) { if (e.l) anyL = true; if (e.r) rd = true; else if (rd) ru = true; }
    CHECK(rd && ru && !anyL, "Backspace = right click only");
    g_events.clear(); g_clickLevel = stuckLow ? 1 : 0; tick(60, 5); bool pd = false; for (auto& e : g_events) if (e.l) pd = true;
    g_clickLevel = stuckLow ? 0 : 1; tick(60, 5);
    CHECK(pd && g_events.back().l == 0, "a genuine change in the click pin level = press then release");
    printf("%s\n", fails ? "RESULT: FAILURES" : "RESULT: all checks passed"); return fails != 0;
}
