#ifndef VEYLORIA_AUDIO_H
#define VEYLORIA_AUDIO_H

#include <stdbool.h>

#include "raylib.h"

// SUMMARY: Procedurally synthesised block and footstep sounds.

typedef struct GameAudio {
  Sound breakSound;
  Sound placeSound;
  Sound stepSound;
  bool ready;
} GameAudio;

void AudioInit(GameAudio *audio);
void AudioShutdown(GameAudio *audio);
void AudioPlayBreak(const GameAudio *audio);
void AudioPlayPlace(const GameAudio *audio);
void AudioPlayStep(const GameAudio *audio);

#endif
