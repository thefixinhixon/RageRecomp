// Atari CAGE sound board: TMS320C31 memory map, DMA, timers, serial port,
// host communications and 4-channel DAC mixing. Behaviour follows MAME's
// cage.cpp (see NOTICE). DSP cycle timestamps drive DMA/timer completion.
#pragma once
#include "rec_abi.h"

#define CAGE_DSP_HZ 33868800u
#define CAGE_DAC_CHANNELS 4u
#define CAGE_DAC_FIFO 65536u

typedef struct {
    // DSP memory backing (word arrays, owned here).
    uint32_t *ram;      // 0x10000 words @ 0x000000 (external SRAM)
    uint32_t *iram;     // 0x800 words @ 0x809800 (internal DARAM)
    uint32_t *boot;     // 0x80000 words @ 0x400000 (external boot ROM)
    uint32_t *srom;     // 0x400000 words @ 0xC00000 (sound ROMs)
    uint32_t *bootint;  // 0x1000 words shadowing reads @ 0x000000-0x000FFF
    // C31 internal IO regs (0x808000-0x8080FF).
    uint32_t io[0x100];
    // Host communications.
    uint16_t from_main, control, latch;
    uint8_t cpu_to_cage, cage_to_cpu;
    // Deferred 68K->DSP delivery (MAME's scheduler().synchronize parity:
    // the DSP sees a command on the scanline after the 68K sent it, so
    // the 68K-side handshake waits a beat instead of racing ahead).
    uint16_t pend[64];
    uint8_t pend_hd, pend_tl;
    uint16_t vis[64];
    uint8_t vis_hd, vis_tl;
    // DMA state.
    uint8_t dma_enabled, dma_timer_on;
    int64_t dma_end_cyc;
    uint32_t dma_end_addr;
    // Timer state (period expiries in DSP cycles).
    uint8_t timer_on[2];
    int64_t timer_end[2];
    // Serial-derived DAC timing.
    double word_cyc;    // DSP cycles per serial word
    uint32_t dac_rate;  // samples/sec/channel
    // DAC channel FIFOs (int16 samples from DMA, 4-way deinterleaved).
    int16_t *dac[CAGE_DAC_CHANNELS];
    uint32_t dac_wr[CAGE_DAC_CHANNELS], dac_rd[CAGE_DAC_CHANNELS];
    // Reset line state (1 = DSP held in reset).
    uint8_t dsp_held;
} cage_t;

extern cage_t cage;
// DSP-cycle clock base and current slice budget (saved with state).
extern int64_t cage_abs_cyc;
extern int cage_slice_budget;

// Connect the DSP page tables to the board memory. Called after the ROMs
// are loaded (boot/srom may be NULL before that; reads return 0).
void cage_map_pages(void);
// Slice bracketing for the DSP-cycle clock (call around each DSP run).
void cage_slice_begin(int budget);
void cage_slice_end(void);
// Board reset (power-on): clears regs, FIFOs and comm state.
void cage_reset(void);
// Move pending 68K->DSP commands to the DSP-visible queue. Called once
// per scanline before either CPU runs.
void cage_scanline_swap(void);
// Slow DSP memory paths (wired into rr_hostfns_t).
uint32_t cage_drd(uint32_t addr);
void cage_dwr(uint32_t addr, uint32_t val);
// Host (68K) side, at 0xC00000.
uint16_t cage_main_r(void);
void cage_main_w(uint16_t data);
uint16_t cage_control_r(void);
void cage_control_w(uint16_t data);
// Latch bit 21 (/XRESET) handler: 1 = reset the DSP, 0 = release it.
void cage_reset_w(int state);
// Advance board timers to the DSP's current cycle count (call at slice
// boundaries and before idling). Fires DMA/timer completions.
void cage_update(void);
// Recompute the serial word rate from the port registers (state loads).
void cage_retune(void);
// IRQ3 reason for the main CPU (0 = clear line).
int cage_irq_reason(void);
// Mix queued DAC samples into stereo int16. Returns frames written and
// advances the read cursors. Underruns produce silence.
uint32_t cage_mix(int16_t *dst_l, int16_t *dst_r, uint32_t frames);
// Frames currently queued (per channel, all equal length).
uint32_t cage_queued(void);
