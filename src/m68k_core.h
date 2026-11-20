// 68EC020 instruction semantics shared by the interpreter, the translator
// and the translated code itself (UltraCombo's adsp_core.h pattern).
//
// Everything here is static and libc-free, so translated modules compile it
// with -nostdlib. The program's globals rrm/rrfns provide memory and the
// slow device paths. Keep instruction semantics in this file only.
#pragma once
#include "rec_abi.h"

// ------------------------------------------------------------- decode types

typedef enum {
    MSZ_B = 0, MSZ_W = 1, MSZ_L = 2
} m_sz_t;

// Effective-address modes.
typedef enum {
    MEA_D = 0,      // Dn
    MEA_A,          // An
    MEA_AIND,       // (An)
    MEA_AINC,       // (An)+
    MEA_ADEC,       // -(An)
    MEA_ADISP,      // (d16,An)
    MEA_AIDX,       // 020 indexed / memory-indirect
    MEA_ABSW,       // (xxx).W
    MEA_ABSL,       // (xxx).L
    MEA_PCDISP,     // (d16,PC)
    MEA_PCIDX,      // 020 PC-indexed / memory-indirect
    MEA_IMM         // #imm
} m_ea_mode_t;

typedef struct {
    uint8_t mode;   // m_ea_mode_t
    uint8_t reg;    // register number (for D/A/AIND/AINC/ADEC/ADISP/AIDX)
    int32_t disp;   // word/long displacement, brief or full (sign-extended)
    // Full extension word state (020). For brief format, idx fields apply
    // and bd/od sizes are zero with no indirection.
    uint8_t ireg;   // index register 0-7 = D, 8-15 = A
    uint8_t ilong;  // index size: 0 = sign-extended word, 1 = long
    uint8_t scale;  // 0-3
    uint8_t bdsz;   // raw BD size field (0 = brief/none, 1 = null, 2 = word, 3 = long)
    uint8_t odsz;   // outer displacement size: 0 = none, 1 = word, 2 = long
    uint8_t iis;    // indirect/index selection bits (full format)
    uint8_t bs;     // base suppress (full format)
    uint8_t isup;   // index suppress (full format)
    int32_t od;     // outer displacement (sign-extended)
    uint32_t imm;   // immediate value (for MEA_IMM)
    uint8_t immw;   // immediate extension words consumed (0/1/2)
} m_ea_t;

typedef enum {
    MOP_ILL = 0,
    MOP_ABCD, MOP_ADD, MOP_ADDA, MOP_ADDI, MOP_ADDQ, MOP_ADDX,
    MOP_AND, MOP_ANDI, MOP_ASL, MOP_ASR, MOP_BCC, MOP_BCHG, MOP_BCLR,
    MOP_BFCHG, MOP_BFCLR, MOP_BFEXTS, MOP_BFEXTU, MOP_BFFFO, MOP_BFINS,
    MOP_BFSET, MOP_BFTST, MOP_BKPT, MOP_BRA, MOP_BSET, MOP_BSR, MOP_BTST,
    MOP_CALLM, MOP_CAS, MOP_CAS2, MOP_CHK, MOP_CHK2, MOP_CLR, MOP_CMP,
    MOP_CMPA, MOP_CMPI, MOP_CMPM, MOP_CMP2, MOP_DBCC, MOP_DIVS, MOP_DIVU,
    MOP_DIVL, MOP_EOR, MOP_EORI, MOP_EXG, MOP_EXT, MOP_EXTB, MOP_ILLEGAL,
    MOP_JMP, MOP_JSR, MOP_LEA, MOP_LINK, MOP_LSL, MOP_LSR, MOP_MOVE,
    MOP_MOVEA, MOP_MOVEC, MOP_MOVEM, MOP_MOVEP, MOP_MOVEQ, MOP_MOVES,
    MOP_MULS, MOP_MULU, MOP_MULL, MOP_NBCD, MOP_NEG, MOP_NEGX, MOP_NOP,
    MOP_NOT, MOP_OR, MOP_ORI, MOP_PACK, MOP_PEA, MOP_RESET, MOP_ROL,
    MOP_ROR, MOP_ROXL, MOP_ROXR, MOP_RTD, MOP_RTE, MOP_RTM, MOP_RTR,
    MOP_RTS, MOP_SBCD, MOP_SCC, MOP_STOP, MOP_SUB, MOP_SUBA, MOP_SUBI,
    MOP_SUBQ, MOP_SUBX, MOP_SWAP, MOP_TAS, MOP_TRAP, MOP_TRAPCC,
    MOP_TRAPV, MOP_TST, MOP_UNLK, MOP_UNPK
} m_op_t;

// Control-flow classes, for block building.
#define MFLW_NONE 0
#define MFLW_BRANCH 1     // conditional branch (falls through too)
#define MFLW_JUMP 2       // unconditional: bra/jmp
#define MFLW_CALL 3       // bsr/jsr (returns)
#define MFLW_RET 4        // rts/rtr/rte/rtd(unlk? no)/trapcc-trap
#define MFLW_TRAP 5       // trap/trapv/illegal/etc: exception, may resume after
#define MFLW_DBCC 6       // dbcc: branch or fall through
#define MFLW_EXIT 7       // stop: leaves the run loop until an interrupt

typedef struct {
    uint16_t op;        // m_op_t
    uint8_t sz;         // MSZ_* (meaning per op; L for branches' displacement)
    uint8_t cc;         // condition code (bcc/dbcc/scc/trapcc)
    uint8_t r1, r2;     // registers / trap vector / movec code / bkpt code
    m_ea_t ea;          // first (source) operand
    m_ea_t eb;          // second (destination) operand, if any
    uint32_t imm;       // extra immediate (movem mask, quick value, link disp,
                        // cas2 second mask...); disp for branch/call (signed)
    uint32_t imm2;      // second extra (cas2 regs, pack adjust, ext widths)
    uint8_t len;        // instruction length in words
    uint8_t flow;       // MFLW_*
    uint8_t cycles;     // approximate 020 cycle count
} m_insn_t;

// ------------------------------------------------------------- memory access

static inline m68k_t *rr_cpu(void) { return rrm->cpu; }

// Request an exception from anywhere (interpreter or translated code).
// The run loop takes it before the next instruction/block.
static inline void rr_exc(uint32_t vec, uint32_t fault_pc) {
    m68k_t *c = rrm->cpu;
    if (!c->exc) {
        c->exc = (uint8_t)vec;
        c->exc_pc = fault_pc;
    }
}

// Forward: address-error recorder (defined with the exception entries).
static inline void rr_aerr(uint32_t pc, uint32_t ea, uint32_t is_read,
        uint32_t szcode);

static inline uint32_t rr_mrd8(uint32_t a) {
    a &= 0xffffffu;
    uint8_t *p = rrm->mrd[a >> 16];
    return p ? p[a & 0xffffu] : rrfns->mrd8(a);
}

static inline uint32_t rr_mrd16(uint32_t a, uint32_t pc) {
    a &= 0xffffffu;
    if (a & 1) { rr_aerr(pc, a, 1, 2); return 0; }
    uint8_t *p = rrm->mrd[a >> 16];
    if (p) {
        uint32_t o = a & 0xffffu;
        return ((uint32_t)p[o] << 8) | p[o + 1];
    }
    return rrfns->mrd16(a);
}

static inline uint32_t rr_mrd32(uint32_t a, uint32_t pc) {
    a &= 0xffffffu;
    if (a & 1) { rr_aerr(pc, a, 1, 0); return 0; }
    uint8_t *p = rrm->mrd[a >> 16];
    if (p) {
        uint32_t o = a & 0xffffu;
        return ((uint32_t)p[o] << 24) | ((uint32_t)p[o + 1] << 16) |
               ((uint32_t)p[o + 2] << 8) | p[o + 3];
    }
    return rrfns->mrd32(a);
}

// RR_WATCH_ADDR=addr: log every 68K write to that address (debug aid).
// Host-only (needs libc); translated modules compile this header with
// -nostdlib, so the helper compiles out unless RR_CORE_WATCH is defined.
#ifdef RR_CORE_WATCH
#include <stdio.h>
#include <stdlib.h>
static inline void rr_watch_addr(uint32_t a, uint32_t v, int sz, uint32_t pc) {
    static int winit = 0;
    static uint32_t watcha = 0xffffffffu;
    if (!winit) {
        const char *w = getenv("RR_WATCH_ADDR");
        winit = 1;
        if (w && w[0]) watcha = (uint32_t)strtoul(w, NULL, 0) & 0xffffffu;
    }
    if (a == watcha)
        fprintf(stderr, "watchw %06X sz=%d val=%08X cpu_pc=%06X\n",
                a, sz, v, pc & 0xffffffu);
}
#else
static inline void rr_watch_addr(uint32_t a, uint32_t v, int sz, uint32_t pc) {
    (void)a; (void)v; (void)sz; (void)pc;
}
#endif

static inline void rr_mwr8(uint32_t a, uint32_t v) {
    a &= 0xffffffu;
    rr_watch_addr(a, v, 1, rrm->cpu->pc);
    uint8_t *p = rrm->mwr[a >> 16];
    if (p) p[a & 0xffffu] = (uint8_t)v;
    else rrfns->mwr8(a, v);
}

static inline void rr_mwr16(uint32_t a, uint32_t v, uint32_t pc) {
    a &= 0xffffffu;
    if (a & 1) { rr_aerr(pc, a, 0, 2); return; }
    rr_watch_addr(a, v, 2, pc);
    uint8_t *p = rrm->mwr[a >> 16];
    if (p) {
        uint32_t o = a & 0xffffu;
        p[o] = (uint8_t)(v >> 8); p[o + 1] = (uint8_t)v;
    } else rrfns->mwr16(a, v);
}

static inline void rr_mwr32(uint32_t a, uint32_t v, uint32_t pc) {
    a &= 0xffffffu;
    if (a & 1) { rr_aerr(pc, a, 0, 0); return; }
    rr_watch_addr(a, v, 4, pc);
    uint8_t *p = rrm->mwr[a >> 16];
    if (p) {
        uint32_t o = a & 0xffffu;
        p[o] = (uint8_t)(v >> 24); p[o + 1] = (uint8_t)(v >> 16);
        p[o + 2] = (uint8_t)(v >> 8); p[o + 3] = (uint8_t)v;
    } else rrfns->mwr32(a, v);
}

// Fetch helpers for the interpreter (pc always even; odd pc is checked once
// per instruction by the run loop).
static inline uint32_t rr_fetch16(uint32_t a) {
    a &= 0xffffffu;
    uint8_t *p = rrm->mrd[a >> 16];
    if (p) {
        uint32_t o = a & 0xffffu;
        return ((uint32_t)p[o] << 8) | p[o + 1];
    }
    return rrfns->mrd16(a);
}

static inline uint32_t rr_fetch32(uint32_t a) {
    return (rr_fetch16(a) << 16) | rr_fetch16(a + 2);
}

// ------------------------------------------------------------- flags

static inline void rr_set_nz8(m68k_t *c, uint32_t r) {
    c->sr &= ~(M68K_SR_N | M68K_SR_Z | M68K_SR_V | M68K_SR_C);
    if (r & 0x80) c->sr |= M68K_SR_N;
    if (!(r & 0xff)) c->sr |= M68K_SR_Z;
}
static inline void rr_set_nz16(m68k_t *c, uint32_t r) {
    c->sr &= ~(M68K_SR_N | M68K_SR_Z | M68K_SR_V | M68K_SR_C);
    if (r & 0x8000) c->sr |= M68K_SR_N;
    if (!(r & 0xffff)) c->sr |= M68K_SR_Z;
}
static inline void rr_set_nz32(m68k_t *c, uint32_t r) {
    c->sr &= ~(M68K_SR_N | M68K_SR_Z | M68K_SR_V | M68K_SR_C);
    if (r & 0x80000000u) c->sr |= M68K_SR_N;
    if (!r) c->sr |= M68K_SR_Z;
}

// Condition codes, evaluated against CCR (+X unused here).
static inline uint32_t rr_cond(uint32_t sr, uint32_t cc) {
    uint32_t n = (sr & M68K_SR_N) != 0, z = (sr & M68K_SR_Z) != 0;
    uint32_t v = (sr & M68K_SR_V) != 0, co = (sr & M68K_SR_C) != 0;
    switch (cc) {
    case 0: return 1;                       // T
    case 1: return 0;                       // F
    case 2: return !co && !z;               // HI
    case 3: return co || z;                 // LS
    case 4: return !co;                     // CC
    case 5: return co;                      // CS
    case 6: return !z;                      // NE
    case 7: return z;                       // EQ
    case 8: return !v;                      // VC
    case 9: return v;                       // VS
    case 10: return !n;                     // PL
    case 11: return n;                      // MI
    case 12: return (n == v);               // GE
    case 13: return (n != v);               // LT
    case 14: return !z && (n == v);         // GT
    default: return z || (n != v);          // LE
    }
}

// Sync a[7] with the banked stack on a supervisor switch.
static inline void rr_sync_sp(m68k_t *c, uint32_t old_sr) {
    uint32_t was_s = old_sr & M68K_SR_S, is_s = c->sr & M68K_SR_S;
    uint32_t was_m = old_sr & M68K_SR_M, is_m = c->sr & M68K_SR_M;
    if (was_s && !is_s) {
        if (was_m) c->msp = c->a[7]; else c->isp = c->a[7];
        c->a[7] = c->usp;
    } else if (!was_s && is_s) {
        c->usp = c->a[7];
        c->a[7] = is_m ? c->msp : c->isp;
    } else if (was_s && is_s && (was_m != is_m)) {
        if (was_m) { c->msp = c->a[7]; c->a[7] = c->isp; }
        else { c->isp = c->a[7]; c->a[7] = c->msp; }
    }
}

// ------------------------------------------------------------- EA classes

#define ME_DATA 0x001u  // Dn
#define ME_A    0x002u  // An
#define ME_IND  0x004u  // (An)
#define ME_INC  0x008u  // (An)+
#define ME_DEC  0x010u  // -(An)
#define ME_DSP  0x020u  // (d16,An)
#define ME_IDX  0x040u  // (d8,An,Xn) / full
#define ME_ABW  0x080u  // (xxx).W
#define ME_ABL  0x100u  // (xxx).L
#define ME_PCD  0x200u  // (d16,PC)
#define ME_PCI  0x400u  // (d8,PC,Xn) / full
#define ME_IMM  0x800u  // #imm

#define MEC_DATA_ADDR (ME_DATA|ME_IND|ME_INC|ME_DEC|ME_DSP|ME_IDX|ME_ABW|ME_ABL|ME_PCD|ME_PCI|ME_IMM)
#define MEC_DATA_ADDRA (MEC_DATA_ADDR|ME_A) // ... plus An (ADDA/SUBA/CMPA)
#define MEC_MEM_ALT   (ME_IND|ME_INC|ME_DEC|ME_DSP|ME_IDX|ME_ABW|ME_ABL)
#define MEC_DATA_ALT  (ME_DATA|MEC_MEM_ALT)
#define MEC_CTRL      (ME_IND|ME_DSP|ME_IDX|ME_ABW|ME_ABL|ME_PCD|ME_PCI)
#define MEC_CTRL_ALT  (ME_IND|ME_DSP|ME_IDX|ME_ABW|ME_ABL)
#define MEC_ALL       0xfffu

static inline uint32_t m_ea_bit(uint32_t mode) { return 1u << mode; }

// Decode one EA's extension words. words[0..nwords) start at the first
// extension word; pc is the instruction address (PC base = pc + 2).
// Returns words consumed, -1 when more words are needed, or -2 when the
// EA encoding itself is malformed.
static inline int m_ea_decode(m_ea_t *e, uint32_t mode, uint32_t reg,
        const uint16_t *words, int nwords, uint32_t sz) {
    e->mode = (uint8_t)mode; e->reg = (uint8_t)reg;
    e->disp = 0; e->ireg = 0; e->ilong = 0; e->scale = 0;
    e->bdsz = 0; e->odsz = 0; e->iis = 0; e->od = 0;
    e->imm = 0; e->immw = 0; e->bs = 0; e->isup = 0;
    switch (mode) {
    case MEA_D: case MEA_A: case MEA_AIND: case MEA_AINC: case MEA_ADEC:
        return 0;
    case MEA_ADISP: case MEA_PCDISP:
        if (nwords < 1) return -1;
        e->disp = (int32_t)(int16_t)words[0];
        return 1;
    case MEA_AIDX: case MEA_PCIDX: {
        uint32_t xw;
        if (nwords < 1) return -1;
        xw = words[0];
        e->ireg = (uint8_t)(((xw >> 12) & 8) | ((xw >> 12) & 7));
        e->ilong = (uint8_t)((xw >> 11) & 1);
        e->scale = (uint8_t)((xw >> 9) & 3);
        if (!(xw & 0x100)) { // brief extension word
            e->disp = (int32_t)(int8_t)(xw & 0xff);
            return 1;
        }
        // Full extension word.
        if (xw & 8) return -2; // bit 3 must be zero
        e->bs = (uint8_t)((xw >> 7) & 1);
        e->isup = (uint8_t)((xw >> 6) & 1);
        e->bdsz = (uint8_t)((xw >> 4) & 3);
        e->iis = (uint8_t)(xw & 7);
        {
            int used = 1;
            if (e->bdsz == 2) {
                if (nwords < 2) return -1;
                e->disp = (int32_t)(int16_t)words[1]; used = 2;
            } else if (e->bdsz == 3) {
                if (nwords < 3) return -1;
                e->disp = (int32_t)(((uint32_t)words[1] << 16) | words[2]);
                used = 3;
            }
            switch (e->iis & 3) {
            case 2:
                if (nwords < used + 1) return -1;
                e->od = (int32_t)(int16_t)words[used]; e->odsz = 1; used++;
                break;
            case 3:
                if (nwords < used + 2) return -1;
                e->od = (int32_t)(((uint32_t)words[used] << 16) | words[used + 1]);
                e->odsz = 2; used += 2;
                break;
            default:
                break;
            }
            return used;
        }
    }
    case MEA_ABSW:
        if (nwords < 1) return -1;
        e->disp = (int32_t)(int16_t)words[0];
        return 1;
    case MEA_ABSL:
        if (nwords < 2) return -1;
        e->disp = (int32_t)(((uint32_t)words[0] << 16) | words[1]);
        return 2;
    case MEA_IMM:
        if (sz == MSZ_B) {
            if (nwords < 1) return -1;
            e->imm = words[0] & 0xff; e->immw = 1;
            return 1;
        } else if (sz == MSZ_W) {
            if (nwords < 1) return -1;
            e->imm = words[0]; e->immw = 1;
            return 1;
        } else {
            if (nwords < 2) return -1;
            e->imm = ((uint32_t)words[0] << 16) | words[1]; e->immw = 2;
            return 2;
        }
    }
    return -2;
}

// Map a 6-bit ea field to mode/reg. Returns 0 for the (xxx) forms' submodes.
static inline void m_ea_split(uint32_t eafield, uint32_t *mode, uint32_t *reg) {
    uint32_t m = (eafield >> 3) & 7, r = eafield & 7;
    if (m == 7) {
        static const uint8_t tab[8] = {
            MEA_ABSW, MEA_ABSL, MEA_PCDISP, MEA_PCIDX, MEA_IMM, 255, 255, 255
        };
        *mode = tab[r & 7]; *reg = 0;
    } else {
        *mode = m; *reg = r;
    }
}

