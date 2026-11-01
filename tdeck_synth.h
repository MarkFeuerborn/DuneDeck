#pragma once
/**
 * tdeck_synth.h - small, self-contained software synthesizer for Dune II's
 * General-MIDI (SC-55 "C55") score.
 *
 * No hardware or Arduino dependencies on purpose, so the exact same code is
 * unit-tested on a PC (see tools/host_tests). It is NOT a sample-based GM
 * synth: each of the 16 GM instrument families gets a simple oscillator +
 * filter + envelope voice ("chiptune/FM-ish" timbres), and channel 10 gets
 * synthesized drums. The notes, timing, volume and pitch-bend come straight
 * from the game's own sequencer, so the music is the right music; only the
 * instrument sounds differ from a real Roland SC-55.
 *
 * Threading: NOT thread-safe by itself. All calls (init/reset/midi/render)
 * must come from ONE thread - in this port that is the audio task, fed
 * through a lock-free queue (see midi_tdeck.cpp).
 */
#include <stdint.h>
#include <string.h>
#include <math.h>

namespace tdeck {

class Synth {
public:
    enum { kVoices = 24, kChannels = 16, kSineSize = 2048 };

    void init(float sampleRate, float masterGain)
    {
        sr_ = sampleRate;
        master_ = masterGain;
        incScale_ = 4294967296.0f / sampleRate;
        for (int i = 0; i < kSineSize; i++) {
            float v = 32767.0f * sinf(6.2831853f * (float)i / (float)kSineSize);
            sine_[i] = (int16_t)(v >= 0 ? v + 0.5f : v - 0.5f);
        }
        for (int n = 0; n < 128; n++)
            noteHz_[n] = 440.0f * powf(2.0f, ((float)n - 69.0f) / 12.0f);
        counter_ = 0;
        reset();
    }

    /** Silence everything and restore default controllers. */
    void reset()
    {
        memset(voices_, 0, sizeof(voices_));
        for (int c = 0; c < kChannels; c++) resetChannel(c, true);
    }

    /** Feed one packed short MIDI message: status | d1<<8 | d2<<16. 0xFF = reset. */
    void midi(uint32_t msg)
    {
        uint8_t st = (uint8_t)(msg & 0xFF);
        uint8_t d1 = (uint8_t)((msg >> 8) & 0x7F);
        uint8_t d2 = (uint8_t)((msg >> 16) & 0x7F);
        if (st == 0xFF) { reset(); return; }
        if (st < 0x80 || st >= 0xF0) return;
        int ch = st & 0x0F;
        switch (st & 0xF0) {
            case 0x80: noteOff(ch, d1); break;
            case 0x90: if (d2) noteOn(ch, d1, d2); else noteOff(ch, d1); break;
            case 0xB0: control(ch, d1, d2); break;
            case 0xC0: ch_[ch].program = d1; break;
            case 0xE0: ch_[ch].bend = (int16_t)(d1 | (d2 << 7)); break;
            default: break;   // aftertouch etc. ignored
        }
    }

    /** Add `frames` mono samples (int32, roughly +-32768 full scale) into acc. */
    void renderAdd(int32_t* acc, int frames)
    {
        if (activeVoices() == 0) return;
        float chGain[kChannels], bendF[kChannels];
        for (int c = 0; c < kChannels; c++) {
            chGain[c] = ((float)ch_[c].volume / 127.0f) * ((float)ch_[c].expression / 127.0f);
            float semis = ((float)(ch_[c].bend - 8192) / 8192.0f) * (float)ch_[c].bendRange;
            bendF[c] = (semis == 0.0f) ? 1.0f : powf(2.0f, semis / 12.0f);
        }
        for (int i = 0; i < kVoices; i++) {
            Voice& v = voices_[i];
            if (!v.active) continue;
            if (v.drum) renderDrum(v, acc, frames, chGain[v.ch]);
            else        renderTone(v, acc, frames, chGain[v.ch], bendF[v.ch]);
        }
    }

