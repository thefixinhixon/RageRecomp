// Translated-code dispatchers for both CPUs. Each dispatcher runs its
// CPU's cycle budget, calling into translated chunks where the pc is a
// known label and falling back to single-step interpretation elsewhere
// (untranslated data gaps, RAM code, trace mode).
#pragma once
#include "rec_abi.h"

// Linked-in modules (empty unless built with `make recomp`).
extern const rec_module_t rec_linked_m68k;
extern const rec_module_t rec_linked_dsp;

void rec_init(void);
void rec_shutdown(void);
// Use a module's translated code (it must stay loaded).
int rec_use_m68k(const rec_module_t *mod);
int rec_use_dsp(const rec_module_t *mod);
// Run budgets (cpu->cycles / dsp->cycles).
void rec_run_main(void);
void rec_run_dsp(void);
// True when translated code is active for each CPU.
int rec_have_m68k(void);
int rec_have_dsp(void);
