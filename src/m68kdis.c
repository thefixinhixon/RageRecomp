// 68K disassembler: formats decoded instructions for tests and debugging.
#include "m68kdis.h"
#include <stdio.h>

static const char *const cc_names[16] = {
    "t", "f", "hi", "ls", "cc", "cs", "ne", "eq",
    "vc", "vs", "pl", "mi", "ge", "lt", "gt", "le"
};

static char sz_letter(uint32_t sz) {
    return (sz == MSZ_B) ? 'b' : (sz == MSZ_W) ? 'w' : 'l';
}

static char *fmt_ea(char *p, const m_ea_t *e, uint32_t pc) {
    char idx;
    uint32_t xn;
    (void)pc;
    switch (e->mode) {
    case MEA_D: p += sprintf(p, "d%d", e->reg); break;
    case MEA_A: p += sprintf(p, "a%d", e->reg); break;
    case MEA_AIND: p += sprintf(p, "(a%d)", e->reg); break;
    case MEA_AINC: p += sprintf(p, "(a%d)+", e->reg); break;
    case MEA_ADEC: p += sprintf(p, "-(a%d)", e->reg); break;
    case MEA_ADISP: p += sprintf(p, "(%d,a%d)", e->disp, e->reg); break;
    case MEA_ABSW: p += sprintf(p, "($%x).w", e->disp & 0xffffff); break;
    case MEA_ABSL: p += sprintf(p, "($%x).l", e->disp); break;
    case MEA_PCDISP: p += sprintf(p, "(%d,pc)", e->disp); break;
    case MEA_IMM: p += sprintf(p, "#$%x", e->imm); break;
    case MEA_AIDX: case MEA_PCIDX:
        idx = (e->ireg & 8) ? 'a' : 'd';
        xn = e->ireg & 7;
        if (e->bdsz == 0) {
            if (e->mode == MEA_AIDX)
                p += sprintf(p, "(%d,a%d,%c%d.%c*%d)", e->disp, e->reg,
                        idx, xn, e->ilong ? 'l' : 'w', 1 << e->scale);
            else
                p += sprintf(p, "(%d,pc,%c%d.%c*%d)", e->disp, idx, xn,
                        e->ilong ? 'l' : 'w', 1 << e->scale);
        } else {
            p += sprintf(p, "([%d,%s,%s],%d)",
                    e->disp,
                    e->bs ? "0" : (e->mode == MEA_AIDX ? "an" : "pc"),
                    e->isup ? "0" : "xn", e->od);
        }
        break;
    default: p += sprintf(p, "?"); break;
    }
    return p;
}

static char *fmt_movep_regs(char *p, const m_insn_t *o) {
    if (o->imm == 0)
        p += sprintf(p, "(%d,a%d),d%d", (int32_t)o->imm2, o->r2, o->r1);
    else
        p += sprintf(p, "d%d,(%d,a%d)", o->r1, (int32_t)o->imm2, o->r2);
    return p;
}

