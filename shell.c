#include "nesmu.h"
#include <SDL.h>

static int16_t audio_dequeue_sample(t_nes *nes) {
    int16_t sample;

    if (nes->shell.num_available <= 0) {
        //        SDL_Log("audio underrun");
        return 0;
    }

    sample = nes->shell.buf[nes->shell.buf_read_index];
    nes->shell.num_available -= 1;
    nes->shell.buf_read_index += 1;
    nes->shell.buf_read_index %= 1024;
    return sample;
}

void audio_enqueue_sample(t_nes *nes, int16_t sample) {
    while (nes->shell.num_available == 1024) {
        SDL_Delay(1);
    }
    nes->shell.buf[nes->shell.buf_write_index] = sample;
    nes->shell.num_available += 1;
    nes->shell.buf_write_index += 1;
    nes->shell.buf_write_index %= 1024;
    return;
}

static void audio_callback(void *userdata, Uint8 *stream, int len) {
    (void)userdata;
    int16_t *ptr = (int16_t *)stream;

    for (int i = 0; i < len / 2; i++) {
        ptr[i] = audio_dequeue_sample(userdata);
    }
}

static int audio_open(t_nes *nes) {
    SDL_AudioSpec spec;

    (void)SDL_memset(&spec, 0, sizeof(SDL_AudioSpec));

    spec.freq = 48000;
    spec.format = AUDIO_S16SYS;
    spec.channels = 1;
    spec.samples = 512;
    spec.size = 512 * 2 * 1;
    spec.callback = &audio_callback;
    spec.userdata = nes;

    nes->shell.audio_device = SDL_OpenAudioDevice(0, 0, &spec, 0, 0);
    if (!nes->shell.audio_device) {
        (void)SDL_Log("%s", SDL_GetError());
        return 1;
    }

    (void)SDL_PauseAudioDevice(nes->shell.audio_device, 0);
    return 0;
}

static int audio_close(t_nes *nes) {
    if (!nes->shell.audio_device) {
        return 1;
    }

    (void)SDL_PauseAudioDevice(nes->shell.audio_device, 1);
    (void)SDL_CloseAudioDevice(nes->shell.audio_device);
    nes->shell.audio_device = 0;
    return 0;
}

int shell_open(t_nes *nes) {
    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        (void)SDL_Log("%s", SDL_GetError());
        return 1;
    }

    return audio_open(nes);
}

int shell_close(t_nes *nes) {
    audio_close(nes);
    SDL_Quit();
    return 0;
}

int poll_events(t_nes *nes, int *done) {
    SDL_Event event;

    (void)SDL_PollEvent(&event);
    *done |= event.type == SDL_QUIT;
    return 0;
}
