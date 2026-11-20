// Atari GT video for Primal Rage: playfield/alpha tile decode, RLE motion
// objects, CRAM/MRAM composite and palette. Behaviour follows MAME's
// atarigt_v.cpp and atarirle.cpp (see NOTICE).
#pragma once
#include <stdint.h>

// Decoded playfield pixels (6-bit blended) and alpha pixels (4-bit).
// Indexed [tile][y][x]; allocated by gtvid_decode().
extern uint8_t *gt_pf;
extern uint8_t *gt_alpha;
#define GT_PF_TILES 62500u
#define GT_ALPHA_TILES 4096u

// Decode tiles/chars/overlay into gt_pf/gt_alpha (call after ROM load).
void gtvid_decode(void);
void gtvid_free(void);
// RLE tables, object prescan and checksums (also run by gtvid_decode).
void gtvid_mo_init(void);
// Power-on reset of the video state.
void gt_reset(void);
// Vblank hook: finish a pending MO erase, reset the partial line.
void gt_mo_vblank(void);

// Colour RAM access (16-bit halves at byte addresses).
uint16_t gt_colorram_r(uint32_t addr);
void gt_colorram_w(uint32_t addr, uint16_t val);

// RLE motion-object control (latch D13-D11) and commands (0xD7A200).
void gt_mo_control(uint8_t bits);
void gt_mo_command(uint16_t cmd);

// Per-8-line scroll-row update at raster line (multiple of 8).
void gt_scanline(int line);
// Full frame composite into mach.frame_rgb (call at vblank).
void gt_render(void);
// Save-state span (scroll rows, MO VRAMs, palette, control).
uint32_t gt_state_size(void);
void gt_state_save(uint8_t *dst);
void gt_state_load(const uint8_t *src);