    int activeVoices() const
    {
        int n = 0;
        for (int i = 0; i < kVoices; i++) if (voices_[i].active) n++;
        return n;
    }

private:
    enum Wave { W_SINE, W_TRI, W_SQUARE, W_PULSE, W_SAW, W_NOISE };

    struct Timbre {
        uint8_t wave;
        float attackMs, decayMs, sustain, releaseMs;
        float bright;        // 0..1, one-pole low-pass openness
        float detuneCents;   // >0 => second detuned oscillator (chorus/ensemble)
        float gain;
    };

    struct Voice {
        bool active, releasing, held, drum;
        uint8_t ch, note, vel, wave, stage;
        bool dual;
        uint32_t age, phase1, phase2, rng;
        float baseInc1, baseInc2;
        float env, attackInc, decayMul, sustain, releaseMul;
        float lp, lpK, waveGain, level;
        // drum-only
        float freq, freqEnd, sweepMul, envMul, toneMix, noiseMix, hpPrev;
        bool highpass;
    };

    struct Channel {
        uint8_t program, volume, expression, bendRange, rpnMsb, rpnLsb;
        bool sustain;
        int16_t bend;
    };

    // One timbre per GM family (program >> 3).
    static const Timbre* timbres()
    {
        static const Timbre t[16] = {
            /*  0 piano       */ { W_TRI,    2,  450, 0.00f, 120, 0.55f,  0, 1.00f },
            /*  1 chrom perc  */ { W_SINE,   1,  700, 0.00f, 200, 1.00f,  0, 1.00f },
            /*  2 organ       */ { W_SQUARE, 5,    0, 1.00f,  60, 0.35f,  0, 0.80f },
            /*  3 guitar      */ { W_SAW,    1,  350, 0.00f, 100, 0.40f,  0, 0.80f },
            /*  4 bass        */ { W_SQUARE, 3,  300, 0.40f,  80, 0.25f,  0, 0.90f },
            /*  5 strings     */ { W_SAW,   90,    0, 1.00f, 250, 0.35f,  7, 0.80f },
            /*  6 ensemble    */ { W_SAW,  120,    0, 1.00f, 300, 0.30f,  9, 0.80f },
            /*  7 brass       */ { W_SAW,   45,  120, 0.70f, 100, 0.50f,  0, 0.80f },
            /*  8 reed        */ { W_PULSE, 25,    0, 1.00f,  90, 0.40f,  0, 0.85f },
            /*  9 pipe        */ { W_TRI,   40,    0, 1.00f, 100, 0.90f,  0, 1.00f },
            /* 10 synth lead  */ { W_SQUARE, 3,    0, 1.00f,  80, 0.60f,  0, 0.80f },
            /* 11 synth pad   */ { W_SAW,  200,    0, 1.00f, 500, 0.25f, 12, 0.80f },
            /* 12 synth fx    */ { W_SAW,  250,    0, 1.00f, 600, 0.30f, 15, 0.80f },
            /* 13 ethnic      */ { W_TRI,    2,  300, 0.00f, 100, 0.60f,  0, 1.00f },
            /* 14 percussive  */ { W_SINE,   1,  250, 0.00f,  60, 1.00f,  0, 1.00f },
            /* 15 sound fx    */ { W_NOISE, 20,    0, 0.50f, 300, 0.30f,  0, 0.60f },
        };
        return t;
    }

    static float waveGain(uint8_t w)
    {
        static const float g[6] = { 1.0f, 1.0f, 0.55f, 0.55f, 0.60f, 0.60f };
        return g[w];
    }

    void resetChannel(int c, bool full)
    {
        Channel& k = ch_[c];
        if (full) { k.program = 0; k.volume = 100; }
        k.expression = 127; k.bend = 8192; k.bendRange = 2;
        k.rpnMsb = 127; k.rpnLsb = 127; k.sustain = false;
    }

