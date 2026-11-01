/**
 * midi_tdeck.cpp - music backend for OpenDUNE on the T-Deck.
 *
 * Implements src/audio/midi.h. The engine's own sequencer (mt32mpu.c) is
 * already part of this port and runs on the game task at 120 Hz; it turns
 * Dune II's music tracks into standard short MIDI messages and hands them to
 * midi_send(). The old midi_none.c answered "no music device", which switched
 * the whole music path off. This backend instead queues the messages (lock-free,
 * single producer = game task, single consumer = audio task) for the software
 * synthesizer in tdeck_synth.h, which the audio task renders and mixes with the
 * voice/sound-effect channel (see dsp_tdeck.cpp).
 *
 * The sequencer's MT-32 "string" (SysEx) messages are accepted and ignored.
 */
#include "tdeck_config.h"
#include "tdeck_music.h"

extern "C" {
#include "../lib/opendune/src/types.h"
#include "../lib/opendune/src/audio/midi.h"
}

#if TDECK_ENABLE_MUSIC

#include <atomic>
#include "tdeck_synth.h"

namespace {
const uint32_t kRing = 1024;                  // power of two
uint32_t s_ring[kRing];
std::atomic<uint32_t> s_head(0), s_tail(0);   // head: written by game task; tail: by audio task
std::atomic<uint32_t> s_ready(0);             // 32-bit on purpose: avoids sub-word atomics on Xtensa
tdeck::Synth s_synth;

void post(uint32_t msg)
{
    uint32_t h = s_head.load(std::memory_order_relaxed);
    uint32_t t = s_tail.load(std::memory_order_acquire);
    if (h - t >= kRing) return;               // full: drop rather than block the game
    s_ring[h & (kRing - 1)] = msg;
    s_head.store(h + 1, std::memory_order_release);
}
} // namespace

extern "C" bool midi_init(void)
{
    s_ready.store(0, std::memory_order_release);
    s_synth.init((float)TDECK_AUDIO_SAMPLE_RATE, (float)TDECK_MUSIC_VOLUME / 100.0f);
    s_tail.store(s_head.load(std::memory_order_relaxed), std::memory_order_relaxed);
    s_ready.store(1, std::memory_order_release);
    return true;
}

extern "C" void midi_uninit(void)
{
    s_ready.store(0, std::memory_order_release);
}

extern "C" void midi_send(uint32 data)
{
    if (!s_ready.load(std::memory_order_acquire)) return;
    post((uint32_t)data);
}

extern "C" uint16 midi_send_string(const uint8 *data, uint16 len)
{
    (void)data;
    return len;                                // MT-32/GM SysEx: not needed by the synth
}

extern "C" void midi_reset(void)
{
    if (!s_ready.load(std::memory_order_acquire)) return;
    post(0xFFu);                               // synth treats status 0xFF as "reset"
}

void TDeck_Music_Render(int32_t *acc, int frames)
{
    if (!s_ready.load(std::memory_order_acquire)) return;
    uint32_t t = s_tail.load(std::memory_order_relaxed);
    uint32_t h = s_head.load(std::memory_order_acquire);
    while (t != h) {
        s_synth.midi(s_ring[t & (kRing - 1)]);
        t++;
    }
    s_tail.store(t, std::memory_order_release);
    s_synth.renderAdd(acc, frames);
}

#else   // music disabled: behave exactly like the old midi_none.c

extern "C" bool midi_init(void) { return false; }
extern "C" void midi_uninit(void) {}
extern "C" void midi_send(uint32 data) { (void)data; }
extern "C" uint16 midi_send_string(const uint8 *data, uint16 len) { (void)data; return len; }
extern "C" void midi_reset(void) {}
void TDeck_Music_Render(int32_t *acc, int frames) { (void)acc; (void)frames; }

#endif
