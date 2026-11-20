// C31 disassembler: formats decoded instructions for tests and debugging.
#include "c31dis.h"
#include <stdio.h>

static const char *const d_regname[32] = {
    "R0", "R1", "R2", "R3", "R4", "R5", "R6", "R7",
    "AR0", "AR1", "AR2", "AR3", "AR4", "AR5", "AR6", "AR7",
    "DP", "IR0", "IR1", "BK", "SP", "ST", "IE", "IF",
    "IOF", "RS", "RE", "RC", "X0", "X1", "X2", "X3"
};

static const char *const d_condname[32] = {
    "U", "LO", "LS", "HI", "HS", "EQ", "NE", "LT",
    "LE", "GT", "GE", "C11", "NV", "V", "NUF", "UF",
    "NLV", "LV", "NLUF", "LUF", "ZUF", "C21", "C22", "C23",
    "C24", "C25", "C26", "C27", "C28", "C29", "C30", "C31"
};

static char *fmt_ind(char *p, uint32_t ma, int disp) {
    uint32_t mode = (ma >> 3) & 31, ar = ma & 7;
    char ds[16];
    if (disp < 0)
        sprintf(ds, "(-%X)", (unsigned)(-disp) & 0xffffu);
    else if (disp > 0)
        sprintf(ds, "(%X)", (unsigned)disp);
    else
        ds[0] = 0;
    switch (mode) {
    case 0: p += sprintf(p, "*+AR%u%s", ar, ds); break;
    case 1: p += sprintf(p, "*-AR%u%s", ar, ds); break;
    case 2: p += sprintf(p, "*++AR%u%s", ar, ds); break;
    case 3: p += sprintf(p, "*--AR%u%s", ar, ds); break;
    case 4: p += sprintf(p, "*AR%u++%s", ar, ds); break;
    case 5: p += sprintf(p, "*AR%u--%s", ar, ds); break;
    case 6: p += sprintf(p, "*AR%u++%s%%", ar, ds); break;
    case 7: p += sprintf(p, "*AR%u--%s%%", ar, ds); break;
    case 8: p += sprintf(p, "*+AR%u(IR0)", ar); break;
    case 9: p += sprintf(p, "*-AR%u(IR0)", ar); break;
    case 10: p += sprintf(p, "*++AR%u(IR0)", ar); break;
    case 11: p += sprintf(p, "*--AR%u(IR0)", ar); break;
    case 12: p += sprintf(p, "*AR%u++(IR0)", ar); break;
    case 13: p += sprintf(p, "*AR%u--(IR0)", ar); break;
    case 14: p += sprintf(p, "*AR%u++(IR0)%%", ar); break;
    case 15: p += sprintf(p, "*AR%u--(IR0)%%", ar); break;
    case 16: p += sprintf(p, "*+AR%u(IR1)", ar); break;
    case 17: p += sprintf(p, "*-AR%u(IR1)", ar); break;
    case 18: p += sprintf(p, "*++AR%u(IR1)", ar); break;
    case 19: p += sprintf(p, "*--AR%u(IR1)", ar); break;
    case 20: p += sprintf(p, "*AR%u++(IR1)", ar); break;
    case 21: p += sprintf(p, "*AR%u--(IR1)", ar); break;
    case 22: p += sprintf(p, "*AR%u++(IR1)%%", ar); break;
    case 23: p += sprintf(p, "*AR%u--(IR1)%%", ar); break;
    case 24: p += sprintf(p, "*AR%u", ar); break;
    case 25: p += sprintf(p, "*AR%u++(IR0)B", ar); break;
    default: p += sprintf(p, "(ill%u)", mode); break;
    }
    return p;
}

