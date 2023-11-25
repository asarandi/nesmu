#include "nesmu.h"
#include <SDL.h>
#include <assert.h>

static int16_t audio_dequeue_sample(t_nes *nes, bool *underrun) {
    int16_t sample;

    if (nes->shell.num_available <= 0) {
        if (!*underrun) {
            SDL_Log("audio underrun");
            *underrun = true;
        }
        return 0;
    }

    sample = nes->shell.buf[nes->shell.buf_read_index];
    nes->shell.num_available -= 1;
    nes->shell.buf_read_index += 1;
    nes->shell.buf_read_index %= 2048;
    return sample;
}

void audio_enqueue_sample(t_nes *nes, int16_t sample) {
    while (nes->shell.num_available > 1536) {
        SDL_Delay(1);
    }
    nes->shell.buf[nes->shell.buf_write_index] = sample;
    nes->shell.num_available += 1;
    nes->shell.buf_write_index += 1;
    nes->shell.buf_write_index %= 2048;
    return;
}

static void audio_callback(void *userdata, Uint8 *stream, int len) {
    (void)userdata;
    int16_t *ptr = (int16_t *)stream;
    bool underrun = false;

    for (int i = 0; i < len / 2; i++) {
        ptr[i] = audio_dequeue_sample(userdata, &underrun);
    }
}

static int audio_open(t_nes *nes) {
    SDL_AudioSpec spec;

    (void)SDL_memset(&spec, 0, sizeof(SDL_AudioSpec));

    spec.freq = 48000;
    spec.format = AUDIO_S16;
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

static int video_open(t_nes *nes) {
    nes->shell.window = SDL_CreateWindow("nesmu", 0, 0, 256, 240, 0);
    if (!nes->shell.window) {
        SDL_Log("%s", SDL_GetError());
        return 1;
    }

    nes->shell.renderer = SDL_CreateRenderer(nes->shell.window, -1, 0);
    if (!nes->shell.renderer) {
        SDL_Log("%s", SDL_GetError());
        return 1;
    }

    nes->shell.texture =
        SDL_CreateTexture(nes->shell.renderer, SDL_PIXELFORMAT_ARGB8888,
                          SDL_TEXTUREACCESS_STREAMING, 256, 240);
    if (!nes->shell.texture) {
        SDL_Log("%s", SDL_GetError());
        return 1;
    }

    return 0;
}

static int video_close(t_nes *nes) {
    if (nes->shell.texture) {
        SDL_DestroyTexture(nes->shell.texture);
        nes->shell.texture = NULL;
    }

    if (nes->shell.renderer) {
        SDL_DestroyRenderer(nes->shell.renderer);
        nes->shell.renderer = NULL;
    }

    if (nes->shell.window) {
        SDL_DestroyWindow(nes->shell.window);
        nes->shell.window = NULL;
    }

    return 0;
}

int video_write(t_nes *nes) {
    uint32_t *pixels;
    int pitch, y, x;

    if (SDL_LockTexture(nes->shell.texture, 0, (void *)&pixels, &pitch) < 0) {
        SDL_Log("%s", SDL_GetError());
        return 1;
    }

    for (y = 0; y < 240; y++) {
        for (x = 0; x < 256; x++) {
            pixels[y * 256 + x] = 0x0000ff00;
        }
    }

    SDL_UnlockTexture(nes->shell.texture);
    SDL_RenderClear(nes->shell.renderer);
    SDL_RenderCopy(nes->shell.renderer, nes->shell.texture, NULL, NULL);
    SDL_RenderPresent(nes->shell.renderer);
    return 0;
}

int shell_open(t_nes *nes) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        (void)SDL_Log("%s", SDL_GetError());
        return 1;
    }

    return audio_open(nes) || video_open(nes);
}

int shell_close(t_nes *nes) {
    video_close(nes);
    audio_close(nes);
    SDL_Quit();
    return 0;
}

int poll_events(t_nes *nes, int *done) {
    SDL_Event event;

    (void)SDL_PollEvent(&event);

    *done |= event.type == SDL_QUIT;
    *done |= event.key.keysym.sym == SDLK_ESCAPE;
    if (event.type != SDL_KEYDOWN && event.type != SDL_KEYUP)
        return 0;

    // Status for each controller is returned as an 8-bit report in the
    // following order: A, B, Select, Start, Up, Down, Left, Right.

    SDL_Keycode tab[] = {SDLK_x,  SDLK_z,    SDLK_RSHIFT, SDLK_RETURN,
                         SDLK_UP, SDLK_DOWN, SDLK_LEFT,   SDLK_RIGHT};

    for (int i = 0; i < 8; i++) {
        if (event.key.keysym.sym == tab[i]) {
            if (event.type == SDL_KEYDOWN)
                nes->shell.joy1 |= 1 << (7 - i);
            else
                nes->shell.joy1 &= ~(1 << (7 - i));
        }
    }

    return 0;
}
