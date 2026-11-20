// TMS320C31 instruction semantics shared by the interpreter, the
// translator and the translated code itself (UltraCombo's adsp_core.h
// pattern). Float behaviour follows MAME's integer ALU model (see NOTICE).
//
// Everything here is static and libc-free, so translated modules compile it
// with -nostdlib. The program's globals rrm/rrfns provide memory and the
// slow device paths. Keep instruction semantics in this file only.
#pragma once
#include "rec_abi.h"

// ------------------------------------------------------------- decode types

typedef enum {
    DOP_ILL = 0,
    DOP_ABSF, DOP_ABSI, DOP_ADDC, DOP_ADDF, DOP_ADDI, DOP_AND, DOP_ANDN,
    DOP_ASH, DOP_CMPF, DOP_CMPI, DOP_FIX, DOP_FLOAT, DOP_IDLE, DOP_LDE,
    DOP_LDF, DOP_LDFI, DOP_LDI, DOP_LDII, DOP_LDM, DOP_LSH, DOP_MPYF,
    DOP_MPYI, DOP_NEGB, DOP_NEGF, DOP_NEGI, DOP_NOP, DOP_NORM, DOP_NOT,
    DOP_POP, DOP_POPF, DOP_PUSH, DOP_PUSHF, DOP_OR, DOP_SPEED,
    DOP_RND, DOP_ROL, DOP_ROLC, DOP_ROR, DOP_RORC, DOP_RPTS,
    DOP_STF, DOP_STFI, DOP_STI, DOP_STII, DOP_SIGI, DOP_SUBB, DOP_SUBC,
    DOP_SUBF, DOP_SUBI, DOP_SUBRB, DOP_SUBRF, DOP_SUBRI, DOP_TSTB,
    DOP_XOR, DOP_IACK,
    DOP_ADDC3, DOP_ADDF3, DOP_ADDI3, DOP_AND3, DOP_ANDN3, DOP_ASH3,
    DOP_CMPF3, DOP_CMPI3, DOP_LSH3, DOP_MPYF3, DOP_MPYI3, DOP_OR3,
    DOP_SUBB3, DOP_SUBF3, DOP_SUBI3, DOP_TSTB3, DOP_XOR3,
    DOP_LDFC, DOP_LDIC,
    DOP_BR, DOP_BRD, DOP_CALL, DOP_RPTB, DOP_SWI,
    DOP_BCOND, DOP_BDCOND, DOP_DBCOND, DOP_DBDCOND,
    DOP_CALLC, DOP_TRAPC, DOP_RETI, DOP_RETS,
    DOP_P_MPYADD, DOP_P_MPYSUB, DOP_P_STST, DOP_P_LDLD, DOP_P_ALUST
} d_op_t;

#define DFLW_NONE 0
#define DFLW_BRANCH 1   // conditional: falls through too
#define DFLW_JUMP 2     // unconditional direct branch
#define DFLW_CALL 3     // call (returns)
#define DFLW_RET 4      // conditional or not; cond forms fall through
#define DFLW_TRAP 5     // SWI / TRAPC: falls through after return

typedef struct {
    uint16_t op;    // d_op_t
    uint8_t cc;     // condition code (cond ops)
    uint8_t g;      // G/T form, or parallel subform
    uint8_t dreg, sreg, sreg2, dreg2;
    uint8_t ma1, ma2;   // indirect mode bytes (mode<<3|ar)
    uint8_t disp;   // u8 displacement (general-indirect)
    uint32_t addr;  // direct/abs address, imm16, or pc-rel displacement
    uint8_t flow;   // DFLW_*
    uint8_t xcycles;// extra cycles beyond 1
} d_insn_t;

// General-op mnemonics by 9-bit base (op>>23), 0x00-0x36.
static const uint8_t d_gen_tab[0x37] = {
    DOP_ABSF, DOP_ABSI, DOP_ADDC, DOP_ADDF, DOP_ADDI, DOP_AND, DOP_ANDN,
    DOP_ASH, DOP_CMPF, DOP_CMPI, DOP_FIX, DOP_FLOAT, DOP_IDLE, DOP_LDE,
    DOP_LDF, DOP_LDFI, DOP_LDI, DOP_LDII, DOP_LDM, DOP_LSH, DOP_MPYF,
    DOP_MPYI, DOP_NEGB, DOP_NEGF, DOP_NEGI, DOP_NOP, DOP_NORM, DOP_NOT,
    DOP_POP, DOP_POPF, DOP_PUSH, DOP_PUSHF, DOP_OR, DOP_SPEED,
    DOP_RND, DOP_ROL, DOP_ROLC, DOP_ROR, DOP_RORC, DOP_RPTS,
    DOP_STF, DOP_STFI, DOP_STI, DOP_STII, DOP_SIGI, DOP_SUBB, DOP_SUBC,
    DOP_SUBF, DOP_SUBI, DOP_SUBRB, DOP_SUBRF, DOP_SUBRI, DOP_TSTB,
    DOP_XOR, DOP_IACK
};

// 3-operand mnemonics by 9-bit base 0x40-0x50.
static const uint8_t d_3op_tab[0x11] = {
    DOP_ADDC3, DOP_ADDF3, DOP_ADDI3, DOP_AND3, DOP_ANDN3, DOP_ASH3,
    DOP_CMPF3, DOP_CMPI3, DOP_LSH3, DOP_MPYF3, DOP_MPYI3, DOP_OR3,
    DOP_SUBB3, DOP_SUBF3, DOP_SUBI3, DOP_TSTB3, DOP_XOR3
};

// Parallel 3op||store ALU ops by pair index 0-19.
static const uint8_t d_par_tab[20] = {
    DOP_ABSF, DOP_ABSI, DOP_ADDF3, DOP_ADDI3, DOP_AND3, DOP_ASH3,
    DOP_FIX, DOP_FLOAT, DOP_LDF, DOP_LDI, DOP_LSH3, DOP_MPYF3,
    DOP_MPYI3, DOP_NEGF, DOP_NEGI, DOP_NOT, DOP_OR3, DOP_SUBF3,
    DOP_SUBI3, DOP_XOR3
};

static inline int d_cond_valid(uint32_t cc) {
    return cc <= 10 || (cc >= 12 && cc <= 20);
}