static const char *d_mnemo(uint32_t op) {
    switch (op) {
    case DOP_ABSF: return "ABSF"; case DOP_ABSI: return "ABSI";
    case DOP_ADDC: return "ADDC"; case DOP_ADDF: return "ADDF";
    case DOP_ADDI: return "ADDI"; case DOP_AND: return "AND";
    case DOP_ANDN: return "ANDN"; case DOP_ASH: return "ASH";
    case DOP_CMPF: return "CMPF"; case DOP_CMPI: return "CMPI";
    case DOP_FIX: return "FIX"; case DOP_FLOAT: return "FLOAT";
    case DOP_IDLE: return "IDLE"; case DOP_LDE: return "LDE";
    case DOP_LDF: return "LDF"; case DOP_LDFI: return "LDFI";
    case DOP_LDI: return "LDI"; case DOP_LDII: return "LDII";
    case DOP_LDM: return "LDM"; case DOP_LSH: return "LSH";
    case DOP_MPYF: return "MPYF"; case DOP_MPYI: return "MPYI";
    case DOP_NEGB: return "NEGB"; case DOP_NEGF: return "NEGF";
    case DOP_NEGI: return "NEGI"; case DOP_NOP: return "NOP";
    case DOP_NORM: return "NORM"; case DOP_NOT: return "NOT";
    case DOP_POP: return "POP"; case DOP_POPF: return "POPF";
    case DOP_PUSH: return "PUSH"; case DOP_PUSHF: return "PUSHF";
    case DOP_OR: return "OR"; case DOP_SPEED: return "SPEED";
    case DOP_RND: return "RND"; case DOP_ROL: return "ROL";
    case DOP_ROLC: return "ROLC"; case DOP_ROR: return "ROR";
    case DOP_RORC: return "RORC"; case DOP_RPTS: return "RPTS";
    case DOP_STF: return "STF"; case DOP_STFI: return "STFI";
    case DOP_STI: return "STI"; case DOP_STII: return "STII";
    case DOP_SIGI: return "SIGI"; case DOP_SUBB: return "SUBB";
    case DOP_SUBC: return "SUBC"; case DOP_SUBF: return "SUBF";
    case DOP_SUBI: return "SUBI"; case DOP_SUBRB: return "SUBRB";
    case DOP_SUBRF: return "SUBRF"; case DOP_SUBRI: return "SUBRI";
    case DOP_TSTB: return "TSTB"; case DOP_XOR: return "XOR";
    case DOP_IACK: return "IACK";
    case DOP_ADDC3: return "ADDC3"; case DOP_ADDF3: return "ADDF3";
    case DOP_ADDI3: return "ADDI3"; case DOP_AND3: return "AND3";
    case DOP_ANDN3: return "ANDN3"; case DOP_ASH3: return "ASH3";
    case DOP_CMPF3: return "CMPF3"; case DOP_CMPI3: return "CMPI3";
    case DOP_LSH3: return "LSH3"; case DOP_MPYF3: return "MPYF3";
    case DOP_MPYI3: return "MPYI3"; case DOP_OR3: return "OR3";
    case DOP_SUBB3: return "SUBB3"; case DOP_SUBF3: return "SUBF3";
    case DOP_SUBI3: return "SUBI3"; case DOP_TSTB3: return "TSTB3";
    case DOP_XOR3: return "XOR3";
    default: return "?";
    }
}

