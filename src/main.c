// RageRecomp: native port of the arcade game Primal Rage (Atari GT).
//
// The program: command line, finding the player's MAME files, the game
// version (MAME set), the cabinet's EEPROM, translated code, headless runs.
// The window, menu, video, audio and controls are in frontend.c.
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#ifdef _WIN32
#include <windows.h>
#include <process.h>
#else
#include <unistd.h>
#endif

#include "app.h"
#include "machine.h"
#include "mem.h"
#include "roms.h"
#include "game.h"
#include "gtvid.h"
#include "rec.h"
#include "rec_build.h"
#include "state.h"
#include "cage.h"
#include "eeprom.h"
#include "paths.h"
#include "launch.h"
#include "version.h"
#include "controls.h"
#include "m68k.h"
#include "m68kdis.h"
#include "net.h"
#include "netplay.h"

#ifdef RR_SDL
#include <SDL2/SDL.h>
#include "config.h"
#include "icon.h"
#include "picker.h"
#include "training.h"
#endif

static const char *roms_dir;        // --roms or the [game] roms setting
static int headless_mode;

static void usage(void) {
    printf("usage: ragerecomp [options] [PATH]\n"
           "  PATH            play the game in PATH (a MAME set's zip, or a folder\n"
           "                  holding the zips), as launchers start it: Esc quits\n"
           "                  instead of opening the menu (F1 opens it)\n"
           "  --version       print the program's version and exit\n"
           "  --roms DIR      folder holding primrage.zip and tms32031.zip\n"
           "  --game-version SET  primrage (2.3, Jan 1995, default), primrageo\n"
           "                  (2.3, Dec 1994) or primrage20 (2.0)\n"
           "  --data-dir DIR  keep settings, saves, states, screenshots and the\n"
           "                  built game code in DIR (made if needed); DIR/run is\n"
           "                  searched for the game files\n"
#ifdef RR_SDL
           "  --fullscreen, --windowed  start that way (this session only)\n"
           "  --set SECTION.KEY=VALUE  any setting of ragerecomp.ini, e.g.\n"
           "                  --set video.filter=crt (this session only)\n"
           "  --test-shaders  load and draw every shader preset, report each\n"
           "  --host [PORT]   host a netplay game (default port from ragerecomp.ini, 7845)\n"
           "  --join HOST[:PORT]  join a netplay game\n"
#endif
           "Headless (no window, no audio device):\n"
           "  --headless N    run N frames (also --frames N)\n"
           "  --script EVENTS comma-separated \"INPUT FRAME[+FRAMES]\" presses,\n"
           "                  held 6 frames unless given, e.g.\n"
           "                  \"coin1 600, coin1 620, start1 700, p1b1 900+30\".\n"
           "                  Inputs: coin1 coin2 service start1 start2, p1up\n"
           "                  p1down p1left p1right p1b1..p1b4, the same for p2\n"
           "  --hash          print a hash of the whole machine state at the end\n"
           "  --shot FILE     save the last frame as a PPM\n"
           "  --wav FILE      record the audio\n"
           "  --save-at F FILE  save a state after frame F\n"
           "  --load FILE     start from a state file\n"
           "  --nvram FILE    use (and keep) this EEPROM image (default: blank)\n"
           "  --shot-every N  save frame_NNNNN.ppm every N frames\n"
           "  --rewind-test N every N frames rewind N, replay, check the state matches\n"
           "  --runahead N    run N frames ahead and roll back after each frame\n"
           "  --trace         log every 68K instruction (interpreter); --trace-from F\n"
           "  --dis ADDR N    disassemble N 68K instructions at ADDR (hex)\n"
           "  --net-host PORT / --net-join HOST:PORT  headless netplay test: play this\n"
           "                  side of --script until --headless N, print the hash\n"
           "  --net-delay N, --net-fps F, --net-sim LATENCY:JITTER:LOSS%%  (testing)\n"
           "  --emit-linked DIR  write the translated game code into DIR, for\n"
           "                  `make recomp` (game-derived: never share it)\n");
}

/* ---------------------------------------------------------------- */
/* Messages                                                         */
/* ---------------------------------------------------------------- */

