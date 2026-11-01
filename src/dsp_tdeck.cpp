/**
 * dsp_tdeck.cpp - audio output for OpenDUNE on the T-Deck: voice/sound-effect
 * clips AND music, mixed together and streamed to the speaker over I2S.
 *
 * Implements src/audio/dsp.h for the engine's one-shot voice/effect clips, and
 * additionally mixes in the software synthesizer that midi_tdeck.cpp feeds with
 * the game's music (see TDeck_Music_Render).
 *
 * DESIGN: one audio task runs forever, producing fixed-size blocks of audio
 * (kBlockFrames stereo frames): music synth + the currently playing clip are
 * summed, soft-limited and written to I2S. The blocking i2s_write() paces the
 * loop in real time. Because the stream never stops (silence is just zeros), the
 * old "stutter after a clip ends" problem - the I2S DMA replaying stale data -
 * cannot happen.
 *
 * VOC PARSING: unchanged from the reference dsp_sdl.c. The `data` buffer handed
 * to DSP_Play() is a Creative Voice File:
 *   - offset 0x14: little-endian uint16 offset of the first data block (0x1A)
 *   - there: block type (1 = sound data), 3-byte LE length, frequency divisor,
 *     codec (0 = unsigned 8-bit PCM, all Dune II uses), then the samples.
 *   - sample rate = 1000000 / (256 - freqDivisor)
 */
#include <Arduino.h>
#include <stdlib.h>
#include <string.h>
#include <driver/i2s.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

extern "C" {
#include "../lib/opendune/src/types.h"
#include "../lib/opendune/src/audio/dsp.h"
}

#include "tdeck_pins.h"
#include "tdeck_config.h"
#include "tdeck_music.h"

#define I2S_PORT I2S_NUM_0

enum { kBlockFrames = 128 };

static TaskHandle_t s_audioTask = NULL;
static SemaphoreHandle_t s_lock = NULL;   // guards the clip state below

// --- currently playing clip (our own copy, so the caller's buffer may vanish) ---
static uint8_t *s_clipData = NULL;
static uint32_t s_clipCapacity = 0;
static uint32_t s_clipLen = 0;
static uint32_t s_clipPos = 0;            // 16.16 fixed point index into s_clipData
static uint32_t s_clipStep = 0;           // 16.16 source samples per output sample
static bool s_clipPlaying = false;

// 0 = idle, 2 = playing (driver.c: Sound_IsPlaying() checks == 2)
static volatile uint8_t s_status = 0;

static int32_t s_acc[kBlockFrames];
static int16_t s_out[kBlockFrames * 2];

// Gentle limiter: transparent below the knee, compresses 4:1 above it, then
// hard-stops at the 16-bit limits. Keeps dense music + loud effects from
// harsh digital clipping.
static inline int16_t SoftClip(int32_t x)
{
	const int32_t knee = 24000;
	if (x > knee)       { x = knee + ((x - knee) >> 2);   if (x > 32767)  x = 32767; }
	else if (x < -knee) { x = -knee + ((x + knee) >> 2);  if (x < -32768) x = -32768; }
	return (int16_t)x;
}

// Produce `frames` stereo frames into out[] (interleaved L,R; the amp is mono
// so both channels carry the same signal).
static void MixBlock(int16_t *out, int frames)
{
	memset(s_acc, 0, sizeof(int32_t) * (size_t)frames);

	TDeck_Music_Render(s_acc, frames);

	xSemaphoreTake(s_lock, portMAX_DELAY);
	if (s_clipPlaying) {
		for (int i = 0; i < frames; i++) {
			uint32_t idx = s_clipPos >> 16;
			if (idx >= s_clipLen) {
				s_clipPlaying = false;
				s_status = 0;
				break;
			}
			int32_t s = ((int32_t)s_clipData[idx] - 128) << 8;   // unsigned 8-bit -> signed 16-bit
			s_acc[i] += (s * 13) >> 4;                           // ~0.81: leave headroom for music
			s_clipPos += s_clipStep;
		}
	}
	xSemaphoreGive(s_lock);

	for (int i = 0; i < frames; i++) {
		int16_t v = SoftClip(s_acc[i]);
		out[2 * i] = v;
		out[2 * i + 1] = v;
	}
}

static void AudioTask(void *arg)
{
	(void)arg;
	for (;;) {
		MixBlock(s_out, kBlockFrames);
		size_t written = 0;
		i2s_write(I2S_PORT, s_out, sizeof(s_out), &written, portMAX_DELAY);
	}
}

