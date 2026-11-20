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

#ifdef RR_SDL
#include <SDL2/SDL.h>
#include "config.h"
#include "icon.h"
#include "picker.h"
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

// The port bit for a player's control, by the set's layout. *byte gets
// the port byte (active low) or *flag a held flag. 0 if unknown.
static int input_bit(const char *name, uint8_t **byte, uint8_t **flag) {
    static const char *const keys[] = { "start", "b1", "b2", "b3", "b4",
        "right", "left", "down", "up" };
    int k, p2;
    *byte = *flag = NULL;
    if (!strcmp(name, "coin1")) { *flag = &mach.in.coin1; return 1; }
    if (!strcmp(name, "coin2")) { *flag = &mach.in.coin2; return 1; }
    if (!strcmp(name, "service")) { *flag = &mach.in.service; return 1; }
    if (!strcmp(name, "start1")) name = "p1start";
    if (!strcmp(name, "start2")) name = "p2start";
    if (name[0] != 'p' || (name[1] != '1' && name[1] != '2')) return 0;
    p2 = name[1] == '2';
    for (k = 0; k < 9; k++)
        if (!strcmp(name + 2, keys[k])) break;
    if (k == 9) return 0;
    *byte = p2 ? &mach.in.b1 : &mach.in.b3;
    if (k >= 5) return 0x10 << (k - 5);                 // stick: bits 4-7
    if (prof && prof->has_start_buttons) {
        // Jan 1995: start, buttons 1-3 in the top bytes, button 4 below
        if (k == 4) { *byte = &mach.in.b0; return p2 ? 0x08 : 0x02; }
        return 1 << k;
    }
    // older sets: buttons 1-4 where the start bit was; start = button 1
    return k == 0 ? 1 : 1 << (k - 1);
}

static void script_frame(int frame) {
    int i;
    mach.in.b0 = mach.in.b1 = mach.in.b3 = 0xff;
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

static int run_headless(int frames, const char *shot, const char *wav,
        int want_hash, int save_at, const char *save_path) {
    int16_t *pcm = NULL;
    uint32_t cap = 0, got_frames = 0;
    int i;
    if (wav) {
        cap = (uint32_t)frames * 1200 + 4096;
        pcm = (int16_t *)calloc((size_t)cap * 2, 2);
        if (!pcm) { fprintf(stderr, "out of memory\n"); return 1; }
    }
    for (i = 0; i < frames; i++) {
        if (nscript) script_frame(i);
        machine_frame();
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
        if (save_at && i + 1 == save_at) {
            if (state_save_file(save_path)) fprintf(stderr, "saved %s after frame %d\n", save_path, save_at);
            else fprintf(stderr, "%s: save failed\n", save_path);
        }
    }
    if (pcm) {
        write_wav(wav, pcm, got_frames, cage.dac_rate);
        free(pcm);
    }
    printf("ran %d frames (68K pc=%06X, DSP pc=%06X)\n", frames,
            rrm->cpu->pc & 0xffffffu, rrm->dsp->pc & 0xffffffu);
    if (shot) write_ppm(shot);
    if (want_hash) printf("state hash %016llx\n", (unsigned long long)state_hash());
    return 0;
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
static int option_values(const char *a) {
    static const char *const one[] = { "--roms", "--game-version", "--data-dir", "--set",
        "--headless", "--frames", "--script", "--shot", "--wav", "--load", "--nvram",
        "--emit-linked", NULL };
    int i;
    for (i = 0; one[i]; i++)
        if (!strcmp(a, one[i])) return 1;
    if (!strcmp(a, "--save-at")) return 2;
    return 0;
}

int main(int argc, char **argv) {
    const char *launch_path = NULL, *want_set = NULL;
    const char *shot = NULL, *wav = NULL, *load_path = NULL, *save_path = NULL;
    const char *nvram_given = NULL, *emit_dir = NULL;
    int frames = -1, want_hash = 0, save_at = 0, i;
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
                !strcmp(argv[i], "--emit-linked")) && i + 1 < argc)
            headless_mode = 1;
        if (argv[i][0] != '-' && !launch_path) launch_path = argv[i];
        i += option_values(argv[i]);
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
                i += option_values(argv[i]);
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
        rc = run_headless(frames, shot, wav, want_hash, save_at, save_path);
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