void app_report(bool fatal, const char *fmt, ...) {
    char msg[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    fprintf(stderr, "%s\n", msg);
#ifdef RR_SDL
    if (!headless_mode && SDL_ShowSimpleMessageBox(fatal ? SDL_MESSAGEBOX_ERROR :
            SDL_MESSAGEBOX_WARNING, PRODUCT, msg, NULL) != 0)
        picker_message(PRODUCT, msg);   // no zenity and no X core fonts: our own window
#else
    (void)fatal;
#endif
}

/* ---------------------------------------------------------------- */
/* Game files                                                       */
/* ---------------------------------------------------------------- */

static const game_profile_t *prof;  // the set in use

// The zips a set may draw on, in order: its own, the parent's (shared
// graphics and sound), the C31 boot ROM. Returns how many paths.
static int set_zips(const game_profile_t *p, char paths[3][4200]) {
    char name[64], buf[4200];
    int n = 0;
    snprintf(name, sizeof(name), "%s.zip", p->set);
    snprintf(paths[n++], 4200, "%s", find_data(name, roms_dir, buf, sizeof(buf)));
    if (strcmp(p->set, "primrage"))
        snprintf(paths[n++], 4200, "%s", find_data("primrage.zip", roms_dir, buf, sizeof(buf)));
    snprintf(paths[n++], 4200, "%s", find_data("tms32031.zip", roms_dir, buf, sizeof(buf)));
    return n;
}

bool app_set_there(game_id_t id) {
    char paths[3][4200];
    const char *z[3];
    const game_profile_t *p = game_profile(id);
    roms_t *r;
    int n, i, ok;
    if (!p) return false;
    n = set_zips(p, paths);
    for (i = 0; i < n; i++) z[i] = paths[i];
    r = roms_open(z, n);
    if (!r) return false;
    ok = roms_has_set(r, p);
    roms_close(r);
    return ok != 0;
}

#ifdef RR_SDL
static bool any_set_there(void) {
    int i;
    for (i = 0; i < GAME_COUNT; i++)
        if (app_set_there((game_id_t)i)) return true;
    return false;
}
#endif

static const char *files_help(void) {
    static char buf[9000], where[8400];
    if (appimage_dir() && strcmp(appimage_dir(), data_dir()))
        snprintf(where, sizeof(where), "a folder named run beside the AppImage:\n    %srun\nor:\n    %srun",
                appimage_dir(), data_dir());
    else
        snprintf(where, sizeof(where), "\n    %srun", data_dir());
    snprintf(buf, sizeof(buf),
            "Put your own MAME files in %s\n(or name their folder with --roms).\n\n"
            "Needed:\n"
            "  primrage.zip  - Primal Rage (for primrageo or primrage20, their zip\n"
            "                  as well, with primrage.zip beside it)\n"
            "  tms32031.zip  - the TMS320C31 boot ROM (c31boot.bin), for sound", where);
    return buf;
}

// Load the set's ROMs into the machine; checks every file against MAME's
// CRCs and says exactly what is missing or different.
static int load_set(const game_profile_t *p) {
    char paths[3][4200], report[4096] = "";
    const char *z[3];
    roms_t *r;
    int n, i, failed;
    n = set_zips(p, paths);
    for (i = 0; i < n; i++) z[i] = paths[i];
    r = roms_open(z, n);
    if (!r) return -1;
    failed = roms_load_set(r, p);
    roms_report(r, report, sizeof(report));
    roms_close(r);
    if (failed) {
        app_report(true, "%s can't start: files of %s are missing.\n\n%s\n%s",
                PRODUCT, p->title, report, files_help());
        return -1;
    }
    if (*report)
        app_report(false, "These files don't match MAME's good dumps of %s:\n\n%s\n"
                "The game may not work correctly.", p->title, report);
    if (!cage.bootint || !cage.bootint[0])
        app_report(false, "tms32031.zip (the sound DSP's boot ROM) was not found, so "
                "the game runs without sound.\n\n%s", files_help());
    return 0;
}

/* ---------------------------------------------------------------- */
/* The cabinet's EEPROM                                             */
/* ---------------------------------------------------------------- */

static char nvram_path[4400];

static void nvram_load(void) {
    FILE *f;
    eeprom_factory();
    if (!*nvram_path) return;
    f = fopen(nvram_path, "rb");
    if (!f) return;
    if (fread(eeprom.data, 1, sizeof(eeprom.data), f) != sizeof(eeprom.data))
        eeprom_factory();           // short or damaged: start blank
    fclose(f);
}

void app_nvram_save(void) {
    char tmp[4500];
    FILE *f;
    if (!*nvram_path) return;
    snprintf(tmp, sizeof(tmp), "%s.part", nvram_path);
    f = fopen(tmp, "wb");
    if (!f) return;
    if (fwrite(eeprom.data, 1, sizeof(eeprom.data), f) != sizeof(eeprom.data) ||
            fclose(f) != 0) {
        remove(tmp);
        return;
    }
    remove(nvram_path);
    rename(tmp, nvram_path);
}

/* ---------------------------------------------------------------- */
/* Headless runs                                                    */
/* ---------------------------------------------------------------- */

static void write_ppm(const char *name) {
    FILE *f = fopen(name, "wb");
    int x, y;
    if (!f) { fprintf(stderr, "cannot write %s\n", name); return; }
    fprintf(f, "P6\n%u %u\n255\n", GT_WIDTH, GT_HEIGHT);
    for (y = 0; y < (int)GT_HEIGHT; y++) {
        for (x = 0; x < (int)GT_WIDTH; x++) {
            uint32_t p = mach.frame_rgb[y * GT_WIDTH + x];
            uint8_t b[3] = { (uint8_t)(p >> 16), (uint8_t)(p >> 8), (uint8_t)p };
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

typedef struct {
    int frame, hold;
    char name[16];
} press_t;

static press_t script[1024];
static int nscript;

static int script_input(const char *name, int *player);

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
        {
            int player;
            if (!script_input(e->name, &player)) {
                fprintf(stderr, "--script: unknown input %s\n", e->name);
                return -1;
            }
        }
        s += n;
        if (*s == '+') {
            if (sscanf(s + 1, "%d%n", &e->hold, &n) < 1) return -1;
            s += 1 + n;
        }
        nscript++;
    }
    return 0;
}

// A script input: its player (0/1) and BTN_* bit; the test switch is
// player -1. 0 if the name is unknown.
static int script_input(const char *name, int *player) {
    static const char *const keys[] = { "b1", "b2", "b3", "b4", "up", "down",
        "left", "right", "start" };
    int k;
    *player = 0;
    if (!strcmp(name, "coin1")) return 1 << BTN_COIN;
    if (!strcmp(name, "coin2")) { *player = 1; return 1 << BTN_COIN; }
    if (!strcmp(name, "service") || !strcmp(name, "test")) { *player = -1; return 1; }
    if (!strcmp(name, "start1")) return 1 << BTN_START;
    if (!strcmp(name, "start2")) { *player = 1; return 1 << BTN_START; }
    if (name[0] != 'p' || (name[1] != '1' && name[1] != '2')) return 0;
    *player = name[1] - '1';
    for (k = 0; k < 9; k++)
        if (!strcmp(name + 2, keys[k])) return 1 << (k < 4 ? BTN_B1 + k : k == 8 ? BTN_START : BTN_UP + k - 4);
    return 0;
}

// The script's controls held during this frame.
static void script_held(int frame, uint16_t held[2], int *test) {
    int i;
    held[0] = held[1] = 0;
    *test = 0;
    for (i = 0; i < nscript; i++) {
        int player, bit = script_input(script[i].name, &player);
        if (frame < script[i].frame || frame >= script[i].frame + script[i].hold) continue;
        if (player < 0) *test = 1;
        else held[player] |= (uint16_t)bit;
    }
}

static void script_frame(int frame) {
    uint16_t held[2];
    int test;
    script_held(frame, held, &test);
    controls_apply(held, test);
}

// Translated code for a headless run: linked in, cached, or built now.
static void headless_game_code(void) {
    char msg[512];
    if (getenv("RR_LOAD_MODULE")) {
        if (!rec_build_load_file(getenv("RR_LOAD_MODULE"), msg, sizeof(msg)))
            fprintf(stderr, "rec: %s\n", msg);
        return;
    }
    if (getenv("RR_NOREC") && getenv("RR_NODSP")) return;
    if (rec_have_m68k() && rec_have_dsp()) return;
    if ((!getenv("RR_BUILD") && rec_build_load_cached(msg, sizeof(msg))) ||
            rec_build(NULL, msg, sizeof(msg)))
        fprintf(stderr, "rec: %s\n", msg);
    else
        fprintf(stderr, "rec: %s\n", msg);
}

// Headless options beyond the plain run (as UltraCombo's).
typedef struct {
    int frames, want_hash, save_at, shot_every, rewind_test, runahead, trace_from;
    const char *shot, *wav, *save_path;
} headless_t;

// RR_POKE="F:ADDR:VALUE,...": write a byte at a 68K address (hex) before
// frame F, headless only (testing).
static void apply_pokes(int frame) {
    const char *p = getenv("RR_POKE");
    while (p && *p) {
        int f, n = 0;
        unsigned a, v;
        if (sscanf(p, "%d:%x:%x%n", &f, &a, &v, &n) != 3) break;
        if (f == frame) {
            a &= 0xffffffu;
            if (a >= 0xf80000u) mach.ram[a - 0xf80000u] = (uint8_t)v;
            else if (a >= 0xd70000u && a < 0xd80000u) mach.bank[a - 0xd70000u] = (uint8_t)v;
        }
        p += n;
        if (*p == ',') p++;
    }
}

// RR_DUMP_AT=F: ram.bin / bank.bin (main RAM 0xF80000, work RAM 0xD70000)
// after frame F; a list F1,F2,... writes ram_<F>.bin / bank_<F>.bin.
static void dump_at(int frame) {
    const char *l = getenv("RR_DUMP_AT"), *q;
    int list;
    if (!l) return;
    list = strchr(l, ',') != NULL;
    for (q = l; *q; q = strchr(q, ',') ? strchr(q, ',') + 1 : q + strlen(q)) {
        char n0[32] = "ram.bin", n1[32] = "bank.bin";
        FILE *o;
        if (atoi(q) != frame) continue;
        if (list) {
            snprintf(n0, sizeof(n0), "ram_%05d.bin", frame);
            snprintf(n1, sizeof(n1), "bank_%05d.bin", frame);
        }
        if ((o = fopen(n0, "wb"))) { fwrite(mach.ram, 1, RR_RAM_SIZE, o); fclose(o); }
        if ((o = fopen(n1, "wb"))) { fwrite(mach.bank, 1, RR_BANK_SIZE, o); fclose(o); }
    }
}

static int run_headless(const headless_t *o) {
    int16_t *pcm = NULL;
    uint32_t cap = 0, got_frames = 0;
    int f, start = (int)mach.frames, rewind_fail = 0;
    double acc_ms = 0, max_ms = 0;
    const char *wav = o->wav ? o->wav : getenv("RR_WAV");
    if (wav) {
        cap = (uint32_t)o->frames * 1200 + 4096;
        pcm = (int16_t *)calloc((size_t)cap * 2, 2);
        if (!pcm) { fprintf(stderr, "out of memory\n"); return 1; }
    }
    state_init();
    if (o->rewind_test || getenv("RR_HISTORY")) state_set_history((size_t)1 << 30);
    // frames counted from power-on, so a loaded state continues its script
    for (f = start + 1; f <= o->frames; f++) {
        uint64_t t0 = rr_ticks_us();
        double ms;
        if (o->trace_from && f == o->trace_from) m68k_trace = 1;
        if (nscript) script_frame(f - 1);
        apply_pokes(f);
#ifdef RR_SDL
        if (getenv("RR_TRAINING") && f >= atoi(getenv("RR_TRAINING"))) {   // testing: from frame N
            cfg.training = true;
            training_apply(true);
        }
#endif
        machine_frame();
        state_checkpoint();
        if (o->runahead) {
            int k;
            for (k = 0; k < o->runahead; k++) machine_frame();
            state_rollback();
        }
        if (pcm) {
            static int16_t mL[4096], mR[4096];
            uint32_t n = cage_queued(), k, got;
            if (n > 4096) n = 4096;
            if (got_frames + n > cap) n = cap - got_frames;
            got = cage_mix(mL, mR, n);
            for (k = 0; k < got; k++) {
                pcm[(got_frames + k) * 2] = mL[k];
                pcm[(got_frames + k) * 2 + 1] = mR[k];
            }
            got_frames += got;
        }
        if (o->save_at && f == o->save_at) {
            if (state_save_file(o->save_path)) fprintf(stderr, "saved %s after frame %d\n", o->save_path, f);
            else fprintf(stderr, "%s: save failed\n", o->save_path);
        }
        if (o->rewind_test && f % o->rewind_test == 0 && f > o->rewind_test) {
            // go back N frames, replay them with the same input, compare
            uint64_t want = state_hash();
            int n = 0, k;
            while (n < o->rewind_test && state_rewind()) n++;
            for (k = f - n + 1; k <= f; k++) {
                if (nscript) script_frame(k - 1);
                apply_pokes(k);
                machine_frame();
                state_checkpoint();
            }
            {
                uint64_t got = state_hash();
                if (got != want) rewind_fail++;
                fprintf(stderr, "rewind test frame %d: back %d, %s (%.1f MB history)\n", f, n,
                        got == want ? "match" : "MISMATCH", state_history_bytes() / 1048576.0);
            }
        }
        dump_at(f);
        if (o->shot_every && f % o->shot_every == 0) {
            char name[64];
            snprintf(name, sizeof(name), "frame_%05d.ppm", f);
            write_ppm(name);
        }
        ms = (rr_ticks_us() - t0) / 1000.0;
        acc_ms += ms;
        if (ms > max_ms) max_ms = ms;
        if (f % 60 == 0) {
            if (getenv("RR_SPEED"))
                fprintf(stderr, "frames %d-%d: %.2f ms/frame (%.0f%% speed) worst %.2f ms\n", f - 59, f,
                        acc_ms / 60, 100.0 * (1000.0 * GT_FPS_DEN / GT_FPS_NUM) / (acc_ms / 60), max_ms);
            acc_ms = max_ms = 0;
        }
    }
    if (pcm) {
        write_wav(wav, pcm, got_frames, cage.dac_rate);
        free(pcm);
    }
    printf("ran %d frames (68K pc=%06X, DSP pc=%06X)\n", o->frames - start,
            rrm->cpu->pc & 0xffffffu, rrm->dsp->pc & 0xffffffu);
    if (o->shot) write_ppm(o->shot);
    if (o->want_hash) printf("state hash %016llx\n", (unsigned long long)state_hash());
    if (rewind_fail) {
        fprintf(stderr, "%d rewind mismatches\n", rewind_fail);
        return 3;
    }
    return 0;
}

/* ---------------------------------------------------------------- */
/* Netplay                                                          */
/* ---------------------------------------------------------------- */

void machine_run_inputs(const uint16_t in[2], bool resim) {
    static int desync_at = -1;      // testing: RR_DESYNC_AT=frame breaks this side
    controls_apply(in, 0);
    machine_frame();
    if (desync_at < 0) desync_at = getenv("RR_DESYNC_AT") ? atoi(getenv("RR_DESYNC_AT")) : 0;
    if (desync_at && mach.frames == (uint64_t)desync_at && !resim) mach.ram[0x1000] ^= 0x55;
    state_checkpoint();
    if (resim) {                    // its sound was already heard
        static int16_t l[4096], r[4096];
        while (cage_mix(l, r, 4096) > 0) {}
    }
}

void machine_netplay_reset(void) {
    uint16_t none[2] = { 0, 0 };
    int i;
    machine_reset();
    for (i = 0; i < NP_BOOT_FRAMES; i++) {
        controls_apply(none, 0);
        machine_frame();
    }
    state_init();
}

int net_warmup;     // testing: play locally (with rewind history) until the session starts

// --net-host PORT / --net-join HOST[:PORT]: play this side of --script
// until frame `end` over the network, then print the state hash.
static int headless_netplay(int end, const char *host_port, const char *join, int delay,
        int fps, int lat, int jit, int loss) {
    int me = host_port ? 0 : 1, ok;
    double next = net_now_ms();
    uint32_t last_report = 0;
    netplay_set_callbacks(machine_run_inputs, machine_netplay_reset);
    net_simulate(lat, jit, loss);
    if (host_port) ok = netplay_host(atoi(host_port), delay);
    else {
        char h[256];
        int port = NP_DEFAULT_PORT;
        char *c;
        snprintf(h, sizeof(h), "%s", join);
        c = strrchr(h, ':');
        if (c) { *c = 0; port = atoi(c + 1); }
        ok = netplay_join(h, port, delay);
    }
    if (!ok) { fprintf(stderr, "%s\n", netplay_take_error()); return 1; }
    netplay_set_end((uint32_t)end);
    state_init();
    if (net_warmup) state_set_history((size_t)256 << 20);
    for (;;) {
        uint16_t held[2], local;
        int test;
        const char *err;
        double now;
        if (net_warmup && !netplay_running()) {
            uint16_t none[2] = { 0, 0 };
            controls_apply(none, 0);
            machine_frame();
            state_checkpoint();
        }
        // this side's buttons from the script, for the frame the input will be used in
        script_held((int)netplay_input_frame(), held, &test);
        local = held[me];
        netplay_update(local);
        err = netplay_take_error();
        if (err) { fprintf(stderr, "%s\n", err); return 2; }
        if (netplay_running() && netplay_confirmed() >= (uint32_t)end) {
            int i;
            printf("state hash %016llx at frame %llu\n", (unsigned long long)state_hash(),
                    (unsigned long long)mach.frames);
            for (i = 0; i < 30; i++) { netplay_update(0); rr_sleep_ms(10); }  // let the other side confirm too
            netplay_stop();
            return 0;
        }
        if (netplay_running() && mach.frames / 600 != last_report) {
            last_report = (uint32_t)(mach.frames / 600);
            fprintf(stderr, "%s\n", netplay_status());
        }
        next += 1000.0 / fps;
        now = net_now_ms();
        if (next > now) rr_sleep_ms((int)(next - now));
        else if (now - next > 100) next = now;
    }
}

// --dis ADDR N: disassemble N 68K instructions of the program ROM.
static void disassemble(uint32_t a, int n) {
    while (n-- > 0 && a + 2 <= RR_ROM_SIZE) {
        uint16_t w[12];
        m_insn_t in;
        char buf[128];
        int i, len;
        for (i = 0; i < 11; i++)
            w[i] = a + 2 * i + 1 < RR_ROM_SIZE ?
                    (uint16_t)(mach.rom[a + 2 * i] << 8 | mach.rom[a + 2 * i + 1]) : 0;
        len = m68k_decode(a, w, 11, &in);
        if (len <= 0) len = 1;
        printf("%06x:", a);
        for (i = 0; i < 5; i++) printf(i < len ? " %04x" : "     ", w[i]);
        printf("  %s\n", m68k_format(buf, &in, a));
        a += (uint32_t)len * 2;
    }
}

/* ---------------------------------------------------------------- */
/* Restarting                                                       */
/* ---------------------------------------------------------------- */

static int main_argc;
static char **main_argv;

// Start again with the same arguments, minus --game-version (the setting
// says which version now).
void app_restart(void) {
    char *args[64];
    int n = 0, i;
#ifdef RR_SDL
    config_save();
#endif
    app_nvram_save();
    for (i = 0; i < main_argc && n < 63; i++) {
        if (i > 0 && !strcmp(main_argv[i], "--game-version") && i + 1 < main_argc) { i++; continue; }
        args[n++] = main_argv[i];
    }
    args[n] = NULL;
#ifdef _WIN32
    {
        static char exe[4096];
        GetModuleFileNameA(NULL, exe, sizeof(exe));
        _execv(exe, (const char *const *)args);
    }
#else
    {
        static char exe[4096];
        ssize_t len = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
        exe[len > 0 ? len : 0] = 0;
        execv(len > 0 ? exe : "/proc/self/exe", args);
    }
#endif
    fprintf(stderr, "could not restart: %s\n", strerror(errno));
}

/* ---------------------------------------------------------------- */

// Options followed by values: skipped when looking for a launcher's PATH.
static int option_values(const char *a, const char *next) {
    static const char *const one[] = { "--roms", "--game-version", "--data-dir", "--set",
        "--headless", "--frames", "--script", "--shot", "--wav", "--load", "--nvram",
        "--emit-linked", "--shot-every", "--rewind-test", "--runahead", "--trace-from",
        "--net-host", "--net-join", "--net-delay", "--net-fps", "--net-sim", "--join", NULL };
    int i;
    for (i = 0; one[i]; i++)
        if (!strcmp(a, one[i])) return 1;
    if (!strcmp(a, "--save-at") || !strcmp(a, "--dis")) return 2;
    if (!strcmp(a, "--host")) return next && next[0] != '-';     // the port is optional
    return 0;
}

int main(int argc, char **argv) {
    const char *launch_path = NULL, *want_set = NULL;
    const char *shot = NULL, *wav = NULL, *load_path = NULL, *save_path = NULL;
    const char *nvram_given = NULL, *emit_dir = NULL;
    int frames = -1, want_hash = 0, save_at = 0, i;
    int shot_every = 0, rewind_test = 0, runahead = 0, trace_from = 0, dis_count = 0;
    const char *net_host = NULL, *net_join = NULL;
    int net_delay = 2, net_fps = 60, net_lat = 0, net_jit = 0, net_loss = 0;
    uint32_t dis_addr = 0;
#ifdef RR_SDL
    static char ini[4400];
#endif
    main_argc = argc;
    main_argv = argv;
    if (argc == 2 && !strcmp(argv[1], "--version")) {
        printf("%s %s\n", PRODUCT, rr_version);
        return 0;
    }
    if (argc == 2 && (!strcmp(argv[1], "--help") || !strcmp(argv[1], "-h"))) {
        usage();
        return 0;
    }
#ifdef RR_SDL
    // packaging helper: before anything writes a log or settings
    if (argc == 3 && !strcmp(argv[1], "--write-icon")) {
        size_t n = strlen(argv[2]);
        int png = n > 4 && !strcmp(argv[2] + n - 4, ".png");
        return (png ? icon_write_png(argv[2], 256) : icon_write_ico(argv[2])) ? 0 : 1;
    }
#endif
    // First pass: the data folder, headless or not, a launcher's path.
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--data-dir") && i + 1 < argc) set_data_dir(argv[i + 1]);
        if ((!strcmp(argv[i], "--headless") || !strcmp(argv[i], "--frames") ||
                !strcmp(argv[i], "--emit-linked") || !strcmp(argv[i], "--dis")) && i + 1 < argc)
            headless_mode = 1;
        if (argv[i][0] != '-' && !launch_path) launch_path = argv[i];
        i += option_values(argv[i], i + 1 < argc ? argv[i + 1] : NULL);
    }
#ifndef RR_SDL
    headless_mode = 1;
#endif
    if (launch_path) {
        char err[4400];
        if (!launch_open(launch_path, err, sizeof(err))) {
            app_report(true, "%s", err);
            return 1;
        }
        if (launch_set()) want_set = launch_set();
    }
#if defined(_WIN32) && defined(RR_SDL)
    if (!headless_mode && !getenv("RR_CONSOLE")) {
        // a GUI program has no console: keep the log in the data folder
        static char log[4400];
        snprintf(log, sizeof(log), "%sragerecomp.log", data_dir());
        if (freopen(log, "w", stderr)) setvbuf(stderr, NULL, _IONBF, 0);
    }
#endif
    fprintf(stderr, "%s %s\n", PRODUCT, rr_version);
#ifdef RR_SDL
    snprintf(ini, sizeof(ini), "%sragerecomp.ini", data_dir());
    config_load(ini);
    {   // settings from the command line, for this session; RecompLauncher's
        // Big Picture mode (a controller on the couch) starts in fullscreen
        int screen_given = 0;
        const char *mode = getenv("RECOMPLAUNCHER_MODE");
        for (i = 1; i < argc; i++) {
            if (!strcmp(argv[i], "--set") && i + 1 < argc) {
                const char *s = argv[++i];
                screen_given |= !SDL_strncasecmp(s, "video.fullscreen=", 17) ||
                        !SDL_strncasecmp(s, "video/fullscreen=", 17);
                if (!config_override(s)) return 1;
            } else if (!strcmp(argv[i], "--fullscreen") || !strcmp(argv[i], "--windowed")) {
                config_override(argv[i][2] == 'f' ? "video.fullscreen=yes" : "video.fullscreen=no");
                screen_given = 1;
            } else {
                i += option_values(argv[i], i + 1 < argc ? argv[i + 1] : NULL);
            }
        }
        if (!screen_given && mode && !strcmp(mode, "bigpicture")) config_override("video.fullscreen=yes");
    }
    if (*cfg.roms) roms_dir = cfg.roms;
    if (!want_set) want_set = cfg.version;
#endif
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--roms") && i + 1 < argc) roms_dir = argv[++i];
        else if (!strcmp(argv[i], "--game-version") && i + 1 < argc) want_set = argv[++i];
        else if ((!strcmp(argv[i], "--headless") || !strcmp(argv[i], "--frames")) && i + 1 < argc)
            frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc) shot = argv[++i];
        else if (!strcmp(argv[i], "--wav") && i + 1 < argc) wav = argv[++i];
        else if (!strcmp(argv[i], "--hash")) want_hash = 1;
        else if (!strcmp(argv[i], "--sdl")) {}    // older launchers: the window is the default
        else if (!strcmp(argv[i], "--load") && i + 1 < argc) load_path = argv[++i];
        else if (!strcmp(argv[i], "--nvram") && i + 1 < argc) nvram_given = argv[++i];
        else if (!strcmp(argv[i], "--emit-linked") && i + 1 < argc) emit_dir = argv[++i];
        else if (!strcmp(argv[i], "--shot-every") && i + 1 < argc) shot_every = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--rewind-test") && i + 1 < argc) rewind_test = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--runahead") && i + 1 < argc) runahead = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--trace")) m68k_trace = 1;
        else if (!strcmp(argv[i], "--net-host") && i + 1 < argc) net_host = argv[++i];
        else if (!strcmp(argv[i], "--net-join") && i + 1 < argc) net_join = argv[++i];
        else if (!strcmp(argv[i], "--net-delay") && i + 1 < argc) net_delay = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--net-fps") && i + 1 < argc) net_fps = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--net-warmup")) net_warmup = 1;
        else if (!strcmp(argv[i], "--net-sim") && i + 1 < argc)
            sscanf(argv[++i], "%d:%d:%d", &net_lat, &net_jit, &net_loss);
        else if (!strcmp(argv[i], "--trace-from") && i + 1 < argc) trace_from = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--dis") && i + 2 < argc) {
            dis_addr = (uint32_t)strtoul(argv[++i], NULL, 16);
            dis_count = atoi(argv[++i]);
        }
        else if (!strcmp(argv[i], "--save-at") && i + 2 < argc) {
            save_at = atoi(argv[++i]);
            save_path = argv[++i];
        } else if (!strcmp(argv[i], "--script") && i + 1 < argc) {
            if (parse_script(argv[++i])) {
                fprintf(stderr, "bad --script\n");
                return 1;
            }
        }
