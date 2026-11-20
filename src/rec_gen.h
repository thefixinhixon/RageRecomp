// Helpers for translated 68K code (compiled into modules with -nostdlib).
// Instruction semantics stay in m68k_core.h; this file only holds the
// slice/block plumbing the translator emits calls to.
#pragma once
#include "rec_abi.h"
#include "m68k_core.h"

// Leave translated code when the slice is over, an exception or stop is
// pending, or an interrupt line is above the mask (level 7 always exits).
static inline int rr_mpoll(void) {
    m68k_t *c = rrm->cpu;
    uint32_t mask = (c->sr & M68K_SR_IMASK) >> 8;
    return c->cycles <= 0 || c->exc || c->stop || c->irq > mask ||
            c->irq == 7;
}

// Block epilogue: record the fallthrough pc, charge the block's cycles
// and instruction count, and leave when the slice or machine needs it.
static inline rr_rc_t rr_blk_end(uint32_t fallthrough_pc, int cycles,
        uint32_t ninstrs) {
    m68k_t *c = rrm->cpu;
    c->pc = fallthrough_pc;
    c->cycles -= cycles;
    c->total += ninstrs;
    if (rr_mpoll()) return RR_EXIT;
    return RR_CONT;
}

// Block exit to another chunk (or untranslated code): the dispatcher takes
// it from c->pc and checks the slice there.
static inline rr_rc_t rr_blk_jump(uint32_t target_pc, int cycles,
        uint32_t ninstrs) {
    m68k_t *c = rrm->cpu;
    c->pc = target_pc;
    c->cycles -= cycles;
    c->total += ninstrs;
    return RR_JUMP;
}