extern "C" bool DSP_Init(void)
{
	s_lock = xSemaphoreCreateMutex();
	if (s_lock == NULL) return false;

	i2s_config_t cfg = {};
	cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
	cfg.sample_rate = TDECK_AUDIO_SAMPLE_RATE;
	cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
	cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
	cfg.communication_format = (i2s_comm_format_t)I2S_COMM_FORMAT_STAND_I2S;
	cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
	cfg.dma_buf_count = 4;
	cfg.dma_buf_len = 256;
	cfg.use_apll = false;

	if (i2s_driver_install(I2S_PORT, &cfg, 0, NULL) != ESP_OK) return false;

	i2s_pin_config_t pins = {};
	/* CRITICAL: i2s_pin_config_t has an MCLK field (mck_io_num) that the
	 * "= {}" above zero-initializes to 0 - and in I2S master mode the
	 * legacy driver then routes the I2S master clock OUT to GPIO0
	 * ("I2S0, MCLK output by GPIO0"). GPIO0 is the trackball's click
	 * button on the T-Deck: with the I2S peripheral driving that pad, it
	 * reads LOW 100% of the time and the button can never change it.
	 * That is exactly the long-standing "trackball click does nothing,
	 * pin always LOW, on every unit" symptom. We don't use MCLK (the
	 * speaker amp only needs BCK/WS/DATA), so tell the driver to leave
	 * it alone. */
	pins.mck_io_num = I2S_PIN_NO_CHANGE;
	pins.bck_io_num = TDECK_I2S_BCK;
	pins.ws_io_num = TDECK_I2S_WS;
	pins.data_out_num = TDECK_I2S_DOUT;
	pins.data_in_num = I2S_PIN_NO_CHANGE;

	if (i2s_set_pin(I2S_PORT, &pins) != ESP_OK) return false;

	s_clipCapacity = 32768;   /* generous first allocation; DSP_Play grows it if needed */
	s_clipData = (uint8_t *)heap_caps_malloc(s_clipCapacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
	if (s_clipData == NULL) s_clipData = (uint8_t *)malloc(s_clipCapacity);
	if (s_clipData == NULL) return false;

	xTaskCreatePinnedToCore(AudioTask, "dsp_tdeck", 8192, NULL, 5, &s_audioTask, 0);

	return true;
}

extern "C" void DSP_Uninit(void)
{
	DSP_Stop();
	if (s_audioTask != NULL) {
		vTaskDelete(s_audioTask);
		s_audioTask = NULL;
	}
	i2s_driver_uninstall(I2S_PORT);
	if (s_clipData != NULL) { heap_caps_free(s_clipData); s_clipData = NULL; }
	if (s_lock != NULL) { vSemaphoreDelete(s_lock); s_lock = NULL; }
}

extern "C" void DSP_Play(const uint8 *data)
{
	if (data == NULL || s_lock == NULL) return;

	/* --- VOC header parsing, mirroring the reference dsp_sdl.c --- */
	uint16_t blockOffset = (uint16_t)(data[0x14] | (data[0x15] << 8));
	const uint8_t *block = data + blockOffset;

	if (block[0] != 1) return; /* not a "sound data" block - unsupported/unexpected */

	uint32_t blockLen = (uint32_t)block[1] | ((uint32_t)block[2] << 8) | ((uint32_t)block[3] << 16);
	uint8_t freqDivisor = block[4];
	uint8_t codec = block[5];
	if (codec != 0) return;    /* only unsigned 8-bit PCM is used by Dune II's assets */
	if (blockLen < 3) return;  /* no samples (and avoids an unsigned underflow below) */

	uint32_t sampleLen = blockLen - 2; /* blockLen includes the divisor+codec bytes */
	const uint8_t *samples = block + 6;
	uint32_t srcRate = 1000000UL / (256 - freqDivisor);

	xSemaphoreTake(s_lock, portMAX_DELAY);

	if (sampleLen > s_clipCapacity) {
		uint8_t *grown = (uint8_t *)heap_caps_malloc(sampleLen, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
		if (grown == NULL) grown = (uint8_t *)malloc(sampleLen);
		if (grown != NULL) {
			heap_caps_free(s_clipData);
			s_clipData = grown;
			s_clipCapacity = sampleLen;
		} else {
			sampleLen = s_clipCapacity; /* best effort: truncate rather than drop the sound entirely */
		}
	}

	memcpy(s_clipData, samples, sampleLen);
	s_clipLen = sampleLen;
	s_clipPos = 0;
	s_clipStep = (uint32_t)(((uint64_t)srcRate << 16) / TDECK_AUDIO_SAMPLE_RATE);
	s_clipPlaying = true;
	s_status = 2;  /* visible to the engine immediately, not only once the audio task picks it up */

	xSemaphoreGive(s_lock);
}

extern "C" void DSP_Stop(void)
{
	if (s_lock == NULL) return;
	xSemaphoreTake(s_lock, portMAX_DELAY);
	s_clipPlaying = false;
	s_status = 0;
	xSemaphoreGive(s_lock);
}

extern "C" uint8 DSP_GetStatus(void)
{
	return s_status;
}