// Decode one 32-bit instruction word. Always returns 1; unassigned
// encodings decode as DOP_ILL (a silent NOP, as in MAME).
static inline int d_decode(uint32_t op, d_insn_t *o) {
    uint32_t idx = (op >> 21) & 0x7ff;
    o->op = DOP_ILL; o->cc = 0; o->g = 0;
    o->dreg = o->sreg = o->sreg2 = o->dreg2 = 0;
    o->ma1 = o->ma2 = 0; o->disp = 0; o->addr = 0;
    o->flow = DFLW_NONE; o->xcycles = 0;
    if (idx < 0xdc) { // general ops
        uint32_t base = idx >> 2, g = idx & 3;
        o->op = d_gen_tab[base];
        o->g = (uint8_t)g;
        o->dreg = (uint8_t)((op >> 16) & 31);
        switch (o->op) {
        case DOP_ROL: case DOP_ROLC: case DOP_ROR: case DOP_RORC:
            if (g != 3) { o->op = DOP_ILL; return 1; }
            return 1;
        case DOP_POP: case DOP_POPF: case DOP_PUSH: case DOP_PUSHF:
            if (g != 1) { o->op = DOP_ILL; return 1; }
            return 1;
        case DOP_NOP:
            if (g != 0 && g != 2) { o->op = DOP_ILL; return 1; }
            if (g == 0) return 1;
            break; // g == 2: extract ma1/disp below (side effects matter)
        case DOP_STF: case DOP_STFI: case DOP_STI: case DOP_STII:
        case DOP_LDFI: case DOP_LDII: case DOP_IACK:
            if (g != 1 && g != 2) { o->op = DOP_ILL; return 1; }
            break;
        case DOP_SIGI:
            if (g != 0) { o->op = DOP_ILL; return 1; }
            return 1;
        case DOP_SPEED:
            o->addr = op & 1; // 1 = LOPOWER
            return 1;
        case DOP_RPTS:
            o->xcycles = 3;
            break;
        default:
            break;
        }
        if (g == 0) {
            o->sreg = (uint8_t)(op & 31);
        } else if (g == 1) {
            o->addr = op & 0xffffu; // + DP<<16 at execution
        } else if (g == 2) {
            o->ma1 = (uint8_t)((op >> 8) & 0xff);
            o->disp = (uint8_t)(op & 0xff);
        } else {
            o->addr = op & 0xffffu; // imm16
        }
        return 1;
    }
    if (idx >= 0x100 && idx < 0x144) { // 3-operand ops
        uint32_t t = idx & 3;
        o->op = d_3op_tab[(idx >> 2) - 0x40];
        o->g = (uint8_t)t;
        o->dreg = (uint8_t)((op >> 16) & 31);
        if (t & 1) o->ma1 = (uint8_t)((op >> 8) & 0xff);
        else o->sreg = (uint8_t)((op >> 8) & 31);
        if (t & 2) o->ma2 = (uint8_t)(op & 0xff);
        else o->sreg2 = (uint8_t)(op & 31);
        return 1;
    }
    if (idx >= 0x200 && idx < 0x280) { // LDFcond
        uint32_t cc = (idx >> 2) & 31, g = idx & 3;
        if (!d_cond_valid(cc)) return 1;
        o->op = DOP_LDFC; o->cc = (uint8_t)cc; o->g = (uint8_t)g;
        o->dreg = (uint8_t)((op >> 16) & 7);
        if (g == 0) o->sreg = (uint8_t)(op & 7);
        else if (g == 1) o->addr = op & 0xffffu;
        else if (g == 2) {
            o->ma1 = (uint8_t)((op >> 8) & 0xff);
            o->disp = (uint8_t)(op & 0xff);
        } else o->addr = op & 0xffffu;
        return 1;
    }
    if (idx >= 0x280 && idx < 0x300) { // LDIcond
        uint32_t cc = (idx >> 2) & 31, g = idx & 3;
        if (!d_cond_valid(cc)) return 1;
        o->op = DOP_LDIC; o->cc = (uint8_t)cc; o->g = (uint8_t)g;
        o->dreg = (uint8_t)((op >> 16) & 31);
        if (g == 0) o->sreg = (uint8_t)(op & 31);
        else if (g == 1) o->addr = op & 0xffffu;
        else if (g == 2) {
            o->ma1 = (uint8_t)((op >> 8) & 0xff);
            o->disp = (uint8_t)(op & 0xff);
        } else o->addr = op & 0xffffu;
        return 1;
    }
    if (idx >= 0x300 && idx < 0x308) { // BR
        o->op = DOP_BR; o->addr = op & 0xffffffu;
        o->flow = DFLW_JUMP; o->xcycles = 3;
        return 1;
    }
    if (idx >= 0x308 && idx < 0x310) { // BRD
        o->op = DOP_BRD; o->addr = op & 0xffffffu;
        o->flow = DFLW_JUMP;
        return 1;
    }
    if (idx >= 0x310 && idx < 0x318) { // CALL
        o->op = DOP_CALL; o->addr = op & 0xffffffu;
        o->flow = DFLW_CALL; o->xcycles = 3;
        return 1;
    }
    if (idx >= 0x320 && idx < 0x328) { // RPTB
        o->op = DOP_RPTB; o->addr = op & 0xffffffu;
        o->xcycles = 3;
        return 1;
    }
    if (idx == 0x330) { // SWI
        o->op = DOP_SWI; o->flow = DFLW_TRAP;
        return 1;
    }
    if (idx == 0x340 || idx == 0x341) { // Bcond / BDcond reg
        o->op = (idx & 1) ? DOP_BDCOND : DOP_BCOND;
        o->cc = (uint8_t)((op >> 16) & 31);
        o->sreg = (uint8_t)(op & 31);
        o->flow = (o->cc == 0 && !(idx & 1)) ? DFLW_JUMP : DFLW_BRANCH;
        o->xcycles = (idx & 1) ? 0 : 3;
        return 1;
    }
    if (idx == 0x350 || idx == 0x351) { // Bcond / BDcond disp
        o->op = (idx & 1) ? DOP_BDCOND : DOP_BCOND;
        o->cc = (uint8_t)((op >> 16) & 31);
        o->addr = (uint32_t)(int32_t)(int16_t)(op & 0xffffu);
        o->g = 1; // immediate form
        o->flow = (o->cc == 0 && !(idx & 1)) ? DFLW_JUMP : DFLW_BRANCH;
        o->xcycles = (idx & 1) ? 0 : 3;
        return 1;
    }
    if (idx >= 0x360 && idx < 0x370) { // DBcond reg
        o->op = (idx & 1) ? DOP_DBDCOND : DOP_DBCOND;
        o->cc = (uint8_t)((op >> 16) & 31);
        o->dreg = (uint8_t)((idx >> 1) & 7); // ARn
        o->sreg = (uint8_t)(op & 31);
        o->flow = DFLW_BRANCH;
        o->xcycles = (idx & 1) ? 0 : 3;
        return 1;
    }
    if (idx >= 0x370 && idx < 0x380) { // DBcond disp
        o->op = (idx & 1) ? DOP_DBDCOND : DOP_DBCOND;
        o->cc = (uint8_t)((op >> 16) & 31);
        o->dreg = (uint8_t)((idx >> 1) & 7); // ARn
        o->addr = (uint32_t)(int32_t)(int16_t)(op & 0xffffu);
        o->g = 1;
        o->flow = DFLW_BRANCH;
        o->xcycles = (idx & 1) ? 0 : 3;
        return 1;
    }
    if (idx == 0x380) { // CALLC reg
        o->op = DOP_CALLC; o->cc = (uint8_t)((op >> 16) & 31);
        o->sreg = (uint8_t)(op & 31);
        o->flow = DFLW_CALL; o->xcycles = 3;
        return 1;
    }
    if (idx == 0x390) { // CALLC disp
        o->op = DOP_CALLC; o->cc = (uint8_t)((op >> 16) & 31);
        o->addr = (uint32_t)(int32_t)(int16_t)(op & 0xffffu);
        o->g = 1;
        o->flow = DFLW_CALL; o->xcycles = 3;
        return 1;
    }
    if (idx == 0x3a0) { // TRAPC
        o->op = DOP_TRAPC; o->cc = (uint8_t)((op >> 16) & 31);
        o->addr = op & 0x3fu;
        o->flow = DFLW_TRAP; o->xcycles = 4;
        return 1;
    }
    if (idx == 0x3c0) { // RETIC
        o->op = DOP_RETI; o->cc = (uint8_t)((op >> 16) & 31);
        o->flow = DFLW_RET; o->xcycles = 3;
        return 1;
    }
    if (idx == 0x3c4) { // RETSC
        o->op = DOP_RETS; o->cc = (uint8_t)((op >> 16) & 31);
        o->flow = DFLW_RET; o->xcycles = 3;
        return 1;
    }
    if (idx >= 0x400 && idx < 0x480) { // parallel 3op||3op
        uint32_t pair = (idx >> 5) & 3;
        o->op = (pair == 0 || pair == 2) ? DOP_P_MPYADD : DOP_P_MPYSUB;
        o->g = (uint8_t)(((idx >> 3) & 3) | ((pair & 2) ? 4 : 0));
        o->dreg = (uint8_t)((op >> 23) & 1); // R0/R1 (mpy dest)
        o->dreg2 = (uint8_t)(2 | ((op >> 22) & 1)); // R2/R3 (add dest)
        o->sreg = (uint8_t)((op >> 19) & 7);
        o->sreg2 = (uint8_t)((op >> 16) & 7);
        o->ma1 = (uint8_t)((op >> 8) & 0xff);
        o->ma2 = (uint8_t)(op & 0xff);
        return 1;
    }
    if (idx >= 0x600 && idx < 0x620) { // ST||ST
        o->op = DOP_P_STST; o->g = (idx < 0x610) ? 0 : 1; // 0 float, 1 int
        o->sreg = (uint8_t)((op >> 16) & 7);
        o->sreg2 = (uint8_t)((op >> 22) & 7);
        o->ma1 = (uint8_t)((op >> 8) & 0xff);
        o->ma2 = (uint8_t)(op & 0xff);
        return 1;
    }
    if (idx >= 0x620 && idx < 0x640) { // LD||LD
        o->op = DOP_P_LDLD; o->g = (idx < 0x630) ? 0 : 1;
        o->dreg = (uint8_t)((op >> 19) & 7);
        o->dreg2 = (uint8_t)((op >> 22) & 7);
        o->ma1 = (uint8_t)((op >> 8) & 0xff);
        o->ma2 = (uint8_t)(op & 0xff);
        return 1;
    }
    if (idx >= 0x640 && idx < 0x780) { // ALU||ST
        o->op = DOP_P_ALUST;
        o->g = (uint8_t)(((idx >> 4) & 31) - 4); // pair 0-19
        o->dreg = (uint8_t)((op >> 22) & 7); // ALU dest
        o->sreg = (uint8_t)((op >> 19) & 7); // ALU src1 / unused
        o->sreg2 = (uint8_t)((op >> 16) & 7); // store src
        o->ma1 = (uint8_t)((op >> 8) & 0xff); // store addr (immediate)
        o->ma2 = (uint8_t)(op & 0xff); // ALU mem src (deferred)
        return 1;
    }
    return 1;
}

// ------------------------------------------------------------- memory

static inline c31_t *rr_dsp(void) { return rrm->dsp; }

static inline uint32_t rr_drd(uint32_t a) {
    a &= 0xffffffu;
    {
        uint32_t *p = rrm->drd[a >> 16];
        if (p) return p[a & 0xffffu];
    }
    return rrfns->drd(a);
}

// The translated address space: lowmem RAM 0-0xFFFF, then IRAM
// 0x809800-0x809FFF, as one index range (the code guard's layout).
#define DREC_WORDS 0x10800u

static inline int drec_idx(uint32_t a) {
    a &= 0xffffffu;
    if (a < 0x10000u) return (int)a;
    if (a >= 0x809800u && a < 0x80a000u) return (int)(0x10000u + a - 0x809800u);
    return -1;
}

static inline uint32_t drec_addr(uint32_t i) {
    return i < 0x10000u ? i : 0x809800u + (i - 0x10000u);
}

// Every DSP write: the CAGE speedup (MAME's write-hooked idle loop) and
// the translated-code guard (a changed code word bumps dsp_gen).
static inline void rr_dwr(uint32_t a, uint32_t v) {
    uint32_t *p;
    int i;
    a &= 0xffffffu;
    if (a == rrm->dsp_speedup) rrm->dsp->cycles -= 100;
    p = rrm->dwr[a >> 16];
    if (rrm->dsp_mask && (i = drec_idx(a)) >= 0 && rrm->dsp_mask[i] &&
            (p ? p[a & 0xffffu] : rrfns->drd(a)) != v)
        rrm->dsp_gen++;
    if (p) {
        p[a & 0xffffu] = v;
        return;
    }
    rrfns->dwr(a, v);
}

// ------------------------------------------------------------- registers

// Integer view of a register index (R0-R7 read their mantissa).
static inline uint32_t d_ireg(c31_t *d, uint32_t r) {
    r &= 31;
    if (r < 8) return (uint32_t)d->r[r].man;
    if (r < 16) return d->ar[r - 8];
    switch (r) {
    case 16: return d->dp;
    case 17: return d->ir0;
    case 18: return d->ir1;
    case 19: return d->bk;
    case 20: return d->sp;
    case 21: return d->st;
    case 22: return d->ie;
    case 23: return d->iff;
    case 24: return d->iof;
    case 25: return d->rs;
    case 26: return d->re;
    case 27: return d->rc;
    default: return d->xreg[r - 28];
    }
}

// Write through update_special side effects (BK mask; ST/IE/IF flag users
// re-evaluate interrupts at the next boundary, as in MAME).
static inline void d_special(c31_t *d, uint32_t r) {
    if (r != 19) return; // BK: rebuild the circular mask
    {
        uint32_t t = d->bk, m = t;
        while (t >>= 1) m |= t;
        d->bkmask = m;
    }
}

static inline void d_setreg(c31_t *d, uint32_t r, uint32_t v) {
    r &= 31;
    if (r < 8) {
        d->r[r].man = (int32_t)v; // exponent untouched (MAME)
        return;
    }
    if (r < 16) {
        d->ar[r - 8] = v;
        return;
    }
    switch (r) {
    case 16: d->dp = v; break;
    case 17: d->ir0 = v; break;
    case 18: d->ir1 = v; break;
    case 19: d->bk = v; d_special(d, r); break;
    case 20: d->sp = v; break;
    case 21: d->st = v & 0x3fffu; break;
    case 22: d->ie = v & 0xfffu; break;
    case 23: d->iff = v & 0xfffu; break;
    case 24: d->iof = v; break; // XF pins unconnected on CAGE
    case 25: d->rs = v & 0xffffffu; break;
    case 26: d->re = v & 0xffffffu; break;
    case 27: d->rc = v; break;
    default: d->xreg[r - 28] = v; break;
    }
}

// ------------------------------------------------------------- float core
//
// Internal format: value = mantissa * 2^(exponent - 31); the zero
// exponent is -128 (mantissa 0 or 0x80000000). Ported from MAME's
// integer ALU (see NOTICE); USE_FP is off there too.