// Decode one instruction. words[0..nwords) hold the instruction words;
// pc is the guest address of words[0]. Fills *o and returns the words
// consumed (1..11), or 0 if the words do not decode (caller ran out of
// words) — with op MOP_ILL. Invalid-EA forms decode as MOP_ILL too, with
// len covering the parsed extensions and imm = exception vector (4/10/11).
static inline int m68k_decode(uint32_t pc, const uint16_t *words, int nwords,
        m_insn_t *o) {
    uint32_t w, nib, x, y, s, eaf, mode = 0, reg = 0, dmode = 0, dreg = 0;
    int u1, u2;
    o->op = MOP_ILL; o->sz = MSZ_W; o->cc = 0; o->r1 = 0; o->r2 = 0;
    o->imm = 4; o->imm2 = 0; o->len = 1; o->flow = MFLW_TRAP; o->cycles = 4;
    o->ea.mode = MEA_D; o->ea.reg = 0; o->ea.imm = 0; o->ea.immw = 0;
    o->eb = o->ea;
    if (nwords < 1) { o->op = MOP_ILL; return 0; }
    w = words[0];
    nib = w >> 12;
    x = (w >> 9) & 7; y = (w >> 6) & 7; s = (w >> 6) & 3; eaf = w & 0x3f;

    switch (nib) {
    case 0x0: {
        if (w & 0x100) { // dynamic bit ops / MOVEP
            if (((w >> 3) & 7) == 1) { // MOVEP
                uint32_t dir = (w >> 7) & 1, l = (w >> 6) & 1;
                o->op = MOP_MOVEP; o->sz = l ? MSZ_L : MSZ_W;
                o->r1 = (uint8_t)x; o->r2 = (uint8_t)(w & 7);
                o->imm = dir; // 0 = (d,Ay)->Dx, 1 = Dx->(d,Ay)
                if (nwords < 2) { o->op = MOP_ILL; return 0; }
                o->imm2 = (uint32_t)(int32_t)(int16_t)words[1];
                o->len = 2; o->flow = MFLW_NONE; o->cycles = 8;
                return 2;
            }
            o->cc = (uint8_t)s; o->r1 = (uint8_t)x;
            o->op = (s == 0) ? MOP_BTST : (s == 1) ? MOP_BCHG :
                    (s == 2) ? MOP_BCLR : MOP_BSET;
            m_ea_split(eaf, &mode, &reg);
            if (mode > MEA_IMM) { o->op = MOP_ILL; return 1; }
            u1 = m_ea_decode(&o->ea, mode, reg, words + 1, nwords - 1, MSZ_B);
            if (u1 < 0) { if (u1 == -1) { o->op = MOP_ILL; return 0; } o->op = MOP_ILL; return 1; }
            o->len = (uint8_t)(1 + u1);
            {
                uint32_t allow = (s == 0) ? (MEC_DATA_ADDR & ~ME_A)
                                          : (MEC_DATA_ALT & ~ME_A);
                if (!(m_ea_bit(mode) & allow)) { o->op = MOP_ILL; o->imm = 4; o->flow = MFLW_TRAP; return o->len; }
            }
            o->flow = MFLW_NONE; o->cycles = 6;
            return o->len;
        }
        {
            uint32_t g = (w >> 8) & 15;
            if (s == 3 && g != 8) { // 020 specials (static BSET keeps s=3)
                switch (g) {
                case 0x0: case 0x2: case 0x4: // CHK2/CMP2.B/W/L
                    o->op = MOP_CHK2; o->sz = (g == 0) ? MSZ_B :
                            (g == 2) ? MSZ_W : MSZ_L;
                    if (nwords < 2) { o->op = MOP_ILL; return 0; }
                    o->imm = words[1]; // extension word
                    m_ea_split(eaf, &mode, &reg);
                    if (mode > MEA_IMM) { o->op = MOP_ILL; return 1; }
                    u1 = m_ea_decode(&o->ea, mode, reg, words + 2,
                            nwords - 2, o->sz);
                    if (u1 < 0) { if (u1 == -1) { o->op = MOP_ILL; return 0; } o->op = MOP_ILL; return 1; }
                    o->len = (uint8_t)(2 + u1);
                    if (!(m_ea_bit(mode) & MEC_CTRL)) { o->op = MOP_ILL; o->imm = 4; o->flow = MFLW_TRAP; return o->len; }
                    o->flow = MFLW_TRAP; o->cycles = 8;
                    return o->len;
                case 0x6: // CALLM / RTM
                    if (eaf < 0x10) { // 0000 0110 1100 Trrr
                        o->op = MOP_RTM; o->r1 = (uint8_t)(w & 7);
                        o->r2 = (uint8_t)((w >> 3) & 1);
                        o->len = 1; o->flow = MFLW_TRAP; return 1;
                    }
                    o->op = MOP_CALLM;
                    if (nwords < 2) { o->op = MOP_ILL; return 0; }
                    o->imm = words[1] & 0xff;
                    m_ea_split(eaf, &mode, &reg);
                    if (mode > MEA_IMM) { o->op = MOP_ILL; return 1; }
                    u1 = m_ea_decode(&o->ea, mode, reg, words + 2,
                            nwords - 2, MSZ_B);
                    if (u1 < 0) { if (u1 == -1) { o->op = MOP_ILL; return 0; } o->op = MOP_ILL; return 1; }
                    o->len = (uint8_t)(2 + u1);
                    if (!(m_ea_bit(mode) & MEC_CTRL)) { o->op = MOP_ILL; o->imm = 4; o->flow = MFLW_TRAP; return o->len; }
                    o->flow = MFLW_TRAP; return o->len;
                case 0xa: case 0xc: case 0xe: // CAS.B/W/L, CAS2.W/L
                    if (eaf == 0x3c) { // CAS2
                        if (g == 0xa) return 1; // no CAS2.B
                        o->op = MOP_CAS2; o->sz = (g == 0xc) ? MSZ_W : MSZ_L;
                        if (nwords < 3) { o->op = MOP_ILL; return 0; }
                        o->imm = words[1]; o->imm2 = words[2];
                        o->len = 3; o->flow = MFLW_NONE; o->cycles = 12;
                        return 3;
                    }
                    o->op = MOP_CAS; o->sz = (g == 0xa) ? MSZ_B :
                            (g == 0xc) ? MSZ_W : MSZ_L;
                    if (nwords < 2) { o->op = MOP_ILL; return 0; }
                    o->imm = words[1];
                    m_ea_split(eaf, &mode, &reg);
                    if (mode > MEA_IMM) { o->op = MOP_ILL; return 1; }
                    u1 = m_ea_decode(&o->ea, mode, reg, words + 2,
                            nwords - 2, o->sz);
                    if (u1 < 0) { if (u1 == -1) { o->op = MOP_ILL; return 0; } o->op = MOP_ILL; return 1; }
                    o->len = (uint8_t)(2 + u1);
                    if (!(m_ea_bit(mode) & MEC_MEM_ALT)) { o->op = MOP_ILL; o->imm = 4; o->flow = MFLW_TRAP; return o->len; }
                    o->flow = MFLW_NONE; o->cycles = 10;
                    return o->len;
                default:
                    return 1;
                }
            }
            if (g == 0xe) { // MOVES
                o->op = MOP_MOVES; o->sz = (uint8_t)s;
                if (nwords < 2) { o->op = MOP_ILL; return 0; }
                o->imm = words[1];
                m_ea_split(eaf, &mode, &reg);
                if (mode > MEA_IMM) { o->op = MOP_ILL; return 1; }
                u1 = m_ea_decode(&o->ea, mode, reg, words + 2, nwords - 2,
                        o->sz);
                if (u1 < 0) { if (u1 == -1) { o->op = MOP_ILL; return 0; } o->op = MOP_ILL; return 1; }
                o->len = (uint8_t)(2 + u1);
                if (!(m_ea_bit(mode) & MEC_MEM_ALT)) { o->op = MOP_ILL; o->imm = 4; o->flow = MFLW_TRAP; return o->len; }
                o->flow = MFLW_NONE; o->cycles = 8;
                return o->len;
            }
            if (g == 0x8) { // static bit ops
                o->op = (s == 0) ? MOP_BTST : (s == 1) ? MOP_BCHG :
                        (s == 2) ? MOP_BCLR : MOP_BSET;
                if (nwords < 2) { o->op = MOP_ILL; return 0; }
                o->r1 = 0x80 | (words[1] & 0xff); // static bit number
                m_ea_split(eaf, &mode, &reg);
                if (mode > MEA_IMM) { o->op = MOP_ILL; return 1; }
                u1 = m_ea_decode(&o->ea, mode, reg, words + 2, nwords - 2,
                        MSZ_B);
                if (u1 < 0) { if (u1 == -1) { o->op = MOP_ILL; return 0; } o->op = MOP_ILL; return 1; }
                o->len = (uint8_t)(2 + u1);
                {
                    uint32_t allow = (s == 0) ? (MEC_DATA_ADDR & ~ME_A)
                                              : (MEC_DATA_ALT & ~ME_A);
                    if (!(m_ea_bit(mode) & allow)) { o->op = MOP_ILL; o->imm = 4; o->flow = MFLW_TRAP; return o->len; }
                }
                o->flow = MFLW_NONE; o->cycles = 8;
                return o->len;
            }
            // ORI/ANDI/SUBI/ADDI/EORI/CMPI
            o->op = (g == 0x0) ? MOP_ORI : (g == 0x2) ? MOP_ANDI :
                    (g == 0x4) ? MOP_SUBI : (g == 0x6) ? MOP_ADDI :
                    (g == 0xa) ? MOP_EORI : (g == 0xc) ? MOP_CMPI : MOP_ILL;
            if (o->op == MOP_ILL) return 1;
            o->sz = (uint8_t)s;
            // ...to CCR/SR forms: ea == #imm
            if (eaf == 0x3c && (o->op == MOP_ORI || o->op == MOP_ANDI ||
                    o->op == MOP_EORI)) {
                if (s == 0) { o->r1 = 1; }          // to CCR
                else if (s == 1) { o->r1 = 2; }     // to SR
                else { o->op = MOP_ILL; return 1; }
                if (nwords < 2) { o->op = MOP_ILL; return 0; }
                o->imm = words[1];
                o->ea.mode = MEA_D; o->ea.reg = 0;
                o->len = 2; o->flow = MFLW_NONE; o->cycles = 8;
                return 2;
            }
            {
                int iw = (s == 2) ? 2 : 1;
                uint32_t ival;
                if (nwords < 1 + iw) { o->op = MOP_ILL; return 0; }
                ival = (iw == 2) ? (((uint32_t)words[1] << 16) | words[2])
                                 : words[1];
                if (s == 0) ival &= 0xff;
                else if (s == 1) ival &= 0xffff;
                m_ea_split(eaf, &mode, &reg);
                if (mode > MEA_IMM) { o->op = MOP_ILL; return 1; }
                u1 = m_ea_decode(&o->ea, mode, reg, words + 1 + iw,
                        nwords - 1 - iw, o->sz);
                if (u1 < 0) { if (u1 == -1) { o->op = MOP_ILL; return 0; } o->op = MOP_ILL; return 1; }
                o->imm = ival;
                o->len = (uint8_t)(1 + iw + u1);
                if (!(m_ea_bit(mode) & MEC_DATA_ALT))
                    return o->len;
                o->flow = MFLW_NONE; o->cycles = 8;
                return o->len;
            }
        }
    }
    case 0x1: case 0x2: case 0x3: { // MOVE / MOVEA
        uint32_t sz = (nib == 1) ? MSZ_B : (nib == 3) ? MSZ_W : MSZ_L;
        uint32_t seaf = eaf, deaf = ((w >> 3) & 0x38) | x;
        uint32_t smode, sreg;
        int siw;
        o->sz = (uint8_t)sz;
        m_ea_split(seaf, &smode, &sreg);
        if (smode > MEA_IMM) { o->op = MOP_ILL; return 1; }
        // Source immediate size follows the operation size.
        u1 = m_ea_decode(&o->ea, smode, sreg, words + 1, nwords - 1, sz);
        if (u1 < 0) { if (u1 == -1) { o->op = MOP_ILL; return 0; } o->op = MOP_ILL; return 1; }
        siw = u1;
        m_ea_split(deaf, &dmode, &dreg);
        if (dmode > MEA_IMM) { o->op = MOP_ILL; return 1; }
        u2 = m_ea_decode(&o->eb, dmode, dreg, words + 1 + siw,
                nwords - 1 - siw, sz);
        if (u2 < 0) { if (u2 == -1) { o->op = MOP_ILL; return 0; } o->op = MOP_ILL; return 1; }
        o->len = (uint8_t)(1 + siw + u2);
        if (smode == MEA_A) {
            if (sz == MSZ_B) return o->len; // MOVE.B from An is illegal
        } else if (!(m_ea_bit(smode) & MEC_DATA_ADDR)) { o->op = MOP_ILL; o->imm = 4; o->flow = MFLW_TRAP; return o->len; }
        if (dmode == MEA_A) {
            if (sz == MSZ_B) return o->len; // MOVE.B to An is illegal
            o->op = MOP_MOVEA;
            o->r1 = (uint8_t)dreg;
            o->flow = MFLW_NONE; o->cycles = 4;
            return o->len;
        }
        if (!(m_ea_bit(dmode) & MEC_DATA_ALT)) { o->op = MOP_ILL; o->imm = 4; o->flow = MFLW_TRAP; return o->len; }
        o->op = MOP_MOVE;
        o->flow = MFLW_NONE;
        o->cycles = (smode == MEA_D && dmode == MEA_D) ? 2 : 4;
        return o->len;
    }
    case 0x4: {
        uint32_t h = (w >> 8) & 15;
        switch (h) {
        case 0x0: // NEGX / MOVE from SR
            if (s == 3) { // MOVE from SR
                o->op = MOP_MOVE; o->sz = MSZ_W; o->r1 = 2; o->r2 = 1;
                m_ea_split(eaf, &mode, &reg);
                if (mode > MEA_IMM) { o->op = MOP_ILL; return 1; }
                u1 = m_ea_decode(&o->eb, mode, reg, words + 1, nwords - 1,
                        MSZ_W);
                if (u1 < 0) { if (u1 == -1) { o->op = MOP_ILL; return 0; } o->op = MOP_ILL; return 1; }
                o->len = (uint8_t)(1 + u1);
                if (!(m_ea_bit(mode) & MEC_DATA_ALT)) { o->op = MOP_ILL; o->imm = 4; o->flow = MFLW_TRAP; return o->len; }
                o->flow = MFLW_NONE; o->cycles = 6;
                return o->len;
            }
            o->op = MOP_NEGX; o->sz = (uint8_t)s;
            break;
        case 0x1: // CHK.L / CHK.W / LEA
            if (s == 1) return 1;
            if (s == 3) {
                o->op = MOP_LEA; o->sz = MSZ_L; o->r1 = (uint8_t)x;
                m_ea_split(eaf, &mode, &reg);
                if (mode > MEA_IMM) { o->op = MOP_ILL; return 1; }
                u1 = m_ea_decode(&o->ea, mode, reg, words + 1, nwords - 1,
                        MSZ_L);
                if (u1 < 0) { if (u1 == -1) { o->op = MOP_ILL; return 0; } o->op = MOP_ILL; return 1; }
                o->len = (uint8_t)(1 + u1);
                if (!(m_ea_bit(mode) & MEC_CTRL)) { o->op = MOP_ILL; o->imm = 4; o->flow = MFLW_TRAP; return o->len; }
                o->flow = MFLW_NONE; o->cycles = 4;
                return o->len;
            }
            o->op = MOP_CHK; o->sz = (s == 0) ? MSZ_L : MSZ_W;
            o->r1 = (uint8_t)x;
            break;
        case 0x2: // CLR / MOVE from CCR
            if (s == 3) {
                o->op = MOP_MOVE; o->sz = MSZ_W; o->r1 = 1; o->r2 = 1;
                m_ea_split(eaf, &mode, &reg);
                if (mode > MEA_IMM) { o->op = MOP_ILL; return 1; }
                u1 = m_ea_decode(&o->eb, mode, reg, words + 1, nwords - 1,
                        MSZ_W);
                if (u1 < 0) { if (u1 == -1) { o->op = MOP_ILL; return 0; } o->op = MOP_ILL; return 1; }
                o->len = (uint8_t)(1 + u1);
                if (!(m_ea_bit(mode) & MEC_DATA_ALT)) { o->op = MOP_ILL; o->imm = 4; o->flow = MFLW_TRAP; return o->len; }
                o->flow = MFLW_NONE; o->cycles = 6;
                return o->len;
            }
            o->op = MOP_CLR; o->sz = (uint8_t)s;
            break;
        case 0x4: // NEG / MOVE to CCR
            if (s == 3) {
                o->op = MOP_MOVE; o->sz = MSZ_W; o->r1 = 1; o->r2 = 2;
                m_ea_split(eaf, &mode, &reg);
                if (mode > MEA_IMM) { o->op = MOP_ILL; return 1; }
                u1 = m_ea_decode(&o->ea, mode, reg, words + 1, nwords - 1,
                        MSZ_W);
                if (u1 < 0) { if (u1 == -1) { o->op = MOP_ILL; return 0; } o->op = MOP_ILL; return 1; }
                o->len = (uint8_t)(1 + u1);
                if (!(m_ea_bit(mode) & MEC_DATA_ADDR)) { o->op = MOP_ILL; o->imm = 4; o->flow = MFLW_TRAP; return o->len; }
                o->flow = MFLW_NONE; o->cycles = 8;
                return o->len;
            }
            o->op = MOP_NEG; o->sz = (uint8_t)s;
            break;
        case 0x6: // NOT / MOVE to SR
            if (s == 3) {
                o->op = MOP_MOVE; o->sz = MSZ_W; o->r1 = 2; o->r2 = 2;
                m_ea_split(eaf, &mode, &reg);
                if (mode > MEA_IMM) { o->op = MOP_ILL; return 1; }
                u1 = m_ea_decode(&o->ea, mode, reg, words + 1, nwords - 1,
                        MSZ_W);
                if (u1 < 0) { if (u1 == -1) { o->op = MOP_ILL; return 0; } o->op = MOP_ILL; return 1; }
                o->len = (uint8_t)(1 + u1);
                if (!(m_ea_bit(mode) & MEC_DATA_ADDR)) { o->op = MOP_ILL; o->imm = 4; o->flow = MFLW_TRAP; return o->len; }
                o->flow = MFLW_NONE; o->cycles = 8;
                return o->len;
            }
            o->op = MOP_NOT; o->sz = (uint8_t)s;
            break;
        case 0x8: // NBCD/LINK.L/PEA/SWAP/BKPT/MOVEM/EXT
            if (s == 0) {
                if (((eaf >> 3) & 7) == 1) { // LINK.L
                    o->op = MOP_LINK; o->sz = MSZ_L; o->r1 = (uint8_t)(w & 7);
                    if (nwords < 3) { o->op = MOP_ILL; return 0; }
                    o->imm = ((uint32_t)words[1] << 16) | words[2];
                    o->len = 3; o->flow = MFLW_NONE; o->cycles = 8;
                    return 3;
                }
                o->op = MOP_NBCD; o->sz = MSZ_B;
                break;
            } else if (s == 1) {
                if (((eaf >> 3) & 7) == 0) { // SWAP
                    o->op = MOP_SWAP; o->r1 = (uint8_t)(w & 7);
                    o->len = 1; o->flow = MFLW_NONE; o->cycles = 4;
                    return 1;
                } else if (((eaf >> 3) & 7) == 1) { // BKPT
                    o->op = MOP_BKPT; o->r1 = (uint8_t)(w & 7);
                    o->len = 1; o->flow = MFLW_TRAP; o->cycles = 6;
                    return 1;
                }
                o->op = MOP_PEA; o->sz = MSZ_L;
                m_ea_split(eaf, &mode, &reg);
                if (mode > MEA_IMM) { o->op = MOP_ILL; return 1; }
                u1 = m_ea_decode(&o->ea, mode, reg, words + 1, nwords - 1,
                        MSZ_L);
                if (u1 < 0) { if (u1 == -1) { o->op = MOP_ILL; return 0; } o->op = MOP_ILL; return 1; }
                o->len = (uint8_t)(1 + u1);
                if (!(m_ea_bit(mode) & MEC_CTRL)) { o->op = MOP_ILL; o->imm = 4; o->flow = MFLW_TRAP; return o->len; }
                o->flow = MFLW_NONE; o->cycles = 6;
                return o->len;
            } else if (s == 2 || s == 3) {
                if (((eaf >> 3) & 7) == 0) { // EXT.W/L
                    o->op = MOP_EXT; o->sz = (s == 2) ? MSZ_W : MSZ_L;
                    o->r1 = (uint8_t)(w & 7);
                    o->len = 1; o->flow = MFLW_NONE; o->cycles = 4;
                    return 1;
                }
                o->op = MOP_MOVEM; o->sz = (s == 2) ? MSZ_W : MSZ_L;
                o->r1 = 0; // regs -> memory
                if (nwords < 2) { o->op = MOP_ILL; return 0; }
                o->imm = words[1];
                m_ea_split(eaf, &mode, &reg);
                if (mode > MEA_IMM) { o->op = MOP_ILL; return 1; }
                u1 = m_ea_decode(&o->ea, mode, reg, words + 2, nwords - 2,
                        o->sz);
                if (u1 < 0) { if (u1 == -1) { o->op = MOP_ILL; return 0; } o->op = MOP_ILL; return 1; }
                o->len = (uint8_t)(2 + u1);
                if (!(m_ea_bit(mode) & (MEC_CTRL_ALT | ME_DEC))) { o->op = MOP_ILL; o->imm = 4; o->flow = MFLW_TRAP; return o->len; }
                o->flow = MFLW_NONE;
                { // 020 MOVEM store: base 4 + 4/2 cycles per L/W reg.
                    uint32_t m = o->imm & 0xffffu, n = 0;
                    while (m) { n += m & 1u; m >>= 1; }
                    o->cycles = (uint8_t)(4 + n *
                            (o->sz == MSZ_L ? 4 : 2));
                }
                return o->len;
            }
            return 1;
        case 0x3: case 0x5: case 0x7: case 0xb: case 0xd: case 0xf:
            // CHK.L / CHK.W / LEA (same shape as case 0x1).
            if (s == 1) return 1;
            if (s == 3) {
                o->op = MOP_LEA; o->sz = MSZ_L; o->r1 = (uint8_t)x;
                m_ea_split(eaf, &mode, &reg);
                if (mode > MEA_IMM) { o->op = MOP_ILL; return 1; }
                u1 = m_ea_decode(&o->ea, mode, reg, words + 1, nwords - 1,
                        MSZ_L);
                if (u1 < 0) { if (u1 == -1) { o->op = MOP_ILL; return 0; } o->op = MOP_ILL; return 1; }
                o->len = (uint8_t)(1 + u1);
                if (!(m_ea_bit(mode) & MEC_CTRL)) { o->op = MOP_ILL; o->imm = 4; o->flow = MFLW_TRAP; return o->len; }
                o->flow = MFLW_NONE; o->cycles = 4;
                return o->len;
            }
            o->op = MOP_CHK; o->sz = (s == 0) ? MSZ_L : MSZ_W;
            o->r1 = (uint8_t)x;
            break;
        case 0x9: // EXTB / CHK / LEA
            if (s == 3 && ((eaf >> 3) & 7) == 0) {
                o->op = MOP_EXTB; o->r1 = (uint8_t)(w & 7);
                o->len = 1; o->flow = MFLW_NONE; o->cycles = 4;
                return 1;
            }
            if (s == 1) return 1;
            if (s == 3) {
                o->op = MOP_LEA; o->sz = MSZ_L; o->r1 = (uint8_t)x;
                m_ea_split(eaf, &mode, &reg);
                if (mode > MEA_IMM) { o->op = MOP_ILL; return 1; }
                u1 = m_ea_decode(&o->ea, mode, reg, words + 1, nwords - 1,
                        MSZ_L);
                if (u1 < 0) { if (u1 == -1) { o->op = MOP_ILL; return 0; } o->op = MOP_ILL; return 1; }
                o->len = (uint8_t)(1 + u1);
                if (!(m_ea_bit(mode) & MEC_CTRL)) { o->op = MOP_ILL; o->imm = 4; o->flow = MFLW_TRAP; return o->len; }
                o->flow = MFLW_NONE; o->cycles = 4;
                return o->len;
            }
            o->op = MOP_CHK; o->sz = (s == 0) ? MSZ_L : MSZ_W;
            o->r1 = (uint8_t)x;
            break;
        case 0xa: // TST / TAS / ILLEGAL
            if (s == 3) {
                if (eaf == 0x3c) { // ILLEGAL
                    o->op = MOP_ILLEGAL; o->imm = 4;
                    o->len = 1; o->flow = MFLW_TRAP; return 1;
                }
                o->op = MOP_TAS; o->sz = MSZ_B;
                break;
            }
            o->op = MOP_TST; o->sz = (uint8_t)s;
            m_ea_split(eaf, &mode, &reg);
            if (mode > MEA_IMM) { o->op = MOP_ILL; return 1; }
            u1 = m_ea_decode(&o->ea, mode, reg, words + 1, nwords - 1, o->sz);
            if (u1 < 0) { if (u1 == -1) { o->op = MOP_ILL; return 0; } o->op = MOP_ILL; return 1; }
            o->len = (uint8_t)(1 + u1);
            if (!(m_ea_bit(mode) & (MEC_DATA_ADDR & ~ME_A & ~ME_IMM &
                    ~ME_PCD & ~ME_PCI)))
                return o->len;
            o->flow = MFLW_NONE; o->cycles = 4;
            return o->len;
        case 0xc: // MULL / DIVL / MOVEM (memory -> regs)
            if (s == 0) {
                o->op = MOP_MULL; o->sz = MSZ_L;
                if (nwords < 2) { o->op = MOP_ILL; return 0; }
                o->imm = words[1];
                m_ea_split(eaf, &mode, &reg);
                if (mode > MEA_IMM) { o->op = MOP_ILL; return 1; }
                u1 = m_ea_decode(&o->ea, mode, reg, words + 2, nwords - 2,
                        MSZ_L);
                if (u1 < 0) { if (u1 == -1) { o->op = MOP_ILL; return 0; } o->op = MOP_ILL; return 1; }
                o->len = (uint8_t)(2 + u1);
                if (!(m_ea_bit(mode) & MEC_DATA_ADDR)) { o->op = MOP_ILL; o->imm = 4; o->flow = MFLW_TRAP; return o->len; }
                o->flow = MFLW_NONE; o->cycles = 40;
                return o->len;
            } else if (s == 1) {
                o->op = MOP_DIVL; o->sz = MSZ_L;
                if (nwords < 2) { o->op = MOP_ILL; return 0; }
                o->imm = words[1];
                m_ea_split(eaf, &mode, &reg);
                if (mode > MEA_IMM) { o->op = MOP_ILL; return 1; }
                u1 = m_ea_decode(&o->ea, mode, reg, words + 2, nwords - 2,
                        MSZ_L);
                if (u1 < 0) { if (u1 == -1) { o->op = MOP_ILL; return 0; } o->op = MOP_ILL; return 1; }
                o->len = (uint8_t)(2 + u1);
                if (!(m_ea_bit(mode) & MEC_DATA_ADDR)) { o->op = MOP_ILL; o->imm = 4; o->flow = MFLW_TRAP; return o->len; }
                o->flow = MFLW_TRAP; o->cycles = 80;
                return o->len;
            } else {
                o->op = MOP_MOVEM; o->sz = (s == 2) ? MSZ_W : MSZ_L;
                o->r1 = 1; // memory -> regs
                if (nwords < 2) { o->op = MOP_ILL; return 0; }
                o->imm = words[1];
                m_ea_split(eaf, &mode, &reg);
                if (mode > MEA_IMM) { o->op = MOP_ILL; return 1; }
                u1 = m_ea_decode(&o->ea, mode, reg, words + 2, nwords - 2,
                        o->sz);
                if (u1 < 0) { if (u1 == -1) { o->op = MOP_ILL; return 0; } o->op = MOP_ILL; return 1; }
                o->len = (uint8_t)(2 + u1);
                if (!(m_ea_bit(mode) & (MEC_CTRL | ME_INC))) { o->op = MOP_ILL; o->imm = 4; o->flow = MFLW_TRAP; return o->len; }
                o->flow = MFLW_NONE;
                { // 020 MOVEM load: base 8 + 4/2 cycles per L/W reg.
                    uint32_t m = o->imm & 0xffffu, n = 0;
                    while (m) { n += m & 1u; m >>= 1; }
                    o->cycles = (uint8_t)(8 + n *
                            (o->sz == MSZ_L ? 4 : 2));
                }
                return o->len;
            }
        case 0xe: { // TRAP/LINK/UNLK/MOVE USP/misc/MOVEC/JSR/JMP
            uint32_t lo = w & 0xff;
            if (lo < 0x40) return 1;
            if (lo < 0x50) { // TRAP
                o->op = MOP_TRAP; o->r1 = (uint8_t)(w & 15);
                o->len = 1; o->flow = MFLW_TRAP; o->cycles = 20;
                return 1;
            } else if (lo < 0x58) { // LINK.W
                o->op = MOP_LINK; o->sz = MSZ_W; o->r1 = (uint8_t)(w & 7);
                if (nwords < 2) { o->op = MOP_ILL; return 0; }
                o->imm = (uint32_t)(int32_t)(int16_t)words[1];
                o->len = 2; o->flow = MFLW_NONE; o->cycles = 5;
                return 2;
            } else if (lo < 0x60) { // UNLK
                o->op = MOP_UNLK; o->r1 = (uint8_t)(w & 7);
                o->len = 1; o->flow = MFLW_NONE; o->cycles = 6;
                return 1;
            } else if (lo < 0x70) { // MOVE USP
                o->op = MOP_MOVE; o->sz = MSZ_L; o->r1 = 3; // USP
                o->r2 = (lo & 8) ? 1 : 2; // 1 = USP->An, 2 = An->USP
                o->ea.mode = MEA_A; o->ea.reg = (uint8_t)(w & 7);
                o->eb = o->ea;
                o->len = 1; o->flow = MFLW_NONE; o->cycles = 4;
                return 1;
            }
            switch (lo) {
            case 0x70: o->op = MOP_RESET; break;
            case 0x71: o->op = MOP_NOP; break;
            case 0x72:
                o->op = MOP_STOP;
                if (nwords < 2) { o->op = MOP_ILL; return 0; }
                o->imm = words[1];
                o->len = 2; o->flow = MFLW_EXIT; o->cycles = 4;
                return 2;
            case 0x73: o->op = MOP_RTE; o->flow = MFLW_RET; break;
            case 0x74:
                o->op = MOP_RTD;
                if (nwords < 2) { o->op = MOP_ILL; return 0; }
                o->imm = (uint32_t)(int32_t)(int16_t)words[1];
                o->len = 2; o->flow = MFLW_RET; o->cycles = 8;
                return 2;
            case 0x75: o->op = MOP_RTS; o->flow = MFLW_RET;
                o->len = 1; o->cycles = 10; return 1;
            case 0x76: o->op = MOP_TRAPV; o->flow = MFLW_TRAP; break;
            case 0x77: o->op = MOP_RTR; o->flow = MFLW_RET; break;
            case 0x7a: case 0x7b:
                o->op = MOP_MOVEC; o->r1 = (uint8_t)(lo & 1);
                if (nwords < 2) { o->op = MOP_ILL; return 0; }
                o->imm = words[1];
                o->len = 2; o->flow = MFLW_NONE; o->cycles = 8;
                return 2;
            default:
                if (lo < 0x80) return 1;
                break;
            }
            if (o->op != MOP_ILL && o->op != MOP_STOP && o->op != MOP_MOVEC &&
                    o->op != MOP_RTD) {
                o->len = 1; o->cycles = 8;
                if (o->flow != MFLW_RET && o->flow != MFLW_TRAP)
                    o->flow = MFLW_NONE;
                return 1;
            }
            if (o->op == MOP_ILL && lo >= 0x80) {
                // JSR / JMP
                o->op = (lo < 0xc0) ? MOP_JSR : MOP_JMP; o->sz = MSZ_L;
                m_ea_split(eaf, &mode, &reg);
                if (mode > MEA_IMM) { o->op = MOP_ILL; return 1; }
                u1 = m_ea_decode(&o->ea, mode, reg, words + 1, nwords - 1,
                        MSZ_L);
                if (u1 < 0) { if (u1 == -1) { o->op = MOP_ILL; return 0; } o->op = MOP_ILL; return 1; }
                o->len = (uint8_t)(1 + u1);
                if (!(m_ea_bit(mode) & MEC_CTRL)) { o->op = MOP_ILL; o->imm = 4; o->flow = MFLW_TRAP; return o->len; }
                o->flow = (lo < 0xc0) ? MFLW_CALL : MFLW_JUMP; o->cycles = 6;
                return o->len;
            }
            return 1;
        }
        default:
            return 1;
        }
        // Shared single-EA tail for NEGX/CLR/NEG/NOT/CHK/NBCD/TAS.
        m_ea_split(eaf, &mode, &reg);
        if (mode > MEA_IMM) { o->op = MOP_ILL; return 1; }
        u1 = m_ea_decode(&o->ea, mode, reg, words + 1, nwords - 1, o->sz);
        if (u1 < 0) { if (u1 == -1) { o->op = MOP_ILL; return 0; } o->op = MOP_ILL; return 1; }
        o->len = (uint8_t)(1 + u1);
        {
            uint32_t allow = (o->op == MOP_CHK) ? MEC_DATA_ADDR : MEC_DATA_ALT;
            if (!(m_ea_bit(mode) & allow)) { o->op = MOP_ILL; o->imm = 4; o->flow = MFLW_TRAP; return o->len; }
        }
        o->flow = (o->op == MOP_CHK) ? MFLW_TRAP : MFLW_NONE;
        o->cycles = 6;
        return o->len;
    }
    case 0x5: { // ADDQ/SUBQ/Scc/DBcc/TRAPcc
        if (s == 3) {
            uint32_t cc = (w >> 8) & 15, lo = w & 0xff;
            if ((lo & 0xf8) == 0xc8) { // DBcc
                o->op = MOP_DBCC; o->cc = (uint8_t)cc;
                o->r1 = (uint8_t)(w & 7);
                if (nwords < 2) { o->op = MOP_ILL; return 0; }
                o->imm = (uint32_t)(int32_t)(int16_t)words[1];
                o->len = 2; o->flow = MFLW_DBCC; o->cycles = 6;
                return 2;
            } else if (lo == 0xfc || lo == 0xfa || lo == 0xfb) { // TRAPcc
                o->op = MOP_TRAPCC; o->cc = (uint8_t)cc;
                if (lo == 0xfa) {
                    if (nwords < 2) { o->op = MOP_ILL; return 0; }
                    o->imm = words[1]; o->len = 2;
                } else if (lo == 0xfb) {
                    if (nwords < 3) { o->op = MOP_ILL; return 0; }
                    o->imm = ((uint32_t)words[1] << 16) | words[2];
                    o->len = 3;
                } else {
                    o->len = 1;
                }
                o->flow = MFLW_TRAP; o->cycles = 6;
                return o->len;
            }
            o->op = MOP_SCC; o->cc = (uint8_t)cc; o->sz = MSZ_B;
            m_ea_split(eaf, &mode, &reg);
            if (mode > MEA_IMM) { o->op = MOP_ILL; return 1; }
            u1 = m_ea_decode(&o->ea, mode, reg, words + 1, nwords - 1, MSZ_B);
            if (u1 < 0) { if (u1 == -1) { o->op = MOP_ILL; return 0; } o->op = MOP_ILL; return 1; }
            o->len = (uint8_t)(1 + u1);
            if (!(m_ea_bit(mode) & (MEC_DATA_ALT & ~ME_A))) { o->op = MOP_ILL; o->imm = 4; o->flow = MFLW_TRAP; return o->len; }
            o->flow = MFLW_NONE; o->cycles = 6;
            return o->len;
        }
        o->op = (w & 0x100) ? MOP_SUBQ : MOP_ADDQ;
        o->sz = (uint8_t)s; o->r1 = (uint8_t)(x ? x : 8);
        m_ea_split(eaf, &mode, &reg);
        if (mode > MEA_IMM) { o->op = MOP_ILL; return 1; }
        u1 = m_ea_decode(&o->ea, mode, reg, words + 1, nwords - 1, o->sz);
        if (u1 < 0) { if (u1 == -1) { o->op = MOP_ILL; return 0; } o->op = MOP_ILL; return 1; }
        o->len = (uint8_t)(1 + u1);
        {
            uint32_t allow = (s == 0) ? MEC_DATA_ALT : (MEC_DATA_ALT | ME_A);
            if (!(m_ea_bit(mode) & allow)) { o->op = MOP_ILL; o->imm = 4; o->flow = MFLW_TRAP; return o->len; }
        }
        o->flow = MFLW_NONE;
        o->cycles = (mode == MEA_D || mode == MEA_A) ? 2 : 4;
        return o->len;
    }
    case 0x6: { // Bcc / BRA / BSR
        uint32_t cc = (w >> 8) & 15, d8 = w & 0xff;
        o->cc = (uint8_t)cc;
        o->op = (cc == 0) ? MOP_BRA : (cc == 1) ? MOP_BSR : MOP_BCC;
        if (d8 != 0 && d8 != 0xff) {
            o->imm = (uint32_t)(int32_t)(int8_t)d8; o->len = 1;
        } else if (d8 == 0) {
            if (nwords < 2) { o->op = MOP_ILL; return 0; }
            o->imm = (uint32_t)(int32_t)(int16_t)words[1]; o->len = 2;
        } else {
            if (nwords < 3) { o->op = MOP_ILL; return 0; }
            o->imm = ((uint32_t)words[1] << 16) | words[2]; o->len = 3;
        }
        o->flow = (cc == 0) ? MFLW_JUMP : (cc == 1) ? MFLW_CALL : MFLW_BRANCH;
        o->cycles = 6;
        return o->len;
    }
    case 0x7: { // MOVEQ
        if (w & 0x100) return 1;
        o->op = MOP_MOVEQ; o->sz = MSZ_L; o->r1 = (uint8_t)x;
        o->imm = (uint32_t)(int32_t)(int8_t)(w & 0xff);
        o->len = 1; o->flow = MFLW_NONE; o->cycles = 4;
        return 1;
    }
    case 0x8: case 0x9: case 0xb: case 0xc: case 0xd: {
        uint32_t dr = (w >> 8) & 1, em = (eaf >> 3) & 7;
        if (s == 3) { // DIV/MUL/ADDA/SUBA/CMPA
            o->r1 = (uint8_t)x;
            o->op = (nib == 0x8) ? (dr ? MOP_DIVS : MOP_DIVU) :
                    (nib == 0xc) ? (dr ? MOP_MULS : MOP_MULU) :
                    (nib == 0x9) ? MOP_SUBA :
                    (nib == 0xb) ? MOP_CMPA : MOP_ADDA;
            o->sz = (nib == 0x8 || nib == 0xc) ? MSZ_W : (dr ? MSZ_L : MSZ_W);
            m_ea_split(eaf, &mode, &reg);
            if (mode > MEA_IMM) { o->op = MOP_ILL; return 1; }
            u1 = m_ea_decode(&o->ea, mode, reg, words + 1, nwords - 1, o->sz);
            if (u1 < 0) { if (u1 == -1) { o->op = MOP_ILL; return 0; } o->op = MOP_ILL; return 1; }
            o->len = (uint8_t)(1 + u1);
            {
                // Address ops take An sources; DIV/MUL do not (MAME).
                uint32_t allow = (nib == 0x8 || nib == 0xc) ?
                        MEC_DATA_ADDR : MEC_DATA_ADDRA;
                if (!(m_ea_bit(mode) & allow)) { o->op = MOP_ILL; o->imm = 4; o->flow = MFLW_TRAP; return o->len; }
            }
            o->flow = (nib == 0x8) ? MFLW_TRAP : MFLW_NONE;
            o->cycles = (nib == 0x8) ? 60 : 8;
            return o->len;
        }
        o->sz = (uint8_t)s; o->r1 = (uint8_t)x; o->r2 = (uint8_t)(w & 7);
        if (dr && (em == 0 || em == 1)) {
            // Register/memory pairs: ABCD/SBCD/ADDX/SUBX/PACK/UNPK/EXG/CMPM.
            if (nib == 0xb) {
                if (em != 1) { /* EOR to Dn: use the RE path below */ }
                else o->op = MOP_CMPM;
            } else if (nib == 0xc && s == 1) {
                o->op = MOP_EXG; o->r2 |= (em == 1) ? 0x10 : 0x00;
            } else if (nib == 0xc && s == 2) {
                if (em != 1) { /* AND_RE_L to Dn: fall through */ }
                else { o->op = MOP_EXG; o->r2 |= 0x20; }
            } else {
                o->op = (nib == 0x8) ? (s == 0 ? MOP_SBCD :
                            (s == 1 ? MOP_PACK : MOP_UNPK)) :
                        (nib == 0x9) ? MOP_SUBX :
                        (nib == 0xc) ? (s == 0 ? MOP_ABCD : MOP_ILL) :
                        MOP_ADDX;
                if (o->op == MOP_ILL) return 1;
                o->r2 |= (em == 1) ? 0x10 : 0x00; // 0x10 = -(A)-(A) form
            }
            if (o->op == MOP_PACK || o->op == MOP_UNPK) {
                if (nwords < 2) { o->op = MOP_ILL; return 0; }
                o->imm = words[1];
                o->len = 2;
            } else if (o->op == MOP_EXG || o->op == MOP_CMPM ||
                    o->op == MOP_ABCD || o->op == MOP_SBCD ||
                    o->op == MOP_ADDX || o->op == MOP_SUBX) {
                o->len = 1;
            }
            if (o->op != MOP_ILL) {
                o->flow = MFLW_NONE; o->cycles = 6;
                return o->len;
            }
            // else: AND_RE_L to Dn etc: fall out to the RE path below
            o->op = MOP_ILL;
        }
        if (!dr) {
            o->op = (nib == 0x8) ? MOP_OR : (nib == 0x9) ? MOP_SUB :
                    (nib == 0xb) ? MOP_CMP : (nib == 0xc) ? MOP_AND : MOP_ADD;
            m_ea_split(eaf, &mode, &reg);
            if (mode > MEA_IMM) { o->op = MOP_ILL; return 1; }
            u1 = m_ea_decode(&o->ea, mode, reg, words + 1, nwords - 1, o->sz);
            if (u1 < 0) { if (u1 == -1) { o->op = MOP_ILL; return 0; } o->op = MOP_ILL; return 1; }
            o->len = (uint8_t)(1 + u1);
            if (mode == MEA_A) {
                // An source only for W/L CMP/ADD/SUB (not B, not logic).
                if (s == 0 || nib == 0x8 || nib == 0xc) return o->len;
            } else if (!(m_ea_bit(mode) & MEC_DATA_ADDR)) { o->op = MOP_ILL; o->imm = 4; o->flow = MFLW_TRAP; return o->len; }
            // 020 cache-hit: ea-to-Dn ALU ops retire in 2 cycles
            // (MAME counts 2 for register and memory sources alike).
            o->flow = MFLW_NONE;
            o->cycles = 2;
            return o->len;
        }
        // Register-to-memory forms (OR/AND/EOR and ADD/SUB).
        o->op = (nib == 0x8) ? MOP_OR : (nib == 0x9) ? MOP_SUB :
                (nib == 0xb) ? MOP_EOR : (nib == 0xc) ? MOP_AND : MOP_ADD;
        o->r2 = 0; // r1 = Dn source; ea is the destination
        m_ea_split(eaf, &mode, &reg);
        if (mode > MEA_IMM) { o->op = MOP_ILL; return 1; }
        u1 = m_ea_decode(&o->ea, mode, reg, words + 1, nwords - 1, o->sz);
        if (u1 < 0) { if (u1 == -1) { o->op = MOP_ILL; return 0; } o->op = MOP_ILL; return 1; }
        o->len = (uint8_t)(1 + u1);
        {
            // EOR allows Dn destinations; the rest need memory.
            uint32_t allow = (nib == 0xb) ? MEC_DATA_ALT : MEC_MEM_ALT;
            if (!(m_ea_bit(mode) & allow)) { o->op = MOP_ILL; o->imm = 4; o->flow = MFLW_TRAP; return o->len; }
        }
        o->imm = 0x80000000u; // flag: ea is the destination, r1 the source
        o->flow = MFLW_NONE;
        // EOR Dn,Dm retires in 2 cycles; memory destinations take 4
        // (MAME's 020 counts).
        o->cycles = (nib == 0xb && mode == MEA_D) ? 2 : 4;
        return o->len;
    }
    case 0xa: // Line-A trap
        o->op = MOP_ILL; o->imm = 10;
        o->len = 1; o->flow = MFLW_TRAP;
        return 1;
    case 0xe: { // shifts and bitfields
        if ((w & 0xf8c0) == 0xe8c0) { // bitfield
            uint32_t bf = (w >> 8) & 7;
            o->op = (bf == 0) ? MOP_BFTST : (bf == 1) ? MOP_BFEXTU :
                    (bf == 2) ? MOP_BFCHG : (bf == 3) ? MOP_BFEXTS :
                    (bf == 4) ? MOP_BFCLR : (bf == 5) ? MOP_BFFFO :
                    (bf == 6) ? MOP_BFSET : MOP_BFINS;
            if (nwords < 2) { o->op = MOP_ILL; return 0; }
            o->imm = words[1];
            m_ea_split(eaf, &mode, &reg);
            if (mode > MEA_IMM) { o->op = MOP_ILL; return 1; }
            u1 = m_ea_decode(&o->ea, mode, reg, words + 2, nwords - 2, MSZ_B);
            if (u1 < 0) { if (u1 == -1) { o->op = MOP_ILL; return 0; } o->op = MOP_ILL; return 1; }
            o->len = (uint8_t)(2 + u1);
            {
                uint32_t altering = (bf == 2 || bf == 4 || bf == 6 || bf == 7);
                uint32_t allow = altering ? (ME_DATA | MEC_CTRL_ALT)
                        : (ME_DATA | MEC_CTRL_ALT | ME_PCD | ME_PCI);
                if (!(m_ea_bit(mode) & allow)) { o->op = MOP_ILL; o->imm = 4; o->flow = MFLW_TRAP; return o->len; }
            }
            o->flow = MFLW_NONE; o->cycles = 10;
            return o->len;
        }
        if (s == 3 && ((w >> 11) & 1) == 0) { // memory shift (word)
            uint32_t type = (w >> 9) & 3, dr = (w >> 8) & 1;
            o->op = (type == 0) ? (dr ? MOP_ASL : MOP_ASR) :
                    (type == 1) ? (dr ? MOP_LSL : MOP_LSR) :
                    (type == 2) ? (dr ? MOP_ROXL : MOP_ROXR) :
                                  (dr ? MOP_ROL : MOP_ROR);
            o->sz = MSZ_W; o->r1 = 0x80; // memory form, count = 1
            o->imm = 1;
            m_ea_split(eaf, &mode, &reg);
            if (mode > MEA_IMM) { o->op = MOP_ILL; return 1; }
            u1 = m_ea_decode(&o->ea, mode, reg, words + 1, nwords - 1,
                    MSZ_W);
            if (u1 < 0) { if (u1 == -1) { o->op = MOP_ILL; return 0; } o->op = MOP_ILL; return 1; }
            o->len = (uint8_t)(1 + u1);
            if (!(m_ea_bit(mode) & MEC_MEM_ALT)) { o->op = MOP_ILL; o->imm = 4; o->flow = MFLW_TRAP; return o->len; }
            o->flow = MFLW_NONE; o->cycles = 6;
            return o->len;
        }
        { // register shift/rotate (s != 3 here; s == 3 went above)
            uint32_t type = (w >> 3) & 3, dr = (w >> 8) & 1;
            o->op = (type == 0) ? (dr ? MOP_ASL : MOP_ASR) :
                    (type == 1) ? (dr ? MOP_LSL : MOP_LSR) :
                    (type == 2) ? (dr ? MOP_ROXL : MOP_ROXR) :
                                  (dr ? MOP_ROL : MOP_ROR);
            o->sz = (uint8_t)s; o->r1 = (uint8_t)(w & 7);
            o->imm = (w & 0x20) ? (0x100u | x) : (x ? x : 8);
            o->len = 1; o->flow = MFLW_NONE;
            // MAME's 020 counts logical shifts at 4 cycles.
            o->cycles = (type == 1) ? 4 : 6;
            return 1;
        }
    }
    case 0xf: // Line-F trap (no FPU/MMU on this board)
        o->op = MOP_ILL; o->imm = 11;
        o->len = 1; o->flow = MFLW_TRAP;
        return 1;
    default:
        break;
    }
    (void)pc; (void)y; (void)dmode; (void)dreg;
    return 1;
}

