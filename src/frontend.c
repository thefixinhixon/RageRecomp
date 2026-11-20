// Windowed frontend: video, audio, input, the menu, and the quality-of-life
// features (pause, frame advance, fast-forward, fullscreen, screenshots,
// save states, volume). Adapted from UltraCombo's.
//
// Everything that reaches the emulated machine goes through the cabinet's
// input ports once per frame (mach.in), so the emulation stays a pure
// function of those inputs.
#include "app.h"
#include "config.h"
#include "controller_labels.h"
#include "machine.h"
#include "cage.h"
#include "osd.h"
#include "png.h"
#include "state.h"
#include "launch.h"
#include "icon.h"
#include "paths.h"
#include "rec.h"
#include "rec_build.h"
#include "screen.h"
#include "video.h"
#include "shaders.h"
#include "version.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define AUDIO_HZ 48000
#define FRAME_HZ ((double)GT_FPS_NUM / GT_FPS_DEN)   // 59.92 Hz

static SDL_Window *win;
static SDL_AudioDeviceID adev;
static uint32_t px[SCR_W * SCR_H];     // game image
static uint32_t ov[SCR_W * SCR_H];     // overlay: messages, menu

static bool paused, step_once, fullscreen, muted, vsync;
static bool vsync_lock;     // vsync on a ~60 Hz display: one game frame per refresh
static bool vsync_broken;   // vsync turned out not to wait for the display: audio pacing
static bool focused = true;
static bool apply_shader(void);
static int  volume;
static double display_hz;
static uint64_t frame_count;
int opt_test_shaders;       // --test-shaders (main.c)

static void to_rgb(uint32_t *dst) { memcpy(dst, mach.frame_rgb, sizeof(px)); }

/* ---------------------------------------------------------------- */
/* Audio                                                            */
/* ---------------------------------------------------------------- */

// Resample the CAGE's stereo DAC output (~22 kHz) to the device rate and
// queue it. `speed` > 1 when the game runs faster than 59.92 Hz.
static void pump_audio(bool discard, double speed)
{
    static int16_t l[4096], r[4096], out[2 * 16384];
    static double pos;      // fractional read position; -1 is the previous block's last
    static int16_t prev_l, prev_r;
    uint32_t want = cage_queued();
    int n = (int)cage_mix(l, r, want > 4096 ? 4096 : want);
    if (!adev || discard || !n) return;
    double rate = cage.dac_rate ? cage.dac_rate : 22050;
    double step = rate / AUDIO_HZ * speed;
    int gain = muted || (!focused && cfg.mute_unfocused) ? 0 : volume * 256 / 100;
    int m = 0;
    while (pos < n && m < 16384) {
        int i = (int)pos;
        double f = pos - i;
        int al = i == 0 ? prev_l : l[i - 1], bl = l[i];
        int ar = i == 0 ? prev_r : r[i - 1], br = r[i];
        out[2 * m] = (int16_t)((int)(al + (bl - al) * f) * gain / 256);
        out[2 * m + 1] = (int16_t)((int)(ar + (br - ar) * f) * gain / 256);
        m++;
        pos += step;
    }
    pos -= n;
    prev_l = l[n - 1];
    prev_r = r[n - 1];
    // after a long hitch, throw away the backlog instead of lagging behind
    if (SDL_GetQueuedAudioSize(adev) > AUDIO_HZ * 4 * 250 / 1000) SDL_ClearQueuedAudio(adev);
    SDL_QueueAudio(adev, out, (Uint32)m * 4);
}

static double queued_ms(void)
{
    return adev ? SDL_GetQueuedAudioSize(adev) * 1000.0 / (AUDIO_HZ * 4) : 0;
}

/* ---------------------------------------------------------------- */
/* Video                                                            */
/* ---------------------------------------------------------------- */

// On a ~60 Hz display, vsync runs one game frame per refresh (smooth, 0.1%
// fast). Vsync on any other display stays tear-free but the game is timed by
// the clock, at its own speed. Auto uses vsync on ~60 Hz displays only. If
// vsync turns out not to wait (a driver setting can force it off), audio
// pacing takes over.
static void choose_pacing(void)
{
    SDL_DisplayMode m;
    int idx = SDL_GetWindowDisplayIndex(win);
    display_hz = (idx >= 0 && SDL_GetCurrentDisplayMode(idx, &m) == 0 && m.refresh_rate) ? m.refresh_rate : 60;
    if (getenv("RR_DISPLAY_HZ")) display_hz = atof(getenv("RR_DISPLAY_HZ"));   // testing
    bool sixty = display_hz >= 59 && display_hz <= 61;
    bool want = !vsync_broken && (cfg.pacing == PACING_VSYNC || (cfg.pacing == PACING_AUTO && sixty));
    bool lock = want && sixty;
    if (want != vsync || lock != vsync_lock || !frame_count) {
        vsync = want;
        vsync_lock = lock;
        video_set_vsync(vsync);
        fprintf(stderr, "video: %.0f Hz display, %s pacing\n", display_hz,
                lock ? "vsync" : vsync ? "vsync display, clock-timed" : "audio");
    }
}

// Called once per game frame in display-locked pacing. Vsync that works
// can't present faster than the display refreshes; seeing far more for two
// seconds running means it isn't waiting, and the game would run too fast.
static void check_vsync(void)
{
    static uint64_t t0;
    static int frames, strikes;
    uint64_t now = SDL_GetPerformanceCounter(), f = SDL_GetPerformanceFrequency();
    if (!t0 || !focused || now - t0 > 2 * f) { t0 = now; frames = strikes = 0; return; }
    frames++;
    if (now - t0 < f) return;
    double rate = frames * (double)f / (double)(now - t0);
    strikes = rate > display_hz * 1.25 + 5 ? strikes + 1 : 0;
    t0 = now;
    frames = 0;
    if (strikes >= 2) {
        fprintf(stderr, "video: vsync isn't waiting for the display (%.0f frames/s on %.0f Hz): audio pacing\n", rate, display_hz);
        osd_message("Vsync isn't working here: timing by audio instead");
        vsync_broken = true;
        strikes = 0;
        choose_pacing();
    }
}

