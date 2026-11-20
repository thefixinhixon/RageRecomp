// Machine wiring: power-on reset and the scanline-interleaved frame loop.
// Clocks and interrupt sources follow MAME's atarigt driver (see NOTICE):
// 68EC020 @ 25 MHz, CAGE DSP @ 33.8688 MHz (2 clocks/cycle), 250 Hz IRQ6,
// vblank IRQ4, CAGE IRQ3, 336x240 @ 59.9228 Hz.
#include "machine.h"
#include "mem.h"
#include "m68k.h"
#include "c31.h"
#include "cage.h"
#include "rec.h"
#include "gtvid.h"
#include "adjust.h"
#include "eeprom.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

machine_t mach;
const game_profile_t *mach_profile = NULL;

// Program-side rrm/rrfns (translated modules get their own copies).
rr_mem_t *rrm = &mach.mem;
static void dirq_noop(void) {
    // Translated DSP code services interrupts itself (shared d_check_irqs
    // at block ends); this slot stays for ABI stability.
}
static const rr_hostfns_t rr_hostfns_default = {
    mem_mrd8, mem_mrd16, mem_mrd32,
    mem_mwr8, mem_mwr16, mem_mwr32,
    mem_mtrap, mem_mirq,
    cage_drd, cage_dwr, dirq_noop,
    mem_poll
};
const rr_hostfns_t *rrfns = &rr_hostfns_default;

void machine_reset(void) {
    memset(&mach.cpu, 0, sizeof(mach.cpu));
    memset(&mach.dsp, 0, sizeof(mach.dsp));
    mach.dsp.dcount = -1;
    if (mach.ram) memset(mach.ram, 0, RR_RAM_SIZE);
    if (mach.bank) memset(mach.bank, 0, RR_BANK_SIZE);
    if (mach.colorram) memset(mach.colorram, 0, RR_CR_SIZE);
    if (cage.ram) memset(cage.ram, 0, RR_DSP_RAM_WORDS * 4);
    if (cage.iram) memset(cage.iram, 0, 0x800u * 4);
    eeprom_reset();
    adjust_powered_on();    // the operator settings the game reads now
    xga_reset();
    cage_reset();
    gt_reset();
    if (!mach.frame_ind)
        mach.frame_ind = (uint16_t *)calloc(
                (size_t)GT_WIDTH * GT_HEIGHT, 2);
    if (!mach.frame_rgb)
        mach.frame_rgb = (uint32_t *)calloc(
                (size_t)GT_WIDTH * GT_HEIGHT, 4);
    mach.irq3 = mach.irq4 = mach.irq6 = 0;
    mach.coin_counters = 0;
    mach.led_state = 0;
    mach.color_latch = 0;
    mach.mo_control = 0;
    mach.scanline = 0;
    mach.vblank = 0;
    mach.scan_cyc = 0;
    mach.vblank_cyc = 0;
    mach.carry68k = 0;
    mach.carrydsp = 0;
    mach.irq6_cyc = M68K_HZ / 250;
    mach.playfield_tile_bank = 0;
    mach.frames = 0;
    // Wire the shared view, then map pages and reset both CPUs.
    rrm->abi = REC_ABI_VERSION;
    rrm->sizeof_m68k = (uint32_t)sizeof(m68k_t);
    rrm->sizeof_c31 = (uint32_t)sizeof(c31_t);
    rrm->cpu = &mach.cpu;
    rrm->dsp = &mach.dsp;
    rrm->dsp_speedup = mach_profile ? mach_profile->dsp_speedup : 0xffffffffu;
    rrm->dsp_gen++;    // DSP RAM is about to be reloaded
    mem_map_pages();
    cage_map_pages();
    mem_update_irq();
    m68k_reset();
    // The DSP starts held in reset until the 68K releases it, as on the
    // board (latch /XRESET plus the CAGE control register).
    cage_control_w(0);
}

// Exact per-frame cycle budgets with error carry (MAME-attotime style).
static void frame_budgets(uint32_t *k, uint32_t *d) {
    uint64_t n;
    mach.carry68k += (uint64_t)M68K_HZ * (uint64_t)GT_FPS_DEN;
    n = mach.carry68k / GT_FPS_NUM;
    mach.carry68k %= GT_FPS_NUM;
    *k = (uint32_t)n;
    mach.carrydsp += (uint64_t)(CAGE_DSP_HZ / 2) * (uint64_t)GT_FPS_DEN;
    n = mach.carrydsp / GT_FPS_NUM;
    mach.carrydsp %= GT_FPS_NUM;
    *d = (uint32_t)n;
}

static int log_lines;

void machine_frame(void) {
    uint32_t fk, fd, line;
    frame_budgets(&fk, &fd);
    {
        static int log_frame = -1;
        if (log_frame < 0) log_frame = getenv("RR_LOG_FRAME") ? atoi(getenv("RR_LOG_FRAME")) + 1 : 0;
        log_lines = log_frame >= 3;
        if (!log_frame) goto run;
    }
    if (getenv("RR_LOG_FRAME"))
        fprintf(stderr, "frame fk=%u fd=%u lines=%u\n", fk, fd,
                GT_LINES);
run:
    mach.frames++;
    for (line = 0; line < GT_LINES; line++) {
        uint32_t k0 = fk * line / GT_LINES, k1 = fk * (line + 1) / GT_LINES;
        uint32_t d0 = fd * line / GT_LINES, d1 = fd * (line + 1) / GT_LINES;
        mach.scanline = (uint16_t)line;
        if (line == GT_VBLANK_LINE) {
            // MAME draws before vblank: render, then latch IRQ4.
            gt_render();
            mach.vblank = 1;
            gt_mo_vblank();
            mem_set_video(1);
        } else if (line == 0) {
            mach.vblank = 0;
        }
        if ((line & 7) == 0) gt_scanline((int)line);
        // 250 Hz periodic IRQ6 (re-asserts even without an ack).
        mach.irq6_cyc -= (int32_t)(k1 - k0);
        if (mach.irq6_cyc <= 0) {
            mach.irq6_cyc += M68K_HZ / 250;
            mem_set_scanline(1);
        }
        cage_scanline_swap();
        rrm->cpu->cycles = (int32_t)(k1 - k0);
        rec_run_main();
        if (log_lines)  // RR_LOG_FRAME=2: CPU position after every line
            fprintf(stderr, "line %u 68k total %llu pc %06X sr %04X irq %u "
                    "left %d\n", line, (unsigned long long)rrm->cpu->total,
                    rrm->cpu->pc & 0xffffffu, rrm->cpu->sr, rrm->cpu->irq,
                    rrm->cpu->cycles);
        if (!cage.dsp_held) {
            rrm->dsp->cycles = (int32_t)(d1 - d0);
            rec_run_dsp();
        }
        // Level-driven CAGE IRQ3, sampled every scanline.
        {
            int want = cage_irq_reason() ? 1 : 0;
            if (want != mach.irq3) {
                mach.irq3 = (uint8_t)want;
                mem_update_irq();
            }
        }
    }
}
