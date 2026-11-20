// RageRecomp: native Primal Rage (Atari GT) static recompilation.
//
// Usage:
//   ragerecomp --roms DIR [--frames N] [--shot out.ppm] [--wav out.wav]
//   ragerecomp --roms DIR --sdl
//
// Bring-your-own ROMs: DIR must hold primrage.zip (merged MAME set).
// The TMS320C31 boot ROM (tms32031.zip/c31boot.bin) is picked up from
// DIR when present; DSP audio is degraded without it.
// Headless mode runs N frames and optionally dumps a screenshot and
// the mixed DAC audio. --sdl opens an interactive window (needs SDL2).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "machine.h"
#include "mem.h"
#include "roms.h"
#include "game.h"
#include "gtvid.h"
#include "rec.h"
#include "rec_build.h"
#include "state.h"
#include "cage.h"

#ifdef RR_SDL
#include <SDL.h>
#endif

static void usage(const char *argv0) {
    printf("usage: %s --roms DIR [--frames N] [--shot out.ppm] [--wav out.wav]\n"
           "          [--script EVENTS] [--hash]\n"
           "       %s --roms DIR --sdl\n"
           "\n"
           "--script: comma-separated \"INPUT FRAME[+FRAMES]\" presses, held\n"
           "  6 frames unless given, e.g. \"coin1 60, start1 200, p1b1 400+30\".\n"
           "  Inputs: coin1 coin2 service start1 start2, p1up p1down p1left\n"
           "  p1right p1b1 p1b2 p1b3, and the same for p2.\n"
           "--hash: print a hash of the whole machine state at the end.\n",
            argv0, argv0);
}

// ------------------------------------------------------------ scripted input

typedef struct {
    int frame, hold;
    char name[16];
} press_t;

static press_t script[512];
static int nscript;

static int parse_script(const char *s) {
    while (*s) {
        press_t *e;
        int n = 0;
        while (*s == ' ' || *s == ',') s++;
        if (!*s) break;
        if (nscript >= (int)(sizeof(script) / sizeof(script[0]))) return -1;
        e = &script[nscript];
        e->hold = 6;
        if (sscanf(s, "%15s %d%n", e->name, &e->frame, &n) < 2) return -1;
        s += n;
        if (*s == '+') {
            if (sscanf(s + 1, "%d%n", &e->hold, &n) < 1) return -1;
            s += 1 + n;
        }
        nscript++;
    }
    return 0;
}

// Cabinet bit for an input name: *byte points at the port byte (active
// low), or *flag at a held-flag. Returns 0 if unknown.
static int input_bit(const char *name, uint8_t **byte, uint8_t **flag) {
    static const char *keys[8] = { "start", "b1", "b2", "b3", "right",
        "left", "down", "up" };
    int i;
    *byte = *flag = NULL;
    if (!strcmp(name, "coin1")) { *flag = &mach.in.coin1; return 1; }
    if (!strcmp(name, "coin2")) { *flag = &mach.in.coin2; return 1; }
    if (!strcmp(name, "service")) { *flag = &mach.in.service; return 1; }
    if (!strcmp(name, "start1")) name = "p1start";
    if (!strcmp(name, "start2")) name = "p2start";
    if ((name[0] != 'p') || (name[1] != '1' && name[1] != '2')) return 0;
    for (i = 0; i < 8; i++) {
        if (!strcmp(name + 2, keys[i])) {
            *byte = name[1] == '1' ? &mach.in.b3 : &mach.in.b1;
            return 1 << i;
        }
    }
    return 0;
}

// Apply the script's held inputs for this frame (before it runs).
static void script_frame(int frame) {
    int i;
    mach.in.b1 = mach.in.b3 = 0xff;
    mach.in.coin1 = mach.in.coin2 = mach.in.service = 0;
    for (i = 0; i < nscript; i++) {
        uint8_t *byte, *flag;
        int bit = input_bit(script[i].name, &byte, &flag);
        if (frame < script[i].frame || frame >= script[i].frame + script[i].hold)
            continue;
        if (byte) *byte &= (uint8_t)~bit;
        if (flag) *flag = 1;
    }
}