static void set_fullscreen(bool on)
{
    fullscreen = on;
    if (on && cfg.fullscreen_exclusive) {
        // exclusive: switch the display to the chosen mode (or keep the desktop's)
        SDL_DisplayMode want = { 0 }, got;
        int d = SDL_GetWindowDisplayIndex(win);
        if (*cfg.fullscreen_mode && sscanf(cfg.fullscreen_mode, "%dx%d@%d", &want.w, &want.h, &want.refresh_rate) == 3 &&
            SDL_GetClosestDisplayMode(d, &want, &got))
            SDL_SetWindowDisplayMode(win, &got);
        else if (SDL_GetDesktopDisplayMode(d, &got) == 0)
            SDL_SetWindowDisplayMode(win, &got);
        SDL_SetWindowFullscreen(win, SDL_WINDOW_FULLSCREEN);
    } else SDL_SetWindowFullscreen(win, on ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    SDL_ShowCursor(on ? SDL_DISABLE : SDL_ENABLE);
    choose_pacing();
}

// Menu changes to the display are applied once the choice has settled:
// switching at every step of a cycle, exclusive modes especially, can lose
// the window under XWayland on GNOME. want_display: -1 = none pending, else
// 0 off, 1 borderless, 2 exclusive.
static int want_display = -1;
static uint32_t display_due;

static int shown_display(void)
{
    if (want_display >= 0) return want_display;
    return !fullscreen ? 0 : cfg.fullscreen_exclusive ? 2 : 1;
}

static void request_display(int m)
{
    want_display = m;
    display_due = SDL_GetTicks() + 400;
}

// now: apply a pending change at once (closing the menu, quitting)
static void settle_display(bool now)
{
    if (want_display < 0 || (!now && !SDL_TICKS_PASSED(SDL_GetTicks(), display_due))) return;
    int m = want_display;
    want_display = -1;
    set_fullscreen(m != 0);
}

static void draw_menu(uint32_t *ov);

// Performance overlay: measured over the last second.
static struct { double emu_ms, emu_acc, fps; int emu_n, frames; uint64_t t0; } perf;

static void perf_emu(uint64_t start)
{
    perf.emu_acc += (SDL_GetPerformanceCounter() - start) * 1000.0 / SDL_GetPerformanceFrequency();
    perf.emu_n++;
}

static void draw_perf(uint32_t *out)
{
    uint64_t now = SDL_GetPerformanceCounter(), f = SDL_GetPerformanceFrequency();
    perf.frames++;
    if (!perf.t0) perf.t0 = now;
    if (now - perf.t0 >= f) {
        double s = (double)(now - perf.t0) / (double)f;
        perf.fps = perf.frames / s;
        perf.emu_ms = perf.emu_n ? perf.emu_acc / perf.emu_n : 0;
        perf.frames = perf.emu_n = 0;
        perf.emu_acc = 0;
        perf.t0 = now;
    }
    if (!cfg.perf_overlay) return;
    char l1[48], l2[48], l3[48];
    snprintf(l1, sizeof l1, "%.1f FPS  AUDIO %.0f MS", perf.fps, queued_ms());
    // load: the share of each frame's time spent emulating it (under 100% keeps up)
    snprintf(l2, sizeof l2, "EMU %.2f MS  LOAD %.0f%%", perf.emu_ms, perf.emu_ms * FRAME_HZ / 10.0);
    snprintf(l3, sizeof l3, "CODE %s", rec_have_m68k() && rec_have_dsp() ? "TRANSLATED" :
             rec_have_m68k() || rec_have_dsp() ? "PART TRANSLATED" : "INTERPRETED");
    int w = (int)(strlen(l1) > strlen(l2) ? strlen(l1) : strlen(l2)) * 6 + 6;
    osd_box(out, SCR_W - w - 2, SCR_H - 43, w, 29, 0xb0000000u);
    osd_text(out, SCR_W - w + 1, SCR_H - 41, l1, 0x80ff80);
    osd_text(out, SCR_W - w + 1, SCR_H - 32, l2, 0x80ff80);
    osd_text(out, SCR_W - w + 1, SCR_H - 23, l3, 0x80ff80);
}

static void present(const char *status)
{
    to_rgb(px);
    osd_clear(ov);
    draw_perf(ov);
    draw_menu(ov);
    osd_draw(ov, status);           // after the menu: messages show over it too
    video_present(px, ov, cfg.filter, cfg.scale_mode);
}

static void screenshot(void)
{
    char dir[4200], path[4400], stamp[32];
    snprintf(dir, sizeof dir, "%sscreenshots", data_dir());
    make_dir(dir);
    time_t t = time(NULL);
    strftime(stamp, sizeof stamp, "%Y%m%d_%H%M%S", localtime(&t));
    snprintf(path, sizeof path, "%s/%s_%s.png", dir, mach_profile->set, stamp);
    for (int i = 2; i < 100 && file_readable(path); i++)     // several in one second
        snprintf(path, sizeof path, "%s/%s_%s_%d.png", dir, mach_profile->set, stamp, i);
    to_rgb(px);
    if (png_write(path, px, SCR_W, SCR_H)) { osd_message("Screenshot saved"); fprintf(stderr, "screenshot: %s\n", path); }
    else osd_message("Screenshot failed");
}

/* ---------------------------------------------------------------- */
/* Input                                                            */
/* ---------------------------------------------------------------- */

#define MAX_PADS 2      // players
#define MAX_OPEN 8      // controllers kept open: any of them drives the menu or can be picked for a player
static SDL_GameController *open_pads[MAX_OPEN];   // in the order they connected
static SDL_GameController *pads[MAX_PADS];        // each player's, one of open_pads
static SDL_JoystickID pinned[MAX_PADS] = { -1, -1 };  // picked in the menu this session
static pad_prompt_t pad_prompt;

static int prompt_override(void)
{
    for (int p = 0; p < MAX_PADS; p++)
        if (pads[p] && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pads[p])) == pad_prompt.last_pad)
            return cfg.pad_family[p];
    return PAD_FAMILY_AUTO;
}

static const char *pad_label(int button)
{
    return pad_button_name(SDL_GameControllerFromInstanceID(pad_prompt.last_pad), prompt_override(), button);
}

static SDL_JoystickID pad_id(SDL_GameController *c) { return SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(c)); }

static const char *pad_title(SDL_GameController *c)
{
    const char *n = c ? SDL_GameControllerName(c) : NULL;
    return n && *n ? n : "Unknown controller";
}

// Who plays with which pad: the pad picked in the menu this session, else the
// one named in the settings, else the one they had, else the next free one in
// connection order.
static void assign_pads(void)
{
    SDL_GameController *was[MAX_PADS] = { pads[0], pads[1] };
    bool taken[MAX_OPEN] = { 0 };
    for (int p = 0; p < MAX_PADS; p++) pads[p] = NULL;
    for (int pass = 0; pass < 4; pass++)
        for (int p = 0; p < MAX_PADS; p++) {
            if (pads[p]) continue;
            for (int i = 0; i < MAX_OPEN; i++) {
                SDL_GameController *c = open_pads[i];
                if (!c || taken[i]) continue;
                bool ok = pass == 0 ? pad_id(c) == pinned[p]
                        : pass == 1 ? cfg.pad_choice[p][0] && !strcmp(pad_title(c), cfg.pad_choice[p])
                        : pass == 2 ? c == was[p] : true;
                if (ok) { pads[p] = c; taken[i] = true; break; }
            }
        }
    char msg[160] = "";
    for (int p = 0; p < MAX_PADS; p++) {
        if (pads[p] == was[p] || !pads[p]) continue;
        fprintf(stderr, "pad: player %d = %s\n", p + 1, pad_title(pads[p]));
        size_t n = strlen(msg);
        snprintf(msg + n, sizeof msg - n, "%sPlayer %d: %.40s", n ? ", " : "", p + 1, pad_title(pads[p]));
    }
    if (*msg) osd_message("%s", msg);
}

// Every joystick SDL finds goes into the log, so a pad that does nothing can
// be told apart: not a known gamepad, or a known one that isn't a player's.
static void log_joystick(int index)
{
    static SDL_JoystickID seen[32];
    static int nseen;
    SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(index);
    for (int i = 0; i < nseen; i++) if (seen[i] == id) return;
    if (nseen < 32) seen[nseen++] = id;
    char guid[40];
    SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(index), guid, sizeof guid);
    const char *name = SDL_JoystickNameForIndex(index);
    bool known = SDL_IsGameController(index);
    fprintf(stderr, "joystick: %s (guid %s)%s\n", name ? name : "?", guid,
            known ? "" : ": no gamepad layout known for it, so it is not used. A line for it in "
                         "gamecontrollerdb.txt beside the program adds one");
    if (!known) osd_message("Controller not recognised: %.40s", name ? name : "?");
}

static void pad_added(int index)
{
    log_joystick(index);
    SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(index);
    for (int i = 0; i < MAX_OPEN; i++)
        if (open_pads[i] && pad_id(open_pads[i]) == id) return;
    for (int i = 0; i < MAX_OPEN; i++) {
        if (open_pads[i]) continue;
        if (!(open_pads[i] = SDL_GameControllerOpen(index)))
            fprintf(stderr, "pad: can't open %s: %s\n", SDL_JoystickNameForIndex(index), SDL_GetError());
        else fprintf(stderr, "pad: connected %s\n", pad_title(open_pads[i]));
        break;
    }
    assign_pads();
}

static void pad_removed(SDL_JoystickID id)
{
    for (int i = 0; i < MAX_OPEN; i++) {
        if (!open_pads[i] || pad_id(open_pads[i]) != id) continue;
        for (int p = 0; p < MAX_PADS; p++)
            if (pads[p] == open_pads[i]) { osd_message("Player %d controller disconnected", p + 1); pads[p] = NULL; }
        fprintf(stderr, "pad: disconnected %s\n", pad_title(open_pads[i]));
        SDL_GameControllerClose(open_pads[i]);
        open_pads[i] = NULL;
    }
    assign_pads();
}

