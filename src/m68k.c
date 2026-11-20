// 68EC020 interpreter: fetch, decode (m68k_core.h), execute, exceptions.
#include "m68k.h"
#include "m68k_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void m68k_reset(void) {
    m68k_t *c = rrm->cpu;
    uint32_t i;
    for (i = 0; i < 8; i++) c->d[i] = c->a[i] = 0;
    c->usp = c->isp = c->msp = 0;
    c->vbr = 0;
    c->sfc = c->dfc = 0;
    c->cacr = c->caar = 0;
    c->irq = 0;
    c->stop = 0;
    c->halted = 0;
    c->exc = 0;
    c->exc_pc = c->exc_ea = 0;
    c->exc_ssw = 0;
    c->sr = (uint16_t)(M68K_SR_S | M68K_SR_IMASK);
    c->a[7] = c->isp = rr_mrd32(0, 0);
    c->pc = rr_mrd32(4, 0) & 0xffffffu;
}

void m68k_set_irq(uint32_t level) {
    rrm->cpu->irq = (uint8_t)(level & 7);
}

int m68k_service(void) {
    m68k_t *c = rrm->cpu;
    if (c->halted) return 0;
    if (c->exc) {
        uint32_t fault = c->exc_pc;
        uint32_t vec = c->exc;
        if (getenv("RR_LOG_EXC"))
            fprintf(stderr, "exc vec=%u pc=%06X ea=%06X ssw=%04X sp=%06X\n",
                    vec, fault & 0xffffffu, c->exc_ea & 0xffffffu,
                    c->exc_ssw, c->a[7] & 0xffffffu);
        rr_take_exc(c, fault);
        if (c->exc) { c->halted = 1; return 0; } // fault during entry
        return 1;
    }
    {
        uint32_t mask = (c->sr & M68K_SR_IMASK) >> 8;
        if (c->irq > mask || c->irq == 7) {
            if (c->irq == 0) return 0;
            c->stop = 0;
            rr_enter_irq(c, c->irq, c->pc);
            if (c->exc) { c->halted = 1; return 0; }
            return 1;
        }
    }
    return 0;
}

// Execute one instruction (service + fetch + decode + run + trace).
// Returns 0 normally, 1 if stopped, 2 if halted.
int m68k_step(void) {
    m68k_t *c = rrm->cpu;
    uint16_t words[12];
    m_insn_t in;
    uint32_t pc, npc, nw;
    int len;
    if (c->halted) return 2;
    if (c->stop) {
        uint32_t mask = (c->sr & M68K_SR_IMASK) >> 8;
        if (c->irq > mask || c->irq == 7) {
            c->stop = 0;
        } else {
            return 1;
        }
    }
    if (m68k_service()) {
        c->cycles -= 20;
        c->total++;
        return 0;
    }
    pc = c->pc & 0xffffffu;
    if (pc & 1) { // odd fetch address
        c->exc = 3; c->exc_pc = pc; c->exc_ea = pc;
        c->exc_ssw = (uint16_t)(0x0100 |
                ((c->sr & M68K_SR_S) ? 6 : 2) | (1 << 6) | (2 << 4));
        return 0;
    }
    // Fetch exactly what decode consumes, one word at a time.
    nw = 0;
    for (;;) {
        words[nw] = (uint16_t)rr_fetch16(pc + nw * 2);
        nw++;
        len = m68k_decode(pc, words, (int)nw, &in);
        if (len != 0 || nw >= 11) break;
    }
    if (len <= 0) { // undecodable: treat as illegal
        in.op = MOP_ILL; in.imm = 4; in.len = 1;
        len = 1;
    }
    npc = (pc + (uint32_t)len * 2) & 0xffffffu;
    c->pc = npc;
    {
        // RR_WATCH_PC=addr: log registers executing there (debug aid).
        static int wpinit = 0;
        static uint32_t watchpc = 0xffffffffu;
        if (!wpinit) {
            const char *w = getenv("RR_WATCH_PC");
            wpinit = 1;
            if (w && w[0]) watchpc = (uint32_t)strtoul(w, NULL, 0) & 0xffffffu;
        }
        if (pc == watchpc) {
            fprintf(stderr, "watch %06X: d0=%08X d1=%08X d2=%08X a0=%08X "
                    "a1=%08X a2=%08X a3=%08X a4=%08X a5=%08X a6=%08X "
                    "sp=%06X tos=%08X sr=%04X\n", pc,
                    c->d[0], c->d[1], c->d[2], c->a[0], c->a[1], c->a[2],
                    c->a[3], c->a[4], c->a[5], c->a[6],
                    c->a[7] & 0xffffffu, rr_mrd32(c->a[7] & 0xffffffu, pc), c->sr);
        }
    }
    rr_exec(c, &in, pc);
#ifdef RR_CORE_WATCH
    {
        // RR_TRACE_CALLS=1: log every call/jump target (debug aid).
        static int tcinit = 0, tracing = 0;
        if (!tcinit) {
            const char *t = getenv("RR_TRACE_CALLS");
            tcinit = 1;
            tracing = (t && t[0] && strcmp(t, "0") != 0);
        }
        if (tracing && !c->exc &&
                (in.flow == MFLW_CALL || in.op == MOP_JMP ||
                 in.flow == MFLW_RET || in.flow == MFLW_BRANCH ||
                 in.flow == MFLW_DBCC || in.flow == MFLW_JUMP))
            fprintf(stderr, "call %06X -> %06X op=%d flow=%d\n", pc,
                    c->pc & 0xffffffu, in.op, in.flow);
    }
#endif
    c->cycles -= in.cycles;
    c->total++;
    if (c->exc) return 0; // taken next step
    // Trace exceptions (020: T1 every insn, T0 on control flow).
    if (c->sr & M68K_SR_T) {
        c->exc = 9; c->exc_pc = pc;
    } else if ((c->sr & M68K_SR_T0) &&
            (in.flow == MFLW_JUMP || in.flow == MFLW_CALL ||
             in.flow == MFLW_RET || in.flow == MFLW_TRAP ||
             in.flow == MFLW_EXIT ||
             ((in.flow == MFLW_BRANCH || in.flow == MFLW_DBCC) &&
              c->pc != npc))) {
        c->exc = 9; c->exc_pc = pc;
    }
    return 0;
}

int m68k_run(void) {
    m68k_t *c = rrm->cpu;
    for (;;) {
        int st;
        if (c->cycles <= 0) return M68K_RUN_DONE;
        st = m68k_step();
        if (st == 1) return M68K_RUN_STOP;
        if (st == 2) return M68K_RUN_HALT;
    }
}