static uint64_t state_hash(void) {
    uint32_t size, i;
    uint8_t *blob;
    uint64_t h = 1469598103934665603ull;
    machine_state_size(&size);
    blob = (uint8_t *)calloc(size, 1);
    if (!blob) return 0;
    machine_state_save(blob);
    for (i = 0; i < size; i++) {
        h ^= blob[i];
        h *= 1099511628211ull;
    }
    free(blob);
    return h;
}

static void write_ppm(const char *name) {
    FILE *f = fopen(name, "wb");
    int x, y;
    if (!f) { fprintf(stderr, "cannot write %s\n", name); return; }
    fprintf(f, "P6\n336 240\n255\n");
    for (y = 0; y < 240; y++) {
        for (x = 0; x < 336; x++) {
            uint32_t p = mach.frame_rgb[y * 336u + x];
            uint8_t b[3] = { (uint8_t)(p >> 16), (uint8_t)(p >> 8),
                (uint8_t)p };
            fwrite(b, 1, 3, f);
        }
    }
    fclose(f);
    printf("wrote %s\n", name);
}

static void write_wav(const char *name, const int16_t *pcm, uint32_t frames,
        uint32_t rate) {
    FILE *f = fopen(name, "wb");
    uint32_t bytes = frames * 4, riff = bytes + 36, br;
    uint8_t h[44];
    if (!f) { fprintf(stderr, "cannot write %s\n", name); return; }
    if (!rate) rate = 22050;
    br = rate * 4;
    memset(h, 0, sizeof(h));
    memcpy(h + 0, "RIFF", 4);
    h[4] = (uint8_t)riff; h[5] = (uint8_t)(riff >> 8);
    h[6] = (uint8_t)(riff >> 16); h[7] = (uint8_t)(riff >> 24);
    memcpy(h + 8, "WAVEfmt ", 8);
    h[16] = 16;
    h[20] = 1; h[22] = 2;
    h[24] = (uint8_t)rate; h[25] = (uint8_t)(rate >> 8);
    h[26] = (uint8_t)(rate >> 16); h[27] = (uint8_t)(rate >> 24);
    h[28] = (uint8_t)br; h[29] = (uint8_t)(br >> 8);
    h[30] = (uint8_t)(br >> 16); h[31] = (uint8_t)(br >> 24);
    h[32] = 4; h[34] = 16;
    memcpy(h + 36, "data", 4);
    h[40] = (uint8_t)bytes; h[41] = (uint8_t)(bytes >> 8);
    h[42] = (uint8_t)(bytes >> 16); h[43] = (uint8_t)(bytes >> 24);
    fwrite(h, 1, 44, f);
    fwrite(pcm, 1, bytes, f);
    fclose(f);
    printf("wrote %s (%u frames)\n", name, frames);
}

// Use translated code: linked in, cached from an earlier run, or built
// now. Without it the game still runs, on the interpreters.
static void prepare_game_code(void) {
    const char *cache = getenv("RR_CACHE_DIR") ? getenv("RR_CACHE_DIR") :
            "cache";
    if (!getenv("RR_NOREC") && !rec_have_m68k() && rec_build_m68k(cache))
        fprintf(stderr, "rec: 68K code not translated, interpreting\n");
    if (!getenv("RR_NODSP") && !rec_have_dsp() && rec_build_dsp(cache))
        fprintf(stderr, "rec: DSP code not translated, interpreting\n");
    machine_reset();    // the DSP build ran a warm-up boot
}

