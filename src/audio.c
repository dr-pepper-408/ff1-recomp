/**
 * @file audio.c
 * @brief Plays the APU's output through an SDL audio stream.
 *
 * One NES frame (60.0988 Hz) of samples is rendered and queued after each
 * frame of game logic.  The queue is kept short so sound stays in step
 * with the picture: when more than ~100 ms is waiting (the game ran ahead
 * of the device), a frame's samples are dropped.
 */

#include <SDL3/SDL.h>
#include "audio.h"
#include "apu.h"
#include "config.h"

#define SAMPLE_RATE 44100
#define MAX_QUEUED_BYTES (SAMPLE_RATE * 2 / 10)

static SDL_AudioStream* stream;
static double frame_samples;        /* fractional samples carried between frames */

int Audio_Init(void) {
    Apu_Reset();
    SDL_AudioSpec spec = { SDL_AUDIO_S16, 1, SAMPLE_RATE };
    stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (!stream) return 0;
    SDL_ResumeAudioStreamDevice(stream);
    return 1;
}

void Audio_Frame(void) {
    static int16_t buf[2048];
    frame_samples += SAMPLE_RATE * (NES_FRAME_NS / 1e9);
    int n = (int)frame_samples;
    frame_samples -= n;
    if (n > (int)(sizeof(buf) / sizeof(buf[0]))) n = sizeof(buf) / sizeof(buf[0]);
    Apu_Render(buf, n, SAMPLE_RATE);
    if (stream && SDL_GetAudioStreamQueued(stream) < MAX_QUEUED_BYTES)
        SDL_PutAudioStreamData(stream, buf, n * (int)sizeof(int16_t));
}

void Audio_Shutdown(void) {
    if (stream) SDL_DestroyAudioStream(stream);
    stream = NULL;
}