// Testing without devices: RR_VIRTUAL_PADS="Name,Name,?Name" attaches SDL
// virtual gamepads, in that order ('?': a plain joystick with no layout).
static void attach_test_pads(void)
{
    static char buf[512];
    const char *e = getenv("RR_VIRTUAL_PADS");
    if (!e) return;
    snprintf(buf, sizeof buf, "%s", e);
    for (char *tok = strtok(buf, ","); tok; tok = strtok(NULL, ",")) {
        bool pad = *tok != '?';
        SDL_VirtualJoystickDesc d;
        SDL_zero(d);
        d.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
        d.type = pad ? SDL_JOYSTICK_TYPE_GAMECONTROLLER : SDL_JOYSTICK_TYPE_UNKNOWN;
        d.naxes = pad ? SDL_CONTROLLER_AXIS_MAX : 2;
        d.nbuttons = pad ? SDL_CONTROLLER_BUTTON_MAX : 4;
        d.name = pad ? tok : tok + 1;
        if (SDL_JoystickAttachVirtualEx(&d) < 0) fprintf(stderr, "virtual pad: %s\n", SDL_GetError());
    }
}

// Give player p the pad c, as chosen in the menu, and remember it by name.
// If the other player had it, they take this player's old pad.
static void choose_pad(int p, SDL_GameController *c)
{
    if (!c) return;
    int o = !p;
    if (pads[o] == c) {
        pinned[o] = pads[p] ? pad_id(pads[p]) : -1;
        snprintf(cfg.pad_choice[o], sizeof cfg.pad_choice[o], "%s", pads[p] ? pad_title(pads[p]) : "");
    }
    pinned[p] = pad_id(c);
    snprintf(cfg.pad_choice[p], sizeof cfg.pad_choice[p], "%s", pad_title(c));
    assign_pads();
}

// The next connected pad after player p's, for left/right in the menu.
static SDL_GameController *next_pad(int p, int dir)
{
    int cur = -1;
    for (int i = 0; i < MAX_OPEN; i++) if (open_pads[i] && open_pads[i] == pads[p]) cur = i;
    for (int k = 1; k <= MAX_OPEN; k++) {
        int start = cur >= 0 ? cur : dir > 0 ? -1 : MAX_OPEN;
        int i = ((start + dir * k) % MAX_OPEN + MAX_OPEN) % MAX_OPEN;
        if (open_pads[i]) return open_pads[i];
    }
    return NULL;
}

static bool pad_pressed(SDL_GameController *c, int b)
{
    if (b == PAD_NONE) return false;
    if (b == PAD_LT) return SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 12000;
    if (b == PAD_RT) return SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 12000;
    return SDL_GameControllerGetButton(c, (SDL_GameControllerButton)b);
}

// Buttons held by each player this frame, one bit per BTN_*.
static void read_players(uint32_t held[2])
{
    const uint8_t *ks = SDL_GetKeyboardState(NULL);
    for (int p = 0; p < 2; p++) {
        held[p] = 0;
        for (int b = 0; b < NBUTTONS; b++)
            if (cfg.keys[p][b] && ks[cfg.keys[p][b]]) held[p] |= 1u << b;
        SDL_GameController *c = pads[p];
        if (!c) continue;
        for (int b = 0; b < NBUTTONS; b++)
            if (pad_pressed(c, cfg.pad[b])) held[p] |= 1u << b;
        int x = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX);
        int y = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY);
        if (y < -cfg.pad_deadzone) held[p] |= 1u << BTN_UP;
        if (y > cfg.pad_deadzone) held[p] |= 1u << BTN_DOWN;
        if (x < -cfg.pad_deadzone) held[p] |= 1u << BTN_LEFT;
        if (x > cfg.pad_deadzone) held[p] |= 1u << BTN_RIGHT;
    }
    // SOCD cleaning: opposite directions together resolve the way fight sticks do
    for (int p = 0; p < 2 && cfg.socd; p++) {
        const uint32_t lr = 1u << BTN_LEFT | 1u << BTN_RIGHT, ud = 1u << BTN_UP | 1u << BTN_DOWN;
        if ((held[p] & lr) == lr) held[p] &= ~lr;
        if ((held[p] & ud) == ud) held[p] &= ~(1u << BTN_DOWN);
    }
}

static bool key_held(int hk)
{
    const uint8_t *ks = SDL_GetKeyboardState(NULL);
    return cfg.hotkeys[hk] && ks[cfg.hotkeys[hk]];
}

// The cabinet's ports from the players' controls. The Jan 1995 set has
// start buttons, its button 4 in the low byte; the older sets put buttons
// 1-4 where start was and start with an attack button (button 1 here).
static void set_ports(const uint32_t held[2])
{
    uint8_t *byte[2] = { &mach.in.b3, &mach.in.b1 };
    mach.in.b0 = mach.in.b1 = mach.in.b3 = 0xff;
    for (int p = 0; p < 2; p++) {
        uint32_t h = held[p];
        uint8_t v = 0;
        if (h >> BTN_RIGHT & 1) v |= 0x10;
        if (h >> BTN_LEFT & 1) v |= 0x20;
        if (h >> BTN_DOWN & 1) v |= 0x40;
        if (h >> BTN_UP & 1) v |= 0x80;
        if (mach_profile->has_start_buttons) {
            if (h >> BTN_START & 1) v |= 0x01;
            if (h >> BTN_B1 & 1) v |= 0x02;
            if (h >> BTN_B2 & 1) v |= 0x04;
            if (h >> BTN_B3 & 1) v |= 0x08;
            if (h >> BTN_B4 & 1) mach.in.b0 &= (uint8_t)~(p ? 0x08 : 0x02);
        } else {
            if (h >> BTN_B1 & 1 || h >> BTN_START & 1) v |= 0x01;
            if (h >> BTN_B2 & 1) v |= 0x02;
            if (h >> BTN_B3 & 1) v |= 0x04;
            if (h >> BTN_B4 & 1) v |= 0x08;
        }
        *byte[p] = (uint8_t)~v;
    }
    mach.in.coin1 = held[0] >> BTN_COIN & 1;
    mach.in.coin2 = held[1] >> BTN_COIN & 1;
    mach.in.service = key_held(HK_TEST);
}

static bool is_hotkey(SDL_Scancode sc, int hk) { return cfg.hotkeys[hk] && sc == cfg.hotkeys[hk]; }

static bool pad_held(int b)
{
    for (int i = 0; i < MAX_OPEN; i++)
        if (open_pads[i] && pad_pressed(open_pads[i], b)) return true;
    return false;
}

static void set_volume(int v)
{
    volume = v < 0 ? 0 : v > 100 ? 100 : v;
    muted = false;
    osd_message("Volume %d%%", volume);
}

static void stop_audio(void) { if (adev) SDL_ClearQueuedAudio(adev); }

/* ---------------------------------------------------------------- */
/* Save states                                                      */
/* ---------------------------------------------------------------- */

static int slot(void) { return cfg.state_slot >= 1 && cfg.state_slot <= 9 ? cfg.state_slot : 1; }

static void slot_path(char *buf, size_t n, int s)
{
    char dir[4200];
    snprintf(dir, sizeof dir, "%ssaves", data_dir());
    make_dir(dir);
    snprintf(buf, n, "%s/%s-slot%d.rrs", dir, mach_profile->set, s);
}

static void save_state(void)
{
    char path[4400];
    slot_path(path, sizeof path, slot());
    if (state_save_file(path)) osd_message("Saved state %d", slot());
    else osd_message("Could not save state %d", slot());
}

static void load_state(void)
{
    char path[4400];
    slot_path(path, sizeof path, slot());
    if (!file_readable(path)) { osd_message("State %d is empty", slot()); return; }
    if (!state_load_file(path)) { osd_message("State %d is from another version", slot()); return; }
    stop_audio();
    osd_message("Loaded state %d", slot());
}

/* ---------------------------------------------------------------- */
/* Menu                                                             */
/* ---------------------------------------------------------------- */

enum { PG_MAIN, PG_GRAPHICS, PG_PARAMS, PG_CONTROLS, PG_BIND, PG_GAME };

// Power-cycle the cabinet, as "Reset game" does. The EEPROM keeps its
// contents, as on the board.
static void hard_reset(void)
{
    app_nvram_save();
    machine_reset();
    frame_count = 0;
    stop_audio();
}

