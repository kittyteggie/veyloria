#include "audio.h"

#include <math.h>
#include <string.h>

// SUMMARY: Builds short noise-and-tone bursts and plays them for game events.

static Wave MakeSoundWave(float seconds, float frequency, float noiseAmount,
                          float decay, unsigned int *seed) {
  unsigned int sampleRate = 22050;
  unsigned int frameCount = (unsigned int)(seconds * (float)sampleRate);
  if (frameCount < 1)
    frameCount = 1;

  short *samples = (short *)MemAlloc(frameCount * sizeof(short));
  for (unsigned int i = 0; i < frameCount; i++) {
    float t = (float)i / (float)sampleRate;
    float envelope = expf(-decay * t);
    *seed = (*seed) * 1103515245u + 12345u;
    float noise = (float)((*seed >> 16) & 0x7fff) / 16384.0f - 1.0f;
    float tone = sinf(2.0f * PI * frequency * t);
    float sample = tone * (1.0f - noiseAmount) + noise * noiseAmount;
    samples[i] = (short)(sample * envelope * 22000.0f);
  }

  Wave wave = {0};
  wave.frameCount = frameCount;
  wave.sampleRate = sampleRate;
  wave.sampleSize = 16;
  wave.channels = 1;
  wave.data = samples;
  return wave;
}

static Sound MakeSound(float seconds, float frequency, float noiseAmount,
                       float decay, unsigned int *seed, float volume) {
  Wave wave = MakeSoundWave(seconds, frequency, noiseAmount, decay, seed);
  Sound sound = LoadSoundFromWave(wave);
  UnloadWave(wave);
  SetSoundVolume(sound, volume);
  return sound;
}

static void PlayIfReady(const GameAudio *audio, Sound sound) {
  if (audio->ready && IsSoundValid(sound))
    PlaySound(sound);
}

void AudioInit(GameAudio *audio) {
  memset(audio, 0, sizeof(*audio));
  InitAudioDevice();
  if (!IsAudioDeviceReady()) {
    TraceLog(LOG_WARNING, "Audio device unavailable; sound is disabled.");
    return;
  }

  unsigned int seed = 0x9e3779b9u;
  audio->breakSound = MakeSound(0.16f, 160.0f, 0.85f, 16.0f, &seed, 0.9f);
  audio->placeSound = MakeSound(0.10f, 120.0f, 0.30f, 28.0f, &seed, 0.9f);
  audio->stepSound = MakeSound(0.07f, 90.0f, 0.90f, 40.0f, &seed, 0.5f);
  audio->ready = true;
}

void AudioShutdown(GameAudio *audio) {
  if (audio->ready) {
    UnloadSound(audio->breakSound);
    UnloadSound(audio->placeSound);
    UnloadSound(audio->stepSound);
    audio->ready = false;
  }
  if (IsAudioDeviceReady())
    CloseAudioDevice();
}

void AudioPlayBreak(const GameAudio *audio) {
  PlayIfReady(audio, audio->breakSound);
}

void AudioPlayPlace(const GameAudio *audio) {
  PlayIfReady(audio, audio->placeSound);
}

void AudioPlayStep(const GameAudio *audio) {
  PlayIfReady(audio, audio->stepSound);
}