// ------------------------------------------------------------- EA evaluation

static inline uint32_t rr_index_val(m68k_t *c, const m_ea_t *e) {
    uint32_t v = (e->ireg & 8) ? c->a[e->ireg & 7] : c->d[e->ireg & 7];
    if (!e->ilong) v = (uint32_t)(int32_t)(int16_t)v;
    return v << e->scale;
}

// Compute the memory address for an EA. No register updates (the caller
// handles -(An)/(An)+ for data access; control EAs never use them).
static inline uint32_t rr_ea_addr(m68k_t *c, const m_ea_t *e, uint32_t pc) {
    switch (e->mode) {
    case MEA_AIND: case MEA_AINC: case MEA_ADEC:
        return c->a[e->reg];
    case MEA_ADISP:
        return c->a[e->reg] + (uint32_t)e->disp;
    case MEA_AIDX: case MEA_PCIDX: {
        uint32_t base;
        if (e->mode == MEA_AIDX)
            base = e->bs ? 0 : c->a[e->reg];
        else
            base = e->bs ? 0 : pc + 2;
        if (e->bdsz == 0) // brief format
            return base + (uint32_t)e->disp + rr_index_val(c, e);
        {
            uint32_t idx = e->isup ? 0 : rr_index_val(c, e);
            uint32_t tmp = base + (uint32_t)e->disp;
            uint32_t iis = e->iis;
            if (iis == 0 || iis == 4) // no indirection (100 reserved)
                return tmp + idx;
            if (iis & 4) { // postindexed: @(tmp) + idx + od
                uint32_t addr = rr_mrd32(tmp, pc);
                return addr + idx + (uint32_t)e->od;
            }
            // preindexed: @(tmp + idx) + od
            {
                uint32_t addr = rr_mrd32(tmp + idx, pc);
                return addr + (uint32_t)e->od;
            }
        }
    }
    case MEA_ABSW: case MEA_ABSL:
        return (uint32_t)e->disp;
    case MEA_PCDISP:
        return pc + 2 + (uint32_t)e->disp;
    default:
        return 0;
    }
}

