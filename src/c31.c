// TMS320C31 interpreter: fetch, decode (c31_core.h), execute, repeats,
// delay slots, interrupts and idle.
#include "c31.h"
#include "c31_core.h"

void c31_reset(void) {
    c31_t *d = rrm->dsp;
    d->pc = rr_drd(0) & 0xffffffu;
    d->ie = d->iff = d->st = d->iof = 0;
    d->iff |= d->irq & 0x0fu;
    d->djump = 0;
    d->dcount = -1;
    d->rpts_active = 0;
    d->lopower = 0;
    d->idle = 0;
}

void c31_set_irq(int line, int state) {
    c31_t *d = rrm->dsp;
    uint32_t mask = 1u << (line & 31);
    if (state) {
        d->irq |= (uint16_t)mask;
        d->iff |= mask;
    } else {
        d->irq &= (uint16_t)~mask;
    }
    d->iff |= d->irq & 0x0fu;
}

int c31_step(void) {
    c31_t *d = rrm->dsp;
    d_insn_t in;
    uint32_t op;
    if (d->idle) {
        d_check_irqs(d);
        if (d->idle) return 1;
    }
    // Repeat blocks loop back here (not from inside delay slots).
    if (d->dcount < 0 && (d->st & C31_ST_RM) && d->pc == d->re + 1) {
        if ((int32_t)--d->rc >= 0) {
            d->pc = d->rs & 0xffffffu;
        } else {
            d->st &= ~C31_ST_RM;
            d->rpts_active = 0;
        }
    }
    op = rr_drd(d->pc);
    d->pc = (d->pc + 1) & 0xffffffu;
    d_decode(op, &in);
    d_exec(d, &in);
    d->cycles -= (1 + in.xcycles) * (d->lopower ? 16 : 1);
    d->total++;
    if (d->dcount > 0) {
        d->dcount--;
    } else if (d->dcount == 0) {
        // Third delay slot just ran: the pending jump applies now.
        d->pc = d->djump & 0xffffffu;
        d->dcount = -1;
    }
    d_check_irqs(d);
    return 0;
}

int c31_run(void) {
    c31_t *d = rrm->dsp;
    for (;;) {
        if (d->cycles <= 0) return C31_RUN_DONE;
        // Idling eats the rest of the budget (MAME's m_icount = 0), so
        // DMA/timers observe wall time even when nothing executes.
        if (c31_step()) { d->cycles = 0; return C31_RUN_IDLE; }
    }
}