static inline uint32_t d_fp2long(const c31reg_t *r) {
    return ((uint32_t)(r->exp & 0xff) << 24) |
            (((uint32_t)r->man >> 8) & 0xffffffu);
}

static inline void d_long2fp(c31reg_t *r, uint32_t v) {
    r->man = (int32_t)(v << 8);
    r->exp = (int32_t)v >> 24;
}

static inline void d_short2fp(c31reg_t *r, uint32_t v) {
    v &= 0xffffu;
    if (v == 0x8000) {
        r->man = 0;
        r->exp = -128;
    } else {
        r->man = (int32_t)(v << 20);
        r->exp = (int32_t)(int16_t)v >> 12;
    }
}

static inline void d_clr_nzvuf(c31_t *d) {
    d->st &= ~(C31_ST_N | C31_ST_Z | C31_ST_V | C31_ST_UF);
}
static inline void d_clr_nzcvuf(c31_t *d) {
    d->st &= ~(C31_ST_N | C31_ST_Z | C31_ST_C | C31_ST_V | C31_ST_UF);
}
static inline void d_or_nz(c31_t *d, uint32_t v) {
    if (v & 0x80000000u) d->st |= C31_ST_N;
    if (!v) d->st |= C31_ST_Z;
}
static inline void d_or_nzf(c31_t *d, const c31reg_t *r) {
    if ((uint32_t)r->man & 0x80000000u) d->st |= C31_ST_N;
    if (r->exp == -128) d->st |= C31_ST_Z;
}
static inline void d_or_nuf(c31_t *d, const c31reg_t *r) {
    if ((uint32_t)r->man & 0x80000000u) d->st |= C31_ST_N;
    if (r->exp == -128) d->st |= C31_ST_UF | C31_ST_LUF;
}
static inline void d_or_v_add(c31_t *d, uint32_t a, uint32_t b,
        uint32_t r) {
    uint32_t t = ((((a ^ r) & (b ^ r)) >> 30) & 1) ? C31_ST_V : 0;
    d->st |= t | (t ? C31_ST_LV : 0);
}
static inline void d_or_v_sub(c31_t *d, uint32_t a, uint32_t b,
        uint32_t r) {
    uint32_t t = ((((a ^ b) & (a ^ r)) >> 30) & 1) ? C31_ST_V : 0;
    d->st |= t | (t ? C31_ST_LV : 0);
}

static inline int d_clz32(uint32_t v) {
    int n = 0;
    if (!v) return 32;
    while (!(v & 0x80000000u)) {
        n++;
        v <<= 1;
    }
    return n;
}
static inline int d_clo32(uint32_t v) { return d_clz32(~v); }

static inline void d_int2float(c31_t *d, c31reg_t *r) {
    uint32_t man = (uint32_t)r->man;
    int exp, cnt;
    d->st &= ~(C31_ST_N | C31_ST_Z | C31_ST_V | C31_ST_UF);
    if (!man) {
        man = 0x80000000u;
        exp = -128;
    } else if (man == 0xffffffffu) {
        man = 0;
        exp = -1;
    } else if ((int32_t)man > 0) {
        cnt = d_clz32(man);
        man <<= cnt;
        exp = 31 - cnt;
    } else {
        cnt = d_clo32(man);
        man <<= cnt;
        exp = 31 - cnt;
    }
    r->man = (int32_t)(man ^ 0x80000000u);
    r->exp = exp;
    d_or_nzf(d, r);
}

static inline void d_float2int(c31_t *d, c31reg_t *r, int setflags) {
    int32_t man = r->man;
    int shift = 31 - r->exp;
    if (setflags)
        d->st &= ~(C31_ST_N | C31_ST_Z | C31_ST_V | C31_ST_UF);
    if (shift <= 0) {
        r->man = (man >= 0) ? 0x7fffffff : (int32_t)0x80000000u;
        if (setflags) d->st |= C31_ST_V | C31_ST_LV;
    } else if (shift > 31) {
        r->man = man >> 31;
    } else {
        r->man = (man >> shift) ^ (int32_t)(1u << (31 - shift));
    }
    if (setflags) d_or_nz(d, (uint32_t)r->man);
}

static inline void d_negf(c31_t *d, c31reg_t *dst, const c31reg_t *src) {
    int32_t man = src->man;
    d->st &= ~(C31_ST_N | C31_ST_Z | C31_ST_V | C31_ST_UF);
    if (src->exp == -128) {
        dst->man = 0;
        dst->exp = -128;
    } else if ((man & 0x7fffffff) != 0) {
        dst->man = -man;
        dst->exp = src->exp;
    } else {
        dst->man = man ^ (int32_t)0x80000000u;
        dst->exp = (man == 0) ? src->exp - 1 : src->exp + 1;
    }
    d_or_nzf(d, dst);
}

static inline void d_addf(c31_t *d, c31reg_t *dst, const c31reg_t *s1,
        const c31reg_t *s2) {
    int64_t man, m1, m2;
    int exp, cnt;
    d->st &= ~(C31_ST_N | C31_ST_Z | C31_ST_V | C31_ST_UF);
    if (s1->exp == -128) {
        *dst = *s2;
        d_or_nzf(d, dst);
        return;
    }
    if (s2->exp == -128) {
        *dst = *s1;
        d_or_nzf(d, dst);
        return;
    }
    m1 = (int64_t)s1->man ^ (int64_t)0x80000000u;
    m2 = (int64_t)s2->man ^ (int64_t)0x80000000u;
    if (s1->exp > s2->exp) {
        exp = s1->exp;
        cnt = exp - s2->exp;
        if (cnt >= 32) {
            *dst = *s1;
            d_or_nzf(d, dst);
            return;
        }
        m2 >>= cnt;
    } else {
        exp = s2->exp;
        cnt = exp - s1->exp;
        if (cnt >= 32) {
            *dst = *s2;
            d_or_nzf(d, dst);
            return;
        }
        m1 >>= cnt;
    }
    man = m1 + m2;
    if (man == 0 || exp == -128) {
        exp = -128;
        man = (int64_t)0x80000000u;
    } else if (man >= (int64_t)0x100000000ull ||
            man < (int64_t)0xffffffff00000000ull) {
        man >>= 1;
        exp++;
    } else if (man < (int64_t)0x80000000ull &&
            man >= (int64_t)0xffffffff80000000ull) {
        if (man > 0) {
            cnt = d_clz32((uint32_t)man);
            man <<= cnt;
            exp -= cnt;
        } else {
            cnt = d_clo32((uint32_t)man);
            man <<= cnt;
            exp -= cnt;
        }
    }
    if (exp <= -128) {
        man = (int64_t)0x80000000u;
        exp = -128;
        d->st |= C31_ST_UF | C31_ST_LUF;
    } else if (exp > 127) {
        man = (man < 0) ? 0 : (int64_t)0xffffffffu;
        exp = 127;
        d->st |= C31_ST_V | C31_ST_LV;
    }
    dst->man = (int32_t)((uint32_t)man ^ 0x80000000u);
    dst->exp = exp;
    d_or_nzf(d, dst);
}

static inline void d_subf(c31_t *d, c31reg_t *dst, const c31reg_t *s1,
        const c31reg_t *s2) {
    int64_t man, m1, m2;
    int exp, cnt;
    d->st &= ~(C31_ST_N | C31_ST_Z | C31_ST_V | C31_ST_UF);
    if (s2->exp == -128) {
        *dst = *s1;
        d_or_nzf(d, dst);
        return;
    }
    m1 = (int64_t)s1->man ^ (int64_t)0x80000000u;
    m2 = (int64_t)s2->man ^ (int64_t)0x80000000u;
    if (s1->exp > s2->exp) {
        exp = s1->exp;
        cnt = exp - s2->exp;
        if (cnt >= 32) {
            *dst = *s1;
            d_or_nzf(d, dst);
            return;
        }
        m2 >>= cnt;
    } else {
        exp = s2->exp;
        cnt = exp - s1->exp;
        if (cnt >= 32) {
            d_negf(d, dst, s2);
            return;
        }
        m1 >>= cnt;
    }
    man = m1 - m2;
    if (man == 0 || exp == -128) {
        exp = -128;
        man = (int64_t)0x80000000u;
    } else if (man >= (int64_t)0x100000000ull ||
            man < (int64_t)0xffffffff00000000ull) {
        man >>= 1;
        exp++;
    } else if (man < (int64_t)0x80000000ull &&
            man >= (int64_t)0xffffffff80000000ull) {
        if (man > 0) {
            cnt = d_clz32((uint32_t)man);
            man <<= cnt;
            exp -= cnt;
        } else {
            cnt = d_clo32((uint32_t)man);
            man <<= cnt;
            exp -= cnt;
        }
    }
    if (exp <= -128) {
        if (man != 0 || exp < -128)
            d->st |= C31_ST_UF | C31_ST_LUF;
        man = (int64_t)0x80000000u;
        exp = -128;
    } else if (exp > 127) {
        man = (man < 0) ? 0 : (int64_t)0xffffffffu;
        exp = 127;
        d->st |= C31_ST_V | C31_ST_LV;
    }
    dst->man = (int32_t)((uint32_t)man ^ 0x80000000u);
    dst->exp = exp;
    d_or_nzf(d, dst);
}

static inline void d_mpyf(c31_t *d, c31reg_t *dst, const c31reg_t *s1,
        const c31reg_t *s2) {
    int32_t m1, m2;
    int64_t man;
    int exp;
    d->st &= ~(C31_ST_N | C31_ST_Z | C31_ST_V | C31_ST_UF);
    if (s1->exp == -128 || s2->exp == -128) {
        dst->man = 0;
        dst->exp = -128;
        d_or_nzf(d, dst);
        return;
    }
    m1 = (s1->man >> 8) ^ 0x800000;
    m2 = (s2->man >> 8) ^ 0x800000;
    man = (int64_t)m1 * (int64_t)m2;
    exp = s1->exp + s2->exp;
    man >>= 46 - 31;
    if (man == 0) {
        exp = -128;
        man = (int64_t)0x80000000u;
    } else if (man >= (int64_t)0x100000000ull) {
        man >>= 1;
        exp++;
        if (man >= (int64_t)0x100000000ull) {
            man >>= 1;
            exp++;
        }
    } else if (man < (int64_t)0xffffffff00000000ull) {
        man >>= 1;
        exp++;
    }
    if (exp <= -128) {
        man = (int64_t)0x80000000u;
        exp = -128;
        d->st |= C31_ST_UF | C31_ST_LUF;
    } else if (exp > 127) {
        man = (man < 0) ? 0 : (int64_t)0xffffffffu;
        exp = 127;
        d->st |= C31_ST_V | C31_ST_LV;
    }
    dst->man = (int32_t)((uint32_t)man ^ 0x80000000u);
    dst->exp = exp;
    d_or_nzf(d, dst);
}