static inline uint32_t rr_ainc_step(m68k_t *c, const m_ea_t *e, uint32_t sz) {
    uint32_t st = (sz == MSZ_B) ? 1 : (sz == MSZ_W) ? 2 : 4;
    if (sz == MSZ_B && e->reg == 7) st = 2; // A7 stays even
    (void)c;
    return st;
}

static inline uint32_t rr_ear(m68k_t *c, const m_ea_t *e, uint32_t sz,
        uint32_t pc) {
    switch (e->mode) {
    case MEA_D:
        return (sz == MSZ_B) ? (c->d[e->reg] & 0xff) :
               (sz == MSZ_W) ? (c->d[e->reg] & 0xffff) : c->d[e->reg];
    case MEA_A:
        return (sz == MSZ_W) ? (c->a[e->reg] & 0xffff) : c->a[e->reg];
    case MEA_AIND:
        return (sz == MSZ_B) ? rr_mrd8(c->a[e->reg]) :
               (sz == MSZ_W) ? rr_mrd16(c->a[e->reg], pc) :
                                rr_mrd32(c->a[e->reg], pc);
    case MEA_AINC: {
        uint32_t a = c->a[e->reg], v;
        v = (sz == MSZ_B) ? rr_mrd8(a) :
            (sz == MSZ_W) ? rr_mrd16(a, pc) : rr_mrd32(a, pc);
        c->a[e->reg] = a + rr_ainc_step(c, e, sz);
        return v;
    }
    case MEA_ADEC: {
        uint32_t a = c->a[e->reg] - rr_ainc_step(c, e, sz);
        c->a[e->reg] = a;
        return (sz == MSZ_B) ? rr_mrd8(a) :
               (sz == MSZ_W) ? rr_mrd16(a, pc) : rr_mrd32(a, pc);
    }
    case MEA_IMM:
        return e->imm;
    default: {
        uint32_t a = rr_ea_addr(c, e, pc);
        if (c->exc) return 0;
        return (sz == MSZ_B) ? rr_mrd8(a) :
               (sz == MSZ_W) ? rr_mrd16(a, pc) : rr_mrd32(a, pc);
    }
    }
}

static inline void rr_eaw(m68k_t *c, const m_ea_t *e, uint32_t sz,
        uint32_t pc, uint32_t v) {
    switch (e->mode) {
    case MEA_D:
        if (sz == MSZ_B) c->d[e->reg] = (c->d[e->reg] & ~0xffu) | (v & 0xff);
        else if (sz == MSZ_W)
            c->d[e->reg] = (c->d[e->reg] & ~0xffffu) | (v & 0xffff);
        else c->d[e->reg] = v;
        return;
    case MEA_A:
        if (sz == MSZ_W) c->a[e->reg] = (uint32_t)(int32_t)(int16_t)v;
        else c->a[e->reg] = v;
        return;
    case MEA_AIND:
        if (sz == MSZ_B) rr_mwr8(c->a[e->reg], v);
        else if (sz == MSZ_W) rr_mwr16(c->a[e->reg], v, pc);
        else rr_mwr32(c->a[e->reg], v, pc);
        return;
    case MEA_AINC: {
        uint32_t a = c->a[e->reg];
        if (sz == MSZ_B) rr_mwr8(a, v);
        else if (sz == MSZ_W) rr_mwr16(a, v, pc);
        else rr_mwr32(a, v, pc);
        c->a[e->reg] = a + rr_ainc_step(c, e, sz);
        return;
    }
    case MEA_ADEC: {
        uint32_t a = c->a[e->reg] - rr_ainc_step(c, e, sz);
        c->a[e->reg] = a;
        if (sz == MSZ_B) rr_mwr8(a, v);
        else if (sz == MSZ_W) rr_mwr16(a, v, pc);
        else rr_mwr32(a, v, pc);
        return;
    }
    default: {
        uint32_t a = rr_ea_addr(c, e, pc);
        if (c->exc) return;
        if (sz == MSZ_B) rr_mwr8(a, v);
        else if (sz == MSZ_W) rr_mwr16(a, v, pc);
        else rr_mwr32(a, v, pc);
        return;
    }
    }
}

// Write-back half of a read-modify-write: the EA was already consumed by
// rr_ear (side effects applied once), so this must NOT re-apply them.
// AINC: ear bumped An past the operand; write to the pre-bump address.
// ADEC: ear already decremented An; write in place. All other modes have
// no side effects, so plain rr_eaw is equivalent.
static inline void rr_eaw_rmw(m68k_t *c, const m_ea_t *e, uint32_t sz,
        uint32_t pc, uint32_t v) {
    uint32_t a;
    switch (e->mode) {
    case MEA_AINC:
        a = c->a[e->reg] - rr_ainc_step(c, e, sz);
        break;
    case MEA_ADEC:
        a = c->a[e->reg];
        break;
    default:
        rr_eaw(c, e, sz, pc, v);
        return;
    }
    if (sz == MSZ_B) rr_mwr8(a, v);
    else if (sz == MSZ_W) rr_mwr16(a, v, pc);
    else rr_mwr32(a, v, pc);
}

// ------------------------------------------------------------- ALU helpers

static inline uint32_t rr_add8(m68k_t *c, uint32_t d, uint32_t s) {
    uint32_t r = (d & 0xff) + (s & 0xff);
    c->sr &= ~(M68K_SR_X | M68K_SR_N | M68K_SR_Z | M68K_SR_V | M68K_SR_C);
    if (r & 0x100) c->sr |= M68K_SR_X | M68K_SR_C;
    if (r & 0x80) c->sr |= M68K_SR_N;
    if (!(r & 0xff)) c->sr |= M68K_SR_Z;
    if (((d ^ r) & (s ^ r) & 0x80)) c->sr |= M68K_SR_V;
    return r & 0xff;
}
static inline uint32_t rr_add16(m68k_t *c, uint32_t d, uint32_t s) {
    uint32_t r = (d & 0xffff) + (s & 0xffff);
    c->sr &= ~(M68K_SR_X | M68K_SR_N | M68K_SR_Z | M68K_SR_V | M68K_SR_C);
    if (r & 0x10000) c->sr |= M68K_SR_X | M68K_SR_C;
    if (r & 0x8000) c->sr |= M68K_SR_N;
    if (!(r & 0xffff)) c->sr |= M68K_SR_Z;
    if (((d ^ r) & (s ^ r) & 0x8000)) c->sr |= M68K_SR_V;
    return r & 0xffff;
}
static inline uint32_t rr_add32(m68k_t *c, uint32_t d, uint32_t s) {
    uint64_t r = (uint64_t)d + s;
    c->sr &= ~(M68K_SR_X | M68K_SR_N | M68K_SR_Z | M68K_SR_V | M68K_SR_C);
    if (r >> 32) c->sr |= M68K_SR_X | M68K_SR_C;
    if (r & 0x80000000u) c->sr |= M68K_SR_N;
    if (!(uint32_t)r) c->sr |= M68K_SR_Z;
    if (((d ^ r) & (s ^ r) & 0x80000000u)) c->sr |= M68K_SR_V;
    return (uint32_t)r;
}
static inline uint32_t rr_sub8(m68k_t *c, uint32_t d, uint32_t s) {
    uint32_t r = (d & 0xff) - (s & 0xff);
    c->sr &= ~(M68K_SR_X | M68K_SR_N | M68K_SR_Z | M68K_SR_V | M68K_SR_C);
    if (r & 0x100) c->sr |= M68K_SR_X | M68K_SR_C;
    if (r & 0x80) c->sr |= M68K_SR_N;
    if (!(r & 0xff)) c->sr |= M68K_SR_Z;
    if (((d ^ s) & (d ^ r) & 0x80)) c->sr |= M68K_SR_V;
    return r & 0xff;
}
static inline uint32_t rr_sub16(m68k_t *c, uint32_t d, uint32_t s) {
    uint32_t r = (d & 0xffff) - (s & 0xffff);
    c->sr &= ~(M68K_SR_X | M68K_SR_N | M68K_SR_Z | M68K_SR_V | M68K_SR_C);
    if (r & 0x10000) c->sr |= M68K_SR_X | M68K_SR_C;
    if (r & 0x8000) c->sr |= M68K_SR_N;
    if (!(r & 0xffff)) c->sr |= M68K_SR_Z;
    if (((d ^ s) & (d ^ r) & 0x8000)) c->sr |= M68K_SR_V;
    return r & 0xffff;
}
static inline uint32_t rr_sub32(m68k_t *c, uint32_t d, uint32_t s) {
    uint32_t r = d - s;
    c->sr &= ~(M68K_SR_X | M68K_SR_N | M68K_SR_Z | M68K_SR_V | M68K_SR_C);
    if (s > d) c->sr |= M68K_SR_X | M68K_SR_C;
    if (r & 0x80000000u) c->sr |= M68K_SR_N;
    if (!r) c->sr |= M68K_SR_Z;
    if (((d ^ s) & (d ^ r) & 0x80000000u)) c->sr |= M68K_SR_V;
    return r;
}
// CMP: like SUB but X is untouched.
static inline void rr_cmp8(m68k_t *c, uint32_t d, uint32_t s) {
    uint32_t r = (d & 0xff) - (s & 0xff);
    c->sr &= ~(M68K_SR_N | M68K_SR_Z | M68K_SR_V | M68K_SR_C);
    if (r & 0x100) c->sr |= M68K_SR_C;
    if (r & 0x80) c->sr |= M68K_SR_N;
    if (!(r & 0xff)) c->sr |= M68K_SR_Z;
    if (((d ^ s) & (d ^ r) & 0x80)) c->sr |= M68K_SR_V;
}
static inline void rr_cmp16(m68k_t *c, uint32_t d, uint32_t s) {
    uint32_t r = (d & 0xffff) - (s & 0xffff);
    c->sr &= ~(M68K_SR_N | M68K_SR_Z | M68K_SR_V | M68K_SR_C);
    if (r & 0x10000) c->sr |= M68K_SR_C;
    if (r & 0x8000) c->sr |= M68K_SR_N;
    if (!(r & 0xffff)) c->sr |= M68K_SR_Z;
    if (((d ^ s) & (d ^ r) & 0x8000)) c->sr |= M68K_SR_V;
}
static inline void rr_cmp32(m68k_t *c, uint32_t d, uint32_t s) {
    uint32_t r = d - s;
    c->sr &= ~(M68K_SR_N | M68K_SR_Z | M68K_SR_V | M68K_SR_C);
    if (s > d) c->sr |= M68K_SR_C;
    if (r & 0x80000000u) c->sr |= M68K_SR_N;
    if (!r) c->sr |= M68K_SR_Z;
    if (((d ^ s) & (d ^ r) & 0x80000000u)) c->sr |= M68K_SR_V;
}
static inline uint32_t rr_addx8(m68k_t *c, uint32_t d, uint32_t s) {
    uint32_t x = (c->sr & M68K_SR_X) ? 1 : 0;
    uint32_t r = (d & 0xff) + (s & 0xff) + x, z = (c->sr & M68K_SR_Z);
    c->sr &= ~(M68K_SR_X | M68K_SR_N | M68K_SR_V | M68K_SR_C);
    if (r & 0x100) c->sr |= M68K_SR_X | M68K_SR_C;
    if (r & 0x80) c->sr |= M68K_SR_N;
    if ((r & 0xff) || !z) c->sr &= ~M68K_SR_Z; else c->sr |= M68K_SR_Z;
    if (((d ^ r) & (s ^ r) & 0x80)) c->sr |= M68K_SR_V;
    return r & 0xff;
}
static inline uint32_t rr_addx16(m68k_t *c, uint32_t d, uint32_t s) {
    uint32_t x = (c->sr & M68K_SR_X) ? 1 : 0;
    uint32_t r = (d & 0xffff) + (s & 0xffff) + x, z = (c->sr & M68K_SR_Z);
    c->sr &= ~(M68K_SR_X | M68K_SR_N | M68K_SR_V | M68K_SR_C);
    if (r & 0x10000) c->sr |= M68K_SR_X | M68K_SR_C;
    if (r & 0x8000) c->sr |= M68K_SR_N;
    if ((r & 0xffff) || !z) c->sr &= ~M68K_SR_Z; else c->sr |= M68K_SR_Z;
    if (((d ^ r) & (s ^ r) & 0x8000)) c->sr |= M68K_SR_V;
    return r & 0xffff;
}
static inline uint32_t rr_addx32(m68k_t *c, uint32_t d, uint32_t s) {
    uint64_t r = (uint64_t)d + s + ((c->sr & M68K_SR_X) ? 1 : 0);
    uint32_t z = (c->sr & M68K_SR_Z);
    c->sr &= ~(M68K_SR_X | M68K_SR_N | M68K_SR_V | M68K_SR_C);
    if (r >> 32) c->sr |= M68K_SR_X | M68K_SR_C;
    if (r & 0x80000000u) c->sr |= M68K_SR_N;
    if ((uint32_t)r || !z) c->sr &= ~M68K_SR_Z; else c->sr |= M68K_SR_Z;
    if (((d ^ r) & (s ^ r) & 0x80000000u)) c->sr |= M68K_SR_V;
    return (uint32_t)r;
}
static inline uint32_t rr_subx8(m68k_t *c, uint32_t d, uint32_t s) {
    uint32_t x = (c->sr & M68K_SR_X) ? 1 : 0;
    uint32_t r = (d & 0xff) - (s & 0xff) - x, z = (c->sr & M68K_SR_Z);
    c->sr &= ~(M68K_SR_X | M68K_SR_N | M68K_SR_V | M68K_SR_C);
    if (r & 0x100) c->sr |= M68K_SR_X | M68K_SR_C;
    if (r & 0x80) c->sr |= M68K_SR_N;
    if ((r & 0xff) || !z) c->sr &= ~M68K_SR_Z; else c->sr |= M68K_SR_Z;
    if (((d ^ s) & (d ^ r) & 0x80)) c->sr |= M68K_SR_V;
    return r & 0xff;
}
static inline uint32_t rr_subx16(m68k_t *c, uint32_t d, uint32_t s) {
    uint32_t x = (c->sr & M68K_SR_X) ? 1 : 0;
    uint32_t r = (d & 0xffff) - (s & 0xffff) - x, z = (c->sr & M68K_SR_Z);
    c->sr &= ~(M68K_SR_X | M68K_SR_N | M68K_SR_V | M68K_SR_C);
    if (r & 0x10000) c->sr |= M68K_SR_X | M68K_SR_C;
    if (r & 0x8000) c->sr |= M68K_SR_N;
    if ((r & 0xffff) || !z) c->sr &= ~M68K_SR_Z; else c->sr |= M68K_SR_Z;
    if (((d ^ s) & (d ^ r) & 0x8000)) c->sr |= M68K_SR_V;
    return r & 0xffff;
}
static inline uint32_t rr_subx32(m68k_t *c, uint32_t d, uint32_t s) {
    uint32_t x = (c->sr & M68K_SR_X) ? 1 : 0;
    uint32_t r = d - s - x, z = (c->sr & M68K_SR_Z);
    c->sr &= ~(M68K_SR_X | M68K_SR_N | M68K_SR_V | M68K_SR_C);
    if ((uint64_t)s + x > d) c->sr |= M68K_SR_X | M68K_SR_C;
    if (r & 0x80000000u) c->sr |= M68K_SR_N;
    if (r || !z) c->sr &= ~M68K_SR_Z; else c->sr |= M68K_SR_Z;
    if (((d ^ s) & (d ^ r) & 0x80000000u)) c->sr |= M68K_SR_V;
    return r;
}

