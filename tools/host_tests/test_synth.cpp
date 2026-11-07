// Unit tests for the software synthesizer (src/tdeck_synth.h): pitch, envelopes, polyphony, drums, controllers.
#include "../../src/tdeck_synth.h"
#include <cstdio>
#include <vector>
#include <cmath>
#include <chrono>
#include <algorithm>
using tdeck::Synth;
static const float SR = 22050.0f;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); fails++; } else { printf("  ok:   "); printf(__VA_ARGS__); printf("\n"); } } while (0)
static uint32_t M(int st, int d1, int d2) { return st | (d1 << 8) | (d2 << 16); }
static std::vector<int32_t> render(Synth& s, int frames) { std::vector<int32_t> a(frames, 0); for (int o = 0; o < frames; o += 128) s.renderAdd(&a[o], std::min(128, frames - o)); return a; }
static double rms(const std::vector<int32_t>& a, int from, int to) { double e = 0; for (int i = from; i < to; i++) e += (double)a[i] * a[i]; return sqrt(e / (to - from)); }
static double freqOf(const std::vector<int32_t>& a, int from, int to) { int c = 0; for (int i = from + 1; i < to; i++) if ((a[i-1] < 0) != (a[i] < 0)) c++; return (c / 2.0) / ((to - from) / SR); }
static int32_t peak(const std::vector<int32_t>& a) { int32_t p = 0; for (auto v : a) p = std::max(p, std::abs(v)); return p; }
static void fresh(Synth& s) { s.init(SR, 0.7f); }
int main() {
    static Synth s;
    printf("== basics ==\n");
    fresh(s); { std::vector<int32_t> a(512, 7); s.renderAdd(a.data(), 512); bool same = true; for (auto v : a) if (v != 7) same = false; CHECK(same, "no notes -> output untouched"); }
    printf("== pitch accuracy (zero-crossing) ==\n");
    struct { int prog, note; const char* name; } cases[] = { {73,69,"flute(tri) A4"}, {73,81,"flute(tri) A5"}, {8,60,"bell(sine) C4"}, {73,45,"flute(tri) A2"}, {80,72,"lead(square) C5"} };
    for (auto& c : cases) { fresh(s); s.midi(M(0xC0, c.prog, 0)); s.midi(M(0x90, c.note, 100)); auto a = render(s, 11025);
        double want = 440.0 * pow(2.0, (c.note - 69) / 12.0), got = freqOf(a, 2205, 11025); CHECK(fabs(got - want) / want < 0.03, "%-16s want %.1f Hz got %.1f Hz", c.name, want, got); }
    printf("== envelope / release ==\n");
    fresh(s); s.midi(M(0xC0, 16, 0)); s.midi(M(0x90, 60, 100)); auto a = render(s, 6615);
    CHECK(rms(a, 3000, 6615) > 500, "organ holds a sustained level while the key is down");
    s.midi(M(0x80, 60, 0)); a = render(s, 22050); CHECK(rms(a, 20000, 22050) < 5 && s.activeVoices() == 0, "note-off releases to silence and frees the voice");
    printf("== velocity / volume / expression ==\n");
    fresh(s); s.midi(M(0xC0, 16, 0)); s.midi(M(0x90, 60, 30)); a = render(s, 4410); double r30 = rms(a, 1000, 4410);
    fresh(s); s.midi(M(0xC0, 16, 0)); s.midi(M(0x90, 60, 127)); a = render(s, 4410); double r127 = rms(a, 1000, 4410);
    CHECK(r127 > r30 * 1.5, "louder velocity -> louder (%.0f vs %.0f)", r127, r30);
    fresh(s); s.midi(M(0xC0, 16, 0)); s.midi(M(0xB0, 7, 0)); s.midi(M(0x90, 60, 127)); a = render(s, 4410); CHECK(peak(a) == 0, "channel volume 0 -> silent");
    fresh(s); s.midi(M(0xC0, 16, 0)); s.midi(M(0xB0, 11, 40)); s.midi(M(0x90, 60, 127)); a = render(s, 4410); double rE = rms(a, 1000, 4410);
    fresh(s); s.midi(M(0xC0, 16, 0)); s.midi(M(0x90, 60, 127)); a = render(s, 4410); CHECK(rE < rms(a, 1000, 4410) * 0.6, "expression CC11 attenuates");
    printf("== polyphony / safety ==\n");
    fresh(s); s.midi(M(0xC0, 48, 0)); for (int n = 36; n < 80; n++) s.midi(M(0x90, n, 120));
    CHECK(s.activeVoices() <= Synth::kVoices && s.activeVoices() > 20, "44 simultaneous notes -> voice stealing keeps %d voices (max %d)", s.activeVoices(), (int)Synth::kVoices);
    a = render(s, 22050); bool finite = true; for (auto v : a) if (std::abs((int64_t)v) > 3000000) finite = false;
    printf("        peak of the raw 24-voice pile-up: %d (the mixer soft-limits this)\n", peak(a)); CHECK(finite, "dense chord output is bounded");
    for (int i = 0; i < 5000; i++) { s.midi(M(0x90, i % 128, 1 + i % 127)); s.midi(M(0x80, (i * 7) % 128, 0)); s.midi(M(0xB0 + (i % 16), i % 128, (i * 3) % 128)); s.midi(M(0xE0 + (i % 16), i % 128, i % 128)); render(s, 128); }
    CHECK(true, "5000 random MIDI messages interleaved with rendering: no crash");
    printf("== drums (channel 10) ==\n");
    int drums[] = {36, 38, 42, 46, 49, 41, 56, 60};
    for (int d : drums) { fresh(s); s.midi(M(0x99, d, 110)); a = render(s, 4410); double e = rms(a, 0, 2000); a = render(s, 44100); CHECK(e > 100 && s.activeVoices() == 0, "drum note %d sounds (rms %.0f) and rings out to silence", d, e); }
    printf("== pitch bend ==\n");
    fresh(s); s.midi(M(0xC0, 73, 0)); s.midi(M(0x90, 69, 100)); s.midi(M(0xE0, 127, 127)); a = render(s, 11025);
    double fb = freqOf(a, 2205, 11025); CHECK(fabs(fb / 440.0 - pow(2.0, 2.0 / 12)) < 0.03, "bend up (range 2 semitones): %.1f Hz", fb);
    fresh(s); s.midi(M(0xB0,101,0)); s.midi(M(0xB0,100,0)); s.midi(M(0xB0,6,12)); s.midi(M(0xC0, 73, 0)); s.midi(M(0x90, 69, 100)); s.midi(M(0xE0, 127, 127)); a = render(s, 11025);
    fb = freqOf(a, 2205, 11025); CHECK(fabs(fb / 440.0 - 2.0) < 0.06, "RPN bend range 12 -> full bend = octave: %.1f Hz", fb);
    printf("== sustain pedal / controllers ==\n");
    fresh(s); s.midi(M(0xC0, 16, 0)); s.midi(M(0xB0, 64, 127)); s.midi(M(0x90, 60, 100)); render(s, 2000); s.midi(M(0x80, 60, 0)); a = render(s, 4410);
    CHECK(rms(a, 2000, 4410) > 300, "pedal down: note keeps sounding after note-off");
    s.midi(M(0xB0, 64, 0)); a = render(s, 22050); CHECK(s.activeVoices() == 0, "pedal up: held note releases");
    fresh(s); s.midi(M(0x90, 60, 100)); s.midi(M(0x91, 64, 100)); s.midi(M(0xB0, 123, 0)); render(s, 22050); CHECK(s.activeVoices() <= 1, "CC123 all-notes-off affects only its channel");
    fresh(s); s.midi(M(0x90, 60, 100)); s.midi(M(0xB0, 120, 0)); CHECK(s.activeVoices() == 0, "CC120 all-sound-off is immediate");
    fresh(s); s.midi(M(0x90, 60, 100)); s.midi(M(0x95, 70, 100)); s.midi(0xFF); CHECK(s.activeVoices() == 0, "0xFF reset silences everything");
    printf("== all instrument families ==\n");
    int bad = 0; for (int p = 0; p < 128; p += 8) { fresh(s); s.midi(M(0xC0, p, 0)); s.midi(M(0x90, 60, 100)); a = render(s, 8820); if (rms(a, 4000, 8820) < 20 && rms(a, 0, 4000) < 20) { bad++; printf("        silent family at program %d\n", p); } }
    CHECK(bad == 0, "all 16 GM families audible");
    printf("== CPU cost (host) ==\n");
    fresh(s); s.midi(M(0xC0, 48, 0)); for (int n = 40; n < 64; n++) s.midi(M(0x90, n, 100));
    auto t0 = std::chrono::steady_clock::now(); for (int i = 0; i < 172; i++) { std::vector<int32_t> b(128, 0); s.renderAdd(b.data(), 128); }
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    printf("        1.0 s of audio with %d dual-osc voices: %.2f ms on this PC (%.1f ns per voice-sample) - informational only\n", s.activeVoices(), ms, ms * 1e6 / (172 * 128.0 * s.activeVoices()));
    printf("%s\n", fails ? "RESULT: FAILURES" : "RESULT: all synth checks passed"); return fails != 0;
}