static inline void d_norm(c31_t *d, c31reg_t *dst, const c31reg_t *src) {
    int32_t man = src->man;
    int exp = src->exp, cnt;
    d->st &= ~(C31_ST_N | C31_ST_Z | C31_ST_V | C31_ST_UF);
    if (exp == -128 || man == 0) {
        man = 0;
        exp = -128;
        if (src->man != 0) d->st |= C31_ST_UF | C31_ST_LUF;
    } else {
        if (man > 0) {
            cnt = d_clz32((uint32_t)man);
            man = (int32_t)((uint32_t)man << cnt);
            exp -= cnt;
        } else {
            cnt = d_clo32((uint32_t)man);
            man = (int32_t)((uint32_t)man << cnt);
            exp -= cnt;
        }
        if (exp <= -128) {
            man = 0;
            exp = -128;
            d->st |= C31_ST_UF | C31_ST_LUF;
        }
    }
    dst->man = man;
    dst->exp = exp;
    d_or_nzf(d, dst);
}

// ------------------------------------------------------------- integer ALU

static inline int d_ovm(c31_t *d) { return (d->st & C31_ST_OVM) != 0; }

static inline void d_addc(c31_t *d, uint32_t dg, uint32_t s1, uint32_t s2) {
    uint32_t c = (d->st & C31_ST_C) ? 1 : 0;
    uint32_t r = s1 + s2 + c;
    int ov = ((int32_t)((s1 ^ r) & (s2 ^ r)) < 0);
    if (!d_ovm(d) || !ov) d_setreg(d, dg, r);
    else d_setreg(d, dg, ((int32_t)s1 < 0) ? 0x80000000u : 0x7fffffffu);
    if (dg < 8) {
        d_clr_nzcvuf(d);
        if ((uint64_t)s1 + s2 + c > 0xffffffffull) d->st |= C31_ST_C;
        d_or_v_add(d, s1, s2, r);
        d_or_nz(d, r);
    } else if (dg >= 19) d_special(d, dg);
}

static inline void d_addi(c31_t *d, uint32_t dg, uint32_t s1, uint32_t s2) {
    uint32_t r = s1 + s2;
    int ov = ((int32_t)((s1 ^ r) & (s2 ^ r)) < 0);
    if (!d_ovm(d) || !ov) d_setreg(d, dg, r);
    else d_setreg(d, dg, ((int32_t)s1 < 0) ? 0x80000000u : 0x7fffffffu);
    if (dg < 8) {
        d_clr_nzcvuf(d);
        if (s1 > r) d->st |= C31_ST_C;
        d_or_v_add(d, s1, s2, r);
        d_or_nz(d, r);
    } else if (dg >= 19) d_special(d, dg);
}

static inline void d_subb(c31_t *d, uint32_t dg, uint32_t s1, uint32_t s2) {
    uint32_t c = (d->st & C31_ST_C) ? 1 : 0;
    uint32_t r = s1 - s2 - c;
    int ov = ((int32_t)((s1 ^ s2) & (s1 ^ r)) < 0);
    if (!d_ovm(d) || !ov) d_setreg(d, dg, r);
    else d_setreg(d, dg, ((int32_t)s1 < 0) ? 0x80000000u : 0x7fffffffu);
    if (dg < 8) {
        d_clr_nzcvuf(d);
        if ((int64_t)s1 - s2 - c < 0) d->st |= C31_ST_C;
        d_or_v_sub(d, s1, s2, r);
        d_or_nz(d, r);
    } else if (dg >= 19) d_special(d, dg);
}

static inline void d_subi(c31_t *d, uint32_t dg, uint32_t s1, uint32_t s2) {
    uint32_t r = s1 - s2;
    int ov = ((int32_t)((s1 ^ s2) & (s1 ^ r)) < 0);
    if (!d_ovm(d) || !ov) d_setreg(d, dg, r);
    else d_setreg(d, dg, ((int32_t)s1 < 0) ? 0x80000000u : 0x7fffffffu);
    if (dg < 8) {
        d_clr_nzcvuf(d);
        if (s2 > s1) d->st |= C31_ST_C;
        d_or_v_sub(d, s1, s2, r);
        d_or_nz(d, r);
    } else if (dg >= 19) d_special(d, dg);
}

static inline void d_logic(c31_t *d, uint32_t dg, uint32_t r) {
    d_setreg(d, dg, r);
    if (dg < 8) {
        d_clr_nzvuf(d);
        d_or_nz(d, r);
    } else if (dg >= 19) d_special(d, dg);
}

static inline void d_ash(c31_t *d, uint32_t dg, uint32_t src, uint32_t cnt) {
    int32_t n = (int32_t)(cnt << 25) >> 25; // low 7 bits, signed
    uint32_t r;
    if (n < 0) r = (n >= -31) ? (uint32_t)((int32_t)src >> -n) :
            (uint32_t)((int32_t)src >> 31);
    else r = (n <= 31) ? (uint32_t)((int32_t)src << n) : 0;
    d_setreg(d, dg, r);
    if (dg < 8) {
        d_clr_nzcvuf(d);
        d_or_nz(d, r);
        if (n < 0) {
            uint32_t bit = (n >= -32) ?
                    (uint32_t)(((int32_t)src >> (-n - 1)) & 1) :
                    (uint32_t)(((int32_t)src >> 31) & 1);
            if (bit) d->st |= C31_ST_C;
        } else if (n > 0 && n <= 32) {
            if (((src << (n - 1)) >> 31) & 1) d->st |= C31_ST_C;
        }
    } else if (dg >= 19) d_special(d, dg);
}

static inline void d_lsh(c31_t *d, uint32_t dg, uint32_t src, uint32_t cnt) {
    int32_t n = (int32_t)(cnt << 25) >> 25;
    uint32_t r;
    if (n < 0) r = (n >= -31) ? src >> -n : 0;
    else r = (n <= 31) ? src << n : 0;
    d_setreg(d, dg, r);
    if (dg < 8) {
        d_clr_nzcvuf(d);
        d_or_nz(d, r);
        if (n < 0 && n >= -32) {
            if ((src >> (-n - 1)) & 1) d->st |= C31_ST_C;
        } else if (n > 0 && n <= 32) {
            if (((src << (n - 1)) >> 31) & 1) d->st |= C31_ST_C;
        }
    } else if (dg >= 19) d_special(d, dg);
}

static inline void d_mpyi(c31_t *d, uint32_t dg, uint32_t s1, uint32_t s2) {
    // Only the low 24 bits participate (signed).
    int64_t r = (int64_t)((int32_t)(s1 << 8) >> 8) *
            (int64_t)((int32_t)(s2 << 8) >> 8);
    if (!d_ovm(d) || (r >= -(int64_t)0x80000000 &&
            r <= (int64_t)0x7fffffff))
        d_setreg(d, dg, (uint32_t)r);
    else d_setreg(d, dg, (r < 0) ? 0x80000000u : 0x7fffffffu);
    if (dg < 8) {
        d_clr_nzvuf(d);
        d_or_nz(d, (uint32_t)r);
        if (r < -(int64_t)0x80000000 || r > (int64_t)0x7fffffff)
            d->st |= C31_ST_V | C31_ST_LV;
    } else if (dg >= 19) d_special(d, dg);
}

static inline void d_negb(c31_t *d, uint32_t dg, uint32_t src) {
    uint32_t c = (d->st & C31_ST_C) ? 1 : 0;
    uint32_t r = 0 - src - c;
    int ov = ((int32_t)((0 ^ src) & (0 ^ r)) < 0);
    if (!d_ovm(d) || !ov) d_setreg(d, dg, r);
    else d_setreg(d, dg, ((int32_t)src < 0) ? 0x80000000u : 0x7fffffffu);
    if (dg < 8) {
        d_clr_nzcvuf(d);
        if ((int64_t)0 - src - c < 0) d->st |= C31_ST_C;
        d_or_v_sub(d, 0, src, r);
        d_or_nz(d, r);
    } else if (dg >= 19) d_special(d, dg);
}

static inline void d_negi(c31_t *d, uint32_t dg, uint32_t src) {
    uint32_t r = 0 - src;
    int ov = ((int32_t)((0 ^ src) & (0 ^ r)) < 0);
    if (!d_ovm(d) || !ov) d_setreg(d, dg, r);
    else d_setreg(d, dg, ((int32_t)src < 0) ? 0x80000000u : 0x7fffffffu);
    if (dg < 8) {
        d_clr_nzcvuf(d);
        if (src > 0) d->st |= C31_ST_C;
        d_or_v_sub(d, 0, src, r);
        d_or_nz(d, r);
    } else if (dg >= 19) d_special(d, dg);
}

static inline void d_cmpi(c31_t *d, uint32_t s1, uint32_t s2) {
    uint32_t r = s1 - s2;
    d_clr_nzcvuf(d);
    if (s2 > s1) d->st |= C31_ST_C;
    d_or_v_sub(d, s1, s2, r);
    d_or_nz(d, r);
}

static inline void d_absi(c31_t *d, uint32_t dg, uint32_t src) {
    uint32_t r = ((int32_t)src < 0) ? (0u - src) : src;
    if (!d_ovm(d) || r != 0x80000000u) d_setreg(d, dg, r);
    else d_setreg(d, dg, 0x7fffffffu);
    if (dg < 8) {
        d_clr_nzvuf(d);
        d_or_nz(d, r);
        if (r == 0x80000000u) d->st |= C31_ST_V | C31_ST_LV;
    } else if (dg >= 19) d_special(d, dg);
}

static inline void d_subc(c31_t *d, uint32_t dg, uint32_t src) {
    uint32_t dst = d_ireg(d, dg), r;
    if (dst >= src) r = ((dst - src) << 1) | 1;
    else r = dst << 1;
    d_setreg(d, dg, r);
    if (dg >= 19) d_special(d, dg); // no flags
}

static inline void d_tstb(c31_t *d, uint32_t s1, uint32_t s2) {
    d_clr_nzvuf(d);
    d_or_nz(d, s1 & s2);
}