// ------------------------------------------------------------- shifts

static inline uint32_t rr_mask_w(uint32_t w) {
    return (w == 32) ? 0xffffffffu : ((1u << w) - 1);
}

static inline uint32_t rr_asl(m68k_t *c, uint32_t v, uint32_t sh, uint32_t w) {
    uint32_t m = rr_mask_w(w);
    v &= m;
    c->sr &= ~(M68K_SR_N | M68K_SR_Z | M68K_SR_V | M68K_SR_C);
    if (sh == 0) {
        if (v & (1u << (w - 1))) c->sr |= M68K_SR_N;
        if (!v) c->sr |= M68K_SR_Z;
        return v; // X unchanged
    }
    if (sh < w) {
        uint32_t r = (v << sh) & m, top = m << (w - sh - 1), t = v & top;
        if ((v >> (w - sh)) & 1) c->sr |= M68K_SR_X | M68K_SR_C;
        else c->sr &= ~M68K_SR_X;
        if (r & (1u << (w - 1))) c->sr |= M68K_SR_N;
        if (!r) c->sr |= M68K_SR_Z;
        if (t && t != (top & m)) c->sr |= M68K_SR_V;
        return r;
    }
    if (sh == w && (v & 1)) c->sr |= M68K_SR_X | M68K_SR_C;
    else c->sr &= ~M68K_SR_X;
    c->sr |= M68K_SR_Z;
    if (v) c->sr |= M68K_SR_V;
    return 0;
}

static inline uint32_t rr_asr(m68k_t *c, uint32_t v, uint32_t sh, uint32_t w) {
    uint32_t m = rr_mask_w(w), sign;
    v &= m; sign = (v >> (w - 1)) & 1;
    c->sr &= ~(M68K_SR_N | M68K_SR_Z | M68K_SR_V | M68K_SR_C);
    if (sh == 0) {
        if (sign) c->sr |= M68K_SR_N;
        if (!v) c->sr |= M68K_SR_Z;
        return v;
    }
    if (sh < w) {
        uint32_t r = (v >> sh) | (sign ? (m << (w - sh)) & m : 0);
        if ((v >> (sh - 1)) & 1) c->sr |= M68K_SR_X | M68K_SR_C;
        else c->sr &= ~M68K_SR_X;
        if (r & (1u << (w - 1))) c->sr |= M68K_SR_N;
        if (!r) c->sr |= M68K_SR_Z;
        return r;
    }
    if (sign) {
        c->sr |= M68K_SR_X | M68K_SR_C | M68K_SR_N;
        return m;
    }
    c->sr &= ~M68K_SR_X;
    c->sr |= M68K_SR_Z;
    return 0;
}

static inline uint32_t rr_lsl(m68k_t *c, uint32_t v, uint32_t sh, uint32_t w) {
    uint32_t m = rr_mask_w(w);
    v &= m;
    c->sr &= ~(M68K_SR_N | M68K_SR_Z | M68K_SR_V | M68K_SR_C);
    if (sh == 0) {
        if (v & (1u << (w - 1))) c->sr |= M68K_SR_N;
        if (!v) c->sr |= M68K_SR_Z;
        return v;
    }
    if (sh < w) {
        uint32_t r = (v << sh) & m;
        if ((v >> (w - sh)) & 1) c->sr |= M68K_SR_X | M68K_SR_C;
        else c->sr &= ~M68K_SR_X;
        if (r & (1u << (w - 1))) c->sr |= M68K_SR_N;
        if (!r) c->sr |= M68K_SR_Z;
        return r;
    }
    if (sh == w && (v & 1)) c->sr |= M68K_SR_X | M68K_SR_C;
    else c->sr &= ~M68K_SR_X;
    c->sr |= M68K_SR_Z;
    return 0;
}

static inline uint32_t rr_lsr(m68k_t *c, uint32_t v, uint32_t sh, uint32_t w) {
    uint32_t m = rr_mask_w(w);
    v &= m;
    c->sr &= ~(M68K_SR_N | M68K_SR_Z | M68K_SR_V | M68K_SR_C);
    if (sh == 0) {
        if (v & (1u << (w - 1))) c->sr |= M68K_SR_N;
        if (!v) c->sr |= M68K_SR_Z;
        return v;
    }
    if (sh < w) {
        uint32_t r = v >> sh;
        if ((v >> (sh - 1)) & 1) c->sr |= M68K_SR_X | M68K_SR_C;
        else c->sr &= ~M68K_SR_X;
        if (!r) c->sr |= M68K_SR_Z;
        return r;
    }
    if (sh == w && (v & (1u << (w - 1)))) c->sr |= M68K_SR_X | M68K_SR_C;
    else c->sr &= ~M68K_SR_X;
    c->sr |= M68K_SR_Z;
    return 0;
}

static inline uint32_t rr_rol(m68k_t *c, uint32_t v, uint32_t sh, uint32_t w) {
    uint32_t m = rr_mask_w(w);
    v &= m;
    c->sr &= ~(M68K_SR_N | M68K_SR_Z | M68K_SR_V | M68K_SR_C);
    if (sh == 0) {
        if (v & (1u << (w - 1))) c->sr |= M68K_SR_N;
        if (!v) c->sr |= M68K_SR_Z;
        return v;
    } else {
        uint32_t s = sh & (w - 1), r;
        r = (s == 0) ? v : (((v << s) | (v >> (w - s))) & m);
        if ((v >> ((w - s) & (w - 1))) & 1) c->sr |= M68K_SR_C;
        if (r & (1u << (w - 1))) c->sr |= M68K_SR_N;
        if (!r) c->sr |= M68K_SR_Z;
        return r;
    }
}

static inline uint32_t rr_ror(m68k_t *c, uint32_t v, uint32_t sh, uint32_t w) {
    uint32_t m = rr_mask_w(w);
    v &= m;
    c->sr &= ~(M68K_SR_N | M68K_SR_Z | M68K_SR_V | M68K_SR_C);
    if (sh == 0) {
        if (v & (1u << (w - 1))) c->sr |= M68K_SR_N;
        if (!v) c->sr |= M68K_SR_Z;
        return v;
    } else {
        uint32_t s = sh & (w - 1), r;
        r = (s == 0) ? v : (((v >> s) | (v << (w - s))) & m);
        if ((v >> ((s - 1) & (w - 1))) & 1) c->sr |= M68K_SR_C;
        if (r & (1u << (w - 1))) c->sr |= M68K_SR_N;
        if (!r) c->sr |= M68K_SR_Z;
        return r;
    }
}

static inline uint32_t rr_roxl(m68k_t *c, uint32_t v, uint32_t sh, uint32_t w) {
    uint32_t m = rr_mask_w(w);
    v &= m;
    c->sr &= ~(M68K_SR_N | M68K_SR_Z | M68K_SR_V | M68K_SR_C);
    if (sh == 0) {
        if (c->sr & M68K_SR_X) c->sr |= M68K_SR_C;
        if (v & (1u << (w - 1))) c->sr |= M68K_SR_N;
        if (!v) c->sr |= M68K_SR_Z;
        return v;
    } else {
        uint32_t s = sh % (w + 1), r, nxc;
        uint32_t xb = (c->sr & M68K_SR_X) ? 1 : 0;
        if (w == 32) {
            uint64_t big = (uint64_t)v | ((uint64_t)xb << 32);
            big = (s == 0) ? big :
                ((big << s) | (big >> (33 - s))) & 0x1ffffffffull;
            r = (uint32_t)big & m; nxc = (uint32_t)(big >> 32) & 1;
        } else {
            uint32_t wm = (1u << (w + 1)) - 1;
            uint32_t val = v | (xb << w);
            r = (s == 0) ? val : ((val << s) | (val >> (w + 1 - s))) & wm;
            nxc = (r >> w) & 1; r &= m;
        }
        c->sr &= ~M68K_SR_X;
        if (nxc) c->sr |= M68K_SR_X | M68K_SR_C;
        if (r & (1u << (w - 1))) c->sr |= M68K_SR_N;
        if (!r) c->sr |= M68K_SR_Z;
        return r;
    }
}

static inline uint32_t rr_roxr(m68k_t *c, uint32_t v, uint32_t sh, uint32_t w) {
    uint32_t m = rr_mask_w(w);
    v &= m;
    c->sr &= ~(M68K_SR_N | M68K_SR_Z | M68K_SR_V | M68K_SR_C);
    if (sh == 0) {
        if (c->sr & M68K_SR_X) c->sr |= M68K_SR_C;
        if (v & (1u << (w - 1))) c->sr |= M68K_SR_N;
        if (!v) c->sr |= M68K_SR_Z;
        return v;
    } else {
        uint32_t s = sh % (w + 1), r, nxc;
        uint32_t xb = (c->sr & M68K_SR_X) ? 1 : 0;
        if (w == 32) {
            uint64_t big = (uint64_t)v | ((uint64_t)xb << 32);
            big = (s == 0) ? big :
                ((big >> s) | (big << (33 - s))) & 0x1ffffffffull;
            r = (uint32_t)big & m; nxc = (uint32_t)(big >> 32) & 1;
        } else {
            uint32_t wm = (1u << (w + 1)) - 1;
            uint32_t val = v | (xb << w);
            r = (s == 0) ? val : ((val >> s) | (val << (w + 1 - s))) & wm;
            nxc = (r >> w) & 1; r &= m;
        }
        c->sr &= ~M68K_SR_X;
        if (nxc) c->sr |= M68K_SR_X | M68K_SR_C;
        if (r & (1u << (w - 1))) c->sr |= M68K_SR_N;
        if (!r) c->sr |= M68K_SR_Z;
        return r;
    }
}

// ------------------------------------------------------------- BCD helpers

static inline uint32_t rr_abcd(m68k_t *c, uint32_t dst, uint32_t src) {
    uint32_t res = (src & 0xf) + (dst & 0xf) +
        ((c->sr & M68K_SR_X) ? 1 : 0);
    uint32_t corf = (res > 9) ? 6 : 0, v1;
    res += (src & 0xf0) + (dst & 0xf0);
    v1 = res;
    res += corf;
    c->sr &= ~(M68K_SR_X | M68K_SR_N | M68K_SR_V | M68K_SR_C);
    if (res > 0x9f) {
        res -= 0xa0;
        c->sr |= M68K_SR_X | M68K_SR_C;
    }
    if ((~v1 & res) & 0x80) c->sr |= M68K_SR_V;
    if (res & 0x80) c->sr |= M68K_SR_N;
    res &= 0xff;
    if (res) c->sr &= ~M68K_SR_Z; // Z sticky: cleared if nonzero
    return res;
}

static inline uint32_t rr_sbcd(m68k_t *c, uint32_t dst, uint32_t src) {
    uint32_t res = (dst & 0xf) - (src & 0xf) -
        ((c->sr & M68K_SR_X) ? 1 : 0);
    uint32_t corf = (res > 9) ? 6 : 0, v1;
    res += (dst & 0xf0) - (src & 0xf0);
    v1 = res;
    res -= corf;
    c->sr &= ~(M68K_SR_X | M68K_SR_N | M68K_SR_V | M68K_SR_C);
    if (res > 0x9f) {
        res += 0xa0;
        c->sr |= M68K_SR_X | M68K_SR_C;
    }
    if ((~v1 & res) & 0x80) c->sr |= M68K_SR_V;
    if (res & 0x80) c->sr |= M68K_SR_N;
    res &= 0xff;
    if (res) c->sr &= ~M68K_SR_Z;
    return res;
}

static inline uint32_t rr_nbcd(m68k_t *c, uint32_t dst) {
    uint32_t res = 0u - (dst & 0xff) - ((c->sr & M68K_SR_X) ? 1 : 0);
    if (res != 0) {
        uint32_t v1 = res;
        if (((res | dst) & 0x0f) == 0) res = (res & 0xf0) | 6;
        res = (res + 0x9a) & 0xff;
        c->sr &= ~(M68K_SR_X | M68K_SR_N | M68K_SR_V | M68K_SR_C);
        if ((v1 & ~res) & 0x80) c->sr |= M68K_SR_V;
        if (res & 0x80) c->sr |= M68K_SR_N;
        if (res) c->sr &= ~M68K_SR_Z;
        c->sr |= M68K_SR_X | M68K_SR_C;
        return res;
    }
    c->sr &= ~(M68K_SR_X | M68K_SR_N | M68K_SR_V | M68K_SR_C);
    return dst & 0xff; // unchanged (was 0, X was 0)
}

// ------------------------------------------------------------- stack

static inline void rr_push16(m68k_t *c, uint32_t v, uint32_t pc) {
    c->a[7] -= 2;
    rr_mwr16(c->a[7], v, pc);
}
static inline void rr_push32(m68k_t *c, uint32_t v, uint32_t pc) {
    c->a[7] -= 4;
    rr_mwr32(c->a[7], v, pc);
}
static inline uint32_t rr_pop16(m68k_t *c, uint32_t pc) {
    uint32_t v = rr_mrd16(c->a[7], pc);
    c->a[7] += 2;
    return v;
}
static inline uint32_t rr_pop32(m68k_t *c, uint32_t pc) {
    uint32_t v = rr_mrd32(c->a[7], pc);
    c->a[7] += 4;
    return v;
}

// ------------------------------------------------------------- exceptions

// Record an address error from a data access (020 short frame content).
static inline void rr_aerr(uint32_t pc, uint32_t ea, uint32_t is_read,
        uint32_t szcode) {
    m68k_t *c = rrm->cpu;
    if (c->exc) return;
    c->exc = 3;
    c->exc_pc = pc;
    c->exc_ea = ea;
    {
        uint32_t fc = (c->sr & M68K_SR_S) ? 5 : 1;
        c->exc_ssw = (uint16_t)(0x0100 | fc | (is_read << 6) |
                ((szcode & 3) << 4));
    }
}

static inline void rr_enter_exc(m68k_t *c, uint32_t vec, uint32_t fault_pc,
        uint32_t frame_fmt, uint32_t frame_pc) {
    uint32_t old_sr = c->sr, new_pc;
    // Supervisor mode, trace off. M is cleared after the real frame.
    c->sr |= M68K_SR_S;
    c->sr &= ~(M68K_SR_T | 0x4000u);
    rr_sync_sp(c, old_sr);
    if (frame_fmt == 2) { // group-2: SR, PC, fmt/vo, fault address
        rr_push32(c, fault_pc, fault_pc);
        rr_push16(c, 0x2000 | ((vec << 2) & 0xfff), fault_pc);
        rr_push32(c, frame_pc, fault_pc);
        rr_push16(c, old_sr & 0xffff, fault_pc);
    } else { // format 0: SR, PC, fmt/vo
        rr_push16(c, (vec << 2) & 0xffff, fault_pc);
        rr_push32(c, frame_pc, fault_pc);
        rr_push16(c, old_sr & 0xffff, fault_pc);
    }
    if (old_sr & M68K_SR_M) { // throwaway frame on the interrupt stack
        uint32_t msp_done = c->a[7];
        c->msp = msp_done;
        c->sr &= ~M68K_SR_M;
        c->a[7] = c->isp;
        rr_push16(c, 0x1000 | ((vec << 2) & 0xfff), fault_pc);
        rr_push32(c, frame_pc, fault_pc);
        rr_push16(c, (old_sr | M68K_SR_S) & 0xffff, fault_pc);
    }
    new_pc = rr_mrd32(c->vbr + (vec << 2), fault_pc);
    c->pc = new_pc & 0xffffffu;
}

// Interrupt entry: autovector 24+level, mask raised, format-0 frame.
static inline void rr_enter_irq(m68k_t *c, uint32_t level, uint32_t fault_pc) {
    uint32_t vec = 24 + level, old_sr = c->sr, new_pc;
    new_pc = rr_mrd32(c->vbr + (vec << 2), fault_pc);
    if (!new_pc) // uninitialised vector
        new_pc = rr_mrd32(c->vbr + (15 << 2), fault_pc);
    c->sr |= M68K_SR_S;
    c->sr &= ~(M68K_SR_T | 0x4000u);
    c->sr = (c->sr & ~M68K_SR_IMASK) | ((level << 8) & M68K_SR_IMASK);
    rr_sync_sp(c, old_sr);
    rr_push16(c, (vec << 2) & 0xffff, fault_pc);
    rr_push32(c, c->pc, fault_pc);
    rr_push16(c, old_sr & 0xffff, fault_pc);
    if (old_sr & M68K_SR_M) {
        c->msp = c->a[7];
        c->sr &= ~M68K_SR_M;
        c->a[7] = c->isp;
        rr_push16(c, 0x1000 | ((vec << 2) & 0xfff), fault_pc);
        rr_push32(c, c->pc, fault_pc);
        rr_push16(c, (old_sr | M68K_SR_S) & 0xffff, fault_pc);
    }
    c->pc = new_pc & 0xffffffu;
}

// Address error: 020 format-A (short) frame. Content past SR/PC/fmt is
// best-effort; only the format word matters to RTE.
static inline void rr_enter_aerr(m68k_t *c, uint32_t fault_pc, uint32_t npc) {
    uint32_t old_sr = c->sr, new_pc;
    c->sr |= M68K_SR_S;
    c->sr &= ~(M68K_SR_T | 0x4000u);
    rr_sync_sp(c, old_sr);
    rr_push16(c, 0, fault_pc); // internal
    rr_push16(c, 0, fault_pc); // internal
    rr_push32(c, 0, fault_pc); // data output buffer (unknown)
    rr_push16(c, 0, fault_pc); // internal
    rr_push16(c, 0, fault_pc); // internal
    rr_push32(c, c->exc_ea, fault_pc); // fault address
    rr_push16(c, 0, fault_pc); // instruction pipe stage B (unknown)
    rr_push16(c, 0, fault_pc); // instruction pipe stage C (unknown)
    rr_push16(c, c->exc_ssw, fault_pc); // SSW
    rr_push16(c, 0, fault_pc); // internal
    rr_push16(c, 0xa000 | 0x00c, fault_pc); // fmt A, vector 3
    rr_push32(c, npc, fault_pc);
    rr_push16(c, old_sr & 0xffff, fault_pc);
    if (old_sr & M68K_SR_M) {
        c->msp = c->a[7];
        c->sr &= ~M68K_SR_M;
        c->a[7] = c->isp;
        rr_push16(c, 0x1000 | 0x00c, fault_pc);
        rr_push32(c, npc, fault_pc);
        rr_push16(c, (old_sr | M68K_SR_S) & 0xffff, fault_pc);
    }
    new_pc = rr_mrd32(c->vbr + (3 << 2), fault_pc);
    c->pc = new_pc & 0xffffffu;
}