char *c31_format(char *buf, const d_insn_t *o, uint32_t pc) {
    char *p = buf;
    if (o->op == DOP_ILL) {
        sprintf(p, ".ill");
        return buf;
    }
    switch (o->op) {
    case DOP_BR:
        sprintf(p, "BR $%06X", o->addr & 0xffffffu);
        return buf;
    case DOP_BRD:
        sprintf(p, "BRD $%06X", o->addr & 0xffffffu);
        return buf;
    case DOP_CALL:
        sprintf(p, "CALL $%06X", o->addr & 0xffffffu);
        return buf;
    case DOP_RPTB:
        sprintf(p, "RPTB $%06X", o->addr & 0xffffffu);
        return buf;
    case DOP_SWI:
        sprintf(p, "SWI");
        return buf;
    case DOP_BCOND: case DOP_BDCOND: {
        const char *d = (o->op == DOP_BDCOND) ? "D" : "";
        if (o->g == 1)
            sprintf(p, "B%s%s $%06X", d_condname[o->cc], d,
                    (pc + 1 + o->addr + (o->op == DOP_BDCOND ? 2 : 0)) &
                    0xffffffu);
        else
            sprintf(p, "B%s%s %s", d_condname[o->cc], d,
                    d_regname[o->sreg & 31]);
        return buf;
    }
    case DOP_DBCOND: case DOP_DBDCOND: {
        const char *d = (o->op == DOP_DBDCOND) ? "D" : "";
        if (o->g == 1)
            sprintf(p, "DB%s%s AR%u,$%06X", d_condname[o->cc], d,
                    o->dreg & 7, (pc + 1 + o->addr +
                    (o->op == DOP_DBDCOND ? 2 : 0)) & 0xffffffu);
        else
            sprintf(p, "DB%s%s AR%u,%s", d_condname[o->cc], d,
                    o->dreg & 7, d_regname[o->sreg & 31]);
        return buf;
    }
    case DOP_CALLC:
        if (o->g == 1)
            sprintf(p, "CALL%s $%06X", d_condname[o->cc],
                    (pc + 1 + o->addr) & 0xffffffu);
        else
            sprintf(p, "CALL%s %s", d_condname[o->cc],
                    d_regname[o->sreg & 31]);
        return buf;
    case DOP_TRAPC:
        sprintf(p, "TRAP%s $%02X", d_condname[o->cc], o->addr & 0x3fu);
        return buf;
    case DOP_RETI:
        sprintf(p, "RETI%s", d_condname[o->cc]);
        return buf;
    case DOP_RETS:
        sprintf(p, "RETS%s", d_condname[o->cc]);
        return buf;
    case DOP_LDFC:
        p += sprintf(p, "LDF%s ", d_condname[o->cc]);
        goto general;
    case DOP_LDIC:
        p += sprintf(p, "LDI%s ", d_condname[o->cc]);
        goto general;
    case DOP_P_MPYADD: case DOP_P_MPYSUB: {
        const char *m = (o->op == DOP_P_MPYADD) ? "ADDF3" : "SUBF3";
        const char *f = (o->g & 4) ? "I" : "F";
        char s1[32], s2[32], s3[48], s4[48], *q;
        char *r1 = s1, *r2 = s2;
        sprintf(s1, "R%u", o->sreg & 7);
        sprintf(s2, "R%u", o->sreg2 & 7);
        q = s3; q = fmt_ind(q, o->ma1, 1); *q = 0;
        q = s4; q = fmt_ind(q, o->ma2, 1); *q = 0;
        sprintf(p, "MPY%s3 ..,R%u || %s%s ..,R%u ; %s,%s,%s,%s", f,
                o->dreg & 7, (o->op == DOP_P_MPYADD) ? "ADD" : "SUB", f,
                o->dreg2 & 7, r1, r2, s3, s4);
        (void)m;
        return buf;
    }
    case DOP_P_STST: {
        char d1[48], d2[48], *q;
        q = d1; q = fmt_ind(q, o->ma1, 1); *q = 0;
        q = d2; q = fmt_ind(q, o->ma2, 1); *q = 0;
        sprintf(p, "%s R%u,%s || %s R%u,%s", o->g ? "STI" : "STF",
                o->sreg & 7, d1, o->g ? "STI" : "STF", o->sreg2 & 7, d2);
        return buf;
    }
    case DOP_P_LDLD: {
        char s1[48], s2[48], *q;
        q = s1; q = fmt_ind(q, o->ma1, 1); *q = 0;
        q = s2; q = fmt_ind(q, o->ma2, 1); *q = 0;
        sprintf(p, "%s %s,R%u || %s %s,R%u", o->g ? "LDI" : "LDF", s1,
                o->dreg & 7, o->g ? "LDI" : "LDF", s2, o->dreg2 & 7);
        return buf;
    }
    case DOP_P_ALUST: {
        char ms[48], ds[48], *q;
        q = ms; q = fmt_ind(q, o->ma2, 1); *q = 0;
        q = ds; q = fmt_ind(q, o->ma1, 1); *q = 0;
        sprintf(p, "%s ..,R%u || ST. R%u,%s ; %s", d_mnemo(d_par_tab[o->g]),
                o->dreg & 7, o->sreg2 & 7, ds, ms);
        return buf;
    }
    default:
        break;
    }
    p += sprintf(p, "%-6s", d_mnemo(o->op));
general:
    // General-operand suffix (also used by LDFC/LDIC).
    if (o->op == DOP_STF || o->op == DOP_STFI || o->op == DOP_STI ||
            o->op == DOP_STII) {
        p += sprintf(p, "%s,", (o->op == DOP_STF || o->op == DOP_STFI) ?
                d_regname[o->dreg & 7] : d_regname[o->dreg & 31]);
        if (o->g == 1) p += sprintf(p, "($%04X)", o->addr & 0xffffu);
        else p = fmt_ind(p, o->ma1, o->disp);
        return buf;
    }
    if (o->op == DOP_NOP && o->g == 2) {
        p = fmt_ind(p, o->ma1, o->disp);
        return buf;
    }
    if (o->op == DOP_POP || o->op == DOP_POPF || o->op == DOP_PUSH ||
            o->op == DOP_PUSHF || o->op == DOP_ROL || o->op == DOP_ROLC ||
            o->op == DOP_ROR || o->op == DOP_RORC || o->op == DOP_IDLE ||
            o->op == DOP_SPEED || o->op == DOP_SIGI || o->op == DOP_NOP) {
        if (o->op == DOP_PUSH || o->op == DOP_POP)
            p += sprintf(p, "%s", d_regname[o->dreg & 31]);
        else if (o->op == DOP_PUSHF || o->op == DOP_POPF)
            p += sprintf(p, "R%u", o->dreg & 7);
        else if (o->op == DOP_SPEED)
            p += sprintf(p, "%s", (o->addr & 1) ? "LOPOWER" : "MAXSPEED");
        else if (o->op == DOP_ROL || o->op == DOP_ROLC ||
                o->op == DOP_ROR || o->op == DOP_RORC)
            p += sprintf(p, "%s", d_regname[o->dreg & 31]);
        return buf;
    }
    if (o->op >= DOP_ADDC3 && o->op <= DOP_XOR3) {
        if (o->g & 1) p = fmt_ind(p, o->ma1, 1);
        else p += sprintf(p, "%s", d_regname[o->sreg & 31]);
        p += sprintf(p, ",");
        if (o->g & 2) p = fmt_ind(p, o->ma2, 1);
        else p += sprintf(p, "%s", d_regname[o->sreg2 & 31]);
        if (o->op != DOP_CMPF3 && o->op != DOP_CMPI3 &&
                o->op != DOP_TSTB3)
            p += sprintf(p, ",%s", d_regname[o->dreg & 31]);
        return buf;
    }
    if (o->g == 0) p += sprintf(p, "%s,", d_regname[o->sreg & 31]);
    else if (o->g == 1) p += sprintf(p, "($%04X),", o->addr & 0xffffu);
    else if (o->g == 2) {
        p = fmt_ind(p, o->ma1, o->disp);
        p += sprintf(p, ",");
    } else p += sprintf(p, "$%04X,", o->addr & 0xffffu);
    if (o->op == DOP_RPTS || o->op == DOP_IACK) {
        if (p > buf && p[-1] == ',') { p--; *p = 0; }
        return buf;
    }
    p += sprintf(p, "%s", d_regname[o->dreg & 31]);
    return buf;
}