static inline void d_rol(c31_t *d, uint32_t dg) {
    uint32_t v = d_ireg(d, dg), nc = v >> 31, r = (v << 1) | nc;
    d_setreg(d, dg, r);
    if (dg < 8) {
        d_clr_nzcvuf(d);
        d_or_nz(d, r);
        if (nc) d->st |= C31_ST_C;
    } else if (dg >= 19) d_special(d, dg);
}
static inline void d_rolc(c31_t *d, uint32_t dg) {
    uint32_t v = d_ireg(d, dg), nc = v >> 31;
    uint32_t r = (v << 1) | (d->st & C31_ST_C);
    d_setreg(d, dg, r);
    if (dg < 8) {
        d_clr_nzcvuf(d);
        d_or_nz(d, r);
        if (nc) d->st |= C31_ST_C;
    } else if (dg >= 19) d_special(d, dg);
}
static inline void d_ror(c31_t *d, uint32_t dg) {
    uint32_t v = d_ireg(d, dg), nc = v & 1;
    uint32_t r = (v >> 1) | (nc << 31);
    d_setreg(d, dg, r);
    if (dg < 8) {
        d_clr_nzcvuf(d);
        d_or_nz(d, r);
        if (nc) d->st |= C31_ST_C;
    } else if (dg >= 19) d_special(d, dg);
}
static inline void d_rorc(c31_t *d, uint32_t dg) {
    uint32_t v = d_ireg(d, dg), nc = v & 1;
    uint32_t r = (v >> 1) | ((d->st & C31_ST_C) << 31);
    d_setreg(d, dg, r);
    if (dg < 8) {
        d_clr_nzcvuf(d);
        d_or_nz(d, r);
        if (nc) d->st |= C31_ST_C;
    } else if (dg >= 19) d_special(d, dg);
}

static inline void d_absf(c31_t *d, uint32_t dg, const c31reg_t *s) {
    int32_t man = s->man;
    int8_t fexp = (int8_t)s->exp;
    d_clr_nzvuf(d);
    d->r[dg & 7] = *s;
    if (fexp == -128) {
        d->r[dg & 7].man = 0;
    } else if (man < 0) {
        if (man != (int32_t)0x80000000u) {
            d->r[dg & 7].man = -man;
        } else if (fexp == 127) {
            d->st |= C31_ST_V | C31_ST_LV;
            d->r[dg & 7].man = 0x7fffffff;
        } else {
            d->r[dg & 7].exp = fexp + 1;
            d->r[dg & 7].man = 0;
        }
    }
    d_or_nzf(d, &d->r[dg & 7]);
}

static inline void d_rnd(c31_t *d, uint32_t dg) {
    int32_t man = d->r[dg & 7].man;
    d->st &= ~(C31_ST_N | C31_ST_V | C31_ST_UF);
    if (man < 0x7fffff80) {
        d->r[dg & 7].man = (int32_t)(((uint32_t)man + 0x80) & 0xffffff00u);
        d_or_nuf(d, &d->r[dg & 7]);
    } else if (d->r[dg & 7].exp < 127) {
        d->r[dg & 7].man = (int32_t)(((uint32_t)man + 0x80) & 0x7fffff00u);
        d->r[dg & 7].exp++;
        d_or_nuf(d, &d->r[dg & 7]);
    } else {
        d->r[dg & 7].man = 0x7fffff00;
        d->st |= C31_ST_V | C31_ST_LV;
    }
}

// ------------------------------------------------------------- conditions

static inline uint32_t d_cond(c31_t *d, uint32_t cc) {
    uint32_t st = d->st;
    uint32_t c = (st & C31_ST_C) != 0, v = (st & C31_ST_V) != 0;
    uint32_t z = (st & C31_ST_Z) != 0, n = (st & C31_ST_N) != 0;
    uint32_t uf = (st & C31_ST_UF) != 0, lv = (st & C31_ST_LV) != 0;
    uint32_t luf = (st & C31_ST_LUF) != 0;
    switch (cc & 31) {
    case 0: return 1;
    case 1: return c;
    case 2: return c || z;
    case 3: return !c && !z;
    case 4: return !c;
    case 5: return z;
    case 6: return !z;
    case 7: return n;
    case 8: return n || z;
    case 9: return !n && !z;
    case 10: return !n;
    case 12: return !v;
    case 13: return v;
    case 14: return !uf;
    case 15: return uf;
    case 16: return !lv;
    case 17: return lv;
    case 18: return !luf;
    case 19: return luf;
    case 20: return z || uf;
    default: return 0;
    }
}

// ------------------------------------------------------------- indirect

// Bit-reversed-carry addition within the BK field (carry propagates from
// the MSB down). MAME fatals here; the CAGE code is not expected to use
// it, but this implements the documented behaviour.
static inline uint32_t d_bitrev_add(uint32_t idx, uint32_t ir,
        uint32_t bkmask) {
    uint32_t res = idx;
    uint32_t carry = 0;
    int b, top = -1;
    for (b = 31; b >= 0; b--) {
        if ((bkmask >> b) & 1) {
            top = b;
            break;
        }
    }
    for (b = top; b >= 0; b--) {
        uint32_t ib = (idx >> b) & 1, rb = (ir >> b) & 1;
        uint32_t s = ib ^ rb ^ carry;
        carry = (ib + rb + carry) >> 1;
        res = (res & ~(1u << b)) | (s << b);
    }
    return res;
}

// Compute an indirect address. disp = u8 displacement (or 1 when implied).
// When defer != 0, the AR update is recorded in *defar/*defval (applied
// later) instead of being written immediately.
static inline uint32_t d_ind(c31_t *d, uint32_t ma, uint32_t disp,
        int defer, int *defar, uint32_t *defval) {
    uint32_t mode = (ma >> 3) & 31, ar = ma & 7, base = d->ar[ar], ret, u;
    switch (mode) {
    case 0: return base + disp;
    case 1: return base - disp;
    case 2:
        ret = base + disp;
        goto commit;
    case 3:
        ret = base - disp;
        goto commit;
    case 4:
        ret = base;
        u = base + disp;
        goto post;
    case 5:
        ret = base;
        u = base - disp;
        goto post;
    case 6:
        ret = base;
        u = (base & d->bkmask) + disp;
        if (u >= d->bk) u -= d->bk;
        u = (base & ~d->bkmask) | (u & d->bkmask);
        goto post;
    case 7:
        ret = base;
        u = (base & d->bkmask) - disp;
        if ((int32_t)u < 0) u += d->bk;
        u = (base & ~d->bkmask) | (u & d->bkmask);
        goto post;
    case 8: return base + d->ir0;
    case 9: return base - d->ir0;
    case 10:
        ret = base + d->ir0;
        goto commit;
    case 11:
        ret = base - d->ir0;
        goto commit;
    case 12:
        ret = base;
        u = base + d->ir0;
        goto post;
    case 13:
        ret = base;
        u = base - d->ir0;
        goto post;
    case 14:
        ret = base;
        u = (base & d->bkmask) + d->ir0;
        if (u >= d->bk) u -= d->bk;
        u = (base & ~d->bkmask) | (u & d->bkmask);
        goto post;
    case 15:
        ret = base;
        u = (base & d->bkmask) - d->ir0;
        if ((int32_t)u < 0) u += d->bk;
        u = (base & ~d->bkmask) | (u & d->bkmask);
        goto post;
    case 16: return base + d->ir1;
    case 17: return base - d->ir1;
    case 18:
        ret = base + d->ir1;
        goto commit;
    case 19:
        ret = base - d->ir1;
        goto commit;
    case 20:
        ret = base;
        u = base + d->ir1;
        goto post;
    case 21:
        ret = base;
        u = base - d->ir1;
        goto post;
    case 22:
        ret = base;
        u = (base & d->bkmask) + d->ir1;
        if (u >= d->bk) u -= d->bk;
        u = (base & ~d->bkmask) | (u & d->bkmask);
        goto post;
    case 23:
        ret = base;
        u = (base & d->bkmask) - d->ir1;
        if ((int32_t)u < 0) u += d->bk;
        u = (base & ~d->bkmask) | (u & d->bkmask);
        goto post;
    case 24: return base;
    case 25: // bit-reversed post-increment by IR0
        ret = base;
        u = (base & ~d->bkmask) |
                (d_bitrev_add(base & d->bkmask, d->ir0, d->bkmask) &
                 d->bkmask);
        goto post;
    default: return 0; // modes 26-31: MAME reads address 0
    commit:
        if (defer) {
            *defar = (int)ar;
            *defval = ret;
        } else d->ar[ar] = ret;
        return ret;
    post:
        if (defer) {
            *defar = (int)ar;
            *defval = u;
        } else d->ar[ar] = u;
        return ret;
    }
}

// Apply a deferred AR update (defar < 0 = none).
static inline void d_def_apply(c31_t *d, int defar, uint32_t defval) {
    if (defar >= 0) d->ar[defar] = defval;
}

// ------------------------------------------------------------- traps/irqs

static inline void d_trap(c31_t *d, uint32_t n) {
    d->sp++;
    rr_dwr(d->sp, d->pc);
    d->st &= ~C31_ST_GIE;
    d->pc = rr_drd(n & 0xffffffu) & 0xffffffu;
}

static inline void d_check_irqs(c31_t *d) {
    uint32_t valid = d->iff & d->ie & 0xfffu;
    int i, trap = 0;
    if (!valid || !(d->st & C31_ST_GIE)) return;
    d->idle = 0; // any live interrupt wakes the core (MAME)
    // Delay slots (MAME's m_delayed) and RPTS repeats defer the trap.
    if (d->dcount >= 0 || d->rpts_active) return;
    for (i = 0; i < 12; i++) {
        if (valid & (1u << i)) {
            trap = i + 1;
            break;
        }
    }
    d->iff &= ~(uint32_t)(1u << (trap - 1));
    d_trap(d, (uint32_t)trap);
    d->iff |= d->irq & 0x0fu; // level-sensitive re-trigger
    d->cycles -= 4 * (d->lopower ? 16 : 1);
}

// One translated instruction, exactly as c31_step runs it: repeat-block
// loop-back, fetch-free execute, cycles, delay slots, interrupts. Leaves
// when the slice ends, the DSP idles or code was overwritten. Expects
// `d`, `gen` and a `dispatch` label in the enclosing function.
#define DI(PC, INS, XC) do { \
        if (d->dcount < 0 && (d->st & C31_ST_RM) && d->pc == d->re + 1) { \
            if ((int32_t)--d->rc >= 0) { \
                d->pc = d->rs & 0xffffffu; \
                goto dispatch; \
            } \
            d->st &= ~C31_ST_RM; \
            d->rpts_active = 0; \
        } \
        d->pc = ((PC) + 1) & 0xffffffu; \
        d_exec(d, &(INS)); \
        d->cycles -= (1 + (XC)) * (d->lopower ? 16 : 1); \
        d->total++; \
        if (d->dcount > 0) { \
            d->dcount--; \
        } else if (d->dcount == 0) { \
            d->pc = d->djump & 0xffffffu; \
            d->dcount = -1; \
        } \
        d_check_irqs(d); \
        if (d->cycles <= 0 || d->idle || rrm->dsp_gen != gen) \
            return RR_EXIT; \
    } while (0)

// ------------------------------------------------------------- execute