enum { M_RESUME, M_SAVE, M_LOAD, M_SLOT, M_GRAPHICS, M_CONTROLS, M_GAME, M_RESET, M_QUIT, M_COUNT };
enum { G_FULLSCREEN, G_MODE, G_WINDOW, G_SCALE, G_SHADER, G_PARAMS, G_PACING, G_PERF, G_BACK, G_COUNT };
enum { C_P1PAD, C_P2PAD, C_P1, C_P2, C_PAD, C_HOTKEYS, C_PADHOT, C_SOCD, C_DEADZONE, C_P1LABELS, C_P2LABELS, C_BACK, C_COUNT };
enum { S_VOLUME, S_MUTE_BG, S_PAUSE_BG, S_VERSION, S_OPERATOR, S_BACK, S_COUNT };

static bool menu_open, menu_changed, quit_requested;
static int menu_page, menu_sel;
static int bind_set;                 // PG_BIND: 0 P1 keys, 1 P2 keys, 2 pad buttons, 3 hotkeys, 4 pad hotkeys
static int capturing = -1;           // row waiting for a key or button
static int version_pick = -1;        // version chosen in the menu, not switched to yet

static const char *const button_labels[NBUTTONS] = {
    "Button 1", "Button 2", "Button 3", "Button 4", "Up", "Down", "Left", "Right", "Start", "Coin",
};
static const char *const hotkey_labels[NHOTKEYS] = {
    "Menu", "Pause", "Frame advance", "Fast-forward (hold)", "Save state", "Load state",
    "Next state slot", "Fullscreen", "Screenshot", "Volume up", "Volume down", "Mute",
    "Test switch (operator)", "Performance overlay", "Quit",
};
#define NPADHOT 4
static const char *const pad_hotkey_labels[NPADHOT] = { "Menu", "Fast-forward (hold)", "Quit", "Screenshot" };
static int *pad_hotkey(int i)
{
    int *const p[NPADHOT] = { &cfg.pad_menu, &cfg.pad_fast, &cfg.pad_quit, &cfg.pad_screenshot };
    return p[i];
}

/* --- shader list --- */

#define MAX_SHADERS 256
static char shader_names[MAX_SHADERS][192];
static int nshaders = -1;

static void scan_shaders(void)
{
    char dir[4200];
    snprintf(dir, sizeof dir, "%sshaders", base_dir());
    nshaders = shader_list(dir, shader_names, MAX_SHADERS);
}

// 0-3 built-in filters, 4+ presets
static int shader_index(void)
{
    if (nshaders < 0) scan_shaders();
    if (!*cfg.shader) return cfg.filter;
    for (int i = 0; i < nshaders; i++) {
        char p[256];
        snprintf(p, sizeof p, "shaders/%s", shader_names[i]);
        if (!strcmp(p, cfg.shader)) return NFILTERS + i;
    }
    return NFILTERS + nshaders;          // set in the .ini to something outside shaders/
}

static void shader_label(int idx, char *buf, size_t n)
{
    static const char *const builtin[NFILTERS] = { "Sharp", "Smooth", "Scanlines", "CRT" };
    if (idx < NFILTERS) { snprintf(buf, n, "%s", builtin[idx]); return; }
    if (idx - NFILTERS >= nshaders) { snprintf(buf, n, "(custom)"); return; }
    const char *name = shader_names[idx - NFILTERS], *slash = strrchr(name, '/');
    snprintf(buf, n, "%s", slash ? slash + 1 : name);
    char *dot = strrchr(buf, '.');
    if (dot) *dot = 0;
}

static void select_shader(int idx)
{
    if (!video_shaders_supported() && idx >= NFILTERS) { osd_message("Shaders need OpenGL"); return; }
    if (idx < NFILTERS) { cfg.filter = idx; cfg.shader[0] = 0; }
    else snprintf(cfg.shader, sizeof cfg.shader, "shaders/%s", shader_names[idx - NFILTERS]);
    cfg.shader_params[0] = 0;
    if (!apply_shader() && idx >= NFILTERS) cfg.shader[0] = 0;
}

/* --- display modes --- */

static int display_modes(SDL_DisplayMode *out, int max)
{
    int d = SDL_GetWindowDisplayIndex(win), n = 0;
    for (int i = 0; i < SDL_GetNumDisplayModes(d) && n < max; i++) {
        SDL_DisplayMode m;
        if (SDL_GetDisplayMode(d, i, &m) || m.w < 640) continue;
        bool dup = false;
        for (int k = 0; k < n; k++) if (out[k].w == m.w && out[k].h == m.h && out[k].refresh_rate == m.refresh_rate) dup = true;
        if (!dup) out[n++] = m;
    }
    return n;
}

/* --- versions --- */

static int versions_there(void)
{
    int n = 0;
    for (int v = 0; v < GAME_COUNT; v++) n += app_set_there((game_id_t)v);
    return n;
}

static const char *version_label(int v)
{
    static const char *const names[GAME_COUNT] = { "2.3 Jan 1995", "2.3 Dec 1994", "2.0" };
    return v >= 0 && v < GAME_COUNT ? names[v] : "?";
}

/* --- drawing --- */

static int page_count(void)
{
    switch (menu_page) {
    case PG_GRAPHICS: return G_COUNT;
    case PG_PARAMS: return shader_param_count() + 2;
    case PG_CONTROLS: return C_COUNT;
    case PG_BIND: return (bind_set == 3 ? NHOTKEYS : bind_set == 4 ? NPADHOT : NBUTTONS) + 1;
    case PG_GAME: return S_COUNT;
    default: return M_COUNT;
    }
}