    float perSampleDecay(float ms) const   // -40 dB over `ms`
    {
        float n = ms * 0.001f * sr_;
        if (n < 1.0f) n = 1.0f;
        return expf(-4.6f / n);
    }

    // ------------------------------------------------------------ MIDI
    void control(int ch, uint8_t cc, uint8_t val)
    {
        Channel& k = ch_[ch];
        switch (cc) {
            case 7:   k.volume = val; break;
            case 11:  k.expression = val; break;
            case 64: {
                bool on = val >= 64;
                if (k.sustain && !on) releaseHeld(ch);
                k.sustain = on;
                break;
            }
            case 101: k.rpnMsb = val; break;
            case 100: k.rpnLsb = val; break;
            case 6:
                if (k.rpnMsb == 0 && k.rpnLsb == 0) {
                    k.bendRange = val < 1 ? 1 : (val > 24 ? 24 : val);
                }
                break;
            case 120: for (int i = 0; i < kVoices; i++) if (voices_[i].ch == ch) voices_[i].active = false; break;
            case 121: resetChannel(ch, false); break;
            case 123: allNotesOff(ch); break;
            default: break;
        }
    }

    void noteOff(int ch, uint8_t note)
    {
        for (int i = 0; i < kVoices; i++) {
            Voice& v = voices_[i];
            if (!v.active || v.drum || v.releasing || v.ch != ch || v.note != note) continue;
            if (ch_[ch].sustain) v.held = true; else startRelease(v);
        }
    }

    void allNotesOff(int ch)
    {
        for (int i = 0; i < kVoices; i++) {
            Voice& v = voices_[i];
            if (!v.active || v.drum || v.releasing || v.ch != ch) continue;
            if (ch_[ch].sustain) v.held = true; else startRelease(v);
        }
    }

    void releaseHeld(int ch)
    {
        for (int i = 0; i < kVoices; i++) {
            Voice& v = voices_[i];
            if (v.active && v.held && v.ch == ch) { v.held = false; startRelease(v); }
        }
    }

    void startRelease(Voice& v)
    {
        if (v.drum || v.releasing) return;
        v.releasing = true;
        v.stage = 3;
    }

    int allocVoice()
    {
        int best = 0;
        float bestScore = 1e30f;
        for (int i = 0; i < kVoices; i++) {
            const Voice& v = voices_[i];
            if (!v.active) return i;
            // Steal quiet releasing voices first, then the oldest note.
            float score = v.releasing ? v.env : 2.0f + (float)(v.age & 0xFFFFFF) * 1e-6f;
            if (score < bestScore) { bestScore = score; best = i; }
        }
        return best;
    }

    void noteOn(int ch, uint8_t note, uint8_t vel)
    {
        if (ch != 9) {   // retrigger of the same note: let the old one die away
            for (int i = 0; i < kVoices; i++) {
                Voice& v = voices_[i];
                if (v.active && !v.drum && !v.releasing && v.ch == ch && v.note == note) startRelease(v);
            }
        }
        int idx = allocVoice();
        Voice& v = voices_[idx];
        memset(&v, 0, sizeof(v));
        v.active = true;
        v.ch = (uint8_t)ch;
        v.note = note;
        v.vel = vel;
        v.age = ++counter_;
        v.rng = 0x9E3779B9u ^ (counter_ * 2654435761u);
        if (v.rng == 0) v.rng = 1;
        float velGain = 0.35f + 0.65f * ((float)vel / 127.0f);
        if (ch == 9) startDrum(v, note, velGain); else startTone(v, ch, note, velGain);
    }

