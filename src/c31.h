// TMS320C31 interpreter shell: reset, interrupts, slice runs.
// Instruction semantics live in c31_core.h, shared with translated code.
#pragma once
#include "rec_abi.h"

void c31_reset(void);
// Set an external interrupt line (0-11) level. Lines 0-3 are level
// sensitive; the rest latch into IF on assertion.
void c31_set_irq(int line, int state);

#define C31_RUN_DONE 0
#define C31_RUN_IDLE 1

int c31_run(void);
// Execute one instruction. Returns 1 if idling, else 0.
int c31_step(void);