static void draw_menu(uint32_t *out)
{
    if (!menu_open) return;
    static char b[64][72];
    const char *items[64];
    int n = page_count();
    if (n > 64) n = 64;
    char footer[96];
    pad_prompt_footer(&pad_prompt, prompt_override(), "SELECT", "BACK", footer, sizeof footer);
    const char *title = PRODUCT, *foot = footer;
    static const char *const onoff[2] = { "Off", "On" };
    char lbl[80];

    switch (menu_page) {
    case PG_MAIN: {
        static char main_title[64];
        snprintf(main_title, sizeof main_title, "%s  v%s", PRODUCT, rr_version);
        title = main_title;
        snprintf(b[M_RESUME], 72, "Resume");
        snprintf(b[M_SAVE], 72, "Save state\tslot %d", slot());
        snprintf(b[M_LOAD], 72, "Load state\tslot %d", slot());
        snprintf(b[M_SLOT], 72, "State slot\t< %d >", slot());
        snprintf(b[M_GRAPHICS], 72, "Graphics...");
        snprintf(b[M_CONTROLS], 72, "Controls...");
        snprintf(b[M_GAME], 72, "Game & system...");
        snprintf(b[M_RESET], 72, "Reset game");
        snprintf(b[M_QUIT], 72, "Quit");
        pad_prompt_footer(&pad_prompt, prompt_override(), "SELECT", "CLOSE", footer, sizeof footer);
        break;
    }
    case PG_GRAPHICS: {
        static const char *const scale_names[] = { "Fit 4:3", "Integer", "Stretch" };
        static const char *const pacing_names[] = { "Auto", "Audio", "Vsync" };
        static const char *const display_names[] = { "Off", "Borderless", "Exclusive" };
        title = "GRAPHICS";
        snprintf(b[G_FULLSCREEN], 72, "Fullscreen\t< %s >", display_names[shown_display()]);
        snprintf(b[G_MODE], 72, "Exclusive mode\t< %s >", *cfg.fullscreen_mode ? cfg.fullscreen_mode : "Desktop");
        int ww, wh;
        SDL_GetWindowSize(win, &ww, &wh);
        snprintf(b[G_WINDOW], 72, "Window size\t< %dx%d >", ww, wh);
        snprintf(b[G_SCALE], 72, "Scaling\t< %s >", scale_names[cfg.scale_mode]);
        shader_label(shader_index(), lbl, sizeof lbl);
        snprintf(b[G_SHADER], 72, "Shader\t< %s >", lbl);
        snprintf(b[G_PARAMS], 72, shader_param_count() ? "Shader parameters...\t%d" : "Shader parameters\t(none)", shader_param_count());
        snprintf(b[G_PACING], 72, "Frame pacing\t< %s%s >", pacing_names[cfg.pacing],
                 cfg.pacing == PACING_AUDIO ? "" : vsync_broken ? ": not working" :
                 cfg.pacing == PACING_AUTO ? (vsync ? ": vsync" : ": audio") : "");
        snprintf(b[G_PERF], 72, "Performance overlay\t%s", onoff[cfg.perf_overlay]);
        snprintf(b[G_BACK], 72, "Back");
        pad_prompt_adjust_footer(&pad_prompt, prompt_override(), footer, sizeof footer);
        break;
    }
    case PG_PARAMS:
        title = "SHADER PARAMETERS";
        for (int i = 0; i < n - 2; i++) {
            shader_param_t *p = shader_param(i);
            snprintf(b[i], 72, "%.30s\t%s%.4g", p->desc, p->value != p->def ? "*" : "", p->value);
        }
        snprintf(b[n - 2], 72, "Reset all to defaults");
        snprintf(b[n - 1], 72, "Back");
        foot = "LEFT/RIGHT ADJUST   ENTER RESETS ONE";
        break;
    case PG_CONTROLS:
        title = "CONTROLS";
        for (int p = 0; p < 2; p++)
            snprintf(b[p ? C_P2PAD : C_P1PAD], 72, "P%d controller\t%.26s", p + 1,
                     capturing == (p ? C_P2PAD : C_P1PAD) ? "press a button on it" : pads[p] ? pad_title(pads[p]) : "None");
        snprintf(b[C_P1], 72, "Player 1 keys...");
        snprintf(b[C_P2], 72, "Player 2 keys...");
        snprintf(b[C_PAD], 72, "Gamepad buttons...");
        snprintf(b[C_HOTKEYS], 72, "Hotkeys...");
        snprintf(b[C_PADHOT], 72, "Gamepad hotkeys...");
        snprintf(b[C_SOCD], 72, "Left+right = neutral\t%s", onoff[cfg.socd]);
        snprintf(b[C_DEADZONE], 72, "Stick deadzone\t< %d%% >", cfg.pad_deadzone * 100 / 32767);
        for (int p = 0; p < 2; p++)
            snprintf(b[p ? C_P2LABELS : C_P1LABELS], 72, "P%d button labels\t< %s%s%s >", p + 1,
                     pad_family_name(cfg.pad_family[p]), cfg.pad_family[p] == PAD_FAMILY_AUTO ? ": " : "",
                     cfg.pad_family[p] == PAD_FAMILY_AUTO ? pad_family_name(pad_label_family(pads[p], PAD_FAMILY_AUTO)) : "");
        snprintf(b[C_BACK], 72, "Back");
        if (capturing >= 0) foot = "ESC CANCELS";
        break;
    case PG_BIND: {
        static const char *const titles[] = { "PLAYER 1 KEYS", "PLAYER 2 KEYS", "GAMEPAD BUTTONS", "HOTKEYS", "GAMEPAD HOTKEYS" };
        title = titles[bind_set];
        for (int i = 0; i < n - 1; i++) {
            const char *v;
            if (capturing == i) v = bind_set == 2 || bind_set == 4 ? "press a button" : "press a key";
            else if (bind_set <= 1) v = cfg.keys[bind_set][i] ? SDL_GetScancodeName(cfg.keys[bind_set][i]) : "-";
            else if (bind_set == 2) v = pad_label(cfg.pad[i]);
            else if (bind_set == 3) v = cfg.hotkeys[i] ? SDL_GetScancodeName(cfg.hotkeys[i]) : "-";
            else v = pad_label(*pad_hotkey(i));
            const char *name = bind_set <= 2 ? button_labels[i] : bind_set == 3 ? hotkey_labels[i] : pad_hotkey_labels[i];
            snprintf(b[i], 72, "%s\t%s", name, v);
        }
        snprintf(b[n - 1], 72, "Back");
        if (capturing >= 0) foot = "ESC CANCELS   BACKSPACE CLEARS";
        else pad_prompt_footer(&pad_prompt, prompt_override(), "CHANGE", "BACK", footer, sizeof footer);
        break;
    }
    case PG_GAME: {
        title = "GAME & SYSTEM";
        if (muted) snprintf(b[S_VOLUME], 72, "Volume\t< Muted >");
        else snprintf(b[S_VOLUME], 72, "Volume\t< %d%% >", volume);
        snprintf(b[S_MUTE_BG], 72, "Mute in background\t%s", onoff[cfg.mute_unfocused]);
        snprintf(b[S_PAUSE_BG], 72, "Pause in background\t%s", onoff[cfg.pause_unfocused]);
        int cur = (int)mach_profile->id;
        if (version_pick >= 0 && version_pick != cur) {
            snprintf(b[S_VERSION], 72, "Game version\t< %s >  Enter: switch", version_label(version_pick));
            foot = "SWITCHING RESTARTS THE GAME";
        } else if (versions_there() > 1) snprintf(b[S_VERSION], 72, "Game version\t< %s >", version_label(cur));
        else snprintf(b[S_VERSION], 72, "Game version\t%s", version_label(cur));
        snprintf(b[S_OPERATOR], 72, "Operator menu\t%s (hold)", cfg.hotkeys[HK_TEST] ? SDL_GetScancodeName(cfg.hotkeys[HK_TEST]) : "-");
        snprintf(b[S_BACK], 72, "Back");
        if (menu_sel == S_OPERATOR) foot = "COINAGE, FREE PLAY, DIFFICULTY: IN THE GAME'S TEST MODE";
        break;
    }
    }
    for (int i = 0; i < n; i++) items[i] = b[i];
    osd_menu(out, title, items, n, menu_sel, foot);
}

/* --- actions --- */

static void menu_close(void)
{
    menu_open = false;
    menu_page = PG_MAIN;
    capturing = -1;
    version_pick = -1;
    settle_display(true);
    if (menu_changed) {
        cfg.volume = volume;
        cfg.fullscreen = fullscreen;
        if (shader_active()) shader_params_save(cfg.shader_params, sizeof cfg.shader_params);
        if (config_save()) osd_message("Settings saved");
        menu_changed = false;
    }
}

static void cycle(int *v, int n, int dir) { *v = ((*v + dir) % n + n) % n; }

static void go(int page, int sel) { menu_page = page; menu_sel = sel; capturing = -1; }