#ifdef RR_SDL
        else if ((!strcmp(argv[i], "--data-dir") || !strcmp(argv[i], "--set")) && i + 1 < argc) i++;
        else if (!strcmp(argv[i], "--fullscreen") || !strcmp(argv[i], "--windowed")) {}
        else if (!strcmp(argv[i], "--test-shaders")) { extern int opt_test_shaders; opt_test_shaders = 1; }
        else if (!strcmp(argv[i], "--host")) {
            extern const char *opt_net_host;
            opt_net_host = (i + 1 < argc && argv[i + 1][0] != '-') ? argv[++i] : "";
        } else if (!strcmp(argv[i], "--join") && i + 1 < argc) {
            extern const char *opt_net_join;
            opt_net_join = argv[++i];
        }
#else
        else if (!strcmp(argv[i], "--data-dir") && i + 1 < argc) i++;
#endif
        else if (argv[i] != launch_path) {
            fprintf(stderr, "unknown option: %s\n", argv[i]);
            usage();
            return 1;
        }
    }
    if (headless_mode && frames < 0) frames = 600;

    // The game version: the one asked for, else the first one there is.
    prof = want_set ? game_by_set(want_set) : NULL;
    if (want_set && !prof) fprintf(stderr, "version %s: unknown, using primrage\n", want_set);
    if (!prof) prof = game_profile(GAME_PRIMRAGE);
