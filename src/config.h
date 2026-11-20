// Frontend settings, kept in ragerecomp.ini in the data folder.
#pragma once
#include <SDL2/SDL.h>
#include <stdbool.h>
#include "controls.h"

enum { HK_MENU, HK_PAUSE, HK_STEP, HK_FAST, HK_REWIND, HK_SAVE_STATE, HK_LOAD_STATE, HK_NEXT_SLOT,
       HK_FULLSCREEN, HK_SCREENSHOT, HK_VOL_UP, HK_VOL_DOWN, HK_MUTE, HK_TEST,
       HK_INPUT_DISPLAY, HK_TRAINING, HK_DUMMY_RECORD, HK_DUMMY_PLAY,
       HK_PERF, HK_QUIT, NHOTKEYS };

enum { SCALE_FIT, SCALE_INTEGER, SCALE_STRETCH };
enum { PACING_AUTO, PACING_AUDIO, PACING_VSYNC };

// Pad inputs: SDL_GameControllerButton values, plus the two triggers.
#define PAD_LT (SDL_CONTROLLER_BUTTON_MAX + 0)
#define PAD_RT (SDL_CONTROLLER_BUTTON_MAX + 1)
#define PAD_NONE (-1)

typedef struct {
    char roms[1024];             // folder with the MAME zips; empty = the usual places
    char version[16];            // MAME set: primrage, primrageo, primrage20

    bool fullscreen;
    bool fullscreen_exclusive;   // change the display mode instead of a borderless desktop window
    char fullscreen_mode[32];    // "1920x1080@60" for exclusive fullscreen; empty = desktop
    int  window_scale;
    int  window_x, window_y, window_w, window_h;   // last windowed geometry (0 = default)
    int  scale_mode;
    int  filter;               // FILTER_* (video.h), used when no shader preset is set
    char shader[1024];         // libretro .glslp preset, relative to the program folder
    char shader_params[1024];  // "name=value;..." changed parameters of that preset
    int  pacing;

    int  volume;               // 0-100
    int  audio_latency_ms;

    int  boot_skip_frames;     // run this many frames unthrottled and silent at startup
    bool auto_coin;
    int  fast_speed;           // fast-forward speed limit, x realtime (0 = unlimited)
    int  run_ahead;            // frames (0-2) emulated ahead to show input sooner
    bool rewind;
    int  rewind_mb;            // memory for the rewind history
    int  state_slot;           // 1-9
    bool input_display;
    bool perf_overlay;
    bool training;             // health refill + clock held
    bool training_refill;      // training refills health (else only the clock stops)
    int  training_p2;          // player 2's health in training: 0 full, 1 low
    char net_address[1024];    // last host joined
    int  net_port;
    int  net_delay;            // frames of input delay (0-4)
    bool pause_unfocused, mute_unfocused;

    SDL_Scancode keys[2][NBUTTONS];
    SDL_Scancode hotkeys[NHOTKEYS];
    int  pad[NBUTTONS];        // same layout for both pads; PAD_* or SDL button
    int  pad_family[2];        // label overrides only, PAD_FAMILY_*
    char pad_choice[2][128];   // each player's controller, by name; empty = connection order
    int  pad_deadzone;         // 0-32767
    bool socd;                 // left+right = neutral, up+down = up (keyboards, hitboxes)
    bool rumble;
    int  pad_menu, pad_rewind, pad_fast, pad_quit, pad_screenshot;   // gamepad hotkeys (PAD_NONE = unbound)
} config_t;

extern config_t cfg;

// Load from `path`, writing a commented default file there if it does not exist.
void config_load(const char *path);
// Write the current settings back (to the path given to config_load).
bool config_save(void);
// A setting from the command line, "section.key=value" (or section/key), for
// this session only: saving keeps the file's value unless the player changes
// it in the game. After config_load. False (and a message) if it's unknown.
bool config_override(const char *spec);
