// 68EC020 memory map and I/O dispatch (Atari GT). Fast page tables cover
// ROM and main RAM; everything else goes through the slow paths below,
// which mirror MAME's main_map entry by entry (see NOTICE).
#pragma once
#include "machine.h"

// Map the fast pages after images are loaded. Safe to call again.
void mem_map_pages(void);
// Recompute cpu->irq from the three IPL sources.
void mem_update_irq(void);
// Interrupt line inputs (called by the video/CAGE timing).
void mem_set_scanline(int state);
void mem_set_video(int state);
// Slow paths (wired into rr_hostfns_t).
uint32_t mem_mrd8(uint32_t addr);
uint32_t mem_mrd16(uint32_t addr);
uint32_t mem_mrd32(uint32_t addr);
void mem_mwr8(uint32_t addr, uint32_t val);
void mem_mwr16(uint32_t addr, uint32_t val);
void mem_mwr32(uint32_t addr, uint32_t val);
void mem_mtrap(uint32_t vec, uint32_t pc);
void mem_mirq(void);
int mem_poll(void);