#ifdef RR_SDL
    if (!headless_mode && !any_set_there()) {
        // nothing there: the welcome screen, until the files are added
        char folder[4400], alt[4400] = "";
        int w;
        if (appimage_dir() && strcmp(appimage_dir(), data_dir())) {
            snprintf(folder, sizeof(folder), "%srun", appimage_dir());
            snprintf(alt, sizeof(alt), "%srun", data_dir());
        } else {
            snprintf(folder, sizeof(folder), "%srun", data_dir());
        }
        fprintf(stderr, "No game files found. Put them in %s (see README.md).\n", folder);
        w = picker_welcome(folder, alt, any_set_there);
        if (w == 0) return 0;               // closed; with no window the error follows
    }
#endif
    if (!app_set_there(prof->id)) {
        for (i = 0; i < GAME_COUNT; i++)
            if (app_set_there((game_id_t)i)) {
                fprintf(stderr, "version %s: files not found, using %s\n", prof->set,
                        game_profile((game_id_t)i)->set);
                prof = game_profile((game_id_t)i);
                break;
            }
    }
    fprintf(stderr, "playing %s (%s)\n", prof->title, prof->set);
    mach_profile = prof;
    if (load_set(prof)) return 1;
    gtvid_decode();
    gtvid_mo_init();

    // The cabinet's EEPROM: blank for headless runs (reproducible) unless
    // one is given, else the player's own for this version.
    if (nvram_given) snprintf(nvram_path, sizeof(nvram_path), "%s", nvram_given);
    else if (!headless_mode) {
        char dir[4400];
        snprintf(dir, sizeof(dir), "%ssaves", data_dir());
        make_dir(dir);
        snprintf(nvram_path, sizeof(nvram_path), "%s/%s.eeprom", dir, prof->set);
    }
    nvram_load();
    rec_init();
    machine_reset();
    if (dis_count) {
        disassemble(dis_addr, dis_count);
        return 0;
    }
    if (emit_dir) {
        int rc = rec_emit_linked(emit_dir);
        fprintf(stderr, "%s %s\n", rc ? "could not write" : "wrote", emit_dir);
        return rc ? 1 : 0;
    }
    mach.in.b0 = mach.in.b1 = mach.in.b3 = 0xff;

    if (headless_mode) {
        int rc;
        headless_game_code();
        machine_reset();                    // the DSP's code was read by booting it
        mach.in.b0 = mach.in.b1 = mach.in.b3 = 0xff;
        if (load_path && !state_load_file(load_path)) {
            fprintf(stderr, "%s: cannot load state\n", load_path);
            return 1;
        }
        if (net_host || net_join)
            return headless_netplay(frames, net_host, net_join, net_delay, net_fps,
                    net_lat, net_jit, net_loss);
        if (getenv("RR_RESET_TEST")) {
            // testing: does anything survive a power-on reset? Run N frames,
            // power-cycle, and print the hash (equal to a fresh power-on's)
            int n = atoi(getenv("RR_RESET_TEST"));
            for (i = 0; i < n; i++) machine_frame();
            machine_reset();
            fprintf(stderr, "after reset: hash %016llx quick %016llx\n",
                    (unsigned long long)state_hash(), (unsigned long long)state_quick_hash());
            state_save_file("reset_state.bin");     // to compare with a fresh power-on's
            return 0;
        }
        {
            headless_t o;
            memset(&o, 0, sizeof(o));
            o.frames = frames;
            o.want_hash = want_hash;
            o.save_at = save_at;
            o.save_path = save_path;
            o.shot = shot;
            o.wav = wav;
            o.shot_every = shot_every;
            o.rewind_test = rewind_test;
            o.runahead = runahead;
            o.trace_from = trace_from;
            rc = run_headless(&o);
        }
        if (nvram_given) app_nvram_save();
        rec_shutdown();
        return rc;
    }
#ifdef RR_SDL
    {
        int rc = frontend_main();
        app_nvram_save();
        rec_shutdown();
        return rc;
    }
#else
    return 0;
#endif
}