// Integer general-op source. sext selects the immediate conversion.
static inline uint32_t d_gsrc_i(c31_t *d, const d_insn_t *o, int sext) {
    int da = -1;
    uint32_t dv = 0;
    switch (o->g) {
    case 0: return d_ireg(d, o->sreg);
    case 1: return rr_drd(((d->dp & 0xffu) << 16) | (o->addr & 0xffffu));
    case 2: return rr_drd(d_ind(d, o->ma1, o->disp, 0, &da, &dv));
    default:
        return sext ? (uint32_t)(int32_t)(int16_t)o->addr :
                (o->addr & 0xffffu);
    }
}

static inline void d_gsrc_f(c31_t *d, const d_insn_t *o, c31reg_t *out) {
    int da = -1;
    uint32_t dv = 0;
    switch (o->g) {
    case 0:
        *out = d->r[o->sreg & 7];
        return;
    case 1:
        d_long2fp(out, rr_drd(((d->dp & 0xffu) << 16) |
                (o->addr & 0xffffu)));
        return;
    case 2:
        d_long2fp(out, rr_drd(d_ind(d, o->ma1, o->disp, 0, &da, &dv)));
        return;
    default:
        d_short2fp(out, o->addr);
        return;
    }
}

// Direct/indirect store address for STx general ops.
static inline uint32_t d_gdst_a(c31_t *d, const d_insn_t *o) {
    int da = -1;
    uint32_t dv = 0;
    if (o->g == 1) return ((d->dp & 0xffu) << 16) | (o->addr & 0xffffu);
    return d_ind(d, o->ma1, o->disp, 0, &da, &dv);
}

static inline void d_exec_rest(c31_t *d, const d_insn_t *o);

static inline void d_exec(c31_t *d, const d_insn_t *o) {
    c31reg_t t1, t2;
    uint32_t s, s2;
    switch (o->op) {
    case DOP_ABSF:
        d_gsrc_f(d, o, &t1);
        d_absf(d, o->dreg & 7, &t1);
        return;
    case DOP_ABSI:
        d_absi(d, o->dreg, d_gsrc_i(d, o, 1));
        return;
    case DOP_ADDC:
        d_addc(d, o->dreg, d_ireg(d, o->dreg), d_gsrc_i(d, o, 1));
        return;
    case DOP_ADDF:
        d_gsrc_f(d, o, &t1);
        d_addf(d, &d->r[o->dreg & 7], &d->r[o->dreg & 7], &t1);
        return;
    case DOP_ADDI:
        d_addi(d, o->dreg, d_ireg(d, o->dreg), d_gsrc_i(d, o, 1));
        return;
    case DOP_AND:
        d_logic(d, o->dreg, d_ireg(d, o->dreg) & d_gsrc_i(d, o, 0));
        return;
    case DOP_ANDN:
        d_logic(d, o->dreg, d_ireg(d, o->dreg) & ~d_gsrc_i(d, o, 0));
        return;
    case DOP_ASH:
        d_ash(d, o->dreg, d_ireg(d, o->dreg), d_gsrc_i(d, o, 1));
        return;
    case DOP_CMPF:
        d_gsrc_f(d, o, &t1);
        d_subf(d, &t2, &d->r[o->dreg & 7], &t1);
        return;
    case DOP_CMPI:
        d_cmpi(d, d_ireg(d, o->dreg), d_gsrc_i(d, o, 1));
        return;
    case DOP_FIX:
        // FIX accepts any register as its destination (MAME's
        // fix_reg decodes a 5-bit dreg); route non-R results
        // through a temp so RS/ARx see the converted value.
        d_gsrc_f(d, o, &t1);
        if (o->dreg < 8) {
            d->r[o->dreg] = t1;
            d_float2int(d, &d->r[o->dreg], 1);
        } else {
            d_float2int(d, &t1, 1);
            d_setreg(d, o->dreg, (uint32_t)t1.man);
        }
        return;
    case DOP_FLOAT:
        s = d_gsrc_i(d, o, 1);
        d->r[o->dreg & 7].man = (int32_t)s;
        d_int2float(d, &d->r[o->dreg & 7]);
        return;
    case DOP_LDE:
        d_gsrc_f(d, o, &t1);
        d->r[o->dreg & 7].exp = t1.exp;
        if (t1.exp == -128) d->r[o->dreg & 7].man = 0;
        return;
    case DOP_LDF: case DOP_LDFI:
        d_gsrc_f(d, o, &d->r[o->dreg & 7]);
        d_clr_nzvuf(d);
        d_or_nzf(d, &d->r[o->dreg & 7]);
        return;
    case DOP_LDI: case DOP_LDII:
        s = d_gsrc_i(d, o, 1);
        d_setreg(d, o->dreg, s);
        if (o->dreg < 8) {
            d_clr_nzvuf(d);
            d_or_nz(d, s);
        } else if (o->dreg >= 19) d_special(d, o->dreg);
        return;
    case DOP_LDM:
        if (o->g == 0) {
            d->r[o->dreg & 7].man = d->r[o->sreg & 7].man;
        } else if (o->g == 3) {
            d_short2fp(&t1, o->addr);
            d->r[o->dreg & 7].man = t1.man;
        } else {
            d->r[o->dreg & 7].man = (int32_t)d_gsrc_i(d, o, 1);
        }
        return;
    case DOP_LSH:
        d_lsh(d, o->dreg, d_ireg(d, o->dreg), d_gsrc_i(d, o, 1));
        return;
    case DOP_MPYF:
        d_gsrc_f(d, o, &t1);
        d_mpyf(d, &d->r[o->dreg & 7], &d->r[o->dreg & 7], &t1);
        return;
    case DOP_MPYI:
        d_mpyi(d, o->dreg, d_ireg(d, o->dreg), d_gsrc_i(d, o, 1));
        return;
    case DOP_NEGB:
        d_negb(d, o->dreg, d_gsrc_i(d, o, 1));
        return;
    case DOP_NEGF:
        d_gsrc_f(d, o, &t1);
        d_negf(d, &d->r[o->dreg & 7], &t1);
        return;
    case DOP_NEGI:
        d_negi(d, o->dreg, d_gsrc_i(d, o, 1));
        return;
    case DOP_NOP:
        // NOP with an indirect operand still runs the addressing mode
        // (the boot ROM uses NOP *AR1++(1) to step its table pointer).
        if (o->g == 2) (void)d_gsrc_i(d, o, 1);
        return;
    case DOP_SIGI:
        return;
    case DOP_NORM:
        d_gsrc_f(d, o, &t1);
        d_norm(d, &d->r[o->dreg & 7], &t1);
        return;
    case DOP_NOT:
        d_logic(d, o->dreg, ~d_gsrc_i(d, o, 0));
        return;
    case DOP_OR:
        d_logic(d, o->dreg, d_ireg(d, o->dreg) | d_gsrc_i(d, o, 0));
        return;
    case DOP_RND:
        d_gsrc_f(d, o, &d->r[o->dreg & 7]);
        d_rnd(d, o->dreg & 7);
        return;
    case DOP_ROL: d_rol(d, o->dreg); return;
    case DOP_ROLC: d_rolc(d, o->dreg); return;
    case DOP_ROR: d_ror(d, o->dreg); return;
    case DOP_RORC: d_rorc(d, o->dreg); return;
    case DOP_SUBB:
        d_subb(d, o->dreg, d_ireg(d, o->dreg), d_gsrc_i(d, o, 1));
        return;
    case DOP_SUBC:
        d_subc(d, o->dreg, d_gsrc_i(d, o, 1));
        return;
    case DOP_SUBF:
        d_gsrc_f(d, o, &t1);
        d_subf(d, &d->r[o->dreg & 7], &d->r[o->dreg & 7], &t1);
        return;
    case DOP_SUBI:
        d_subi(d, o->dreg, d_ireg(d, o->dreg), d_gsrc_i(d, o, 1));
        return;
    case DOP_SUBRB:
        s2 = d_ireg(d, o->dreg);
        d_subb(d, o->dreg, d_gsrc_i(d, o, 1), s2);
        return;
    case DOP_SUBRF:
        d_gsrc_f(d, o, &t1);
        t2 = d->r[o->dreg & 7];
        d_subf(d, &d->r[o->dreg & 7], &t1, &t2);
        return;
    case DOP_SUBRI:
        s2 = d_ireg(d, o->dreg);
        d_subi(d, o->dreg, d_gsrc_i(d, o, 1), s2);
        return;
    case DOP_TSTB:
        d_tstb(d, d_ireg(d, o->dreg), d_gsrc_i(d, o, 0));
        return;
    case DOP_XOR:
        d_logic(d, o->dreg, d_ireg(d, o->dreg) ^ d_gsrc_i(d, o, 0));
        return;
    default:
        break;
    }
    d_exec_rest(d, o);
}

// 3-operand sources: T bit 0 (g&1) selects src1 ind/reg, bit 1 (g&2)
// src2. Src1's AR update is deferred until src2's address is computed
// (MAME's INDIRECT_1_DEF); the caller applies it with d_def_apply.
static inline uint32_t d_3src_i(c31_t *d, const d_insn_t *o, int which,
        int *dd, uint32_t *dvv) {
    if (which == 1)
        return (o->g & 1) ? rr_drd(d_ind(d, o->ma1, 1, 1, dd, dvv)) :
                d_ireg(d, o->sreg);
    return (o->g & 2) ? rr_drd(d_ind(d, o->ma2, 1, 0, dd, dvv)) :
            d_ireg(d, o->sreg2);
}

static inline void d_3src_f(c31_t *d, const d_insn_t *o, int which,
        c31reg_t *out, int *dd, uint32_t *dvv) {
    if (which == 1) {
        if (o->g & 1)
            d_long2fp(out, rr_drd(d_ind(d, o->ma1, 1, 1, dd, dvv)));
        else
            *out = d->r[o->sreg & 7];
    } else {
        if (o->g & 2)
            d_long2fp(out, rr_drd(d_ind(d, o->ma2, 1, 0, dd, dvv)));
        else
            *out = d->r[o->sreg2 & 7];
    }
}

