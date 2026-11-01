#pragma once
/** Bridge between the music backend (midi_tdeck.cpp) and the audio mixer (dsp_tdeck.cpp). */
#include <stdint.h>

/** Called by the audio task once per block: applies any MIDI events the game's
 *  sequencer has queued, then ADDS `frames` mono music samples into acc[]. */
void TDeck_Music_Render(int32_t *acc, int frames);