// Left/right on the selected item (dir = -1/+1), or Enter (dir = 0).
static void menu_change(int dir)
{
    int d = dir ? dir : 1;
    switch (menu_page) {
    case PG_MAIN:
        switch (menu_sel) {
        case M_RESUME: if (!dir) menu_close(); return;
        case M_SAVE: if (!dir) { save_state(); menu_close(); } return;
        case M_LOAD: if (!dir) { load_state(); menu_close(); } return;
        case M_SLOT: { int s = slot() - 1; cycle(&s, 9, d); cfg.state_slot = s + 1; break; }
        case M_GRAPHICS: if (!dir) go(PG_GRAPHICS, 0); return;
        case M_CONTROLS: if (!dir) go(PG_CONTROLS, 0); return;
        case M_GAME: if (!dir) go(PG_GAME, 0); return;
        case M_RESET:
            if (dir) return;
            hard_reset();
            menu_close();
            osd_message("Reset");
            return;
        case M_QUIT: if (!dir) quit_requested = true; return;
        }
        break;

    case PG_GRAPHICS:
        switch (menu_sel) {
        case G_FULLSCREEN: {
            int m = shown_display();
            cycle(&m, 3, d);
            cfg.fullscreen_exclusive = m == 2;
            request_display(m);
            break;
        }
        case G_MODE: {
            SDL_DisplayMode modes[64];
            int n = display_modes(modes, 64), cur = -1;
            for (int i = 0; i < n; i++) {
                char s[32];
                snprintf(s, sizeof s, "%dx%d@%d", modes[i].w, modes[i].h, modes[i].refresh_rate);
                if (!strcmp(s, cfg.fullscreen_mode)) cur = i;
            }
            cur += d;                                   // -1 means "desktop"
            if (cur < -1) cur = n - 1;
            if (cur >= n) cur = -1;
            if (cur < 0) cfg.fullscreen_mode[0] = 0;
            else snprintf(cfg.fullscreen_mode, sizeof cfg.fullscreen_mode, "%dx%d@%d", modes[cur].w, modes[cur].h, modes[cur].refresh_rate);
            if (shown_display() == 2) request_display(2);
            break;
        }
        case G_WINDOW: {
            if (shown_display()) { osd_message("Leave fullscreen to resize the window"); return; }
            int ww, wh;
            SDL_GetWindowSize(win, &ww, &wh);
            int k = (wh + 120) / 240 + d;
            if (k < 1) k = 8;
            if (k > 8) k = 1;
            SDL_SetWindowSize(win, 320 * k, 240 * k);
            SDL_SetWindowPosition(win, SDL_WINDOWPOS_CENTERED_DISPLAY(SDL_GetWindowDisplayIndex(win)),
                                  SDL_WINDOWPOS_CENTERED_DISPLAY(SDL_GetWindowDisplayIndex(win)));
            cfg.window_scale = k;
            break;
        }
        case G_SCALE: cycle(&cfg.scale_mode, 3, d); break;
        case G_SHADER: {
            if (nshaders < 0 || !dir) scan_shaders();
            int idx = shader_index(), total = NFILTERS + nshaders;
            idx = idx >= total ? 0 : ((idx + d) % total + total) % total;
            select_shader(idx);
            break;
        }
        case G_PARAMS: if (!dir && shader_param_count()) go(PG_PARAMS, 0); return;
        case G_PACING: cycle(&cfg.pacing, 3, d); vsync_broken = false; choose_pacing(); break;
        case G_PERF: cfg.perf_overlay = !cfg.perf_overlay; break;
        case G_BACK: if (!dir) go(PG_MAIN, M_GRAPHICS); return;
        }
        break;

    case PG_PARAMS: {
        int n = shader_param_count();
        if (menu_sel == n + 1) { if (!dir) go(PG_GRAPHICS, G_PARAMS); return; }
        if (menu_sel == n) {
            if (dir) return;
            for (int i = 0; i < n; i++) shader_param(i)->value = shader_param(i)->def;
            break;
        }
        shader_param_t *p = shader_param(menu_sel);
        if (!dir) p->value = p->def;
        else {
            p->value += p->step * dir;
            if (p->value < p->min) p->value = p->min;
            if (p->value > p->max) p->value = p->max;
            if (p->value > -1e-6f && p->value < 1e-6f) p->value = 0;
        }
        break;
    }

    case PG_CONTROLS:
        switch (menu_sel) {
        case C_P1: case C_P2: case C_PAD: case C_HOTKEYS: case C_PADHOT:
            if (!dir) { bind_set = menu_sel - C_P1; go(PG_BIND, 0); }
            return;
        case C_P1PAD: case C_P2PAD: {
            int p = menu_sel == C_P2PAD;
            if (dir) choose_pad(p, next_pad(p, dir));
            else { capturing = menu_sel; return; }
            break;
        }
        case C_SOCD: cfg.socd = !cfg.socd; break;
        case C_DEADZONE: {
            int pct = cfg.pad_deadzone * 100 / 32767 + 5 * d;
            pct = pct < 5 ? 5 : pct > 90 ? 90 : pct;
            cfg.pad_deadzone = pct * 32767 / 100;
            break;
        }
        case C_P1LABELS: case C_P2LABELS:
            cycle(&cfg.pad_family[menu_sel == C_P1LABELS ? 0 : 1], PAD_FAMILY_COUNT, d);
            break;
        case C_BACK: if (!dir) go(PG_MAIN, M_CONTROLS); return;
        }
        break;

    case PG_BIND:
        if (menu_sel == page_count() - 1) { if (!dir) go(PG_CONTROLS, C_P1 + bind_set); return; }
        if (!dir) capturing = menu_sel;
        return;

    case PG_GAME:
        switch (menu_sel) {
        case S_VOLUME:
            if (!dir) muted = !muted;
            else { volume += 10 * dir; volume = volume < 0 ? 0 : volume > 100 ? 100 : volume; muted = false; }
            break;
        case S_MUTE_BG: cfg.mute_unfocused = !cfg.mute_unfocused; break;
        case S_PAUSE_BG: cfg.pause_unfocused = !cfg.pause_unfocused; break;
        case S_VERSION: {
            int cur = (int)mach_profile->id;
            if (versions_there() < 2) return;
            if (version_pick < 0) version_pick = cur;
            if (dir) {                              // browse the versions there are
                do version_pick = (version_pick + dir + GAME_COUNT) % GAME_COUNT;
                while (!app_set_there((game_id_t)version_pick));
                return;
            }
            if (version_pick == cur) return;
            snprintf(cfg.version, sizeof cfg.version, "%s", game_profile((game_id_t)version_pick)->set);
            menu_changed = true;
            menu_close();
            app_restart();                          // returns only if that failed
            osd_message("Could not restart: the version changes next time");
            return;
        }
        case S_OPERATOR: return;
        case S_BACK: if (!dir) go(PG_MAIN, M_GAME); return;
        }
        break;
    }
    menu_changed = true;
}

static void menu_toggle(void)
{
    if (menu_open) menu_close();
    else { menu_open = true; go(PG_MAIN, 0); stop_audio(); }
}

static void menu_back(void)
{
    switch (menu_page) {
    case PG_MAIN: menu_close(); break;
    case PG_PARAMS: go(PG_GRAPHICS, G_PARAMS); break;
    case PG_BIND: go(PG_CONTROLS, C_P1 + bind_set); break;
    case PG_GRAPHICS: go(PG_MAIN, M_GRAPHICS); break;
    case PG_CONTROLS: go(PG_MAIN, M_CONTROLS); break;
    case PG_GAME: go(PG_MAIN, M_GAME); break;
    }
}

static void menu_nav(int up_down, int left_right, bool enter, bool back)
{
    if (up_down) cycle(&menu_sel, page_count(), up_down);
    if (left_right) menu_change(left_right);
    if (enter) menu_change(0);
    if (back) menu_back();
}

// A key or pad button for the binding being captured. key = 0: pad input,
// from the pad `from`.
static void capture(SDL_Scancode key, int pad_button, bool clear, SDL_JoystickID from)
{
    int row = capturing;
    if (menu_page == PG_CONTROLS) {         // picking a player's controller: a press on it
        if (key == SDL_SCANCODE_ESCAPE) capturing = -1;
        if (key || clear) return;
        capturing = -1;
        choose_pad(row == C_P2PAD, SDL_GameControllerFromInstanceID(from));
        menu_changed = true;
        return;
    }
    bool pad_set = bind_set == 2 || bind_set == 4;
    if (!clear && (pad_set ? pad_button == PAD_NONE : !key)) return;    // wrong kind of input: keep waiting
    capturing = -1;
    switch (bind_set) {
    case 0: case 1: cfg.keys[bind_set][row] = clear ? SDL_SCANCODE_UNKNOWN : key; break;
    case 2: cfg.pad[row] = clear ? PAD_NONE : pad_button; break;
    case 3: cfg.hotkeys[row] = clear ? SDL_SCANCODE_UNKNOWN : key; break;
    case 4: *pad_hotkey(row) = clear ? PAD_NONE : pad_button; break;
    }
    menu_changed = true;
}

/* ---------------------------------------------------------------- */
/* Events                                                           */
/* ---------------------------------------------------------------- */

// Holding Back+Start quits (a tap opens the menu): the pad and since when.
#define QUIT_HOLD_MS 2000
static SDL_JoystickID quit_pad = -1;
static uint32_t quit_since;

static void check_quit_hold(void)
{
    SDL_GameController *c = quit_pad >= 0 ? SDL_GameControllerFromInstanceID(quit_pad) : NULL;
    if (!c || !SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_BACK) || !SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_START)) {
        quit_pad = -1;
        return;
    }
    uint32_t held = SDL_GetTicks() - quit_since;
    if (held >= QUIT_HOLD_MS) quit_requested = true;
    else if (held >= 600) osd_message("Keep holding to quit... %u", (QUIT_HOLD_MS - held + 999) / 1000);
}

