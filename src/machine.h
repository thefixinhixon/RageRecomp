// Machine state: everything platform-agnostic (CPUs, RAM/ROM images,
// cabinet I/O, video and protection state) in one bundle. The frontend
// owns windowing/input/audio; this file owns the arcade hardware.
#pragma once
#include "rec_abi.h"
#include "cage.h"
#include "xga.h"
#include "eeprom.h"
#include "game.h"

// Video frame geometry (GT core: 336x240, ~59.92 Hz).
#define GT_WIDTH 336u
#define GT_HEIGHT 240u
#define GT_FPS_NUM 7159090u
#define GT_FPS_DEN 119472u // 456 clocks/line * 262 lines
#define GT_LINES 262u
#define GT_VBLANK_LINE 240u

// Main CPU clock and frame budget.
#define M68K_HZ 25000000u

// Cabinet inputs, sampled once per frame. b0/b1/b3 are the raw P1_P2 port
// bytes (active low; the frontend fills them per the game's layout, since
// older sets reuse the start bits as action buttons). b3 is the top byte
// (P1: start, buttons 1-3, stick), b1 the second (P2, the same); b0, the
// low byte, holds button 4 on the Jan 1995 set (bit 1 P1, bit 3 P2). The
// third byte is unconnected and reads 1s.
typedef struct {
    uint8_t b0;
    uint8_t b1;
    uint8_t b3;
    uint8_t service; // bit0 = self-test button held
    uint8_t coin1;   // coin 1 held
    uint8_t coin2;   // coin 2 held
    uint8_t tilt;    // tilt held
} gt_inputs_t;

typedef struct {
    // CPUs (pointed to by rrm->cpu / rrm->dsp).
    m68k_t cpu;
    c31_t dsp;
    rr_mem_t mem;
    // Main program images.
    uint8_t *rom;       // 2 MB program ROM at 0x000000
    uint8_t *ram;       // 512 KB main RAM at 0xF80000
    uint8_t *bank;      // 64 KB work RAM at 0xD70000
    uint16_t *colorram; // 0x40000 words at 0xD80000
    // Graphics ROM images (owned here, decoded by gtvid).
    uint8_t *tiles;     // 3 MB playfield tiles
    uint8_t *chars;     // 128 KB alpha tiles
    uint8_t *rle;       // 32 MB motion-object ROMs
    // Motion-object RAM is bank[] at 0xD78000 (shared pointer into bank).
    // Latches and interrupt state.
    uint8_t irq3, irq4, irq6; // IPL requests (CAGE, vblank, scanline)
    uint8_t coin_counters;
    uint8_t led_state;
    uint8_t color_latch; // palette bank + CRAM/A select bits (D0-D7)
    uint8_t mo_control;  // motion-object control (X/Z/Y flip + frame)
    uint8_t ignore_writes; // T-Mek only (unused here, kept for structure)
    // Video scan state.
    uint16_t scanline;  // current raster line (0-261)
    uint8_t vblank;
    int32_t scan_cyc;   // cycles until the next scanline tick
    int32_t vblank_cyc; // cycles until vblank entry/exit
    // Exact-rate frame pacing (saved): error carry plus the 250 Hz
    // IRQ6 countdown in 68K cycles.
    uint64_t carry68k, carrydsp;
    int32_t irq6_cyc;
    uint8_t playfield_tile_bank;
    // Frame output (indexed colour + final RGBA set by gtvid).
    uint16_t *frame_ind; // 336*240 playfield/object/alpha composite indices
    uint32_t *frame_rgb; // 336*240 final XRGB pixels
    // Inputs for this frame.
    gt_inputs_t in;
} machine_t;

extern machine_t mach;
// Active profile (set before machine_reset; selects the DSP speedup).
extern const game_profile_t *mach_profile;

// Full power-on reset: clear RAMs/devices, reset both CPUs, map pages.
void machine_reset(void);
// Run one video frame: interleave both CPUs, render at vblank start.
// Returns after GT_FPS pacing worth of emulated time.
void machine_frame(void);
// Save/load the whole machine state (plus stream helpers in state.c).
void machine_state_size(uint32_t *size);
void machine_state_save(uint8_t *dst);
int machine_state_load(const uint8_t *src, uint32_t size);