static int boot_machine(const char *romdir) {
    char z0[1024], z1[1024];
    const char *zips[2];
    const game_profile_t *prof;
    roms_t *r;
    snprintf(z0, sizeof(z0), "%s/primrage.zip", romdir);
    snprintf(z1, sizeof(z1), "%s/tms32031.zip", romdir);
    zips[0] = z0;
    zips[1] = z1;
    {
        FILE *probe = fopen(z1, "rb");
        int nz = 1;
        if (probe) { fclose(probe); nz = 2; }
        else printf("note: %s missing, DSP boot ROM unavailable\n", z1);
        prof = game_profile(GAME_PRIMRAGE);
        mach_profile = prof;
        r = roms_open(zips, nz);
        if (roms_load_set(r, prof)) {
            fprintf(stderr, "ROM load failed (need %s)\n", z0);
            return 1;
        }
    }
    gtvid_decode();
    gtvid_mo_init();
    rec_init();
    machine_reset();
    prepare_game_code();
    mach.in.b1 = 0xff;
    mach.in.b3 = 0xff;
    return 0;
}

#ifdef RR_SDL
// Minimal interactive frontend: video, audio, keyboard.
static const int kHz = 44100;

static void sdl_key(gt_inputs_t *in, SDL_Keycode k, int down) {
    uint8_t bit = 0, player = 0;
    // P1: arrows + Z/X/C + Enter(start). P2: unused.
    switch (k) {
    case SDLK_RETURN: bit = 0x01; player = 1; break;
    case SDLK_z: bit = 0x02; player = 1; break;
    case SDLK_x: bit = 0x04; player = 1; break;
    case SDLK_c: bit = 0x08; player = 1; break;
    case SDLK_RIGHT: bit = 0x10; player = 1; break;
    case SDLK_LEFT: bit = 0x20; player = 1; break;
    case SDLK_DOWN: bit = 0x40; player = 1; break;
    case SDLK_UP: bit = 0x80; player = 1; break;
    default: break;
    }
    if (player) {
        if (down) mach.in.b3 &= (uint8_t)~bit;
        else mach.in.b3 |= bit;
        (void)in;
        return;
    }
    if (k == SDLK_5 && down) mach.in.coin1 = 1;
    if (k == SDLK_5 && !down) mach.in.coin1 = 0;
    if (k == SDLK_6 && down) mach.in.coin2 = 1;
    if (k == SDLK_6 && !down) mach.in.coin2 = 0;
}

static int run_sdl(void) {
    SDL_Window *win;
    SDL_Renderer *ren;
    SDL_Texture *tex;
    SDL_AudioDeviceID audio;
    SDL_AudioSpec want, got;
    int running = 1;
    static int16_t mL[4096], mR[4096];
    static int16_t stereo[4096 * 2];
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    win = SDL_CreateWindow("RageRecomp - Primal Rage",
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            336 * 2, 240 * 2, 0);
    if (!win) { fprintf(stderr, "window: %s\n", SDL_GetError()); return 1; }
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED |
            SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) { fprintf(stderr, "renderer: %s\n", SDL_GetError()); return 1; }
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_XRGB8888,
            SDL_TEXTUREACCESS_STREAMING, 336, 240);
    if (!tex) { fprintf(stderr, "texture: %s\n", SDL_GetError()); return 1; }
    memset(&want, 0, sizeof(want));
    want.freq = cage.dac_rate ? (int)cage.dac_rate : kHz;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    audio = SDL_OpenAudioDevice(NULL, 0, &want, &got, 0);
    if (!audio) {
        fprintf(stderr, "audio: %s (continuing silent)\n", SDL_GetError());
    } else {
        SDL_PauseAudioDevice(audio, 0);
    }
    printf("keys: arrows/Z/X/C + Enter=P1, 5/6=coins, Esc=quit\n");
    while (running) {
        SDL_Event ev;
        uint32_t mixed;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = 0;
            else if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE)
                running = 0;
            else if (ev.type == SDL_KEYDOWN)
                sdl_key(&mach.in, ev.key.keysym.sym, 1);
            else if (ev.type == SDL_KEYUP)
                sdl_key(&mach.in, ev.key.keysym.sym, 0);
        }
        machine_frame();
        // Drain produced samples into the audio queue (drop when backed up).
        mixed = cage_queued();
        if (mixed > 2048) mixed = 2048;
        if (mixed) {
            uint32_t i, got;
            got = cage_mix(mL, mR, mixed);
            for (i = 0; i < got; i++) {
                stereo[i * 2] = mL[i];
                stereo[i * 2 + 1] = mR[i];
            }
            if (audio && got && SDL_GetQueuedAudioSize(audio) < 16384)
                SDL_QueueAudio(audio, stereo, got * 4);
        }
        SDL_UpdateTexture(tex, NULL, mach.frame_rgb, 336 * 4);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);
    }
    if (audio) SDL_CloseAudioDevice(audio);
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
#endif

