// Mixer + music backend + I2S pin configuration (the GPIO0 trackball-click bug), against mocks.
#include "../../src/midi_tdeck.cpp"
#include "../../src/dsp_tdeck.cpp"
#include <cstdio>
#include <vector>
#include <cmath>
extern i2s_pin_config_t g_pins; extern int g_pinsSet, g_installed;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); fails++; } else { printf("  ok:   "); printf(__VA_ARGS__); printf("\n"); } } while (0)
static std::vector<uint8_t> makeVoc(const std::vector<uint8_t>& pcm, int divisor) {
    std::vector<uint8_t> v(26, 0); memcpy(&v[0], "Creative Voice File\x1a", 20); v[20] = 0x1A; v[22] = 0x0A; v[23] = 1;
    uint32_t len = pcm.size() + 2; v.push_back(1); v.push_back(len & 255); v.push_back((len >> 8) & 255); v.push_back(len >> 16);
    v.push_back(divisor); v.push_back(0); v.insert(v.end(), pcm.begin(), pcm.end()); v.push_back(0); return v; }
static std::vector<int16_t> run(int blocks) { std::vector<int16_t> o; for (int b = 0; b < blocks; b++) { int16_t buf[kBlockFrames * 2]; MixBlock(buf, kBlockFrames); for (int i = 0; i < kBlockFrames; i++) o.push_back(buf[2 * i]); } return o; }
static bool allZero(const std::vector<int16_t>& a, size_t from = 0) { for (size_t i = from; i < a.size(); i++) if (a[i]) return false; return true; }
static uint32_t MSG(int st, int d1, int d2) { return st | (d1 << 8) | (d2 << 16); }
int main() {
    printf("== GPIO0 / I2S pin configuration (the trackball-click bug) ==\n");
    CHECK(DSP_Init() && g_pinsSet == 1 && g_installed == 1, "DSP_Init succeeds, driver installed, pins set once");
    printf("      pins: mck=%d bck=%d ws=%d dout=%d din=%d\n", g_pins.mck_io_num, g_pins.bck_io_num, g_pins.ws_io_num, g_pins.data_out_num, g_pins.data_in_num);
    CHECK(g_pins.mck_io_num == I2S_PIN_NO_CHANGE, "MCLK is explicitly NO_CHANGE (was 0 = GPIO0 = the trackball click button)");
    CHECK(g_pins.bck_io_num != 0 && g_pins.ws_io_num != 0 && g_pins.data_out_num != 0 && g_pins.data_in_num != 0, "no I2S signal is routed to GPIO0 at all");
    CHECK(g_pins.bck_io_num == TDECK_I2S_BCK && g_pins.ws_io_num == TDECK_I2S_WS && g_pins.data_out_num == TDECK_I2S_DOUT, "BCK/WS/DOUT still on the T-Deck speaker pins");
    printf("== idle ==\n");
    CHECK(allZero(run(4)) && DSP_GetStatus() == 0, "idle output is exact silence, status 0");
    printf("== voice / sound-effect clip path ==\n");
    std::vector<uint8_t> pcm; for (int i = 0; i < 400; i++) pcm.push_back((uint8_t)(128 + 100 * sin(i * 0.3)));
    auto voc = makeVoc(pcm, 131);                         // 1e6/(256-131) = 8000 Hz
    DSP_Play(voc.data()); CHECK(DSP_GetStatus() == 2, "status is 2 (playing) immediately after DSP_Play");
    auto o = run(12);
    uint32_t step = ((uint64_t)8000 << 16) / TDECK_AUDIO_SAMPLE_RATE; int mism = 0, n = 0; uint32_t pos = 0;
    for (size_t i = 0; i < o.size(); i++, pos += step) { if ((pos >> 16) >= pcm.size()) break; int32_t s = (((int32_t)pcm[pos >> 16] - 128) << 8); if (o[i] != ((s * 13) >> 4)) mism++; n++; }
    CHECK(mism == 0 && n > 1000, "clip resampled 8 kHz -> %d Hz exactly as expected (%d samples, %d mismatches)", TDECK_AUDIO_SAMPLE_RATE, n, mism);
    size_t expectLen = (size_t)(pcm.size() * (double)TDECK_AUDIO_SAMPLE_RATE / 8000.0);
    CHECK(DSP_GetStatus() == 0 && allZero(o, expectLen + 2), "clip ends after ~%zu frames, status back to 0, output returns to pure silence (no stutter)", expectLen);
    DSP_Play(voc.data()); run(1); DSP_Stop(); CHECK(DSP_GetStatus() == 0 && allZero(run(2)), "DSP_Stop silences immediately");
    auto bad = voc; bad[27] = 2; bad[28] = 0; bad[29] = 0; DSP_Play(bad.data()); CHECK(DSP_GetStatus() == 0, "malformed clip (length < 3) rejected, not played");
    printf("== music through the backend ==\n");
    midi_send(MSG(0x90, 60, 100)); CHECK(allZero(run(2)), "midi_send before midi_init is ignored");
    CHECK(midi_init() == true, "midi_init() returns true (engine will now start its music path)");
    midi_send(MSG(0xC0, 73, 0)); midi_send(MSG(0x90, 69, 110));
    auto m = run(20); double e = 0; for (auto v : m) e += (double)v * v; e = sqrt(e / m.size());
    CHECK(e > 300, "queued MIDI note-on becomes audible music (rms %.0f)", e);
    midi_send(MSG(0x80, 69, 0)); run(40); CHECK(allZero(run(4)), "note-off releases to exact silence");
    midi_send(MSG(0x90, 60, 100)); midi_send(MSG(0x91, 64, 100)); run(4); midi_reset(); run(1); CHECK(allZero(run(2)), "midi_reset() silences everything");
    CHECK(midi_send_string((const uint8*)"\xF0\x41\xF7", 3) == 3, "SysEx strings accepted and ignored");
    printf("== music + effects together ==\n");
    DSP_Play(voc.data()); midi_send(MSG(0x90, 69, 110)); auto both = run(24);
    bool after = false; for (size_t i = expectLen + 4; i < both.size(); i++) if (both[i]) after = true;
    CHECK(after, "music keeps playing after a voice clip ends");
    int32_t pk = 0; for (auto v : both) pk = std::max<int32_t>(pk, std::abs(v)); CHECK(pk <= 32767 && pk > 1000, "mixed output within 16-bit range (peak %d)", pk);
    midi_send(MSG(0xB0, 123, 0)); run(60);
    printf("== robustness ==\n");
    for (int i = 0; i < 5000; i++) midi_send(MSG(0x90, 40 + i % 40, 1 + i % 120));
    run(10); CHECK(true, "5000-message flood (ring holds 1024) drops the excess safely");
    midi_uninit(); midi_send(MSG(0x90, 60, 100)); CHECK(allZero(run(3)), "after midi_uninit nothing is rendered or queued");
    DSP_Uninit(); CHECK(true, "DSP_Uninit completes");
    CHECK(SoftClip(1000000) == 32767 && SoftClip(-1000000) == -32768 && SoftClip(5000) == 5000 && SoftClip(30000) < 30000 && SoftClip(30000) > 24000, "soft limiter: transparent below the knee, compresses above, hard-limits at 16-bit");
    printf("%s\n", fails ? "RESULT: FAILURES" : "RESULT: all audio checks passed"); return fails != 0;
}
