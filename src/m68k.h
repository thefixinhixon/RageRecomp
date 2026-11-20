// 68EC020 interpreter shell: reset, interrupts, slice runs.
// Instruction semantics live in m68k_core.h, shared with translated code.
#pragma once
#include "rec_abi.h"

// Reset the CPU (vectors from VBR, supervisor mode, mask 7).
void m68k_reset(void);
// Set the IPL lines (0-7).
void m68k_set_irq(uint32_t level);

// Run reasons.
#define M68K_RUN_DONE 0   // slice budget exhausted
#define M68K_RUN_STOP 1   // STOP state, nothing pending
#define M68K_RUN_HALT 2   // double fault

// Run until the cycle budget (cpu->cycles) runs out, the CPU stops, or it
// halts. Handles interrupts, exceptions and trace.
int m68k_run(void);

// Execute one instruction. Returns 0 normally, 1 if stopped, 2 if halted.
// Used by the translated-code dispatcher for untranslated regions.
int m68k_step(void);
// --trace: log every instruction the interpreter runs (translated code is
// not used while it is set).
extern int m68k_trace;

// Take a pending interrupt/exception outside the run loop (used by the
// translated-code dispatcher). Returns nonzero if one was taken.
int m68k_service(void);