int main(int argc, char **argv) {
    const char *romdir = NULL;
    const char *shot = NULL;
    const char *wav = NULL;
    int frames = 600;
    int want_sdl = 0, want_hash = 0;
    int i;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--roms") && i + 1 < argc) romdir = argv[++i];
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc)
            frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc) shot = argv[++i];
        else if (!strcmp(argv[i], "--wav") && i + 1 < argc) wav = argv[++i];
        else if (!strcmp(argv[i], "--sdl")) want_sdl = 1;
        else if (!strcmp(argv[i], "--hash")) want_hash = 1;
        else if (!strcmp(argv[i], "--script") && i + 1 < argc) {
            if (parse_script(argv[++i])) {
                fprintf(stderr, "bad --script\n");
                return 1;
            }
            {
                int k;
                for (k = 0; k < nscript; k++) {
                    uint8_t *b, *fl;
                    if (!input_bit(script[k].name, &b, &fl)) {
                        fprintf(stderr, "unknown input in --script: %s\n",
                                script[k].name);
                        return 1;
                    }
                }
            }
        }
        else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "unknown arg: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }
    if (!romdir) {
        usage(argv[0]);
        return 1;
    }
    if (boot_machine(romdir)) return 1;
#ifdef RR_SDL
    if (want_sdl) return run_sdl();
#else
    if (want_sdl) {
        fprintf(stderr, "SDL frontend not built in\n");
        return 1;
    }
#endif
    if (frames < 0) frames = 0;
    if (wav) {
        // Drain whatever the DAC produces each frame.
        uint32_t cap = (uint32_t)frames * 1200 + 4096;
        int16_t *pcm = (int16_t *)calloc((size_t)cap * 2, 2);
        static int16_t mL[4096], mR[4096];
        uint32_t got_frames = 0;
        if (!pcm) { fprintf(stderr, "out of memory\n"); return 1; }
        for (i = 0; i < frames; i++) {
            uint32_t n, k, got;
            if (nscript) script_frame(i);
            machine_frame();
            n = cage_queued();
            if (n > 4096) n = 4096;
            if (got_frames + n > cap) n = cap - got_frames;
            got = cage_mix(mL, mR, n);
            for (k = 0; k < got; k++) {
                pcm[(got_frames + k) * 2] = mL[k];
                pcm[(got_frames + k) * 2 + 1] = mR[k];
            }
            got_frames += got;
        }
        printf("dac rate %u Hz\n", cage.dac_rate);
        write_wav(wav, pcm, got_frames, cage.dac_rate);
        free(pcm);
    } else {
        for (i = 0; i < frames; i++) {
            if (nscript) script_frame(i);
            machine_frame();
        }
    }
    printf("ran %d frames (68K pc=%06X, DSP pc=%06X)\n", frames,
            rrm->cpu->pc & 0xffffffu, rrm->dsp->pc & 0xffffffu);
    if (shot) write_ppm(shot);
    if (want_hash)
        printf("state hash %016llx\n", (unsigned long long)state_hash());
    rec_shutdown();
    return 0;
}