// Take a pending exception recorded in c->exc (with c->exc_pc = fault).
// npc is the resume address (already in c->pc).
static inline void rr_take_exc(m68k_t *c, uint32_t fault_pc) {
    uint32_t vec = c->exc;
    c->exc = 0;
    if (vec == 3) {
        rr_enter_aerr(c, fault_pc, c->pc);
        return;
    }
    // Group-2 traps (format 2): CHK/CHK2(6), TRAPV/TRAPcc(7), DIV0(5),
    // trace(9). Group-1/0 and TRAP #n use format 0.
    if (vec == 5 || vec == 6 || vec == 7 || vec == 9) {
        rr_enter_exc(c, vec, fault_pc, 2, c->pc);
        return;
    }
    if (vec >= 32 && vec < 48) { // TRAP #n: resume after the instruction
        rr_enter_exc(c, vec, fault_pc, 0, c->pc);
        return;
    }
    // Privilege/line-A/line-F/illegal/format: PC = the faulting instruction.
    rr_enter_exc(c, vec, fault_pc, 0, fault_pc);
}

// ------------------------------------------------------------- execute
//
// Execute one decoded instruction. c->pc holds the next instruction's
// address (npc) on entry; control flow overwrites it. pc is this
// instruction's address (PC-relative base and exception record).

static inline void rr_exec_alu(m68k_t *c, const m_insn_t *o, uint32_t pc);
static inline void rr_exec_cf(m68k_t *c, const m_insn_t *o, uint32_t pc);

