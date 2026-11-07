// Typing into the engine's text prompts (save name / high-score name) vs normal-game bindings.
#include <vector>
#include "../../src/input_tdeck.cpp"
#include <cstdio>
extern uint32_t g_ms; extern int g_clickLevel, g_kbd;
struct Ev { uint32_t t; int l, r; unsigned x, y; };
extern std::vector<Ev> g_events; extern std::vector<int> g_keys;
static int fails = 0;
#define CHECK(c, msg) do { if (!(c)) { printf("  FAIL: %s\n", msg); fails++; } else printf("  ok:   %s\n", msg); } while (0)
static void tick(int n) { for (int i = 0; i < n; i++) { g_ms++; TDeck_PollInput(); } }
static void type(int ch) { g_events.clear(); g_keys.clear(); g_kbd = ch; tick(100); g_kbd = 0; tick(100); }
static bool keysAre(std::initializer_list<int> v) { return g_keys == std::vector<int>(v); }
int main() {
    g_clickLevel = 1; TDeck_Input_Init(); tick(50);
    printf("== text prompt OPEN (INPUT_FLAG_NO_TRANSLATE set, exactly as GUI_EditBox does) ==\n");
    g_inputFlags |= INPUT_FLAG_NO_TRANSLATE;
    type('i'); CHECK(keysAre({0x17,0x97}) && g_events.empty(), "'i' types i (was: moved the cursor, typed nothing)");
    type('j'); CHECK(keysAre({0x24,0xA4}) && g_events.empty(), "'j' types j");
    type('k'); CHECK(keysAre({0x25,0xA5}) && g_events.empty(), "'k' types k");
    type('l'); CHECK(keysAre({0x26,0xA6}) && g_events.empty(), "'l' types l");
    type('o'); CHECK(keysAre({0x18,0x98}), "'o' types o (NOT remapped to F2)");
    type('M'); CHECK(keysAre({0x36,0x32,0xB2,0xB6}), "'M' = Shift+m, so capitals work");
    type('1'); CHECK(keysAre({0x02,0x82}), "digit 1");
    type('!'); CHECK(keysAre({0x36,0x02,0x82,0xB6}), "'!' = Shift+1");
    type(' '); CHECK(keysAre({0x39,0xB9}) && g_events.empty(), "Space types a space (no click)");
    type(0x08); CHECK(keysAre({0x0E,0x8E}) && g_events.empty(), "Backspace deletes a character (no right-click)");
    type('\r'); CHECK(keysAre({0x1C,0x9C}) && g_events.empty(), "Enter sends a REAL Enter keypress to finish the name (was: only a click -> soft-lock)");
    printf("== prompt CLOSED - normal game bindings must be back ==\n");
    g_inputFlags &= ~INPUT_FLAG_NO_TRANSLATE;
    type('i'); CHECK(g_keys.empty() && !g_events.empty(), "'i' moves the cursor again");
    type('\r'); bool d = false, u = false; for (auto&e : g_events) { if (e.l) d = true; else if (d) u = true; }
    CHECK(g_keys.empty() && d && u, "Enter is a left click again (and sends no keypress)");
    type(' '); d = u = false; for (auto&e : g_events) { if (e.l) d = true; else if (d) u = true; } CHECK(g_keys.empty() && d && u, "Space is a left click again");
    type('o'); CHECK(keysAre({0x3C,0xBC}), "'o' opens Options (F2) again");
    type('m'); CHECK(keysAre({0x32,0xB2}), "'m' still reaches the Move hotkey");
    printf("%s\n", fails ? "RESULT: FAILURES" : "RESULT: all checks passed"); return fails != 0;
}