    void startTone(Voice& v, int ch, uint8_t note, float velGain)
    {
        const Timbre& t = timbres()[(ch_[ch].program >> 3) & 15];
        v.wave = t.wave;
        v.baseInc1 = noteHz_[note] * incScale_;
        v.dual = t.detuneCents > 0.0f;
        v.baseInc2 = v.baseInc1 * powf(2.0f, t.detuneCents / 1200.0f);
        if (t.wave != W_SINE && t.wave != W_TRI) { v.phase1 = v.rng; v.phase2 = v.rng * 1664525u + 1013904223u; }
        float atkSamples = t.attackMs * 0.001f * sr_;
        v.attackInc = 1.0f / (atkSamples < 1.0f ? 1.0f : atkSamples);
        v.sustain = t.sustain;
        float decMs = t.decayMs;
        if (v.sustain < 0.999f && decMs <= 0.0f) decMs = 30.0f;
        v.decayMul = perSampleDecay(decMs);
        v.releaseMul = perSampleDecay(t.releaseMs);
        v.env = 0.0f;
        v.stage = 0;
        float k = 0.04f + t.bright * 0.90f * (0.55f + 0.45f * velGain);
        v.lpK = k > 1.0f ? 1.0f : k;
        v.waveGain = waveGain(t.wave) * t.gain;
        v.level = kVoiceScale * velGain * master_;
    }

    void startDrum(Voice& v, uint8_t note, float velGain)
    {
        v.drum = true;
        v.env = 1.0f;
        v.level = kVoiceScale * 1.2f * velGain * master_;
        v.freq = 300.0f; v.freqEnd = 300.0f; v.sweepMul = 1.0f;
        v.toneMix = 0.3f; v.noiseMix = 0.8f; v.highpass = false;
        float decMs = 70.0f;
        switch (note) {
            case 35: case 36:                                   // kick
                v.freq = 140.0f; v.freqEnd = 48.0f;
                v.sweepMul = expf(-logf(140.0f / 48.0f) / (0.06f * sr_));
                v.toneMix = 1.0f; v.noiseMix = 0.05f; decMs = 280.0f; break;
            case 38: case 40:                                   // snare
                v.freq = 190.0f; v.freqEnd = 190.0f; v.toneMix = 0.45f; v.noiseMix = 0.8f; decMs = 180.0f; break;
            case 37:                                            // side stick
                v.freq = 800.0f; v.freqEnd = 800.0f; v.toneMix = 0.6f; v.noiseMix = 0.3f; decMs = 40.0f; break;
            case 39:                                            // clap
                v.toneMix = 0.0f; v.noiseMix = 1.0f; v.highpass = true; decMs = 110.0f; break;
            case 41: case 43: case 45: case 47: case 48: case 50: { // toms
                float f = note == 41 ? 80.0f : note == 43 ? 100.0f : note == 45 ? 125.0f :
                          note == 47 ? 150.0f : note == 48 ? 170.0f : 200.0f;
                v.freq = f * 1.3f; v.freqEnd = f;
                v.sweepMul = expf(-logf(1.3f) / (0.05f * sr_));
                v.toneMix = 1.0f; v.noiseMix = 0.08f; decMs = 320.0f; break; }
            case 42: case 44:                                   // closed hat
                v.toneMix = 0.0f; v.noiseMix = 1.0f; v.highpass = true; decMs = 55.0f; break;
            case 46:                                            // open hat
                v.toneMix = 0.0f; v.noiseMix = 1.0f; v.highpass = true; decMs = 300.0f; break;
            case 49: case 52: case 55: case 57:                 // crashes
                v.toneMix = 0.0f; v.noiseMix = 1.0f; v.highpass = true; decMs = 900.0f; break;
            case 51: case 53: case 59:                          // rides
                v.toneMix = 0.0f; v.noiseMix = 1.0f; v.highpass = true; decMs = 600.0f; break;
            case 54:                                            // tambourine
                v.toneMix = 0.0f; v.noiseMix = 1.0f; v.highpass = true; decMs = 90.0f; break;
            case 56:                                            // cowbell
                v.freq = 560.0f; v.freqEnd = 560.0f; v.toneMix = 0.8f; v.noiseMix = 0.0f; decMs = 150.0f; break;
            default: break;                                     // misc percussion: short click
        }
        v.envMul = perSampleDecay(decMs);
    }