char *m68k_format(char *buf, const m_insn_t *o, uint32_t pc) {
    char *p = buf;
    switch (o->op) {
    case MOP_ILL:
        p += sprintf(p, o->imm == 10 ? ".linea" :
                o->imm == 11 ? ".linef" : ".ill");
        return buf;
    case MOP_ILLEGAL:
        p += sprintf(p, "illegal");
        return buf;
    case MOP_MOVE:
        if (o->r1 == 0) {
            p += sprintf(p, "move.%c ", sz_letter(o->sz));
            p = fmt_ea(p, &o->ea, pc);
            p += sprintf(p, ",");
            p = fmt_ea(p, &o->eb, pc);
        } else if (o->r1 == 1 && o->r2 == 1) {
            p += sprintf(p, "move ccr,");
            p = fmt_ea(p, &o->eb, pc);
        } else if (o->r1 == 1) {
            p += sprintf(p, "move ");
            p = fmt_ea(p, &o->ea, pc);
            p += sprintf(p, ",ccr");
        } else if (o->r1 == 2 && o->r2 == 1) {
            p += sprintf(p, "move sr,");
            p = fmt_ea(p, &o->eb, pc);
        } else if (o->r1 == 2) {
            p += sprintf(p, "move ");
            p = fmt_ea(p, &o->ea, pc);
            p += sprintf(p, ",sr");
        } else if (o->r2 == 1) {
            p += sprintf(p, "move usp,a%d", o->ea.reg);
        } else {
            p += sprintf(p, "move a%d,usp", o->ea.reg);
        }
        return buf;
    case MOP_MOVEA:
        p += sprintf(p, "movea.%c ", sz_letter(o->sz));
        p = fmt_ea(p, &o->ea, pc);
        p += sprintf(p, ",a%d", o->r1);
        return buf;
    case MOP_MOVEQ:
        p += sprintf(p, "moveq #$%x,d%d", o->imm & 0xff, o->r1);
        return buf;
    case MOP_MOVEM:
        p += sprintf(p, "movem.%c $%x,", sz_letter(o->sz), o->imm);
        p = fmt_ea(p, &o->ea, pc);
        return buf;
    case MOP_MOVEP:
        p += sprintf(p, "movep.%c ", sz_letter(o->sz));
        p = fmt_movep_regs(p, o);
        return buf;
    case MOP_MOVES: {
        uint32_t rn = (o->imm >> 12) & 7, is_a = (o->imm >> 15) & 1;
        p += sprintf(p, "moves.%c ", sz_letter(o->sz));
        if (o->imm & 0x800) {
            p += sprintf(p, "%c%d,", is_a ? 'a' : 'd', rn);
            p = fmt_ea(p, &o->ea, pc);
        } else {
            p = fmt_ea(p, &o->ea, pc);
            p += sprintf(p, ",%c%d", is_a ? 'a' : 'd', rn);
        }
        return buf;
    }
    case MOP_MOVEC: {
        static const char *rn[16] = { "d0", "d1", "d2", "d3", "d4", "d5",
            "d6", "d7", "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7" };
        const char *cname;
        switch (o->imm & 0xfff) {
        case 0x000: cname = "sfc"; break;
        case 0x001: cname = "dfc"; break;
        case 0x002: cname = "cacr"; break;
        case 0x800: cname = "usp"; break;
        case 0x801: cname = "vbr"; break;
        case 0x802: cname = "caar"; break;
        case 0x803: cname = "msp"; break;
        case 0x804: cname = "isp"; break;
        default: cname = "?"; break;
        }
        if (o->r1 == 0)
            p += sprintf(p, "movec %s,%s", cname, rn[(o->imm >> 12) & 15]);
        else
            p += sprintf(p, "movec %s,%s", rn[(o->imm >> 12) & 15], cname);
        return buf;
    }
    case MOP_LEA:
        p += sprintf(p, "lea ");
        p = fmt_ea(p, &o->ea, pc);
        p += sprintf(p, ",a%d", o->r1);
        return buf;
    case MOP_PEA:
        p += sprintf(p, "pea ");
        p = fmt_ea(p, &o->ea, pc);
        return buf;
    case MOP_ADD: case MOP_SUB: case MOP_CMP: case MOP_AND: case MOP_OR:
    case MOP_EOR: {
        const char *n = (o->op == MOP_ADD) ? "add" : (o->op == MOP_SUB) ?
                "sub" : (o->op == MOP_CMP) ? "cmp" : (o->op == MOP_AND) ?
                "and" : (o->op == MOP_OR) ? "or" : "eor";
        p += sprintf(p, "%s.%c ", n, sz_letter(o->sz));
        if (o->imm & 0x80000000u) {
            p += sprintf(p, "d%d,", o->r1);
            p = fmt_ea(p, &o->ea, pc);
        } else {
            p = fmt_ea(p, &o->ea, pc);
            p += sprintf(p, ",d%d", o->r1);
        }
        return buf;
    }
    case MOP_ADDI: case MOP_SUBI: case MOP_CMPI: case MOP_ANDI: case MOP_ORI:
    case MOP_EORI: {
        const char *n = (o->op == MOP_ADDI) ? "addi" :
                (o->op == MOP_SUBI) ? "subi" : (o->op == MOP_CMPI) ? "cmpi" :
                (o->op == MOP_ANDI) ? "andi" : (o->op == MOP_ORI) ? "ori" :
                "eori";
        if (o->r1 == 1) {
            p += sprintf(p, "%s #$%x,ccr", n, o->imm & 0xff);
            return buf;
        }
        if (o->r1 == 2) {
            p += sprintf(p, "%s #$%x,sr", n, o->imm & 0xffff);
            return buf;
        }
        p += sprintf(p, "%s.%c #$%x,", n, sz_letter(o->sz), o->imm);
        p = fmt_ea(p, &o->ea, pc);
        return buf;
    }
    case MOP_ADDQ: case MOP_SUBQ:
        p += sprintf(p, "%sq.%c #%d,", o->op == MOP_ADDQ ? "add" : "sub",
                sz_letter(o->sz), o->r1);
        p = fmt_ea(p, &o->ea, pc);
        return buf;
    case MOP_ADDA: case MOP_SUBA: case MOP_CMPA: {
        const char *n = (o->op == MOP_ADDA) ? "adda" :
                (o->op == MOP_SUBA) ? "suba" : "cmpa";
        p += sprintf(p, "%s.%c ", n, sz_letter(o->sz));
        p = fmt_ea(p, &o->ea, pc);
        p += sprintf(p, ",a%d", o->r1);
        return buf;
    }
    case MOP_ADDX: case MOP_SUBX:
        if (o->r2 & 0x10)
            p += sprintf(p, "%sx.%c -(a%d),-(a%d)",
                    o->op == MOP_ADDX ? "add" : "sub", sz_letter(o->sz),
                    o->r2 & 7, o->r1);
        else
            p += sprintf(p, "%sx.%c d%d,d%d",
                    o->op == MOP_ADDX ? "add" : "sub", sz_letter(o->sz),
                    o->r2 & 7, o->r1);
        return buf;
    case MOP_ABCD: case MOP_SBCD:
        if (o->r2 & 0x10)
            p += sprintf(p, "%s -(a%d),-(a%d)",
                    o->op == MOP_ABCD ? "abcd" : "sbcd", o->r2 & 7, o->r1);
        else
            p += sprintf(p, "%s d%d,d%d",
                    o->op == MOP_ABCD ? "abcd" : "sbcd", o->r2 & 7, o->r1);
        return buf;
    case MOP_NBCD:
        p += sprintf(p, "nbcd ");
        p = fmt_ea(p, &o->ea, pc);
        return buf;
    case MOP_PACK: case MOP_UNPK:
        if (o->r2 & 0x10)
            p += sprintf(p, "%s.%c -(a%d),-(a%d),#$%x",
                    o->op == MOP_PACK ? "pack" : "unpk", sz_letter(o->sz),
                    o->r2 & 7, o->r1, o->imm & 0xffff);
        else
            p += sprintf(p, "%s.%c d%d,d%d,#$%x",
                    o->op == MOP_PACK ? "pack" : "unpk", sz_letter(o->sz),
                    o->r2 & 7, o->r1, o->imm & 0xffff);
        return buf;
    case MOP_MULS: case MOP_MULU: case MOP_DIVS: case MOP_DIVU: {
        const char *n = (o->op == MOP_MULS) ? "muls" :
                (o->op == MOP_MULU) ? "mulu" : (o->op == MOP_DIVS) ? "divs" :
                "divu";
        p += sprintf(p, "%s.w ", n);
        p = fmt_ea(p, &o->ea, pc);
        p += sprintf(p, ",d%d", o->r1);
        return buf;
    }
    case MOP_MULL: case MOP_DIVL: {
        uint32_t s = (o->imm >> 11) & 1, q = (o->imm >> 10) & 1;
        p += sprintf(p, "%s%c.l ", o->op == MOP_MULL ? "mul" : "div",
                s ? 's' : 'u');
        p = fmt_ea(p, &o->ea, pc);
        if (q || ((o->imm & 7) != ((o->imm >> 12) & 7)))
            p += sprintf(p, ",d%d:d%d", o->imm & 7, (o->imm >> 12) & 7);
        else
            p += sprintf(p, ",d%d", (o->imm >> 12) & 7);
        return buf;
    }
    case MOP_NEGX: case MOP_CLR: case MOP_NEG: case MOP_NOT: case MOP_TST:
    case MOP_TAS: {
        const char *n = (o->op == MOP_NEGX) ? "negx" :
                (o->op == MOP_CLR) ? "clr" : (o->op == MOP_NEG) ? "neg" :
                (o->op == MOP_NOT) ? "not" : (o->op == MOP_TST) ? "tst" :
                "tas";
        p += sprintf(p, "%s.%c ", n, sz_letter(o->sz));
        p = fmt_ea(p, &o->ea, pc);
        return buf;
    }
    case MOP_SWAP: p += sprintf(p, "swap d%d", o->r1); return buf;
    case MOP_EXT:
        p += sprintf(p, "ext.%c d%d", sz_letter(o->sz), o->r1);
        return buf;
    case MOP_EXTB: p += sprintf(p, "extb.l d%d", o->r1); return buf;
    case MOP_EXG:
        if ((o->r2 & 0x30) == 0x00)
            p += sprintf(p, "exg d%d,d%d", o->r1, o->r2 & 7);
        else if ((o->r2 & 0x30) == 0x10)
            p += sprintf(p, "exg a%d,a%d", o->r1, o->r2 & 7);
        else
            p += sprintf(p, "exg d%d,a%d", o->r1, o->r2 & 7);
        return buf;
    case MOP_CMPM:
        p += sprintf(p, "cmpm.%c (a%d)+,(a%d)+", sz_letter(o->sz),
                o->r2 & 7, o->r1);
        return buf;
    case MOP_ASL: case MOP_ASR: case MOP_LSL: case MOP_LSR:
    case MOP_ROL: case MOP_ROR: case MOP_ROXL: case MOP_ROXR: {
        const char *n = (o->op == MOP_ASL) ? "asl" : (o->op == MOP_ASR) ?
                "asr" : (o->op == MOP_LSL) ? "lsl" : (o->op == MOP_LSR) ?
                "lsr" : (o->op == MOP_ROL) ? "rol" : (o->op == MOP_ROR) ?
                "ror" : (o->op == MOP_ROXL) ? "roxl" : "roxr";
        if (o->r1 & 0x80) {
            p += sprintf(p, "%s.w ", n);
            p = fmt_ea(p, &o->ea, pc);
        } else if (o->imm & 0x100) {
            p += sprintf(p, "%s.%c d%d,d%d", n, sz_letter(o->sz),
                    o->imm & 7, o->r1);
        } else {
            p += sprintf(p, "%s.%c #%d,d%d", n, sz_letter(o->sz),
                    o->imm & 0xff, o->r1);
        }
        return buf;
    }
    case MOP_BTST: case MOP_BCHG: case MOP_BCLR: case MOP_BSET: {
        const char *n = (o->op == MOP_BTST) ? "btst" :
                (o->op == MOP_BCHG) ? "bchg" : (o->op == MOP_BCLR) ? "bclr" :
                "bset";
        p += sprintf(p, "%s ", n);
        if (o->r1 & 0x80) p += sprintf(p, "#%d,", o->r1 & 0x1f);
        else p += sprintf(p, "d%d,", o->r1);
        p = fmt_ea(p, &o->ea, pc);
        return buf;
    }
    case MOP_BFTST: case MOP_BFEXTS: case MOP_BFEXTU: case MOP_BFFFO:
    case MOP_BFCHG: case MOP_BFCLR: case MOP_BFSET: case MOP_BFINS: {
        const char *n = (o->op == MOP_BFTST) ? "bftst" :
                (o->op == MOP_BFEXTS) ? "bfexts" :
                (o->op == MOP_BFEXTU) ? "bfextu" :
                (o->op == MOP_BFFFO) ? "bfffo" :
                (o->op == MOP_BFCHG) ? "bfchg" :
                (o->op == MOP_BFCLR) ? "bfclr" :
                (o->op == MOP_BFSET) ? "bfset" : "bfins";
        char off[16], wid[16];
        if (o->imm & 0x800) sprintf(off, "d%d", (o->imm >> 6) & 7);
        else sprintf(off, "%d", (o->imm >> 6) & 31);
        if (o->imm & 0x20) sprintf(wid, "d%d", o->imm & 7);
        else sprintf(wid, "%d", (o->imm & 31) ? (o->imm & 31) : 32);
        if (o->op == MOP_BFEXTS || o->op == MOP_BFEXTU) {
            p += sprintf(p, "%s ", n);
            p = fmt_ea(p, &o->ea, pc);
            p += sprintf(p, "{%s:%s},d%d", off, wid, (o->imm >> 12) & 7);
        } else if (o->op == MOP_BFFFO || o->op == MOP_BFINS) {
            p += sprintf(p, "%s d%d,", n, (o->imm >> 12) & 7);
            p = fmt_ea(p, &o->ea, pc);
            p += sprintf(p, "{%s:%s}", off, wid);
        } else {
            p += sprintf(p, "%s ", n);
            p = fmt_ea(p, &o->ea, pc);
            p += sprintf(p, "{%s:%s}", off, wid);
        }
        return buf;
    }
    case MOP_BCC:
        p += sprintf(p, "b%s $%x", cc_names[o->cc],
                (pc + 2 + o->imm) & 0xffffff);
        return buf;
    case MOP_BRA:
        p += sprintf(p, "bra $%x", (pc + 2 + o->imm) & 0xffffff);
        return buf;
    case MOP_BSR:
        p += sprintf(p, "bsr $%x", (pc + 2 + o->imm) & 0xffffff);
        return buf;
    case MOP_DBCC:
        p += sprintf(p, "db%s d%d,$%x", cc_names[o->cc], o->r1,
                (pc + 2 + o->imm) & 0xffffff);
        return buf;
    case MOP_SCC:
        p += sprintf(p, "s%s ", cc_names[o->cc]);
        p = fmt_ea(p, &o->ea, pc);
        return buf;
    case MOP_TRAP: p += sprintf(p, "trap #%d", o->r1); return buf;
    case MOP_TRAPV: p += sprintf(p, "trapv"); return buf;
    case MOP_TRAPCC:
        p += sprintf(p, "trap%s", cc_names[o->cc]);
        if (o->len > 1) p += sprintf(p, " #$%x", o->imm);
        return buf;
    case MOP_CHK:
        p += sprintf(p, "chk.%c ", sz_letter(o->sz));
        p = fmt_ea(p, &o->ea, pc);
        p += sprintf(p, ",d%d", o->r1);
        return buf;
    case MOP_CHK2: {
        uint32_t reg = (o->imm >> 12) & 7, is_a = (o->imm >> 15) & 1;
        p += sprintf(p, "%s.%c ", (o->imm & 0x800) ? "chk2" : "cmp2",
                sz_letter(o->sz));
        p = fmt_ea(p, &o->ea, pc);
        p += sprintf(p, ",%c%d", is_a ? 'a' : 'd', reg);
        return buf;
    }
    case MOP_CAS:
        p += sprintf(p, "cas.%c d%d,d%d,", sz_letter(o->sz), o->imm & 7,
                (o->imm >> 6) & 7);
        p = fmt_ea(p, &o->ea, pc);
        return buf;
    case MOP_CAS2: {
        uint32_t rn1 = (o->imm >> 12) & 15, rn2 = (o->imm2 >> 12) & 15;
        p += sprintf(p, "cas2.%c d%d:d%d:d%d:d%d,(%c%d):(%c%d)",
                sz_letter(o->sz), o->imm & 7, o->imm2 & 7,
                (o->imm >> 6) & 7, (o->imm2 >> 6) & 7,
                (rn1 & 8) ? 'a' : 'd', rn1 & 7,
                (rn2 & 8) ? 'a' : 'd', rn2 & 7);
        return buf;
    }
    case MOP_JMP:
        p += sprintf(p, "jmp ");
        p = fmt_ea(p, &o->ea, pc);
        return buf;
    case MOP_JSR:
        p += sprintf(p, "jsr ");
        p = fmt_ea(p, &o->ea, pc);
        return buf;
    case MOP_RTS: p += sprintf(p, "rts"); return buf;
    case MOP_RTR: p += sprintf(p, "rtr"); return buf;
    case MOP_RTE: p += sprintf(p, "rte"); return buf;
    case MOP_RTD: p += sprintf(p, "rtd #%d", (int32_t)o->imm); return buf;
    case MOP_LINK:
        p += sprintf(p, "link.%c a%d,#%d", o->sz == MSZ_L ? 'l' : 'w',
                o->r1, (int32_t)o->imm);
        return buf;
    case MOP_UNLK: p += sprintf(p, "unlk a%d", o->r1); return buf;
    case MOP_STOP: p += sprintf(p, "stop #$%x", o->imm & 0xffff); return buf;
    case MOP_RESET: p += sprintf(p, "reset"); return buf;
    case MOP_NOP: p += sprintf(p, "nop"); return buf;
    case MOP_BKPT: p += sprintf(p, "bkpt #%d", o->r1); return buf;
    case MOP_CALLM:
        p += sprintf(p, "callm #$%x,", o->imm & 0xff);
        p = fmt_ea(p, &o->ea, pc);
        return buf;
    case MOP_RTM:
        p += sprintf(p, "rtm %c%d", o->r2 ? 'a' : 'd', o->r1);
        return buf;
    }
    p += sprintf(p, "?op%d", o->op);
    return buf;
}