static inline void d_exec_rest(c31_t *d, const d_insn_t *o) {
    c31reg_t t1, t2, t3;
    uint32_t s;
    int da;
    uint32_t dv;
    switch (o->op) {
    case DOP_IDLE:
        d->idle = 1;
        d->st |= C31_ST_GIE;
        return;
    case DOP_SPEED:
        d->lopower = (o->addr & 1) ? 1 : 0;
        return;
    case DOP_RPTS:
        switch (o->g) {
        case 0: s = d_ireg(d, o->sreg); break;
        case 1:
            s = rr_drd(((d->dp & 0xffu) << 16) | (o->addr & 0xffffu));
            break;
        case 2:
            da = -1; dv = 0;
            s = rr_drd(d_ind(d, o->ma1, o->disp, 0, &da, &dv));
            break;
        default: s = o->addr & 0xffffu; break;
        }
        d->rc = s;
        d->rs = d->pc;
        d->re = d->pc;
        d->st |= C31_ST_RM;
        d->rpts_active = 1;
        return;
    case DOP_PUSH:
        d->sp++;
        rr_dwr(d->sp, d_ireg(d, o->dreg));
        return;
    case DOP_PUSHF:
        d->sp++;
        rr_dwr(d->sp, d_fp2long(&d->r[o->dreg & 7]));
        return;
    case DOP_POP:
        s = rr_drd(d->sp);
        d->sp--;
        d_setreg(d, o->dreg, s);
        if (o->dreg < 8) {
            d_clr_nzvuf(d);
            d_or_nz(d, s);
        } else if (o->dreg >= 19) d_special(d, o->dreg);
        return;
    case DOP_POPF:
        d_long2fp(&d->r[o->dreg & 7], rr_drd(d->sp));
        d->sp--;
        d_clr_nzvuf(d);
        d_or_nzf(d, &d->r[o->dreg & 7]);
        return;
    case DOP_STF: case DOP_STFI:
        rr_dwr(d_gdst_a(d, o), d_fp2long(&d->r[o->dreg & 7]));
        return;
    case DOP_STI: case DOP_STII:
        rr_dwr(d_gdst_a(d, o), d_ireg(d, o->dreg));
        return;
    case DOP_IACK:
        if (o->g == 1)
            s = ((d->dp & 0xffu) << 16) | (o->addr & 0xffffu);
        else {
            da = -1; dv = 0;
            s = d_ind(d, o->ma1, o->disp, 0, &da, &dv);
        }
        rr_drd(s); // acknowledge strobes unconnected on CAGE
        return;
    // LDFcond/LDIcond leave the status flags alone (TI's C3x guide; MAME):
    // code tests a compare, then loads 0 and 1 conditionally on it.
    case DOP_LDFC:
        if (!d_cond(d, o->cc)) return;
        d_gsrc_f(d, o, &d->r[o->dreg & 7]);
        return;
    case DOP_LDIC:
        if (!d_cond(d, o->cc)) return;
        s = d_gsrc_i(d, o, 1);
        d_setreg(d, o->dreg, s);
        if (o->dreg >= 19) d_special(d, o->dreg);
        return;
    case DOP_ADDC3:
        {
        int dd = -1;
        uint32_t dvv = 0, a1 = d_3src_i(d, o, 1, &dd, &dvv);
        uint32_t a2 = d_3src_i(d, o, 2, &dd, &dvv);
        d_def_apply(d, dd, dvv);
        d_addc(d, o->dreg, a1, a2);
        return;
    }
    case DOP_ADDF3:
        {
        int dd = -1;
        uint32_t dvv = 0;
        d_3src_f(d, o, 1, &t1, &dd, &dvv);
        d_3src_f(d, o, 2, &t2, &dd, &dvv);
        d_def_apply(d, dd, dvv);
        d_addf(d, &d->r[o->dreg & 7], &t1, &t2);
        return;
        }
    case DOP_ADDI3:
        {
        int dd = -1;
        uint32_t dvv = 0, a1 = d_3src_i(d, o, 1, &dd, &dvv);
        uint32_t a2 = d_3src_i(d, o, 2, &dd, &dvv);
        d_def_apply(d, dd, dvv);
        d_addi(d, o->dreg, a1, a2);
        return;
    }
    case DOP_AND3:
        {
        int dd = -1;
        uint32_t dvv = 0, a1 = d_3src_i(d, o, 1, &dd, &dvv);
        uint32_t a2 = d_3src_i(d, o, 2, &dd, &dvv);
        d_def_apply(d, dd, dvv);
        d_logic(d, o->dreg, a1 & a2);
        return;
    }
    case DOP_ANDN3:
{
        int dd = -1;
        uint32_t dvv = 0, a1 = d_3src_i(d, o, 1, &dd, &dvv);
        uint32_t a2 = d_3src_i(d, o, 2, &dd, &dvv);
        d_def_apply(d, dd, dvv);
        d_logic(d, o->dreg, a1 & ~a2);
        return;
    }
    case DOP_ASH3:
        {
        int dd = -1;
        uint32_t dvv = 0, a1 = d_3src_i(d, o, 1, &dd, &dvv);
        uint32_t a2 = d_3src_i(d, o, 2, &dd, &dvv);
        d_def_apply(d, dd, dvv);
        d_ash(d, o->dreg, a1, a2);
        return;
    }
    case DOP_CMPF3:
        {
        int dd = -1;
        uint32_t dvv = 0;
        d_3src_f(d, o, 1, &t1, &dd, &dvv);
        d_3src_f(d, o, 2, &t2, &dd, &dvv);
        d_def_apply(d, dd, dvv);
        d_subf(d, &t3, &t1, &t2);
        return;
        }
    case DOP_CMPI3:
{
        int dd = -1;
        uint32_t dvv = 0, a1 = d_3src_i(d, o, 1, &dd, &dvv);
        uint32_t a2 = d_3src_i(d, o, 2, &dd, &dvv);
        d_def_apply(d, dd, dvv);
        d_cmpi(d, a1, a2);
        return;
    }
    case DOP_LSH3:
        {
        int dd = -1;
        uint32_t dvv = 0, a1 = d_3src_i(d, o, 1, &dd, &dvv);
        uint32_t a2 = d_3src_i(d, o, 2, &dd, &dvv);
        d_def_apply(d, dd, dvv);
        d_lsh(d, o->dreg, a1, a2);
        return;
    }
    case DOP_MPYF3:
        {
        int dd = -1;
        uint32_t dvv = 0;
        d_3src_f(d, o, 1, &t1, &dd, &dvv);
        d_3src_f(d, o, 2, &t2, &dd, &dvv);
        d_def_apply(d, dd, dvv);
        d_mpyf(d, &d->r[o->dreg & 7], &t1, &t2);
        return;
        }
    case DOP_MPYI3:
        {
        int dd = -1;
        uint32_t dvv = 0, a1 = d_3src_i(d, o, 1, &dd, &dvv);
        uint32_t a2 = d_3src_i(d, o, 2, &dd, &dvv);
        d_def_apply(d, dd, dvv);
        d_mpyi(d, o->dreg, a1, a2);
        return;
    }
    case DOP_OR3:
        {
        int dd = -1;
        uint32_t dvv = 0, a1 = d_3src_i(d, o, 1, &dd, &dvv);
        uint32_t a2 = d_3src_i(d, o, 2, &dd, &dvv);
        d_def_apply(d, dd, dvv);
        d_logic(d, o->dreg, a1 | a2);
        return;
    }
    case DOP_SUBB3:
        {
        int dd = -1;
        uint32_t dvv = 0, a1 = d_3src_i(d, o, 1, &dd, &dvv);
        uint32_t a2 = d_3src_i(d, o, 2, &dd, &dvv);
        d_def_apply(d, dd, dvv);
        d_subb(d, o->dreg, a1, a2);
        return;
    }
    case DOP_SUBF3:
        {
        int dd = -1;
        uint32_t dvv = 0;
        d_3src_f(d, o, 1, &t1, &dd, &dvv);
        d_3src_f(d, o, 2, &t2, &dd, &dvv);
        d_def_apply(d, dd, dvv);
        d_subf(d, &d->r[o->dreg & 7], &t1, &t2);
        return;
        }
    case DOP_SUBI3:
        {
        int dd = -1;
        uint32_t dvv = 0, a1 = d_3src_i(d, o, 1, &dd, &dvv);
        uint32_t a2 = d_3src_i(d, o, 2, &dd, &dvv);
        d_def_apply(d, dd, dvv);
        d_subi(d, o->dreg, a1, a2);
        return;
    }
    case DOP_TSTB3:
{
        int dd = -1;
        uint32_t dvv = 0, a1 = d_3src_i(d, o, 1, &dd, &dvv);
        uint32_t a2 = d_3src_i(d, o, 2, &dd, &dvv);
        d_def_apply(d, dd, dvv);
        d_tstb(d, a1, a2);
        return;
    }
    case DOP_XOR3:
        {
        int dd = -1;
        uint32_t dvv = 0, a1 = d_3src_i(d, o, 1, &dd, &dvv);
        uint32_t a2 = d_3src_i(d, o, 2, &dd, &dvv);
        d_def_apply(d, dd, dvv);
        d_logic(d, o->dreg, a1 ^ a2);
        return;
    }
    case DOP_BR:
        d->pc = o->addr & 0xffffffu;
        return;
    case DOP_BRD:
        d->djump = o->addr & 0xffffffu;
        d->dcount = 3;
        return;
    case DOP_CALL:
        d->sp++;
        rr_dwr(d->sp, d->pc);
        d->pc = o->addr & 0xffffffu;
        return;
    case DOP_RPTB:
        d->rs = d->pc;
        d->re = o->addr & 0xffffffu;
        d->st |= C31_ST_RM;
        d->rpts_active = 0;
        return;
    case DOP_SWI:
        return; // MAME fatals; the CAGE code never uses it
    case DOP_BCOND:
        if (!d_cond(d, o->cc)) return;
        if (o->g == 1) d->pc = (d->pc + o->addr) & 0xffffffu;
        else d->pc = d_ireg(d, o->sreg) & 0xffffffu;
        return;
    case DOP_BDCOND:
        if (!d_cond(d, o->cc)) return;
        if (o->g == 1) d->djump = (d->pc + 2 + o->addr) & 0xffffffu;
        else d->djump = d_ireg(d, o->sreg) & 0xffffffu;
        d->dcount = 3;
        return;
    case DOP_DBCOND: case DOP_DBDCOND: {
        uint32_t res = (d->ar[o->dreg & 7] - 1) & 0xffffffu;
        d->ar[o->dreg & 7] = res | (d->ar[o->dreg & 7] & 0xff000000u);
        if (!d_cond(d, o->cc) || (res & 0x800000u)) return;
        if (o->op == DOP_DBCOND) {
            if (o->g == 1) d->pc = (d->pc + o->addr) & 0xffffffu;
            else d->pc = d_ireg(d, o->sreg) & 0xffffffu;
        } else {
            if (o->g == 1) d->djump = (d->pc + 2 + o->addr) & 0xffffffu;
            else d->djump = d_ireg(d, o->sreg) & 0xffffffu;
            d->dcount = 3;
        }
        return;
    }
    case DOP_CALLC:
        if (!d_cond(d, o->cc)) return;
        d->sp++;
        rr_dwr(d->sp, d->pc);
        if (o->g == 1) d->pc = (d->pc + o->addr) & 0xffffffu;
        else d->pc = d_ireg(d, o->sreg) & 0xffffffu;
        return;
    case DOP_TRAPC:
        if (!d_cond(d, o->cc)) return;
        d_trap(d, o->addr);
        return;
    case DOP_RETI:
        if (!d_cond(d, o->cc)) return;
        d->pc = rr_drd(d->sp) & 0xffffffu;
        d->sp--;
        d->st |= C31_ST_GIE;
        return;
    case DOP_RETS:
        if (!d_cond(d, o->cc)) return;
        d->pc = rr_drd(d->sp) & 0xffffffu;
        d->sp--;
        return;
    default:
        break;
    }
    // Parallel ops below.
    {
        int is_float = 0;
        if (o->op == DOP_P_MPYADD || o->op == DOP_P_MPYSUB) {
            uint32_t var = o->g & 3;
            c31reg_t mpres;
            uint32_t i1 = 0, i2 = 0;
            int dd = -1;
            uint32_t dvv = 0;
            is_float = !(o->g & 4);
            if (is_float) {
                // Operand 1 (deferred) then operand 2 (immediate).
                d_long2fp(&t1, rr_drd(d_ind(d, o->ma1, 1, 1, &dd, &dvv)));
                d_long2fp(&t2, rr_drd(d_ind(d, o->ma2, 1, 0, &dd, &dvv)));
            } else {
                i1 = rr_drd(d_ind(d, o->ma1, 1, 1, &dd, &dvv));
                i2 = rr_drd(d_ind(d, o->ma2, 1, 0, &dd, &dvv));
            }
            // N.B. m1/i1 = MEM[ma1] (src3), m2/i2 = MEM[ma2] (src4).
            if (is_float) {
                c31reg_t *r1 = &d->r[o->sreg & 7];
                c31reg_t *r2 = &d->r[o->sreg2 & 7];
                c31reg_t *ms1, *ms2, *as1, *as2;
                if (var == 0) {
                    ms1 = &t1; ms2 = &t2; as1 = r1; as2 = r2;
                } else if (var == 1) {
                    ms1 = &t1; ms2 = r1; as1 = &t2; as2 = r2;
                } else if (var == 2) {
                    ms1 = r1; ms2 = r2; as1 = &t1; as2 = &t2;
                } else {
                    ms1 = &t1; ms2 = r1; as1 = r2; as2 = &t2;
                }
                // Multiply first (into a temp: dests may alias sources
                // only across the two ops, never within; MAME uses TEMP3
                // for the same reason).
                if (o->op == DOP_P_MPYSUB) {
                    c31reg_t mm1 = *ms1, mm2 = *ms2;
                    d_mpyf(d, &mpres, &mm1, &mm2);
                } else {
                    c31reg_t mm1 = *ms1, mm2 = *ms2;
                    d_mpyf(d, &mpres, &mm1, &mm2);
                }
                t3 = mpres;
                if (o->op == DOP_P_MPYSUB) {
                    c31reg_t aa1 = *as1, aa2 = *as2;
                    d_subf(d, &d->r[o->dreg2 & 7], &aa1, &aa2);
                } else {
                    c31reg_t aa1 = *as1, aa2 = *as2;
                    d_addf(d, &d->r[o->dreg2 & 7], &aa1, &aa2);
                }
                d->r[o->dreg & 7] = t3;
                // Flags: the ADD/SUB runs last, so its flags win, except
                // V/UF latch bits accumulate (MAME runs addf after mpyf
                // with CLR clearing only the non-latched V/UF... note
                // d_addf clears V/UF but preserves LV/LUF, so a mpyf
                // overflow latched into LV survives; mirror that by
                // re-running in the same order MAME does).
            } else {
                uint32_t r1 = d_ireg(d, o->sreg & 7);
                uint32_t r2 = d_ireg(d, o->sreg2 & 7);
                uint32_t ms1, ms2, as1, as2;
                if (var == 0) {
                    ms1 = i1; ms2 = i2; as1 = r1; as2 = r2;
                } else if (var == 1) {
                    ms1 = i1; ms2 = r1; as1 = i2; as2 = r2;
                } else if (var == 2) {
                    ms1 = r1; ms2 = r2; as1 = i1; as2 = i2;
                } else {
                    ms1 = i1; ms2 = r1; as1 = r2; as2 = i2;
                }
                {
                    // Multiply into a temp first (dest may alias a source
                    // of the other op).
                    int64_t mp = (int64_t)((int32_t)(ms1 << 8) >> 8) *
                            (int64_t)((int32_t)(ms2 << 8) >> 8);
                    uint32_t add_d = o->dreg2 & 7, mpy_d = o->dreg & 7;
                    uint32_t mpy_v;
                    int mpy_ov = (mp < -(int64_t)0x80000000 ||
                            mp > (int64_t)0x7fffffff);
                    if (!d_ovm(d) || !mpy_ov) mpy_v = (uint32_t)mp;
                    else mpy_v = (mp < 0) ? 0x80000000u : 0x7fffffffu;
                    if (o->op == DOP_P_MPYSUB)
                        d_subi(d, add_d, as1, as2);
                    else
                        d_addi(d, add_d, as1, as2);
                    d->r[mpy_d].man = (int32_t)mpy_v;
                    // MAME's mpyi3 runs through MPYI after the add: flags
                    // from the multiply win for N/Z, V accumulates.
                    d_clr_nzvuf(d);
                    d_or_nz(d, mpy_v);
                    if (mpy_ov) d->st |= C31_ST_V | C31_ST_LV;
                }
            }
            d_def_apply(d, dd, dvv);
            return;
        }
        if (o->op == DOP_P_STST) {
            int dd = -1;
            uint32_t dvv = 0;
            is_float = (o->g == 0);
            if (is_float) {
                rr_dwr(d_ind(d, o->ma1, 1, 1, &dd, &dvv),
                        d_fp2long(&d->r[o->sreg & 7]));
                rr_dwr(d_ind(d, o->ma2, 1, 0, &dd, &dvv),
                        d_fp2long(&d->r[o->sreg2 & 7]));
            } else {
                rr_dwr(d_ind(d, o->ma1, 1, 1, &dd, &dvv),
                        d_ireg(d, o->sreg & 7));
                rr_dwr(d_ind(d, o->ma2, 1, 0, &dd, &dvv),
                        d_ireg(d, o->sreg2 & 7));
            }
            d_def_apply(d, dd, dvv);
            return;
        }
        if (o->op == DOP_P_LDLD) {
            int dd = -1;
            uint32_t dvv = 0, w1, w2;
            is_float = (o->g == 0);
            w1 = rr_drd(d_ind(d, o->ma1, 1, 1, &dd, &dvv));
            if (is_float) d_long2fp(&d->r[o->dreg & 7], w1);
            else d->r[o->dreg & 7].man = (int32_t)w1;
            w2 = rr_drd(d_ind(d, o->ma2, 1, 0, &dd, &dvv));
            if (is_float) d_long2fp(&d->r[o->dreg2 & 7], w2);
            else d->r[o->dreg2 & 7].man = (int32_t)w2;
            d_def_apply(d, dd, dvv);
            return;
        }
        if (o->op == DOP_P_ALUST) {
            uint32_t pair = d_par_tab[o->g % 20];
            int dd = -1;
            uint32_t dvv = 0;
            uint32_t mem;
            c31reg_t mf;
            // Both sides see pre-instruction state (MAME): snapshot the
            // store value before the ALU side can overwrite its register.
            uint32_t stval = d_ireg(d, o->sreg2 & 7);
            c31reg_t stvalf = d->r[o->sreg2 & 7];
            // ALU memory source first (deferred), mirroring MAME.
            mem = rr_drd(d_ind(d, o->ma2, 1, 1, &dd, &dvv));
            d_long2fp(&mf, mem);
            switch (pair) {
            case DOP_ABSF:
                d_absf(d, o->dreg & 7, &mf);
                break;
            case DOP_ABSI:
                d_absi(d, o->dreg & 7, mem);
                break;
            case DOP_ADDF3:
                d_addf(d, &d->r[o->dreg & 7], &d->r[o->sreg & 7], &mf);
                break;
            case DOP_ADDI3:
                d_addi(d, o->dreg & 7, d_ireg(d, o->sreg & 7), mem);
                break;
            case DOP_AND3:
                d_logic(d, o->dreg & 7, d_ireg(d, o->sreg & 7) & mem);
                break;
            case DOP_ASH3:
                d_ash(d, o->dreg & 7, d_ireg(d, o->sreg & 7), mem);
                break;
            case DOP_FIX:
                d->r[o->dreg & 7] = mf;
                d_float2int(d, &d->r[o->dreg & 7], 1);
                break;
            case DOP_FLOAT:
                d->r[o->dreg & 7].man = (int32_t)mem;
                d_int2float(d, &d->r[o->dreg & 7]);
                break;
            case DOP_LDF:       // LDF||STF, LDI||STI: flags unaffected
                d->r[o->dreg & 7] = mf;
                break;
            case DOP_LDI:
                d->r[o->dreg & 7].man = (int32_t)mem;
                break;
            case DOP_LSH3:
                d_lsh(d, o->dreg & 7, d_ireg(d, o->sreg & 7), mem);
                break;
            case DOP_MPYF3:
                d_mpyf(d, &d->r[o->dreg & 7], &d->r[o->sreg & 7], &mf);
                break;
            case DOP_MPYI3:
                d_mpyi(d, o->dreg & 7, d_ireg(d, o->sreg & 7), mem);
                break;
            case DOP_NEGF:
                d_negf(d, &d->r[o->dreg & 7], &mf);
                break;
            case DOP_NEGI:
                d_negi(d, o->dreg & 7, mem);
                break;
            case DOP_NOT:
                d_logic(d, o->dreg & 7, ~mem);
                break;
            case DOP_OR3:
                d_logic(d, o->dreg & 7, d_ireg(d, o->sreg & 7) | mem);
                break;
            case DOP_SUBF3:
                d_subf(d, &d->r[o->dreg & 7], &d->r[o->sreg & 7], &mf);
                break;
            case DOP_SUBI3:
                d_subi(d, o->dreg & 7, d_ireg(d, o->sreg & 7), mem);
                break;
            case DOP_XOR3:
                d_logic(d, o->dreg & 7, d_ireg(d, o->sreg & 7) ^ mem);
                break;
            default:
                break;
            }
            // Store second (immediate address): float pairs store FP.
            if (pair == DOP_ABSF || pair == DOP_ADDF3 || pair == DOP_FLOAT ||
                    pair == DOP_LDF || pair == DOP_MPYF3 ||
                    pair == DOP_NEGF || pair == DOP_SUBF3)
                rr_dwr(d_ind(d, o->ma1, 1, 0, &dd, &dvv),
                        d_fp2long(&stvalf));
            else
                rr_dwr(d_ind(d, o->ma1, 1, 0, &dd, &dvv), stval);
            d_def_apply(d, dd, dvv);
            return;
        }
    }
}