    // --------------------------------------------------------- rendering
    inline float osc(uint8_t w, uint32_t p, uint32_t& rng) const
    {
        switch (w) {
            case W_SINE:   return (float)sine_[p >> 21];
            case W_TRI: {
                int32_t q = (int32_t)(p >> 16);
                return (float)(q < 32768 ? q * 2 - 32768 : (65535 - q) * 2 - 32768);
            }
            case W_SQUARE: return (p & 0x80000000u) ? 32767.0f : -32768.0f;
            case W_PULSE:  return (p < 0x40000000u) ? 32767.0f : -32768.0f;
            case W_SAW:    return (float)((int32_t)(p >> 16) - 32768);
            default: {
                rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
                return (float)((int32_t)rng >> 16);
            }
        }
    }

    void renderTone(Voice& v, int32_t* acc, int frames, float chGain, float bendF)
    {
        const uint32_t inc1 = (uint32_t)(v.baseInc1 * bendF);
        const uint32_t inc2 = (uint32_t)(v.baseInc2 * bendF);
        const float g = v.level * chGain * v.waveGain;
        for (int i = 0; i < frames; i++) {
            v.phase1 += inc1;
            float s = osc(v.wave, v.phase1, v.rng);
            if (v.dual) {
                v.phase2 += inc2;
                s = (s + osc(v.wave, v.phase2, v.rng)) * 0.5f;
            }
            v.lp += (s - v.lp) * v.lpK;

            switch (v.stage) {
                case 0:
                    v.env += v.attackInc;
                    if (v.env >= 1.0f) { v.env = 1.0f; v.stage = (v.sustain >= 0.999f) ? 2 : 1; }
                    break;
                case 1:
                    v.env *= v.decayMul;
                    if (v.sustain > 0.0005f) {
                        if (v.env <= v.sustain) { v.env = v.sustain; v.stage = 2; }
                    } else if (v.env < 0.0005f) { v.active = false; return; }
                    break;
                case 3:
                    v.env *= v.releaseMul;
                    if (v.env < 0.0005f) { v.active = false; return; }
                    break;
                default: break;   // sustain: hold
            }
            acc[i] += (int32_t)(v.lp * v.env * g);
        }
    }

    void renderDrum(Voice& v, int32_t* acc, int frames, float chGain)
    {
        const float g = v.level * chGain;
        for (int i = 0; i < frames; i++) {
            if (v.freq > v.freqEnd) { v.freq *= v.sweepMul; if (v.freq < v.freqEnd) v.freq = v.freqEnd; }
            v.phase1 += (uint32_t)(v.freq * incScale_);
            float tone = (float)sine_[v.phase1 >> 21];
            v.rng ^= v.rng << 13; v.rng ^= v.rng >> 17; v.rng ^= v.rng << 5;
            float n = (float)((int32_t)v.rng >> 16);
            if (v.highpass) { float h = n - v.hpPrev; v.hpPrev = n; n = h * 0.5f; }
            v.env *= v.envMul;
            if (v.env < 0.001f) { v.active = false; return; }
            acc[i] += (int32_t)((tone * v.toneMix + n * v.noiseMix) * v.env * g);
        }
    }

    // One voice at full velocity/volume peaks around kVoiceScale * 32767.
    static constexpr float kVoiceScale = 0.28f;

    float sr_ = 22050.0f, master_ = 0.6f, incScale_ = 0.0f;
    uint32_t counter_ = 0;
    int16_t sine_[kSineSize];
    float noteHz_[128];
    Voice voices_[kVoices];
    Channel ch_[kChannels];
};

} // namespace tdeck