/* returns false to quit */
static bool handle_events(void)
{
    settle_display(false);
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        pad_prompt_event(&pad_prompt, &ev);
        switch (ev.type) {
        case SDL_QUIT: return false;
        case SDL_CONTROLLERDEVICEADDED: pad_added(ev.cdevice.which); break;
        case SDL_CONTROLLERDEVICEREMOVED: pad_removed(ev.cdevice.which); break;
        case SDL_JOYDEVICEADDED: log_joystick(ev.jdevice.which); break;
        case SDL_WINDOWEVENT:
            if (ev.window.event == SDL_WINDOWEVENT_DISPLAY_CHANGED || ev.window.event == SDL_WINDOWEVENT_MOVED) choose_pacing();
            if (ev.window.event == SDL_WINDOWEVENT_FOCUS_LOST) { focused = false; if (cfg.mute_unfocused || cfg.pause_unfocused) stop_audio(); }
            if (ev.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) focused = true;
            break;
        case SDL_CONTROLLERAXISMOTION:
            if (menu_open && capturing >= 0 && ev.caxis.value > 20000) {
                if (ev.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT) capture(0, PAD_LT, false, ev.caxis.which);
                if (ev.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) capture(0, PAD_RT, false, ev.caxis.which);
            } else if (ev.caxis.value > 20000 &&
                       ((ev.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT && cfg.pad_quit == PAD_LT) ||
                        (ev.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT && cfg.pad_quit == PAD_RT)))
                quit_requested = true;
            break;
        case SDL_CONTROLLERBUTTONDOWN: {
            int b = ev.cbutton.button;
            if (menu_open && capturing >= 0) { capture(0, b, false, ev.cbutton.which); break; }
            SDL_GameController *c = SDL_GameControllerFromInstanceID(ev.cbutton.which);
            bool combo = c && ((b == SDL_CONTROLLER_BUTTON_START && SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_BACK)) ||
                               (b == SDL_CONTROLLER_BUTTON_BACK && SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_START)));
            if (cfg.pad_quit != PAD_NONE && b == cfg.pad_quit) { quit_requested = true; break; }
            if (combo && quit_pad == ev.cbutton.which) break;     // the pair's other button: one press, one toggle
            if (combo) { quit_pad = ev.cbutton.which; quit_since = SDL_GetTicks(); }
            if ((cfg.pad_menu != PAD_NONE && b == cfg.pad_menu) || combo) { menu_toggle(); break; }
            if (!menu_open) break;
            menu_nav(b == SDL_CONTROLLER_BUTTON_DPAD_UP ? -1 : b == SDL_CONTROLLER_BUTTON_DPAD_DOWN ? 1 : 0,
                     b == SDL_CONTROLLER_BUTTON_DPAD_LEFT ? -1 : b == SDL_CONTROLLER_BUTTON_DPAD_RIGHT ? 1 : 0,
                     b == SDL_CONTROLLER_BUTTON_A, b == SDL_CONTROLLER_BUTTON_B || b == SDL_CONTROLLER_BUTTON_START);
            break;
        }
        case SDL_KEYDOWN: {
            SDL_Scancode sc = ev.key.keysym.scancode;
            if (menu_open && capturing >= 0) {
                if (ev.key.repeat) break;
                if (sc == SDL_SCANCODE_ESCAPE) capturing = -1;
                else if (sc == SDL_SCANCODE_BACKSPACE) capture(0, PAD_NONE, true, -1);
                else capture(sc, PAD_NONE, false, -1);
                break;
            }
            bool alt_enter = sc == SDL_SCANCODE_RETURN && (ev.key.keysym.mod & KMOD_ALT);
            if (is_hotkey(sc, HK_QUIT)) { quit_requested = true; break; }
            if (sc == SDL_SCANCODE_ESCAPE && menu_open) { if (!ev.key.repeat) menu_back(); break; }
            // started by a launcher: Esc quits, as in MAME and RetroArch (F1 opens the menu)
            if (sc == SDL_SCANCODE_ESCAPE && launch_active() && !is_hotkey(sc, HK_MENU)) { quit_requested = true; break; }
            if (sc == SDL_SCANCODE_ESCAPE || is_hotkey(sc, HK_MENU)) { if (!ev.key.repeat) menu_toggle(); break; }
            if (menu_open && !alt_enter) {
                menu_nav(sc == SDL_SCANCODE_UP ? -1 : sc == SDL_SCANCODE_DOWN ? 1 : 0,
                         sc == SDL_SCANCODE_LEFT ? -1 : sc == SDL_SCANCODE_RIGHT ? 1 : 0,
                         !ev.key.repeat && (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_SPACE || sc == SDL_SCANCODE_KP_ENTER),
                         sc == SDL_SCANCODE_BACKSPACE);
                break;
            }
            if (ev.key.repeat && !is_hotkey(sc, HK_VOL_UP) && !is_hotkey(sc, HK_VOL_DOWN) && !is_hotkey(sc, HK_STEP)) break;
            if (alt_enter || is_hotkey(sc, HK_FULLSCREEN)) {
                bool on = shown_display() == 0;
                want_display = -1;
                set_fullscreen(on);
                menu_changed = true;
            }
            else if (is_hotkey(sc, HK_PAUSE)) { paused = !paused; if (paused) stop_audio(); }
            else if (is_hotkey(sc, HK_STEP)) { if (paused) step_once = true; else { paused = true; stop_audio(); } }
            else if (is_hotkey(sc, HK_SAVE_STATE)) save_state();
            else if (is_hotkey(sc, HK_LOAD_STATE)) load_state();
            else if (is_hotkey(sc, HK_NEXT_SLOT)) { cfg.state_slot = slot() % 9 + 1; osd_message("State slot %d", slot()); }
            else if (is_hotkey(sc, HK_SCREENSHOT)) screenshot();
            else if (is_hotkey(sc, HK_PERF)) { cfg.perf_overlay = !cfg.perf_overlay; menu_changed = true; }
            else if (is_hotkey(sc, HK_VOL_UP)) set_volume(volume + 10);
            else if (is_hotkey(sc, HK_VOL_DOWN)) set_volume(volume - 10);
            else if (is_hotkey(sc, HK_MUTE)) { muted = !muted; osd_message(muted ? "Muted" : "Volume %d%%", volume); }
            break;
        }
        }
    }
    check_quit_hold();
    // the pad's screenshot button, polled so a trigger works too: one shot per press
    static bool shot_held;
    bool shot = !(menu_open && capturing >= 0) && pad_held(cfg.pad_screenshot);
    if (shot && !shot_held) screenshot();
    shot_held = shot;
    return !quit_requested;
}

// One real frame of the game: inputs, then emulation.
static void emulate_frame(void)
{
    uint64_t t0 = SDL_GetPerformanceCounter();
    uint32_t held[2];
    read_players(held);
    set_ports(held);
    machine_frame();
    frame_count++;
    perf_emu(t0);
}

static bool apply_shader(void)
{
    char path[4400], err[1024];
    if (!*cfg.shader) { video_set_shader(NULL, err, sizeof err); return true; }
    snprintf(path, sizeof path, "%s%s", base_dir(), cfg.shader);
    if (!video_set_shader(path, err, sizeof err)) {
        fprintf(stderr, "shader: %s\n", err);
        osd_message("Shader failed to load (see the log)");
        return false;
    }
    shader_params_apply(cfg.shader_params);
    return true;
}

// --test-shaders: load and draw every preset under shaders/, report each one.
static int test_shaders(void)
{
    static char names[512][192];
    char dir[4200];
    snprintf(dir, sizeof dir, "%sshaders", base_dir());
    int n = shader_list(dir, names, 512), ok = 0;
    for (int i = 0; i < n; i++) {
        char path[4400], err[1024];
        snprintf(path, sizeof path, "%s/%s", dir, names[i]);
        uint64_t t0 = SDL_GetPerformanceCounter();
        bool good = video_set_shader(path, err, sizeof err);
        if (good) for (int k = 0; k < 3; k++) { to_rgb(px); osd_clear(ov); video_present(px, ov, cfg.filter, cfg.scale_mode); }
        double ms = (SDL_GetPerformanceCounter() - t0) * 1000.0 / SDL_GetPerformanceFrequency();
        printf("%-4s %-60s %6.1f ms  %d params%s%s\n", good ? "OK" : "FAIL", names[i], ms, good ? shader_param_count() : 0,
               good ? "" : "\n     ", good ? "" : err);
        ok += good;
    }
    printf("%d of %d presets work\n", ok, n);
    return ok == n ? 0 : 1;
}

/* ---------------------------------------------------------------- */
/* First launch: build the game code for this PC                    */
/* ---------------------------------------------------------------- */