static inline void rr_exec(m68k_t *c, const m_insn_t *o, uint32_t pc) {
    uint32_t sz = o->sz, v;
    switch (o->op) {
    case MOP_ILL: case MOP_ILLEGAL:
        rr_exc(o->imm & 0xff, pc);
        return;
    case MOP_NOP:
        return;
    case MOP_MOVE: {
        if (o->r1 == 0) { // normal move
            v = rr_ear(c, &o->ea, sz, pc);
            if (c->exc) return;
            if (sz == MSZ_B) rr_set_nz8(c, v);
            else if (sz == MSZ_W) rr_set_nz16(c, v);
            else rr_set_nz32(c, v);
            rr_eaw(c, &o->eb, sz, pc, v);
            return;
        }
        if (o->r1 == 1 && o->r2 == 1) { // MOVE from CCR
            rr_eaw(c, &o->eb, MSZ_W, pc, c->sr & 0xff);
            return;
        }
        if (o->r1 == 1) { // MOVE to CCR
            v = rr_ear(c, &o->ea, MSZ_W, pc);
            if (c->exc) return;
            c->sr = (uint16_t)((c->sr & 0xff00) | (v & 0xff));
            return;
        }
        if (!(c->sr & M68K_SR_S)) { rr_exc(8, pc); return; }
        if (o->r1 == 2 && o->r2 == 1) { // MOVE from SR
            rr_eaw(c, &o->eb, MSZ_W, pc, c->sr);
            return;
        }
        if (o->r1 == 2) { // MOVE to SR
            uint32_t old;
            v = rr_ear(c, &o->ea, MSZ_W, pc);
            if (c->exc) return;
            old = c->sr;
            c->sr = (uint16_t)(v & 0xf71f);
            rr_sync_sp(c, old);
            return;
        }
        // MOVE USP (r1 == 3)
        if (o->r2 == 1) c->a[o->ea.reg] = c->usp; // USP -> An
        else c->usp = c->a[o->ea.reg]; // An -> USP
        return;
    }
    case MOP_MOVEA:
        v = rr_ear(c, &o->ea, sz, pc);
        if (c->exc) return;
        if (sz == MSZ_W) c->a[o->r1] = (uint32_t)(int32_t)(int16_t)v;
        else c->a[o->r1] = v;
        return;
    case MOP_MOVEQ:
        c->d[o->r1] = o->imm;
        rr_set_nz32(c, o->imm);
        return;
    case MOP_MOVEM: {
        uint32_t mask = o->imm, i, addr;
        if (o->r1 == 0) { // regs -> memory
            if (o->ea.mode == MEA_ADEC) {
                addr = c->a[o->ea.reg];
                for (i = 0; i < 16; i++) {
                    if (mask & (1u << i)) {
                        uint32_t rv = (15 - i < 8) ? c->d[15 - i] :
                                c->a[15 - i - 8];
                        if (sz == MSZ_W) {
                            addr -= 2;
                            rr_mwr16(addr, rv, pc);
                        } else {
                            addr -= 4;
                            // Long stores write low word first (Musashi).
                            rr_mwr16(addr + 2, rv, pc);
                            if (c->exc) return;
                            rr_mwr16(addr, rv >> 16, pc);
                        }
                        if (c->exc) return;
                    }
                }
                c->a[o->ea.reg] = addr;
            } else {
                addr = rr_ea_addr(c, &o->ea, pc);
                if (c->exc) return;
                for (i = 0; i < 16; i++) {
                    if (mask & (1u << i)) {
                        uint32_t rv = (i < 8) ? c->d[i] : c->a[i - 8];
                        if (sz == MSZ_W) {
                            rr_mwr16(addr, rv, pc); addr += 2;
                        } else {
                            rr_mwr32(addr, rv, pc); addr += 4;
                        }
                        if (c->exc) return;
                    }
                }
            }
        } else { // memory -> regs
            if (o->ea.mode == MEA_AINC) {
                addr = c->a[o->ea.reg];
                for (i = 0; i < 16; i++) {
                    if (mask & (1u << i)) {
                        if (sz == MSZ_W) {
                            v = rr_mrd16(addr, pc); addr += 2;
                            v = (uint32_t)(int32_t)(int16_t)v;
                        } else {
                            v = rr_mrd32(addr, pc); addr += 4;
                        }
                        if (c->exc) return;
                        if (i < 8) c->d[i] = v; else c->a[i - 8] = v;
                    }
                }
                // An writeback wins over a loaded value (Musashi).
                c->a[o->ea.reg] = addr;
            } else {
                addr = rr_ea_addr(c, &o->ea, pc);
                if (c->exc) return;
                for (i = 0; i < 16; i++) {
                    if (mask & (1u << i)) {
                        if (sz == MSZ_W) {
                            v = rr_mrd16(addr, pc); addr += 2;
                            v = (uint32_t)(int32_t)(int16_t)v;
                        } else {
                            v = rr_mrd32(addr, pc); addr += 4;
                        }
                        if (c->exc) return;
                        if (i < 8) c->d[i] = v; else c->a[i - 8] = v;
                    }
                }
            }
        }
        return;
    }
    case MOP_MOVEP: {
        uint32_t addr = c->a[o->r2] + o->imm2, b0, b1, b2, b3;
        if (o->imm == 0) { // (d,Ay) -> Dx
            b0 = rr_mrd8(addr); b1 = rr_mrd8(addr + 2);
            if (sz == MSZ_W) {
                c->d[o->r1] = (c->d[o->r1] & ~0xffffu) | (b0 << 8) | b1;
            } else {
                b2 = rr_mrd8(addr + 4); b3 = rr_mrd8(addr + 6);
                c->d[o->r1] = (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
            }
        } else { // Dx -> (d,Ay)
            v = c->d[o->r1];
            if (sz == MSZ_W) {
                rr_mwr8(addr, v >> 8); rr_mwr8(addr + 2, v);
            } else {
                rr_mwr8(addr, v >> 24); rr_mwr8(addr + 2, v >> 16);
                rr_mwr8(addr + 4, v >> 8); rr_mwr8(addr + 6, v);
            }
        }
        return;
    }
    case MOP_MOVES: {
        uint32_t ext = o->imm, rn = (ext >> 12) & 7, is_a = (ext >> 15) & 1;
        if (!(c->sr & M68K_SR_S)) { rr_exc(8, pc); return; }
        // No address translation here: SFC/DFC select the same memory.
        if (ext & 0x800) { // Rn -> ea
            v = is_a ? c->a[rn] : c->d[rn];
            rr_eaw(c, &o->ea, sz, pc, v);
        } else { // ea -> Rn
            v = rr_ear(c, &o->ea, sz, pc);
            if (c->exc) return;
            if (is_a) {
                c->a[rn] = (sz == MSZ_L) ? v :
                    (uint32_t)(int32_t)(sz == MSZ_W ? (int16_t)v :
                        (int8_t)v);
            } else {
                if (sz == MSZ_B)
                    c->d[rn] = (c->d[rn] & ~0xffu) | (v & 0xff);
                else if (sz == MSZ_W)
                    c->d[rn] = (c->d[rn] & ~0xffffu) | (v & 0xffff);
                else c->d[rn] = v;
            }
        }
        return;
    }
    case MOP_MOVEC: {
        uint32_t ext = o->imm, rn = (ext >> 12) & 7, is_a = (ext >> 15) & 1;
        uint32_t code = ext & 0xfff, rv;
        if (!(c->sr & M68K_SR_S)) { rr_exc(8, pc); return; }
        rv = is_a ? c->a[rn] : c->d[rn];
        if (o->r1 == 0) { // Rc -> Rn
            switch (code) {
            case 0x000: v = c->sfc; break;
            case 0x001: v = c->dfc; break;
            case 0x002: v = c->cacr; break;
            case 0x800:
                v = (!(c->sr & M68K_SR_S)) ? c->a[7] : c->usp;
                break;
            case 0x801: v = c->vbr; break;
            case 0x802: v = c->caar; break;
            case 0x803:
                v = ((c->sr & (M68K_SR_S | M68K_SR_M)) ==
                        (M68K_SR_S | M68K_SR_M)) ? c->a[7] : c->msp;
                break;
            case 0x804:
                v = ((c->sr & (M68K_SR_S | M68K_SR_M)) == M68K_SR_S) ?
                        c->a[7] : c->isp;
                break;
            default: rr_exc(4, pc); return;
            }
            if (is_a) c->a[rn] = v; else c->d[rn] = v;
        } else { // Rn -> Rc
            switch (code) {
            case 0x000: c->sfc = (uint8_t)(rv & 7); break;
            case 0x001: c->dfc = (uint8_t)(rv & 7); break;
            case 0x002: c->cacr = rv; break; // stored, no cache modelled
            case 0x800:
                if (!(c->sr & M68K_SR_S)) c->a[7] = rv;
                else c->usp = rv;
                break;
            case 0x801: c->vbr = rv & 0xffffffu; break;
            case 0x802: c->caar = rv; break;
            case 0x803:
                c->msp = rv;
                if ((c->sr & (M68K_SR_S | M68K_SR_M)) ==
                        (M68K_SR_S | M68K_SR_M)) c->a[7] = rv;
                break;
            case 0x804:
                c->isp = rv;
                if ((c->sr & (M68K_SR_S | M68K_SR_M)) == M68K_SR_S)
                    c->a[7] = rv;
                break;
            default: rr_exc(4, pc); return;
            }
        }
        return;
    }
    case MOP_LEA:
        v = rr_ea_addr(c, &o->ea, pc);
        if (c->exc) return;
        c->a[o->r1] = v;
        return;
    case MOP_PEA:
        v = rr_ea_addr(c, &o->ea, pc);
        if (c->exc) return;
        rr_push32(c, v, pc);
        return;
    default:
        break;
    }
    rr_exec_alu(c, o, pc);
}

// Integer ALU, multiply/divide, BCD, misc register ops.
static inline void rr_exec_alu(m68k_t *c, const m_insn_t *o, uint32_t pc) {
    uint32_t sz = o->sz, v, r, re = (o->imm & 0x80000000u) != 0;
    switch (o->op) {
    case MOP_ADD: case MOP_SUB: case MOP_CMP: case MOP_AND: case MOP_OR:
    case MOP_EOR: case MOP_ADDI: case MOP_SUBI: case MOP_CMPI:
    case MOP_ANDI: case MOP_ORI: case MOP_EORI: case MOP_ADDQ:
    case MOP_SUBQ: {
        uint32_t src, dst, is_cmp = (o->op == MOP_CMP || o->op == MOP_CMPI);
        if (o->r1 != 0 && (o->op == MOP_ORI || o->op == MOP_ANDI ||
                o->op == MOP_EORI)) {
            // ...to CCR (r1 == 1) or SR (r1 == 2).
            uint32_t s2 = o->imm;
            if (o->r1 == 1) {
                if (o->op == MOP_ORI) c->sr |= (uint16_t)(s2 & 0xff);
                else if (o->op == MOP_ANDI)
                    c->sr = (uint16_t)(c->sr & (0xff00 | (s2 & 0xff)));
                else c->sr ^= (uint16_t)(s2 & 0xff);
            } else {
                uint32_t old;
                if (!(c->sr & M68K_SR_S)) { rr_exc(8, pc); return; }
                old = c->sr;
                if (o->op == MOP_ORI) c->sr |= (uint16_t)(s2 & 0xffff);
                else if (o->op == MOP_ANDI)
                    c->sr = (uint16_t)(c->sr & (s2 & 0xffff));
                else c->sr ^= (uint16_t)(s2 & 0xffff);
                c->sr = (uint16_t)(c->sr & 0xf71f);
                rr_sync_sp(c, old);
            }
            return;
        }
        if ((o->op == MOP_ADDQ || o->op == MOP_SUBQ) &&
                o->ea.mode == MEA_A) {
            // Address quick ops are full-width and leave CCR alone.
            if (o->op == MOP_ADDQ) c->a[o->ea.reg] += o->r1;
            else c->a[o->ea.reg] -= o->r1;
            return;
        }
        if (o->op == MOP_ADDI || o->op == MOP_SUBI || o->op == MOP_CMPI ||
                o->op == MOP_ANDI || o->op == MOP_ORI || o->op == MOP_EORI)
            src = o->imm;
        else if (o->op == MOP_ADDQ || o->op == MOP_SUBQ)
            src = o->r1;
        else if (re)
            src = c->d[o->r1];
        else
            src = rr_ear(c, &o->ea, sz, pc);
        if (c->exc) return;
        if (re || o->op == MOP_ADDI || o->op == MOP_SUBI ||
                o->op == MOP_CMPI || o->op == MOP_ANDI ||
                o->op == MOP_ORI || o->op == MOP_EORI ||
                o->op == MOP_ADDQ || o->op == MOP_SUBQ)
            dst = rr_ear(c, &o->ea, sz, pc);
        else
            dst = (sz == MSZ_B) ? (c->d[o->r1] & 0xff) :
                (sz == MSZ_W) ? (c->d[o->r1] & 0xffff) : c->d[o->r1];
        if (c->exc) return;
        switch (o->op) {
        case MOP_ADD: case MOP_ADDI: case MOP_ADDQ:
            r = (sz == MSZ_B) ? rr_add8(c, dst, src) :
                (sz == MSZ_W) ? rr_add16(c, dst, src) : rr_add32(c, dst, src);
            break;
        case MOP_SUB: case MOP_SUBI: case MOP_SUBQ:
            r = (sz == MSZ_B) ? rr_sub8(c, dst, src) :
                (sz == MSZ_W) ? rr_sub16(c, dst, src) : rr_sub32(c, dst, src);
            break;
        case MOP_CMP: case MOP_CMPI:
            if (sz == MSZ_B) rr_cmp8(c, dst, src);
            else if (sz == MSZ_W) rr_cmp16(c, dst, src);
            else rr_cmp32(c, dst, src);
            return;
        case MOP_AND: case MOP_ANDI:
            r = dst & src;
            goto logic;
        case MOP_OR: case MOP_ORI:
            r = dst | src;
            goto logic;
        default: // EOR/EORI
            r = dst ^ src;
        logic:
            if (sz == MSZ_B) rr_set_nz8(c, r);
            else if (sz == MSZ_W) rr_set_nz16(c, r);
            else rr_set_nz32(c, r);
            break;
        }
        if (is_cmp) return;
        if (re || o->op == MOP_ADDI || o->op == MOP_SUBI ||
                o->op == MOP_ANDI || o->op == MOP_ORI || o->op == MOP_EORI ||
                o->op == MOP_ADDQ || o->op == MOP_SUBQ)
            rr_eaw_rmw(c, &o->ea, sz, pc, r);
        else if (sz == MSZ_B)
            c->d[o->r1] = (c->d[o->r1] & ~0xffu) | r;
        else if (sz == MSZ_W)
            c->d[o->r1] = (c->d[o->r1] & ~0xffffu) | r;
        else c->d[o->r1] = r;
        return;
    }
    case MOP_ADDA: case MOP_SUBA: case MOP_CMPA: {
        uint32_t src = rr_ear(c, &o->ea, sz, pc), dst = c->a[o->r1];
        if (c->exc) return;
        if (sz == MSZ_W) src = (uint32_t)(int32_t)(int16_t)src;
        if (o->op == MOP_ADDA) { c->a[o->r1] = dst + src; return; }
        if (o->op == MOP_SUBA) { c->a[o->r1] = dst - src; return; }
        rr_cmp32(c, dst, src);
        return;
    }
    case MOP_ADDX: case MOP_SUBX: {
        uint32_t rx = o->r2 & 7, ry = o->r1, src, dst;
        if (o->r2 & 0x10) { // -(Ax), -(Ay)
            uint32_t st = (sz == MSZ_B) ? 1 : (sz == MSZ_W) ? 2 : 4;
            uint32_t sst = (sz == MSZ_B && rx == 7) ? 2 : st;
            uint32_t dst_ = (sz == MSZ_B && ry == 7) ? 2 : st;
            uint32_t sa = c->a[rx] - sst, da = c->a[ry] - dst_;
            c->a[rx] = sa; c->a[ry] = da;
            src = (sz == MSZ_B) ? rr_mrd8(sa) :
                (sz == MSZ_W) ? rr_mrd16(sa, pc) : rr_mrd32(sa, pc);
            if (c->exc) return;
            dst = (sz == MSZ_B) ? rr_mrd8(da) :
                (sz == MSZ_W) ? rr_mrd16(da, pc) : rr_mrd32(da, pc);
            if (c->exc) return;
            r = (o->op == MOP_ADDX) ?
                ((sz == MSZ_B) ? rr_addx8(c, dst, src) :
                 (sz == MSZ_W) ? rr_addx16(c, dst, src) :
                                 rr_addx32(c, dst, src)) :
                ((sz == MSZ_B) ? rr_subx8(c, dst, src) :
                 (sz == MSZ_W) ? rr_subx16(c, dst, src) :
                                 rr_subx32(c, dst, src));
            if (sz == MSZ_B) rr_mwr8(da, r);
            else if (sz == MSZ_W) rr_mwr16(da, r, pc);
            else rr_mwr32(da, r, pc);
        } else { // Dx, Dy
            src = (sz == MSZ_B) ? (c->d[rx] & 0xff) :
                (sz == MSZ_W) ? (c->d[rx] & 0xffff) : c->d[rx];
            dst = (sz == MSZ_B) ? (c->d[ry] & 0xff) :
                (sz == MSZ_W) ? (c->d[ry] & 0xffff) : c->d[ry];
            r = (o->op == MOP_ADDX) ?
                ((sz == MSZ_B) ? rr_addx8(c, dst, src) :
                 (sz == MSZ_W) ? rr_addx16(c, dst, src) :
                                 rr_addx32(c, dst, src)) :
                ((sz == MSZ_B) ? rr_subx8(c, dst, src) :
                 (sz == MSZ_W) ? rr_subx16(c, dst, src) :
                                 rr_subx32(c, dst, src));
            if (sz == MSZ_B) c->d[ry] = (c->d[ry] & ~0xffu) | r;
            else if (sz == MSZ_W) c->d[ry] = (c->d[ry] & ~0xffffu) | r;
            else c->d[ry] = r;
        }
        return;
    }
    case MOP_MULS: case MOP_MULU: {
        uint32_t src = rr_ear(c, &o->ea, MSZ_W, pc);
        int64_t res;
        if (c->exc) return;
        if (o->op == MOP_MULS)
            res = (int64_t)(int16_t)src * (int64_t)(int16_t)c->d[o->r1];
        else res = (uint64_t)(uint16_t)src * (uint64_t)(uint16_t)c->d[o->r1];
        c->d[o->r1] = (uint32_t)res;
        rr_set_nz32(c, (uint32_t)res);
        return;
    }
    case MOP_DIVS: case MOP_DIVU: {
        if (o->op == MOP_DIVS) {
            int32_t src = (int32_t)(int16_t)rr_ear(c, &o->ea, MSZ_W, pc);
            if (c->exc) return;
            if (!src) { rr_exc(5, pc); return; }
            c->sr &= ~M68K_SR_C;
            if (c->d[o->r1] == 0x80000000u && src == -1) {
                c->d[o->r1] = 0;
                c->sr &= ~(M68K_SR_N | M68K_SR_Z | M68K_SR_V);
                c->sr |= M68K_SR_Z;
            } else {
                int32_t q = (int32_t)c->d[o->r1] / src;
                int32_t rem = (int32_t)c->d[o->r1] % src;
                if (q == (int32_t)(int16_t)q) {
                    c->d[o->r1] = ((uint32_t)(uint16_t)rem << 16) |
                            (uint32_t)(uint16_t)q;
                    c->sr &= ~(M68K_SR_N | M68K_SR_Z | M68K_SR_V);
                    if (q & 0x8000) c->sr |= M68K_SR_N;
                    if (!(q & 0xffff)) c->sr |= M68K_SR_Z;
                } else {
                    c->sr |= M68K_SR_V;
                }
            }
        } else {
            uint32_t src = rr_ear(c, &o->ea, MSZ_W, pc) & 0xffff;
            if (c->exc) return;
            if (!src) { rr_exc(5, pc); return; }
            c->sr &= ~M68K_SR_C;
            {
                uint32_t q = c->d[o->r1] / src, rem = c->d[o->r1] % src;
                if (q <= 0xffff) {
                    c->d[o->r1] = (rem << 16) | q;
                    c->sr &= ~(M68K_SR_N | M68K_SR_Z | M68K_SR_V);
                    if (q & 0x8000) c->sr |= M68K_SR_N;
                    if (!q) c->sr |= M68K_SR_Z;
                } else {
                    c->sr |= M68K_SR_V;
                }
            }
        }
        return;
    }
    case MOP_MULL: {
        uint32_t ext = o->imm, dh = ext & 7, dl = (ext >> 12) & 7;
        uint32_t src = rr_ear(c, &o->ea, MSZ_L, pc);
        uint64_t res;
        if (c->exc) return;
        c->sr &= ~M68K_SR_C;
        if (ext & 0x800) { // signed
            int64_t sres = (int64_t)(int32_t)src *
                    (int64_t)(int32_t)c->d[dl];
            res = (uint64_t)sres;
            if (!(ext & 0x400)) { // 32-bit
                c->d[dl] = (uint32_t)res;
                c->sr &= ~(M68K_SR_N | M68K_SR_Z | M68K_SR_V);
                if ((uint32_t)res & 0x80000000u) c->sr |= M68K_SR_N;
                if (!(uint32_t)res) c->sr |= M68K_SR_Z;
                if (sres != (int64_t)(int32_t)res) c->sr |= M68K_SR_V;
                return;
            }
            c->d[dh] = (uint32_t)(res >> 32);
            c->d[dl] = (uint32_t)res;
            c->sr &= ~(M68K_SR_N | M68K_SR_Z | M68K_SR_V);
            if (res >> 63) c->sr |= M68K_SR_N;
            if (!res) c->sr |= M68K_SR_Z;
        } else { // unsigned
            res = (uint64_t)src * (uint64_t)c->d[dl];
            if (!(ext & 0x400)) {
                c->d[dl] = (uint32_t)res;
                c->sr &= ~(M68K_SR_N | M68K_SR_Z | M68K_SR_V);
                if ((uint32_t)res & 0x80000000u) c->sr |= M68K_SR_N;
                if (!(uint32_t)res) c->sr |= M68K_SR_Z;
                if (res > 0xffffffffull) c->sr |= M68K_SR_V;
                return;
            }
            c->d[dh] = (uint32_t)(res >> 32);
            c->d[dl] = (uint32_t)res;
            c->sr &= ~(M68K_SR_N | M68K_SR_Z | M68K_SR_V);
            if (res >> 63) c->sr |= M68K_SR_N;
            if (!res) c->sr |= M68K_SR_Z;
        }
        return;
    }
    case MOP_DIVL: {
        uint32_t ext = o->imm, dh = ext & 7, dl = (ext >> 12) & 7;
        uint32_t src = rr_ear(c, &o->ea, MSZ_L, pc);
        if (c->exc) return;
        if (!src) { rr_exc(5, pc); return; }
        if (ext & 0x400) { // 64-bit dividend
            if (ext & 0x800) { // signed
                int64_t dd = ((int64_t)(int32_t)c->d[dh] << 32) | c->d[dl];
                int64_t q = dd / (int64_t)(int32_t)src;
                int64_t rem = dd % (int64_t)(int32_t)src;
                if (q != (int64_t)(int32_t)q) {
                    c->sr |= M68K_SR_V;
                    return;
                }
                c->d[dh] = (uint32_t)rem;
                c->d[dl] = (uint32_t)q;
                c->sr &= ~(M68K_SR_N | M68K_SR_Z | M68K_SR_V | M68K_SR_C);
                if ((uint32_t)q & 0x80000000u) c->sr |= M68K_SR_N;
                if (!(uint32_t)q) c->sr |= M68K_SR_Z;
            } else { // unsigned
                uint64_t dd = ((uint64_t)c->d[dh] << 32) | c->d[dl];
                uint64_t q = dd / src;
                if (q > 0xffffffffull) {
                    c->sr |= M68K_SR_V;
                    return;
                }
                c->d[dh] = (uint32_t)(dd % src);
                c->d[dl] = (uint32_t)q;
                c->sr &= ~(M68K_SR_N | M68K_SR_Z | M68K_SR_V | M68K_SR_C);
                if ((uint32_t)q & 0x80000000u) c->sr |= M68K_SR_N;
                if (!(uint32_t)q) c->sr |= M68K_SR_Z;
            }
        } else { // 32-bit dividend in Dl
            if (ext & 0x800) {
                int32_t dd = (int32_t)c->d[dl];
                int32_t q = (int32_t)((int64_t)dd / (int64_t)(int32_t)src);
                int32_t rem = (int32_t)((int64_t)dd % (int64_t)(int32_t)src);
                c->d[dh] = (uint32_t)rem;
                c->d[dl] = (uint32_t)q;
                c->sr &= ~(M68K_SR_N | M68K_SR_Z | M68K_SR_V | M68K_SR_C);
                if ((uint32_t)q & 0x80000000u) c->sr |= M68K_SR_N;
                if (!(uint32_t)q) c->sr |= M68K_SR_Z;
            } else {
                uint32_t q = c->d[dl] / src, rem = c->d[dl] % src;
                c->d[dh] = rem;
                c->d[dl] = q;
                c->sr &= ~(M68K_SR_N | M68K_SR_Z | M68K_SR_V | M68K_SR_C);
                if (q & 0x80000000u) c->sr |= M68K_SR_N;
                if (!q) c->sr |= M68K_SR_Z;
            }
        }
        return;
    }
    case MOP_ABCD: case MOP_SBCD: {
        uint32_t rx = o->r2 & 7, ry = o->r1;
        if (o->r2 & 0x10) { // -(Ax), -(Ay)
            uint32_t sa = c->a[rx] - (rx == 7 ? 2 : 1);
            uint32_t da = c->a[ry] - (ry == 7 ? 2 : 1);
            uint32_t src, dst;
            c->a[rx] = sa; c->a[ry] = da;
            src = rr_mrd8(sa); dst = rr_mrd8(da);
            r = (o->op == MOP_ABCD) ? rr_abcd(c, dst, src) :
                    rr_sbcd(c, dst, src);
            rr_mwr8(da, r);
        } else {
            uint32_t src = c->d[rx] & 0xff, dst = c->d[ry] & 0xff;
            r = (o->op == MOP_ABCD) ? rr_abcd(c, dst, src) :
                    rr_sbcd(c, dst, src);
            c->d[ry] = (c->d[ry] & ~0xffu) | r;
        }
        return;
    }
    case MOP_NBCD:
        v = rr_ear(c, &o->ea, MSZ_B, pc);
        if (c->exc) return;
        r = rr_nbcd(c, v);
        rr_eaw_rmw(c, &o->ea, MSZ_B, pc, r);
        return;
    case MOP_NEGX:
        v = rr_ear(c, &o->ea, sz, pc);
        if (c->exc) return;
        r = (sz == MSZ_B) ? rr_subx8(c, 0, v) :
            (sz == MSZ_W) ? rr_subx16(c, 0, v) : rr_subx32(c, 0, v);
        rr_eaw_rmw(c, &o->ea, sz, pc, r);
        return;
    case MOP_CLR:
        rr_eaw(c, &o->ea, sz, pc, 0);
        if (c->exc) return;
        c->sr &= ~(M68K_SR_N | M68K_SR_V | M68K_SR_C);
        c->sr |= M68K_SR_Z;
        return;
    case MOP_NEG:
        v = rr_ear(c, &o->ea, sz, pc);
        if (c->exc) return;
        r = (sz == MSZ_B) ? rr_sub8(c, 0, v) :
            (sz == MSZ_W) ? rr_sub16(c, 0, v) : rr_sub32(c, 0, v);
        rr_eaw_rmw(c, &o->ea, sz, pc, r);
        return;
    case MOP_NOT:
        v = rr_ear(c, &o->ea, sz, pc);
        if (c->exc) return;
        r = ~v;
        if (sz == MSZ_B) rr_set_nz8(c, r);
        else if (sz == MSZ_W) rr_set_nz16(c, r);
        else rr_set_nz32(c, r);
        rr_eaw_rmw(c, &o->ea, sz, pc, r);
        return;
    case MOP_TST:
        v = rr_ear(c, &o->ea, sz, pc);
        if (c->exc) return;
        if (sz == MSZ_B) rr_set_nz8(c, v);
        else if (sz == MSZ_W) rr_set_nz16(c, v);
        else rr_set_nz32(c, v);
        return;
    case MOP_TAS:
        v = rr_ear(c, &o->ea, MSZ_B, pc);
        if (c->exc) return;
        rr_set_nz8(c, v);
        rr_eaw_rmw(c, &o->ea, MSZ_B, pc, v | 0x80);
        return;
    case MOP_SWAP:
        v = c->d[o->r1];
        r = (v >> 16) | (v << 16);
        c->d[o->r1] = r;
        rr_set_nz32(c, r);
        return;
    case MOP_EXT:
        if (sz == MSZ_W) {
            c->d[o->r1] = (c->d[o->r1] & ~0xffffu) |
                    (uint32_t)(uint16_t)(int16_t)(int8_t)c->d[o->r1];
            rr_set_nz16(c, c->d[o->r1]);
        } else {
            c->d[o->r1] = (uint32_t)(int32_t)(int16_t)c->d[o->r1];
            rr_set_nz32(c, c->d[o->r1]);
        }
        return;
    case MOP_EXTB:
        c->d[o->r1] = (uint32_t)(int32_t)(int8_t)c->d[o->r1];
        rr_set_nz32(c, c->d[o->r1]);
        return;
    case MOP_EXG: {
        uint32_t rx = o->r1, ry = o->r2 & 7, t;
        if ((o->r2 & 0x30) == 0x00) { // Dx, Dy
            t = c->d[rx]; c->d[rx] = c->d[ry]; c->d[ry] = t;
        } else if ((o->r2 & 0x30) == 0x10) { // Ax, Ay
            t = c->a[rx]; c->a[rx] = c->a[ry]; c->a[ry] = t;
        } else { // Dx, Ay
            t = c->d[rx]; c->d[rx] = c->a[ry]; c->a[ry] = t;
        }
        return;
    }
    case MOP_CMPM: {
        uint32_t xa = c->a[o->r2 & 7], ya = c->a[o->r1];
        uint32_t st = (sz == MSZ_B) ? 1 : (sz == MSZ_W) ? 2 : 4;
        uint32_t stx = (sz == MSZ_B && (o->r2 & 7) == 7) ? 2 : st;
        uint32_t sty = (sz == MSZ_B && o->r1 == 7) ? 2 : st;
        uint32_t src = (sz == MSZ_B) ? rr_mrd8(xa) :
            (sz == MSZ_W) ? rr_mrd16(xa, pc) : rr_mrd32(xa, pc);
        uint32_t dst;
        c->a[o->r2 & 7] = xa + stx;
        if (c->exc) return;
        dst = (sz == MSZ_B) ? rr_mrd8(ya) :
            (sz == MSZ_W) ? rr_mrd16(ya, pc) : rr_mrd32(ya, pc);
        c->a[o->r1] = ya + sty;
        if (c->exc) return;
        if (sz == MSZ_B) rr_cmp8(c, dst, src);
        else if (sz == MSZ_W) rr_cmp16(c, dst, src);
        else rr_cmp32(c, dst, src);
        return;
    }
    default:
        break;
    }
    rr_exec_cf(c, o, pc);
}

// Shifts, bit operations, branches, traps and system ops.
static inline void rr_exec_cf(m68k_t *c, const m_insn_t *o, uint32_t pc) {
    uint32_t sz = o->sz, v, r;
    switch (o->op) {
    case MOP_ASL: case MOP_ASR: case MOP_LSL: case MOP_LSR:
    case MOP_ROL: case MOP_ROR: case MOP_ROXL: case MOP_ROXR: {
        uint32_t w = (sz == MSZ_B) ? 8 : (sz == MSZ_W) ? 16 : 32, sh;
        if (o->r1 & 0x80) { // memory form: count 1, word
            v = rr_ear(c, &o->ea, MSZ_W, pc);
            if (c->exc) return;
            sh = 1; w = 16;
            goto memshift;
        }
        sh = (o->imm & 0x100) ? (c->d[o->imm & 7] & 0x3f) : o->imm;
        v = (sz == MSZ_B) ? (c->d[o->r1] & 0xff) :
            (sz == MSZ_W) ? (c->d[o->r1] & 0xffff) : c->d[o->r1];
        switch (o->op) {
        case MOP_ASL: r = rr_asl(c, v, sh, w); break;
        case MOP_ASR: r = rr_asr(c, v, sh, w); break;
        case MOP_LSL: r = rr_lsl(c, v, sh, w); break;
        case MOP_LSR: r = rr_lsr(c, v, sh, w); break;
        case MOP_ROL: r = rr_rol(c, v, sh, w); break;
        case MOP_ROR: r = rr_ror(c, v, sh, w); break;
        case MOP_ROXL: r = rr_roxl(c, v, sh, w); break;
        default: r = rr_roxr(c, v, sh, w); break;
        }
        if (sz == MSZ_B) c->d[o->r1] = (c->d[o->r1] & ~0xffu) | r;
        else if (sz == MSZ_W) c->d[o->r1] = (c->d[o->r1] & ~0xffffu) | r;
        else c->d[o->r1] = r;
        return;
    memshift:
        switch (o->op) {
        case MOP_ASL: r = rr_asl(c, v, sh, w); break;
        case MOP_ASR: r = rr_asr(c, v, sh, w); break;
        case MOP_LSL: r = rr_lsl(c, v, sh, w); break;
        case MOP_LSR: r = rr_lsr(c, v, sh, w); break;
        case MOP_ROL: r = rr_rol(c, v, sh, w); break;
        case MOP_ROR: r = rr_ror(c, v, sh, w); break;
        case MOP_ROXL: r = rr_roxl(c, v, sh, w); break;
        default: r = rr_roxr(c, v, sh, w); break;
        }
        rr_eaw_rmw(c, &o->ea, MSZ_W, pc, r);
        return;
    }
    case MOP_BTST: case MOP_BCHG: case MOP_BCLR: case MOP_BSET: {
        uint32_t bit = (o->r1 & 0x80) ? (o->r1 & 0x1f) :
                (c->d[o->r1] & 0x1f);
        if (o->ea.mode == MEA_D) {
            uint32_t b = (o->r1 & 0x80) ? (o->r1 & 0x1f) :
                    (c->d[o->r1] & 0x1f);
            v = c->d[o->ea.reg];
            if (v & (1u << b)) c->sr &= ~M68K_SR_Z;
            else c->sr |= M68K_SR_Z;
            if (o->op == MOP_BCHG) c->d[o->ea.reg] = v ^ (1u << b);
            else if (o->op == MOP_BCLR)
                c->d[o->ea.reg] = v & ~(1u << b);
            else if (o->op == MOP_BSET)
                c->d[o->ea.reg] = v | (1u << b);
        } else {
            uint32_t addr;
            bit &= 7;
            if (o->ea.mode == MEA_AINC) {
                v = rr_ear(c, &o->ea, MSZ_B, pc);
                if (c->exc) return;
                addr = c->a[o->ea.reg] -
                        ((o->ea.reg == 7) ? 2 : 1); // byte just read
            } else {
                addr = rr_ea_addr(c, &o->ea, pc);
                if (c->exc) return;
                // Predecrement resolves through a read (updates An).
                if (o->ea.mode == MEA_ADEC) {
                    v = rr_ear(c, &o->ea, MSZ_B, pc);
                    if (c->exc) return;
                    addr = c->a[o->ea.reg];
                } else {
                    v = rr_mrd8(addr);
                }
            }
            if (v & (1u << bit)) c->sr &= ~M68K_SR_Z;
            else c->sr |= M68K_SR_Z;
            if (o->op == MOP_BCHG) rr_mwr8(addr, v ^ (1u << bit));
            else if (o->op == MOP_BCLR) rr_mwr8(addr, v & ~(1u << bit));
            else if (o->op == MOP_BSET) rr_mwr8(addr, v | (1u << bit));
        }
        return;
    }
    case MOP_BFTST: case MOP_BFEXTS: case MOP_BFEXTU: case MOP_BFFFO:
    case MOP_BFCHG: case MOP_BFCLR: case MOP_BFSET: case MOP_BFINS: {
        uint32_t ext = o->imm, dn = (ext >> 12) & 7;
        int32_t off = (ext & 0x800) ? (int32_t)c->d[(ext >> 6) & 7] :
                (int32_t)((ext >> 6) & 31);
        uint32_t wid = (ext & 0x20) ? c->d[ext & 7] : (ext & 31);
        wid = ((wid - 1) & 31) + 1;
        if (o->ea.mode == MEA_D) {
            uint32_t uoff = (uint32_t)off & 31, data = c->d[o->ea.reg];
            uint64_t mask = (wid == 32) ? 0xffffffffull :
                    (((1ull << wid) - 1) << (32 - wid));
            uint32_t m32;
            // Rotate the field mask into place (Musashi: ROR by offset).
            m32 = (uoff == 0) ? (uint32_t)mask :
                    (uint32_t)((mask >> uoff) | (mask << (32 - uoff)));
            switch (o->op) {
            case MOP_BFTST: case MOP_BFEXTS: case MOP_BFEXTU:
            case MOP_BFFFO: {
                uint32_t f;
                c->sr &= ~(M68K_SR_N | M68K_SR_Z | M68K_SR_V | M68K_SR_C);
                if ((data << uoff) & 0x80000000u) c->sr |= M68K_SR_N;
                f = (uoff + wid <= 32) ? ((data >> (32 - uoff - wid)) &
                        (wid == 32 ? 0xffffffffu : ((1u << wid) - 1))) :
                        (uint32_t)(((uint64_t)data << (uoff + wid - 32)) &
                        (wid == 32 ? 0xffffffffull : ((1ull << wid) - 1)));
                if (!f) c->sr |= M68K_SR_Z;
                if (o->op == MOP_BFEXTS)
                    c->d[dn] = (wid == 32) ? f :
                            (uint32_t)((int32_t)(f << (32 - wid)) >>
                            (32 - wid));
                else if (o->op == MOP_BFEXTU)
                    c->d[dn] = f;
                else if (o->op == MOP_BFFFO) {
                    uint32_t b, n = uoff;
                    // Scan from the field's first bit (Musashi: ROL then
                    // walk down from the width's top bit).
                    uint32_t d = (uoff == 0) ? data :
                            ((data << uoff) | (data >> (32 - uoff)));
                    d >>= 32 - wid;
                    for (b = 1u << (wid - 1); b && !(d & b); b >>= 1)
                        n++;
                    c->d[dn] = n;
                }
                return;
            }
            case MOP_BFCHG:
                c->sr &= ~(M68K_SR_N | M68K_SR_Z | M68K_SR_V | M68K_SR_C);
                if ((data << uoff) & 0x80000000u) c->sr |= M68K_SR_N;
                if (!(data & m32)) c->sr |= M68K_SR_Z;
                c->d[o->ea.reg] = data ^ m32;
                return;
            case MOP_BFCLR:
                c->sr &= ~(M68K_SR_N | M68K_SR_Z | M68K_SR_V | M68K_SR_C);
                if ((data << uoff) & 0x80000000u) c->sr |= M68K_SR_N;
                if (!(data & m32)) c->sr |= M68K_SR_Z;
                c->d[o->ea.reg] = data & ~m32;
                return;
            case MOP_BFSET:
                c->sr &= ~(M68K_SR_N | M68K_SR_Z | M68K_SR_V | M68K_SR_C);
                if ((data << uoff) & 0x80000000u) c->sr |= M68K_SR_N;
                if (!(data & m32)) c->sr |= M68K_SR_Z;
                c->d[o->ea.reg] = data | m32;
                return;
            default: { // BFINS
                uint32_t ins = c->d[dn] << (32 - wid);
                c->sr &= ~(M68K_SR_N | M68K_SR_Z | M68K_SR_V | M68K_SR_C);
                if ((data << uoff) & 0x80000000u) c->sr |= M68K_SR_N;
                if (!(data & m32)) c->sr |= M68K_SR_Z;
                c->d[o->ea.reg] = (data & ~m32) |
                        ((uoff == 0) ? ins :
                         (ins >> uoff) | (ins << (32 - uoff)));
                return;
            }
            }
        } else {
            // Memory bitfield: byte address + signed bit offset (Musashi).
            uint32_t base = rr_ea_addr(c, &o->ea, pc);
            int32_t boff;
            uint32_t b0, b1, b2, b3, b4 = 0, data;
            if (c->exc) return;
            base = base + (uint32_t)(off / 8);
            boff = off % 8;
            if (boff < 0) { boff += 8; base--; }
            b0 = rr_mrd8(base); b1 = rr_mrd8(base + 1);
            b2 = rr_mrd8(base + 2); b3 = rr_mrd8(base + 3);
            data = (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
            {
                uint32_t mbase = (wid == 32) ? 0xffffffffu :
                        (0xffffffffu << (32 - wid));
                uint32_t mlong = mbase >> boff;
                uint64_t m40 = (wid == 40) ? 0xffffffffffull :
                        ((((1ull << wid) - 1) << (40 - boff - wid)) &
                        0xffffffffffull);
                uint64_t win = ((uint64_t)data << 8);
                c->sr &= ~(M68K_SR_N | M68K_SR_Z | M68K_SR_V | M68K_SR_C);
                if ((data << boff) & 0x80000000u) c->sr |= M68K_SR_N;
                if (wid + (uint32_t)boff > 32) {
                    b4 = rr_mrd8(base + 4);
                    win |= b4;
                    if (!(win & m40)) c->sr |= M68K_SR_Z;
                } else {
                    if (!(data & mlong)) c->sr |= M68K_SR_Z;
                }
                switch (o->op) {
                case MOP_BFCHG:
                    data ^= mlong;
                    rr_mwr8(base, data >> 24); rr_mwr8(base + 1, data >> 16);
                    rr_mwr8(base + 2, data >> 8); rr_mwr8(base + 3, data);
                    if (wid + (uint32_t)boff > 32) {
                        uint32_t mb = (uint32_t)((mbase & 0xff) <<
                                (8 - boff)) & 0xff;
                        rr_mwr8(base + 4, b4 ^ mb);
                    }
                    return;
                case MOP_BFCLR:
                    data &= ~mlong;
                    rr_mwr8(base, data >> 24); rr_mwr8(base + 1, data >> 16);
                    rr_mwr8(base + 2, data >> 8); rr_mwr8(base + 3, data);
                    if (wid + (uint32_t)boff > 32) {
                        uint32_t mb = (uint32_t)((mbase & 0xff) <<
                                (8 - boff)) & 0xff;
                        rr_mwr8(base + 4, b4 & ~mb);
                    }
                    return;
                case MOP_BFSET:
                    data |= mlong;
                    rr_mwr8(base, data >> 24); rr_mwr8(base + 1, data >> 16);
                    rr_mwr8(base + 2, data >> 8); rr_mwr8(base + 3, data);
                    if (wid + (uint32_t)boff > 32) {
                        uint32_t mb = (uint32_t)((mbase & 0xff) <<
                                (8 - boff)) & 0xff;
                        rr_mwr8(base + 4, b4 | mb);
                    }
                    return;
                case MOP_BFINS: {
                    uint32_t ins = c->d[dn];
                    uint64_t ins40 = ((uint64_t)(ins & (wid == 32 ?
                            0xffffffffu : ((1u << wid) - 1)))) <<
                            (40 - boff - wid);
                    uint64_t nw = (win & ~m40) | (ins40 & m40);
                    rr_mwr8(base, (uint32_t)(nw >> 32));
                    rr_mwr8(base + 1, (uint32_t)(nw >> 24));
                    rr_mwr8(base + 2, (uint32_t)(nw >> 16));
                    rr_mwr8(base + 3, (uint32_t)(nw >> 8));
                    if (wid + (uint32_t)boff > 32)
                        rr_mwr8(base + 4, (uint32_t)nw);
                    return;
                }
                case MOP_BFEXTS: case MOP_BFEXTU: {
                    uint32_t f = (uint32_t)((win & m40) >>
                            (40 - boff - wid));
                    if (o->op == MOP_BFEXTS && wid < 32)
                        f = (uint32_t)((int32_t)(f << (32 - wid)) >>
                                (32 - wid));
                    c->d[dn] = f;
                    return;
                }
                case MOP_BFFFO: {
                    uint32_t f = (uint32_t)((win & m40) >>
                            (40 - boff - wid)), b, n = 0;
                    for (b = (wid == 32) ? 0x80000000u : (1u << (wid - 1));
                            b && !(f & b); b >>= 1)
                        n++;
                    // Counts from the original offset (Musashi).
                    c->d[dn] = (uint32_t)(off + (int32_t)n);
                    return;
                }
                default: // BFTST: flags already set
                    return;
                }
            }
        }
    }
    case MOP_PACK: case MOP_UNPK: {
        uint32_t rx = o->r2 & 7, ry = o->r1, adj = o->imm & 0xffff, src;
        if (o->r2 & 0x10) { // -(Ax), -(Ay): byte operand
            uint32_t sa = c->a[rx] - (sz == MSZ_L ? 2 : 1);
            uint32_t da = c->a[ry] - 1;
            if (o->op == MOP_PACK) {
                if (sz == MSZ_L) {
                    uint32_t hi = rr_mrd8(sa), lo = rr_mrd8(sa + 1);
                    src = ((hi & 0xf) << 4) | (lo & 0xf);
                } else {
                    src = rr_mrd8(sa) & 0xf;
                }
                src = (src + adj) & 0xff;
            } else {
                src = rr_mrd8(sa);
                if (sz == MSZ_L) {
                    uint32_t hi = (((src >> 4) + (adj >> 8)) & 0xff);
                    uint32_t lo = (((src & 0xf) + adj) & 0xff);
                    c->a[rx] = sa; c->a[ry] = da - 1;
                    rr_mwr8(da - 1, hi); rr_mwr8(da, lo);
                    return;
                }
                src = (((src & 0xf) + adj) & 0xff);
            }
            c->a[rx] = sa; c->a[ry] = da;
            rr_mwr8(da, src);
        } else { // Dx, Dy
            if (o->op == MOP_PACK) {
                if (sz == MSZ_L)
                    src = (((c->d[rx] >> 4) & 0xf0) |
                            (c->d[rx] & 0xf)) & 0xff;
                else
                    src = c->d[rx] & 0xf;
                src = (src + adj) & 0xff;
                c->d[ry] = (c->d[ry] & ~0xffu) | src;
            } else {
                src = c->d[rx] & 0xff;
                if (sz == MSZ_L) {
                    uint32_t hi = (((src >> 4) + (adj >> 8)) & 0xff);
                    uint32_t lo = (((src & 0xf) + adj) & 0xff);
                    c->d[ry] = (c->d[ry] & ~0xffffu) | (hi << 8) | lo;
                } else {
                    src = (((src & 0xf) + adj) & 0xff);
                    c->d[ry] = (c->d[ry] & ~0xffu) | src;
                }
            }
        }
        return;
    }
    case MOP_CHK: {
        int32_t src, bound;
        if (sz == MSZ_W) {
            src = (int32_t)(int16_t)c->d[o->r1];
            bound = (int32_t)(int16_t)rr_ear(c, &o->ea, MSZ_W, pc);
        } else {
            src = (int32_t)c->d[o->r1];
            bound = (int32_t)rr_ear(c, &o->ea, MSZ_L, pc);
        }
        if (c->exc) return;
        c->sr &= ~(M68K_SR_Z | M68K_SR_V | M68K_SR_C);
        if (!src) c->sr |= M68K_SR_Z;
        if (src < 0 || src > bound) {
            if (src < 0) c->sr |= M68K_SR_N;
            else c->sr &= ~M68K_SR_N;
            rr_exc(6, pc);
        }
        return;
    }
    case MOP_CHK2: {
        // Also CMP2 (extension bit 11 clear).
        uint32_t ext = o->imm, is_chk = (ext >> 11) & 1;
        uint32_t reg = (ext >> 12) & 7, is_a = (ext >> 15) & 1;
        uint32_t base = rr_ea_addr(c, &o->ea, pc);
        int32_t cmp, lo, hi;
        if (c->exc) return;
        if (is_a) {
            cmp = (int32_t)c->a[reg];
        } else if (sz == MSZ_B) {
            cmp = (int32_t)(c->d[reg] & 0xff);
        } else if (sz == MSZ_W) {
            cmp = (int32_t)(c->d[reg] & 0xffff);
        } else {
            cmp = (int32_t)c->d[reg];
        }
        if (sz == MSZ_B) {
            lo = (int32_t)rr_mrd8(base); hi = (int32_t)rr_mrd8(base + 1);
            if (lo & 0x80) {
                lo = (int32_t)(int8_t)lo; hi = (int32_t)(int8_t)hi;
                if (!is_a) cmp = (int32_t)(int8_t)cmp;
            }
        } else if (sz == MSZ_W) {
            lo = (int32_t)rr_mrd16(base, pc);
            hi = (int32_t)rr_mrd16(base + 2, pc);
            if (c->exc) return;
            if (lo & 0x8000) {
                lo = (int32_t)(int16_t)lo; hi = (int32_t)(int16_t)hi;
                if (!is_a) cmp = (int32_t)(int16_t)cmp;
            }
        } else {
            lo = (int32_t)rr_mrd32(base, pc);
            hi = (int32_t)rr_mrd32(base + 4, pc);
            if (c->exc) return;
        }
        c->sr &= ~(M68K_SR_Z | M68K_SR_C);
        if (cmp < lo || cmp > hi) c->sr |= M68K_SR_C;
        if (cmp == lo || cmp == hi) c->sr |= M68K_SR_Z;
        if ((c->sr & M68K_SR_C) && is_chk) rr_exc(6, pc);
        return;
    }
    case MOP_CAS: {
        uint32_t dc = o->imm & 7, du = (o->imm >> 6) & 7;
        uint32_t dest = rr_ear(c, &o->ea, sz, pc), cd = c->d[dc], res;
        if (c->exc) return;
        if (sz == MSZ_B) {
            res = (dest & 0xff) - (cd & 0xff);
            c->sr &= ~(M68K_SR_N | M68K_SR_Z | M68K_SR_V | M68K_SR_C);
            if (res & 0x100) c->sr |= M68K_SR_C;
            if (res & 0x80) c->sr |= M68K_SR_N;
            if (!(res & 0xff)) c->sr |= M68K_SR_Z;
            if ((((cd ^ dest) & (dest ^ res)) & 0x80)) c->sr |= M68K_SR_V;
            if (res & 0xff) c->d[dc] = (cd & ~0xffu) | (dest & 0xff);
            else rr_eaw_rmw(c, &o->ea, sz, pc, c->d[du]);
        } else if (sz == MSZ_W) {
            res = (dest & 0xffff) - (cd & 0xffff);
            c->sr &= ~(M68K_SR_N | M68K_SR_Z | M68K_SR_V | M68K_SR_C);
            if (res & 0x10000) c->sr |= M68K_SR_C;
            if (res & 0x8000) c->sr |= M68K_SR_N;
            if (!(res & 0xffff)) c->sr |= M68K_SR_Z;
            if ((((cd ^ dest) & (dest ^ res)) & 0x8000)) c->sr |= M68K_SR_V;
            if (res & 0xffff) c->d[dc] = (cd & ~0xffffu) | (dest & 0xffff);
            else rr_eaw_rmw(c, &o->ea, sz, pc, c->d[du]);
        } else {
            res = dest - cd;
            c->sr &= ~(M68K_SR_N | M68K_SR_Z | M68K_SR_V | M68K_SR_C);
            if (cd > dest) c->sr |= M68K_SR_C;
            if (res & 0x80000000u) c->sr |= M68K_SR_N;
            if (!res) c->sr |= M68K_SR_Z;
            if ((((cd ^ dest) & (dest ^ res)) & 0x80000000u))
                c->sr |= M68K_SR_V;
            if (res) c->d[dc] = dest;
            else rr_eaw_rmw(c, &o->ea, sz, pc, c->d[du]);
        }
        return;
    }
    case MOP_CAS2: {
        uint32_t e1 = o->imm, e2 = o->imm2;
        uint32_t dc1 = e1 & 7, du1 = (e1 >> 6) & 7;
        uint32_t dc2 = e2 & 7, du2 = (e2 >> 6) & 7;
        uint32_t rn1 = ((e1 >> 12) & 15), rn2 = ((e2 >> 12) & 15);
        uint32_t a1 = (rn1 & 8) ? c->a[rn1 & 7] : c->d[rn1 & 7];
        uint32_t a2 = (rn2 & 8) ? c->a[rn2 & 7] : c->d[rn2 & 7];
        uint32_t d1 = (sz == MSZ_W) ? rr_mrd16(a1, pc) : rr_mrd32(a1, pc);
        uint32_t d2 = (sz == MSZ_W) ? rr_mrd16(a2, pc) : rr_mrd32(a2, pc);
        uint32_t r1, r2, eq1, eq2;
        if (c->exc) return;
        if (sz == MSZ_W) {
            r1 = (d1 & 0xffff) - (c->d[dc1] & 0xffff);
            c->sr &= ~(M68K_SR_N | M68K_SR_Z | M68K_SR_V | M68K_SR_C);
            if (r1 & 0x10000) c->sr |= M68K_SR_C;
            if (r1 & 0x8000) c->sr |= M68K_SR_N;
            if (!(r1 & 0xffff)) c->sr |= M68K_SR_Z;
            if ((((c->d[dc1] ^ d1) & (d1 ^ r1)) & 0x8000))
                c->sr |= M68K_SR_V;
            eq1 = !(r1 & 0xffff);
            if (eq1) {
                r2 = (d2 & 0xffff) - (c->d[dc2] & 0xffff);
                c->sr &= ~(M68K_SR_N | M68K_SR_Z | M68K_SR_V | M68K_SR_C);
                if (r2 & 0x10000) c->sr |= M68K_SR_C;
                if (r2 & 0x8000) c->sr |= M68K_SR_N;
                if (!(r2 & 0xffff)) c->sr |= M68K_SR_Z;
                if ((((c->d[dc2] ^ d2) & (d2 ^ r2)) & 0x8000))
                    c->sr |= M68K_SR_V;
                eq2 = !(r2 & 0xffff);
            } else eq2 = 0;
            if (eq1 && eq2) {
                rr_mwr16(a1, c->d[du1], pc);
                if (c->exc) return;
                rr_mwr16(a2, c->d[du2], pc);
            } else {
                // Rn type selects sign-extend vs merge (Musashi quirk).
                c->d[dc1] = (rn1 & 8) ? (uint32_t)(int32_t)(int16_t)d1 :
                        ((c->d[dc1] & ~0xffffu) | (d1 & 0xffff));
                c->d[dc2] = (rn2 & 8) ? (uint32_t)(int32_t)(int16_t)d2 :
                        ((c->d[dc2] & ~0xffffu) | (d2 & 0xffff));
            }
        } else {
            r1 = d1 - c->d[dc1];
            c->sr &= ~(M68K_SR_N | M68K_SR_Z | M68K_SR_V | M68K_SR_C);
            if (c->d[dc1] > d1) c->sr |= M68K_SR_C;
            if (r1 & 0x80000000u) c->sr |= M68K_SR_N;
            if (!r1) c->sr |= M68K_SR_Z;
            if ((((c->d[dc1] ^ d1) & (d1 ^ r1)) & 0x80000000u))
                c->sr |= M68K_SR_V;
            eq1 = !r1;
            if (eq1) {
                r2 = d2 - c->d[dc2];
                c->sr &= ~(M68K_SR_N | M68K_SR_Z | M68K_SR_V | M68K_SR_C);
                if (c->d[dc2] > d2) c->sr |= M68K_SR_C;
                if (r2 & 0x80000000u) c->sr |= M68K_SR_N;
                if (!r2) c->sr |= M68K_SR_Z;
                if ((((c->d[dc2] ^ d2) & (d2 ^ r2)) & 0x80000000u))
                    c->sr |= M68K_SR_V;
                eq2 = !r2;
            } else eq2 = 0;
            if (eq1 && eq2) {
                rr_mwr32(a1, c->d[du1], pc);
                if (c->exc) return;
                rr_mwr32(a2, c->d[du2], pc);
            } else {
                c->d[dc1] = d1; c->d[dc2] = d2;
            }
        }
        return;
    }
    case MOP_BCC: case MOP_BRA: case MOP_BSR: {
        // imm = displacement from pc + 2.
        if (o->op == MOP_BCC && !rr_cond(c->sr, o->cc)) return;
        if (o->op == MOP_BSR) rr_push32(c, c->pc, pc);
        c->pc = (pc + 2 + o->imm) & 0xffffffu;
        return;
    }
    case MOP_DBCC: {
        if (!rr_cond(c->sr, o->cc)) {
            uint32_t dn = (c->d[o->r1] - 1) & 0xffff;
            c->d[o->r1] = (c->d[o->r1] & ~0xffffu) | dn;
            if (dn != 0xffff)
                c->pc = (pc + 2 + o->imm) & 0xffffffu;
        }
        return;
    }
    case MOP_SCC:
        rr_eaw(c, &o->ea, MSZ_B, pc, rr_cond(c->sr, o->cc) ? 0xff : 0);
        return;
    case MOP_TRAP:
        rr_exc(32 + o->r1, pc);
        return;
    case MOP_TRAPV:
        if (c->sr & M68K_SR_V) rr_exc(7, pc);
        return;
    case MOP_TRAPCC:
        if (rr_cond(c->sr, o->cc)) rr_exc(7, pc);
        return;
    case MOP_JMP: case MOP_JSR: {
        v = rr_ea_addr(c, &o->ea, pc);
        if (c->exc) return;
        if (o->op == MOP_JSR) {
            rr_push32(c, c->pc, pc);
            if (c->exc) return;
        }
        c->pc = v & 0xffffffu;
        return;
    }
    case MOP_RTS:
        c->pc = rr_pop32(c, pc) & 0xffffffu;
        return;
    case MOP_RTR:
        v = rr_pop16(c, pc);
        if (c->exc) return;
        c->sr = (uint16_t)((c->sr & 0xff00) | (v & 0xff));
        c->pc = rr_pop32(c, pc) & 0xffffffu;
        return;
    case MOP_RTD:
        c->pc = rr_pop32(c, pc) & 0xffffffu;
        if (c->exc) return;
        c->a[7] = (c->a[7] + o->imm) & 0xffffffu;
        return;
    case MOP_RTE: {
        uint32_t nsr, npc, fmtvo, fmt;
        if (!(c->sr & M68K_SR_S)) { rr_exc(8, pc); return; }
        nsr = rr_pop16(c, pc);
        npc = rr_pop32(c, pc);
        if (c->exc) return;
        fmtvo = rr_mrd16(c->a[7], pc);
        if (c->exc) return;
        fmt = fmtvo >> 12;
        if (fmt == 1) { // throwaway: resume on the master stack
            c->a[7] += 8;
            c->isp = c->a[7];
            c->sr |= M68K_SR_M;
            c->a[7] = c->msp;
            nsr = rr_pop16(c, pc);
            npc = rr_pop32(c, pc);
            if (c->exc) return;
            fmtvo = rr_mrd16(c->a[7], pc);
            if (c->exc) return;
            fmt = fmtvo >> 12;
        }
        switch (fmt) {
        case 0x0: c->a[7] += 2; break;
        case 0x2: c->a[7] += 6; break;
        case 0x9: c->a[7] += 12; break;
        case 0xa: c->a[7] += 24; break;
        case 0xb: c->a[7] += 84; break;
        default: rr_exc(14, pc); return; // format error
        }
        {
            uint32_t old = c->sr;
            c->sr = (uint16_t)(nsr & 0xf71f);
            rr_sync_sp(c, old);
        }
        c->pc = npc & 0xffffffu;
        return;
    }
    case MOP_LINK: {
        uint32_t an = o->r1;
        rr_push32(c, c->a[an], pc);
        if (c->exc) return;
        c->a[an] = c->a[7];
        c->a[7] = (c->a[7] + o->imm) & 0xffffffu;
        return;
    }
    case MOP_UNLK: {
        uint32_t an = o->r1;
        c->a[7] = c->a[an];
        c->a[an] = rr_pop32(c, pc);
        return;
    }
    case MOP_STOP: {
        uint32_t old;
        if (!(c->sr & M68K_SR_S)) { rr_exc(8, pc); return; }
        old = c->sr;
        c->sr = (uint16_t)(o->imm & 0xf71f);
        rr_sync_sp(c, old);
        c->stop = 1;
        return;
    }
    case MOP_RESET:
        // Drives the external reset line; MAME's GT driver leaves it
        // unhooked, so this is a no-op here too.
        return;
    case MOP_BKPT: case MOP_CALLM: case MOP_RTM:
        // Emulator breakpoint and module calls: not implemented on this
        // board; the CPU traps as illegal.
        rr_exc(4, pc);
        return;
    default:
        rr_exc(4, pc);
        return;
    }
}