static void prepare_progress(const char *stage, double fraction)
{
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) if (ev.type == SDL_CONTROLLERDEVICEADDED) pad_added(ev.cdevice.which);
    memset(px, 0, sizeof px);
    osd_clear(ov);
    int x0 = (SCR_W - 260) / 2;
    osd_box(ov, x0, 70, 260, 100, 0xf0101018u);
    osd_text(ov, SCR_W / 2 - 18 * 3, 80, "PREPARING THE GAME", 0xffc040);
    osd_text(ov, SCR_W / 2 - 23 * 3, 96, "FOR THIS PC (FIRST RUN)", 0xb0b0b0);
    osd_text(ov, SCR_W / 2 - (int)strlen(stage) * 3, 118, stage, 0xffffff);
    osd_box(ov, x0 + 20, 136, 220, 10, 0xff303040u);
    osd_box(ov, x0 + 21, 137, (int)(218 * (fraction < 0 ? 0 : fraction > 1 ? 1 : fraction)), 8, 0xff208040u);
    osd_text(ov, SCR_W / 2 - 25 * 3, 154, "THIS TAKES ABOUT A MINUTE", 0x808080);
    video_present(px, ov, FILTER_SHARP, cfg.scale_mode);
}

// Use translated code: linked in, cached from an earlier run, or built now.
// Without it the game still runs, on the interpreters.
static void prepare_game_code(void)
{
    char msg[512];
    if (getenv("RR_LOAD_MODULE")) {
        if (!rec_build_load_file(getenv("RR_LOAD_MODULE"), msg, sizeof msg)) fprintf(stderr, "rec: %s\n", msg);
        machine_reset();
        return;
    }
    if ((rec_have_m68k() && rec_have_dsp()) || (getenv("RR_NOREC") && getenv("RR_NODSP"))) return;
    if (!getenv("RR_BUILD") && rec_build_load_cached(msg, sizeof msg)) { fprintf(stderr, "rec: %s\n", msg); return; }
    uint64_t t0 = SDL_GetTicks64();
    bool ok = rec_build(prepare_progress, msg, sizeof msg);
    fprintf(stderr, "rec: %s (%.0f s)\n", msg, (SDL_GetTicks64() - t0) / 1000.0);
    machine_reset();                    // the sound DSP's code was read by booting it
    osd_message("%s", ok ? "Ready" : "Running without translated code (slower)");
    if (!ok) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, PRODUCT,
        "The game code could not be prepared for full speed on this PC, so the game runs on the "
        "slower interpreter. ragerecomp.log and cache/src-*/build.log in the data folder say why.", win);
}

/* ---------------------------------------------------------------- */

int frontend_main(void)
{
    pad_prompt_init(&pad_prompt);
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return 1;
    }
    {   // extra controller mappings (https://github.com/mdqinc/SDL_GameControllerDB), if present
        char buf[4200];
        snprintf(buf, sizeof buf, "%sgamecontrollerdb.txt", base_dir());
        if (file_readable(buf)) SDL_GameControllerAddMappingsFromFile(buf);
    }
    attach_test_pads();
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) pad_added(i);
        else log_joystick(i);
    }
    int scale = cfg.window_scale > 0 ? cfg.window_scale : 3;
    win = video_open(PRODUCT, 320 * scale, 240 * scale);
    if (!win) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    set_window_icon(win);
    if (cfg.window_w >= 320 && cfg.window_h >= 240) {
        // where it was last time, if that is still on a screen
        for (int d = 0; d < SDL_GetNumVideoDisplays(); d++) {
            SDL_Rect r;
            SDL_Point pt = { cfg.window_x + 40, cfg.window_y + 40 };
            if (SDL_GetDisplayBounds(d, &r) == 0 && SDL_PointInRect(&pt, &r)) {
                SDL_SetWindowSize(win, cfg.window_w, cfg.window_h);
                SDL_SetWindowPosition(win, cfg.window_x, cfg.window_y);
                break;
            }
        }
    }
    fprintf(stderr, "video: %s\n", video_backend());
    if (cfg.fullscreen) set_fullscreen(true);
    choose_pacing();

    SDL_AudioSpec want = { .freq = AUDIO_HZ, .format = AUDIO_S16SYS, .channels = 2, .samples = 512 }, have;
    adev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!adev) fprintf(stderr, "audio: %s\n", SDL_GetError());
    else SDL_PauseAudioDevice(adev, 0);
    volume = cfg.volume < 0 ? 0 : cfg.volume > 100 ? 100 : cfg.volume;
    int latency = cfg.audio_latency_ms > 10 ? cfg.audio_latency_ms : 10;

    if (opt_test_shaders) { int r = test_shaders(); video_close(); SDL_Quit(); return r; }
    prepare_game_code();
    apply_shader();

    uint64_t freq = SDL_GetPerformanceFrequency();
    uint64_t next = SDL_GetPerformanceCounter();
    uint32_t last_nvram = SDL_GetTicks();
    while (handle_events()) {
        bool fast = key_held(HK_FAST) || pad_held(cfg.pad_fast);
        if (SDL_GetTicks() - last_nvram > 10000) { app_nvram_save(); last_nvram = SDL_GetTicks(); }

        if (menu_open || (paused && !step_once) || (!focused && cfg.pause_unfocused)) {
            present(menu_open ? NULL : "PAUSED");
            if (!vsync) SDL_Delay(16);
            next = SDL_GetPerformanceCounter();
            continue;
        }
        if (step_once) {
            step_once = false;
            emulate_frame();
            pump_audio(true, 1);
            present("PAUSED");
            continue;
        }
        if (fast) {
            // run unthrottled (or up to the speed limit) for one display frame, silently
            uint64_t start = SDL_GetPerformanceCounter(), budget = freq / 60;
            int limit = cfg.fast_speed > 0 ? cfg.fast_speed : 1000;
            for (int n = 0; n < limit && SDL_GetPerformanceCounter() - start < budget; n++) {
                emulate_frame();
                pump_audio(true, 1);
            }
            stop_audio();
            present(">> FAST");
            if (!vsync) {
                uint64_t now = SDL_GetPerformanceCounter();
                if (now - start < budget) SDL_Delay((uint32_t)((budget - (now - start)) * 1000 / freq));
            }
            next = SDL_GetPerformanceCounter();
            continue;
        }
        if (vsync) {
            if (!vsync_lock) {
                // vsync on a display that isn't ~60 Hz: tear-free, but a frame
                // is emulated only when the clock says one is due
                uint64_t now = SDL_GetPerformanceCounter();
                if (now < next) { SDL_Delay(1); continue; }
                next += (uint64_t)(freq / FRAME_HZ);
                if (now > next + freq / 10) next = now;        // after a hitch: don't race to catch up
            }
            emulate_frame();
            if (vsync_lock) check_vsync();
            // nudge the resampling so the audio queue stays near its target
            double q = queued_ms();
            if (adev && q < 5) {        // (re)starting or after a hitch: pad with silence
                static int16_t silence[AUDIO_HZ / 10 * 2];
                int n = AUDIO_HZ * latency / 2000;
                SDL_QueueAudio(adev, silence, (Uint32)(n < AUDIO_HZ / 10 ? n : AUDIO_HZ / 10) * 4);
                q = queued_ms();
            }
            double err = (q - latency) / latency;
            if (err > 1) err = 1;
            if (err < -1) err = -1;
            // locked: frames come display_hz times a second, so resample to match
            pump_audio(false, (vsync_lock ? display_hz / FRAME_HZ : 1) * (1 + 0.005 * err));
            present(NULL);
        } else {
            emulate_frame();
            pump_audio(false, 1);
            present(NULL);
            if (adev) {
                // the audio device is the clock: keep `latency` ms queued
                while (queued_ms() > latency) SDL_Delay(1);
            } else {
                next += (uint64_t)(freq / FRAME_HZ);
                uint64_t now = SDL_GetPerformanceCounter();
                if (now < next) SDL_Delay((uint32_t)((next - now) * 1000 / freq));
                else next = now;
            }
        }
    }
    if (menu_changed) menu_close();
    settle_display(true);
    if (!fullscreen) {
        SDL_GetWindowPosition(win, &cfg.window_x, &cfg.window_y);
        SDL_GetWindowSize(win, &cfg.window_w, &cfg.window_h);
    }
    cfg.fullscreen = fullscreen;
    cfg.volume = volume;
    config_save();
    video_close();
    SDL_Quit();
    return 0;
}
