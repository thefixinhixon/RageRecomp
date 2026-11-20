// TMS320C31 static translator: recursive-traversal planning plus C
// emission. Delayed branches inline their slots; RPTB/RPTS ranges force
// block splits at RE+1 so the repeat check in the epilogue stays exact.
#include "c31rec.h"
#include <stdlib.h>
#include <string.h>

void drec_plan_init(drec_plan_t *p, const uint32_t *img, uint32_t base,
        uint32_t words) {
    memset(p, 0, sizeof(*p));
    p->img = img;
    p->base = base;
    p->words = words;
    p->is_start = (uint8_t *)calloc(words + 1, 1);
    p->seen = (uint8_t *)calloc(words + 1, 1);
}

void drec_plan_free(drec_plan_t *p) {
    free(p->is_start);
    free(p->seen);
    free(p->starts);
    free(p->work);
    free(p->re_ends);
    memset(p, 0, sizeof(*p));
}

static int in_img(drec_plan_t *p, uint32_t pc) {
    return pc >= p->base && pc < p->base + p->words;
}

void drec_plan_entry(drec_plan_t *p, uint32_t pc) {
    if (!in_img(p, pc)) return;
    if (p->is_start[pc - p->base]) return;
    // Mark early: entries are always starts (dedupes the worklist).
    p->is_start[pc - p->base] = 1;
    if (p->nstarts >= p->cap) {
        p->cap = p->cap ? p->cap * 2 : 4096;
        p->starts = (uint32_t *)realloc(p->starts,
                (size_t)p->cap * sizeof(uint32_t));
    }
    p->starts[p->nstarts++] = pc;
    if (p->nwork >= p->workcap) {
        p->workcap = p->workcap ? p->workcap * 2 : 1024;
        p->work = (uint32_t *)realloc(p->work,
                (size_t)p->workcap * sizeof(uint32_t));
    }
    p->work[p->nwork++] = pc;
}

static void plan_add_re_end(drec_plan_t *p, uint32_t pc) {
    int i;
    for (i = 0; i < p->nre; i++) {
        if (p->re_ends[i] == pc) return;
    }
    if (p->nre >= p->recap) {
        p->recap = p->recap ? p->recap * 2 : 64;
        p->re_ends = (uint32_t *)realloc(p->re_ends,
                (size_t)p->recap * sizeof(uint32_t));
    }
    p->re_ends[p->nre++] = pc;
}

static int cmp_u32(const void *a, const void *b) {
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}

void drec_plan_run(drec_plan_t *p) {
    // visited set: words already walked through as a block start.
    uint8_t *done = (uint8_t *)calloc(p->words + 1, 1);
    while (p->nwork > 0) {
        uint32_t pc = p->work[--p->nwork], cur = pc;
        d_insn_t in;
        if (!in_img(p, cur) || done[cur - p->base]) continue;
        done[cur - p->base] = 1;
        for (;;) {
            uint32_t npc;
            if (!in_img(p, cur)) break;
            if (cur != pc && p->is_start[cur - p->base]) break;
            p->seen[cur - p->base] = 1;
            d_decode(p->img[cur - p->base], &in);
            npc = (cur + 1) & 0xffffffu;
            switch (in.flow) {
            case DFLW_NONE:
                if (in.op == DOP_RPTS) {
                    // Repeated insn is npc; split after it.
                    plan_add_re_end(p, (npc + 1) & 0xffffffu);
                    drec_plan_entry(p, (npc + 1) & 0xffffffu);
                } else if (in.op == DOP_RPTB) {
                    plan_add_re_end(p, (in.addr + 1) & 0xffffffu);
                    drec_plan_entry(p, (in.addr + 1) & 0xffffffu);
                }
                cur = npc;
                continue;
            case DFLW_BRANCH: // cond branches (delayed or not) + DBcond
                if (in.op == DOP_BCOND || in.op == DOP_BDCOND) {
                    if (in.g == 1) {
                        uint32_t t = (in.op == DOP_BDCOND) ?
                                (npc + 2 + in.addr) & 0xffffffu :
                                (npc + in.addr) & 0xffffffu;
                        drec_plan_entry(p, t);
                    }
                    // Register targets are runtime values.
                } else {
                    // DBcond: same, plus the counter always falls through.
                    if (in.g == 1) {
                        uint32_t t = (in.op == DOP_DBDCOND) ?
                                (npc + 2 + in.addr) & 0xffffffu :
                                (npc + in.addr) & 0xffffffu;
                        drec_plan_entry(p, t);
                    }
                }
                drec_plan_entry(p, npc); // delay slots / fallthrough
                break;
            case DFLW_JUMP: // BR / BRD: static targets
            case DFLW_CALL: // CALL: static target + fallthrough
                drec_plan_entry(p, in.addr & 0xffffffu);
                if (in.flow == DFLW_CALL || in.op == DOP_BRD)
                    drec_plan_entry(p, npc);
                break;
            case DFLW_RET: // RETIc/RETSc: fallthrough only if conditional
                if (in.cc != 0) drec_plan_entry(p, npc);
                break;
            default: // TRAP: falls through after return
                drec_plan_entry(p, npc);
                break;
            }
            break;
        }
    }
    free(done);
    qsort(p->starts, (size_t)p->nstarts, sizeof(uint32_t), cmp_u32);
}

int drec_plan_has(drec_plan_t *p, uint32_t pc) {
    if (!in_img(p, pc)) return 0;
    return p->is_start[pc - p->base] != 0;
}

int drec_is_re_end(drec_plan_t *p, uint32_t pc) {
    int i;
    for (i = 0; i < p->nre; i++) {
        if (p->re_ends[i] == pc) return 1;
    }
    return 0;
}

// ------------------------------------------------------------- emission

typedef struct {
    FILE *f;
    drec_plan_t *plan;
    int chunk;
    uint32_t lo, hi;
    int slow_n;
} demit_t;

static int d_in_chunk(demit_t *x, uint32_t pc) {
    return pc >= x->lo && pc < x->hi && drec_plan_has(x->plan, pc);
}

// Goto targets that are repeat ends must go through the dispatcher so the
// loop-back check runs; all others chain directly.
static void d_emit_goto(demit_t *x, uint32_t tgt, int cyc, int n) {
    FILE *f = x->f;
    if (d_in_chunk(x, tgt) && !drec_is_re_end(x->plan, tgt)) {
        fprintf(f, "if (d_blk_end(0x%xu, %d, %d) != RR_CONT) return RR_EXIT;\n",
                tgt, cyc, n);
        fprintf(f, "goto L_%x;\n", tgt);
    } else {
        fprintf(f, "return d_blk_jump(0x%xu, %d, %d);\n", tgt, cyc, n);
    }
}

static void d_emit_fall(demit_t *x, uint32_t npc, int cyc, int n) {
    d_emit_goto(x, npc, cyc, n);
}

static void d_emit_slow(demit_t *x, const d_insn_t *in) {
    FILE *f = x->f;
    int id = x->slow_n++;
    fprintf(f, "static const d_insn_t I_%d_%d = {%u,%u,%u,%u,%u,%u,%u,%u,%u,"
            "%u,%uu,%u,%u};\n", x->chunk, id, in->op, in->cc, in->g,
            in->dreg, in->sreg, in->sreg2, in->dreg2, in->ma1, in->ma2,
            in->disp, in->addr, in->flow, in->xcycles);
    fprintf(f, "d_exec(d, &I_%d_%d);\n", x->chunk, id);
}

// Integer general source into v0 (sext selects the immediate conversion).
static void d_emit_gsrc(demit_t *x, const d_insn_t *o, int sext,
        const char *dst) {
    FILE *f = x->f;
    if (o->g == 0) {
        fprintf(f, "%s = d_ireg(d, %u);\n", dst, o->sreg & 31);
    } else if (o->g == 1) {
        fprintf(f, "%s = rr_drd(((d->dp & 0xffu) << 16) | 0x%xu);\n", dst,
                o->addr & 0xffffu);
    } else if (o->g == 2) {
        fprintf(f, "%s = rr_drd(d_ind(d, 0x%xu, 0x%xu, 0, &dd_, &dvv_));\n",
                dst, o->ma1, o->disp);
    } else if (sext) {
        fprintf(f, "%s = 0x%xu;\n", dst,
                (uint32_t)(int32_t)(int16_t)o->addr);
    } else {
        fprintf(f, "%s = 0x%xu;\n", dst, o->addr & 0xffffu);
    }
}

// Float general source into f0.
static void d_emit_gfsrc(demit_t *x, const d_insn_t *o, const char *dst) {
    FILE *f = x->f;
    if (o->g == 0) {
        fprintf(f, "%s = d->r[%u];\n", dst, o->sreg & 7);
    } else if (o->g == 1) {
        fprintf(f, "d_long2fp(&%s, rr_drd(((d->dp & 0xffu) << 16) | "
                "0x%xu));\n", dst, o->addr & 0xffffu);
    } else if (o->g == 2) {
        fprintf(f, "d_long2fp(&%s, rr_drd(d_ind(d, 0x%xu, 0x%xu, 0, &dd_, "
                "&dvv_)));\n", dst, o->ma1, o->disp);
    } else {
        fprintf(f, "d_short2fp(&%s, 0x%xu);\n", dst, o->addr & 0xffffu);
    }
}

// Emit one instruction. Returns 0 if the block ends here, else 1.
static int d_emit_op(demit_t *x, const d_insn_t *in, uint32_t pc,
        int *cyc, int *nins);

// Straight-line delay slots (3 words, no control flow/repeats/traps).
// Slots must stay in this chunk: an inlined cross-chunk copy would not
// be covered by the written word's own chunk guard.
static int d_slots_simple(demit_t *x, uint32_t pc) {
    drec_plan_t *p = x->plan;
    uint32_t i;
    for (i = 1; i <= 3; i++) {
        d_insn_t s;
        if (pc + i < x->lo || pc + i >= x->hi) return 0;
        if (pc + i < p->base || pc + i >= p->base + p->words) return 0;
        d_decode(p->img[pc + i - p->base], &s);
        if (s.flow != DFLW_NONE) return 0;
        if (s.op == DOP_RPTS || s.op == DOP_RPTB || s.op == DOP_IDLE ||
                s.op == DOP_SWI)
            return 0;
    }
    return 1;
}

// Emit the 3 delay slots inline, accumulating their cycles.
static void d_emit_slots(demit_t *x, uint32_t pc, int *cyc, int *nins) {
    drec_plan_t *p = x->plan;
    uint32_t i;
    for (i = 1; i <= 3; i++) {
        d_insn_t s;
        d_decode(p->img[pc + i - p->base], &s);
        d_emit_op(x, &s, pc + i, cyc, nins);
    }
}

static int d_emit_op(demit_t *x, const d_insn_t *in, uint32_t pc,
        int *cyc, int *nins) {
    FILE *f = x->f;
    uint32_t npc = (pc + 1) & 0xffffffu;
    *cyc += 1 + in->xcycles;
    (*nins)++;
    switch (in->op) {
    case DOP_LDI:
        d_emit_gsrc(x, in, 1, "v0");
        fprintf(f, "d_setreg(d, %u, v0);\n", in->dreg);
        if (in->dreg < 8) {
            fprintf(f, "d_clr_nzvuf(d);\n");
            fprintf(f, "d_or_nz(d, v0);\n");
        } else if (in->dreg >= 19) {
            fprintf(f, "d_special(d, %u);\n", in->dreg);
        }
        return 1;
    case DOP_LDF:
        d_emit_gfsrc(x, in, "f0");
        fprintf(f, "d->r[%u] = f0;\n", in->dreg & 7);
        fprintf(f, "d_clr_nzvuf(d);\n");
        fprintf(f, "d_or_nzf(d, &d->r[%u]);\n", in->dreg & 7);
        return 1;
    case DOP_STI:
        if (in->g == 1)
            fprintf(f, "rr_dwr(((d->dp & 0xffu) << 16) | 0x%xu, "
                    "d_ireg(d, %u));\n", in->addr & 0xffffu, in->dreg);
        else
            fprintf(f, "rr_dwr(d_ind(d, 0x%xu, 0x%xu, 0, &dd_, &dvv_), "
                    "d_ireg(d, %u));\n", in->ma1, in->disp, in->dreg);
        return 1;
    case DOP_STF:
        if (in->g == 1)
            fprintf(f, "rr_dwr(((d->dp & 0xffu) << 16) | 0x%xu, "
                    "d_fp2long(&d->r[%u]));\n", in->addr & 0xffffu,
                    in->dreg & 7);
        else
            fprintf(f, "rr_dwr(d_ind(d, 0x%xu, 0x%xu, 0, &dd_, &dvv_), "
                    "d_fp2long(&d->r[%u]));\n", in->ma1, in->disp,
                    in->dreg & 7);
        return 1;
    case DOP_ADDI: case DOP_SUBI: case DOP_ADDC: case DOP_SUBB:
    case DOP_MPYI: case DOP_CMPI: {
        const char *fn = (in->op == DOP_ADDI) ? "d_addi" :
                (in->op == DOP_SUBI) ? "d_subi" :
                (in->op == DOP_ADDC) ? "d_addc" :
                (in->op == DOP_SUBB) ? "d_subb" :
                (in->op == DOP_MPYI) ? "d_mpyi" : NULL;
        d_emit_gsrc(x, in, 1, "v0");
        if (fn)
            fprintf(f, "%s(d, %u, d_ireg(d, %u), v0);\n", fn, in->dreg,
                    in->dreg);
        else
            fprintf(f, "d_cmpi(d, d_ireg(d, %u), v0);\n", in->dreg);
        return 1;
    }
    case DOP_ADDF: case DOP_SUBF: case DOP_MPYF: case DOP_CMPF: {
        d_emit_gfsrc(x, in, "f0");
        if (in->op == DOP_CMPF) {
            fprintf(f, "d_subf(d, &f1, &d->r[%u], &f0);\n", in->dreg & 7);
        } else {
            const char *fn = (in->op == DOP_ADDF) ? "d_addf" :
                    (in->op == DOP_SUBF) ? "d_subf" : "d_mpyf";
            fprintf(f, "%s(d, &d->r[%u], &d->r[%u], &f0);\n", fn,
                    in->dreg & 7, in->dreg & 7);
        }
        return 1;
    }
    case DOP_AND: case DOP_OR: case DOP_XOR: case DOP_ANDN:
    case DOP_NOT: {
        d_emit_gsrc(x, in, 0, "v0");
        if (in->op == DOP_AND)
            fprintf(f, "d_logic(d, %u, d_ireg(d, %u) & v0);\n", in->dreg,
                    in->dreg);
        else if (in->op == DOP_OR)
            fprintf(f, "d_logic(d, %u, d_ireg(d, %u) | v0);\n", in->dreg,
                    in->dreg);
        else if (in->op == DOP_XOR)
            fprintf(f, "d_logic(d, %u, d_ireg(d, %u) ^ v0);\n", in->dreg,
                    in->dreg);
        else if (in->op == DOP_ANDN)
            fprintf(f, "d_logic(d, %u, d_ireg(d, %u) & ~v0);\n", in->dreg,
                    in->dreg);
        else
            fprintf(f, "d_logic(d, %u, ~v0);\n", in->dreg);
        return 1;
    }
    case DOP_ASH: case DOP_LSH:
        d_emit_gsrc(x, in, 1, "v0");
        fprintf(f, "%s(d, %u, d_ireg(d, %u), v0);\n",
                in->op == DOP_ASH ? "d_ash" : "d_lsh", in->dreg,
                in->dreg);
        return 1;
    case DOP_NEGI: case DOP_NEGB:
        d_emit_gsrc(x, in, 1, "v0");
        fprintf(f, "%s(d, %u, v0);\n",
                in->op == DOP_NEGI ? "d_negi" : "d_negb", in->dreg);
        return 1;
    case DOP_NEGF:
        d_emit_gfsrc(x, in, "f0");
        fprintf(f, "d_negf(d, &d->r[%u], &f0);\n", in->dreg & 7);
        return 1;
    case DOP_ABSI:
        d_emit_gsrc(x, in, 1, "v0");
        fprintf(f, "d_absi(d, %u, v0);\n", in->dreg);
        return 1;
    case DOP_ABSF:
        d_emit_gfsrc(x, in, "f0");
        fprintf(f, "d_absf(d, %u, &f0);\n", in->dreg & 7);
        return 1;
    case DOP_FIX:
        d_emit_gfsrc(x, in, "f0");
        if (in->dreg < 8) {
            fprintf(f, "d->r[%u] = f0;\n", in->dreg);
            fprintf(f, "d_float2int(d, &d->r[%u], 1);\n", in->dreg);
        } else {
            fprintf(f, "d_float2int(d, &f0, 1);\n");
            fprintf(f, "d_setreg(d, %u, (uint32_t)f0.man);\n",
                    in->dreg);
        }
        return 1;
    case DOP_FLOAT:
        d_emit_gsrc(x, in, 1, "v0");
        fprintf(f, "d->r[%u].man = (int32_t)v0;\n", in->dreg & 7);
        fprintf(f, "d_int2float(d, &d->r[%u]);\n", in->dreg & 7);
        return 1;
    case DOP_NORM:
        d_emit_gfsrc(x, in, "f0");
        fprintf(f, "d_norm(d, &d->r[%u], &f0);\n", in->dreg & 7);
        return 1;
    case DOP_RND:
        d_emit_gfsrc(x, in, "f0");
        fprintf(f, "d->r[%u] = f0;\n", in->dreg & 7);
        fprintf(f, "d_rnd(d, %u);\n", in->dreg & 7);
        return 1;
    case DOP_LDE:
        d_emit_gfsrc(x, in, "f0");
        fprintf(f, "d->r[%u].exp = f0.exp;\n", in->dreg & 7);
        fprintf(f, "if (f0.exp == -128) d->r[%u].man = 0;\n",
                in->dreg & 7);
        return 1;
    case DOP_LDM:
        if (in->g == 0) {
            fprintf(f, "d->r[%u].man = d->r[%u].man;\n", in->dreg & 7,
                    in->sreg & 7);
        } else if (in->g == 3) {
            fprintf(f, "d_short2fp(&f0, 0x%xu);\n", in->addr & 0xffffu);
            fprintf(f, "d->r[%u].man = f0.man;\n", in->dreg & 7);
        } else {
            d_emit_gsrc(x, in, 1, "v0");
            fprintf(f, "d->r[%u].man = (int32_t)v0;\n", in->dreg & 7);
        }
        return 1;
    case DOP_TSTB:
        d_emit_gsrc(x, in, 0, "v0");
        fprintf(f, "d_tstb(d, d_ireg(d, %u), v0);\n", in->dreg);
        return 1;
    case DOP_SUBC:
        d_emit_gsrc(x, in, 1, "v0");
        fprintf(f, "d_subc(d, %u, v0);\n", in->dreg);
        return 1;
    case DOP_SUBRB: case DOP_SUBRI:
        d_emit_gsrc(x, in, 1, "v0");
        fprintf(f, "%s(d, %u, v0, d_ireg(d, %u));\n",
                in->op == DOP_SUBRB ? "d_subb" : "d_subi", in->dreg,
                in->dreg);
        return 1;
    case DOP_SUBRF:
        d_emit_gfsrc(x, in, "f0");
        fprintf(f, "f1 = d->r[%u];\n", in->dreg & 7);
        fprintf(f, "d_subf(d, &d->r[%u], &f0, &f1);\n", in->dreg & 7);
        return 1;
    case DOP_ROL: case DOP_ROLC: case DOP_ROR: case DOP_RORC:
        fprintf(f, "%s(d, %u);\n",
                in->op == DOP_ROL ? "d_rol" : in->op == DOP_ROLC ?
                "d_rolc" : in->op == DOP_ROR ? "d_ror" : "d_rorc",
                in->dreg);
        return 1;
    case DOP_PUSH:
        fprintf(f, "d->sp++;\n");
        fprintf(f, "rr_dwr(d->sp, d_ireg(d, %u));\n", in->dreg);
        return 1;
    case DOP_PUSHF:
        fprintf(f, "d->sp++;\n");
        fprintf(f, "rr_dwr(d->sp, d_fp2long(&d->r[%u]));\n", in->dreg & 7);
        return 1;
    case DOP_POP:
        fprintf(f, "v0 = rr_drd(d->sp);\n");
        fprintf(f, "d->sp--;\n");
        fprintf(f, "d_setreg(d, %u, v0);\n", in->dreg);
        if (in->dreg < 8) {
            fprintf(f, "d_clr_nzvuf(d);\n");
            fprintf(f, "d_or_nz(d, v0);\n");
        } else if (in->dreg >= 19) {
            fprintf(f, "d_special(d, %u);\n", in->dreg);
        }
        return 1;
    case DOP_POPF:
        fprintf(f, "d_long2fp(&d->r[%u], rr_drd(d->sp));\n", in->dreg & 7);
        fprintf(f, "d->sp--;\n");
        fprintf(f, "d_clr_nzvuf(d);\n");
        fprintf(f, "d_or_nzf(d, &d->r[%u]);\n", in->dreg & 7);
        return 1;
    case DOP_RPTS:
        if (in->g == 0) fprintf(f, "v0 = d_ireg(d, %u);\n", in->sreg);
        else if (in->g == 1)
            fprintf(f, "v0 = rr_drd(((d->dp & 0xffu) << 16) | 0x%xu);\n",
                    in->addr & 0xffffu);
        else if (in->g == 2)
            fprintf(f, "v0 = rr_drd(d_ind(d, 0x%xu, 0x%xu, 0, &dd_, "
                    "&dvv_));\n", in->ma1, in->disp);
        else fprintf(f, "v0 = 0x%xu;\n", in->addr & 0xffffu);
        fprintf(f, "d->rc = v0;\n");
        fprintf(f, "d->rs = d->re = 0x%xu;\n", npc);
        fprintf(f, "d->st |= 0x100u;\n");
        fprintf(f, "d->rpts_active = 1;\n");
        return 1;
    case DOP_RPTB:
        fprintf(f, "d->rs = 0x%xu;\n", npc);
        fprintf(f, "d->re = 0x%xu;\n", in->addr & 0xffffffu);
        fprintf(f, "d->st |= 0x100u;\n");
        fprintf(f, "d->rpts_active = 0;\n");
        return 1;
    case DOP_LDFC:
        fprintf(f, "if (d_cond(d, %u)) {\n", in->cc);
        d_emit_gfsrc(x, in, "f0");
        fprintf(f, "d->r[%u] = f0;\n", in->dreg & 7);
        fprintf(f, "d_clr_nzvuf(d);\n");
        fprintf(f, "d_or_nzf(d, &d->r[%u]);\n", in->dreg & 7);
        fprintf(f, "}\n");
        return 1;
    case DOP_LDIC:
        fprintf(f, "if (d_cond(d, %u)) {\n", in->cc);
        d_emit_gsrc(x, in, 1, "v0");
        fprintf(f, "d_setreg(d, %u, v0);\n", in->dreg);
        if (in->dreg < 8) {
            fprintf(f, "d_clr_nzvuf(d);\n");
            fprintf(f, "d_or_nz(d, v0);\n");
        } else if (in->dreg >= 19) {
            fprintf(f, "d_special(d, %u);\n", in->dreg);
        }
        fprintf(f, "}\n");
        return 1;
    case DOP_NOP:
        // Indirect NOP still runs the addressing mode (see d_exec).
        if (in->g == 2) d_emit_gsrc(x, in, 1, "v0");
        return 1;
    case DOP_ADDC3: case DOP_ADDI3: case DOP_SUBB3: case DOP_SUBI3:
    case DOP_MPYI3: case DOP_CMPI3: case DOP_AND3: case DOP_ANDN3:
    case DOP_OR3: case DOP_XOR3: case DOP_ASH3: case DOP_LSH3:
    case DOP_TSTB3: {
        // Integer 3-op: src1 (deferred AR) then src2.
        fprintf(f, "dd_ = -1;\n");
        if (in->g & 1)
            fprintf(f, "v0 = rr_drd(d_ind(d, 0x%xu, 1, 1, &dd_, &dvv_));\n",
                    in->ma1);
        else
            fprintf(f, "v0 = d_ireg(d, %u);\n", in->sreg & 31);
        if (in->g & 2)
            fprintf(f, "v1 = rr_drd(d_ind(d, 0x%xu, 1, 0, &dd_, &dvv_));\n",
                    in->ma2);
        else
            fprintf(f, "v1 = d_ireg(d, %u);\n", in->sreg2 & 31);
        fprintf(f, "d_def_apply(d, dd_, dvv_);\n");
        switch (in->op) {
        case DOP_ADDC3:
            fprintf(f, "d_addc(d, %u, v0, v1);\n", in->dreg);
            break;
        case DOP_ADDI3:
            fprintf(f, "d_addi(d, %u, v0, v1);\n", in->dreg);
            break;
        case DOP_SUBB3:
            fprintf(f, "d_subb(d, %u, v0, v1);\n", in->dreg);
            break;
        case DOP_SUBI3:
            fprintf(f, "d_subi(d, %u, v0, v1);\n", in->dreg);
            break;
        case DOP_MPYI3:
            fprintf(f, "d_mpyi(d, %u, v0, v1);\n", in->dreg);
            break;
        case DOP_CMPI3:
            fprintf(f, "d_cmpi(d, v0, v1);\n");
            break;
        case DOP_AND3:
            fprintf(f, "d_logic(d, %u, v0 & v1);\n", in->dreg);
            break;
        case DOP_ANDN3:
            fprintf(f, "d_logic(d, %u, v0 & ~v1);\n", in->dreg);
            break;
        case DOP_OR3:
            fprintf(f, "d_logic(d, %u, v0 | v1);\n", in->dreg);
            break;
        case DOP_XOR3:
            fprintf(f, "d_logic(d, %u, v0 ^ v1);\n", in->dreg);
            break;
        case DOP_ASH3:
            fprintf(f, "d_ash(d, %u, v0, v1);\n", in->dreg);
            break;
        case DOP_LSH3:
            fprintf(f, "d_lsh(d, %u, v0, v1);\n", in->dreg);
            break;
        default:
            fprintf(f, "d_tstb(d, v0, v1);\n");
            break;
        }
        return 1;
    }
    case DOP_ADDF3: case DOP_SUBF3: case DOP_MPYF3: case DOP_CMPF3: {
        const char *fn = (in->op == DOP_ADDF3) ? "d_addf" :
                (in->op == DOP_SUBF3) ? "d_subf" :
                (in->op == DOP_MPYF3) ? "d_mpyf" : NULL;
        fprintf(f, "dd_ = -1;\n");
        if (in->g & 1)
            fprintf(f, "d_long2fp(&f0, rr_drd(d_ind(d, 0x%xu, 1, 1, &dd_, "
                    "&dvv_)));\n", in->ma1);
        else
            fprintf(f, "f0 = d->r[%u];\n", in->sreg & 7);
        if (in->g & 2)
            fprintf(f, "d_long2fp(&f1, rr_drd(d_ind(d, 0x%xu, 1, 0, &dd_, "
                    "&dvv_)));\n", in->ma2);
        else
            fprintf(f, "f1 = d->r[%u];\n", in->sreg2 & 7);
        fprintf(f, "d_def_apply(d, dd_, dvv_);\n");
        if (fn)
            fprintf(f, "%s(d, &d->r[%u], &f0, &f1);\n", fn, in->dreg & 7);
        else {
            fprintf(f, "{\n");
            fprintf(f, "c31reg_t tf;\n");
            fprintf(f, "d_subf(d, &tf, &f0, &f1);\n");
            fprintf(f, "}\n");
        }
        return 1;
    }
    case DOP_P_LDLD: {
        const char *ld = (in->g == 0) ? "d_long2fp" : NULL;
        fprintf(f, "dd_ = -1;\n");
        fprintf(f, "v0 = rr_drd(d_ind(d, 0x%xu, 1, 1, &dd_, &dvv_));\n",
                in->ma1);
        if (ld)
            fprintf(f, "d_long2fp(&d->r[%u], v0);\n", in->dreg & 7);
        else
            fprintf(f, "d->r[%u].man = (int32_t)v0;\n", in->dreg & 7);
        fprintf(f, "v1 = rr_drd(d_ind(d, 0x%xu, 1, 0, &dd_, &dvv_));\n",
                in->ma2);
        if (ld)
            fprintf(f, "d_long2fp(&d->r[%u], v1);\n", in->dreg2 & 7);
        else
            fprintf(f, "d->r[%u].man = (int32_t)v1;\n", in->dreg2 & 7);
        fprintf(f, "d_def_apply(d, dd_, dvv_);\n");
        return 1;
    }
    case DOP_P_STST: {
        int isf = (in->g == 0);
        fprintf(f, "dd_ = -1;\n");
        if (isf)
            fprintf(f, "v0 = d_fp2long(&d->r[%u]);\n", in->sreg & 7);
        else
            fprintf(f, "v0 = d_ireg(d, %u);\n", in->sreg & 7);
        fprintf(f, "v1 = d_ind(d, 0x%xu, 1, 1, &dd_, &dvv_);\n", in->ma1);
        if (isf)
            fprintf(f, "rr_dwr(v1, v0);\n");
        else
            fprintf(f, "rr_dwr(v1, v0);\n");
        if (isf)
            fprintf(f, "v0 = d_fp2long(&d->r[%u]);\n", in->sreg2 & 7);
        else
            fprintf(f, "v0 = d_ireg(d, %u);\n", in->sreg2 & 7);
        fprintf(f, "rr_dwr(d_ind(d, 0x%xu, 1, 0, &dd_, &dvv_), v0);\n",
                in->ma2);
        fprintf(f, "d_def_apply(d, dd_, dvv_);\n");
        return 1;
    }
    case DOP_BR:
        d_emit_goto(x, in->addr & 0xffffffu, *cyc, *nins);
        return 0;
    case DOP_BRD:
        if (d_slots_simple(x, pc)) {
            d_emit_slots(x, pc, cyc, nins);
            d_emit_goto(x, in->addr & 0xffffffu, *cyc, *nins);
        } else {
            // The helper charges the branch plus its slots itself.
            *cyc -= 1 + in->xcycles;
            (*nins)--;
            fprintf(f, "d_exec_delayed_slow(d, 0x%xu);\n", pc);
            fprintf(f, "return d_blk_jump(d->pc, %d, %d);\n", *cyc, *nins);
        }
        return 0;
    case DOP_CALL:
        fprintf(f, "d->sp++;\n");
        fprintf(f, "rr_dwr(d->sp, 0x%xu);\n", npc);
        d_emit_goto(x, in->addr & 0xffffffu, *cyc, *nins);
        return 0;
    case DOP_BCOND: {
        uint32_t tgt = 0;
        int has_tgt = 1;
        if (in->g == 1) tgt = (npc + in->addr) & 0xffffffu;
        else has_tgt = 0;
        if (in->cc == 0 && has_tgt) { // unconditional: direct jump
            d_emit_goto(x, tgt, *cyc, *nins);
            return 0;
        }
        if (in->cc == 0) {
            fprintf(f, "v0 = d_ireg(d, %u) & 0xffffffu;\n", in->sreg);
            fprintf(f, "return d_blk_jump(v0, %d, %d);\n", *cyc, *nins);
            return 0;
        }
        fprintf(f, "if (d_cond(d, %u)) {\n", in->cc);
        if (has_tgt) {
            if (d_in_chunk(x, tgt) && !drec_is_re_end(x->plan, tgt)) {
                fprintf(f, "if (d_blk_end(0x%xu, %d, %d) != RR_CONT) "
                        "return RR_EXIT;\n", npc, *cyc, *nins);
                fprintf(f, "goto L_%x;\n", tgt);
            } else {
                fprintf(f, "return d_blk_jump(0x%xu, %d, %d);\n", tgt,
                        *cyc, *nins);
            }
        } else {
            fprintf(f, "v0 = d_ireg(d, %u) & 0xffffffu;\n", in->sreg);
            fprintf(f, "return d_blk_jump(v0, %d, %d);\n", *cyc, *nins);
        }
        fprintf(f, "}\n");
        d_emit_fall(x, npc, *cyc, *nins);
        return 0;
    }
    case DOP_BDCOND: {
        uint32_t tgt = 0;
        int has_tgt = 1;
        if (in->g == 1) tgt = (npc + 2 + in->addr) & 0xffffffu;
        else has_tgt = 0;
        if (!d_slots_simple(x, pc) && has_tgt) {
            *cyc -= 1 + in->xcycles;
            (*nins)--;
            fprintf(f, "d_exec_delayed_slow(d, 0x%xu);\n", pc);
            fprintf(f, "return d_blk_jump(d->pc, %d, %d);\n", *cyc, *nins);
            return 0;
        }
        if (!has_tgt) {
            // Register target: still inline the slots when simple.
            fprintf(f, "if (d_cond(d, %u)) {\n", in->cc);
            fprintf(f, "v0 = d_ireg(d, %u) & 0xffffffu;\n", in->sreg);
            if (d_slots_simple(x, pc)) {
                d_emit_slots(x, pc, cyc, nins);
                fprintf(f, "return d_blk_jump(v0, %d, %d);\n", *cyc,
                        *nins);
            } else {
                *cyc -= 1 + in->xcycles;
                (*nins)--;
                fprintf(f, "d_exec_delayed_slow(d, 0x%xu);\n", pc);
                fprintf(f, "return d_blk_jump(d->pc, %d, %d);\n", *cyc,
                        *nins);
            }
            fprintf(f, "}\n");
            d_emit_fall(x, npc, *cyc, *nins);
            return 0;
        }
        fprintf(f, "if (d_cond(d, %u)) {\n", in->cc);
        d_emit_slots(x, pc, cyc, nins);
        if (d_in_chunk(x, tgt) && !drec_is_re_end(x->plan, tgt)) {
            fprintf(f, "if (d_blk_end(0x%xu, %d, %d) != RR_CONT) "
                    "return RR_EXIT;\n", npc, *cyc, *nins);
            fprintf(f, "goto L_%x;\n", tgt);
        } else {
            fprintf(f, "return d_blk_jump(0x%xu, %d, %d);\n", tgt, *cyc,
                    *nins);
        }
        fprintf(f, "}\n");
        d_emit_fall(x, npc, *cyc, *nins);
        return 0;
    }
    case DOP_DBCOND: case DOP_DBDCOND: {
        int is_d = (in->op == DOP_DBDCOND);
        uint32_t tgt = 0;
        int has_tgt = 1;
        if (in->g == 1)
            tgt = (npc + (is_d ? 2 : 0) + in->addr) & 0xffffffu;
        else
            has_tgt = 0;
        fprintf(f, "v0 = (d->ar[%u] - 1) & 0xffffffu;\n", in->dreg & 7);
        fprintf(f, "d->ar[%u] = v0 | (d->ar[%u] & 0xff000000u);\n",
                in->dreg & 7, in->dreg & 7);
        if (is_d && !d_slots_simple(x, pc)) {
            *cyc -= 1 + in->xcycles;
            (*nins)--;
            // NB: the AR decrement above already ran; the slow helper
            // would run it again, so inline the rest manually is wrong.
            // Instead the decrement must not be duplicated: since the
            // slow path re-executes the whole branch, undo it here.
            fprintf(f, "d->ar[%u] = ((v0 + 1) & 0xffffffu) | "
                    "(d->ar[%u] & 0xff000000u);\n", in->dreg & 7,
                    in->dreg & 7);
            fprintf(f, "d_exec_delayed_slow(d, 0x%xu);\n", pc);
            fprintf(f, "return d_blk_jump(d->pc, %d, %d);\n", *cyc, *nins);
            return 0;
        }
        fprintf(f, "if (d_cond(d, %u) && !(v0 & 0x800000u)) {\n", in->cc);
        if (is_d) d_emit_slots(x, pc, cyc, nins);
        if (has_tgt) {
            if (d_in_chunk(x, tgt) && !drec_is_re_end(x->plan, tgt)) {
                fprintf(f, "if (d_blk_end(0x%xu, %d, %d) != RR_CONT) "
                        "return RR_EXIT;\n", npc, *cyc, *nins);
                fprintf(f, "goto L_%x;\n", tgt);
            } else {
                fprintf(f, "return d_blk_jump(0x%xu, %d, %d);\n", tgt,
                        *cyc, *nins);
            }
        } else {
            fprintf(f, "v0 = d_ireg(d, %u) & 0xffffffu;\n", in->sreg);
            fprintf(f, "return d_blk_jump(v0, %d, %d);\n", *cyc, *nins);
        }
        fprintf(f, "}\n");
        d_emit_fall(x, npc, *cyc, *nins);
        return 0;
    }
    case DOP_CALLC:
        fprintf(f, "if (d_cond(d, %u)) {\n", in->cc);
        fprintf(f, "d->sp++;\n");
        fprintf(f, "rr_dwr(d->sp, 0x%xu);\n", npc);
        if (in->g == 1) {
            uint32_t tgt = (npc + in->addr) & 0xffffffu;
            if (d_in_chunk(x, tgt) && !drec_is_re_end(x->plan, tgt)) {
                fprintf(f, "if (d_blk_end(0x%xu, %d, %d) != RR_CONT) "
                        "return RR_EXIT;\n", npc, *cyc, *nins);
                fprintf(f, "goto L_%x;\n", tgt);
            } else {
                fprintf(f, "return d_blk_jump(0x%xu, %d, %d);\n", tgt,
                        *cyc, *nins);
            }
        } else {
            fprintf(f, "v0 = d_ireg(d, %u) & 0xffffffu;\n", in->sreg);
            fprintf(f, "return d_blk_jump(v0, %d, %d);\n", *cyc, *nins);
        }
        fprintf(f, "}\n");
        d_emit_fall(x, npc, *cyc, *nins);
        return 0;
    case DOP_TRAPC:
        fprintf(f, "if (d_cond(d, %u)) {\n", in->cc);
        fprintf(f, "d_trap(d, 0x%xu);\n", in->addr & 0x3fu);
        fprintf(f, "return d_blk_jump(d->pc, %d, %d);\n", *cyc, *nins);
        fprintf(f, "}\n");
        d_emit_fall(x, npc, *cyc, *nins);
        return 0;
    case DOP_RETI: case DOP_RETS:
        if (in->cc == 0) {
            fprintf(f, "v0 = rr_drd(d->sp) & 0xffffffu;\n");
            fprintf(f, "d->sp--;\n");
            if (in->op == DOP_RETI) fprintf(f, "d->st |= 0x2000u;\n");
            fprintf(f, "return d_blk_jump(v0, %d, %d);\n", *cyc, *nins);
            return 0;
        }
        fprintf(f, "if (d_cond(d, %u)) {\n", in->cc);
        fprintf(f, "v0 = rr_drd(d->sp) & 0xffffffu;\n");
        fprintf(f, "d->sp--;\n");
        if (in->op == DOP_RETI) fprintf(f, "d->st |= 0x2000u;\n");
        fprintf(f, "return d_blk_jump(v0, %d, %d);\n", *cyc, *nins);
        fprintf(f, "}\n");
        d_emit_fall(x, npc, *cyc, *nins);
        return 0;
    case DOP_IDLE:
        fprintf(f, "d->idle = 1;\n");
        fprintf(f, "d->st |= 0x2000u;\n");
        fprintf(f, "d_check_irqs(d);\n");
        fprintf(f, "d->cycles -= %d * (d->lopower ? 16 : 1);\n", *cyc);
        fprintf(f, "d->total += %d;\n", *nins);
        fprintf(f, "if (d->idle) return RR_EXIT;\n");
        fprintf(f, "return RR_JUMP;\n");
        return 0;
    default:
        break;
    }
    // Slow path: shared semantics (parallel 3op3op/3opstore, SWI, ILL…).
    d_emit_slow(x, in);
    return 1;
}

uint32_t drec_emit(FILE *f, drec_plan_t *p, uint32_t lo, uint32_t hi,
        int chunk_id) {
    demit_t x;
    int i, nblocks = 0;
    uint32_t covered = 0;
    memset(&x, 0, sizeof(x));
    x.f = f;
    x.plan = p;
    x.chunk = chunk_id;
    x.lo = lo;
    x.hi = hi;
    fprintf(f, "// Generated by c31rec. Do not edit.\n");
    fprintf(f, "#define REC_MODULE 1\n");
    fprintf(f, "#include \"rec_abi.h\"\n");
    fprintf(f, "#include \"c31_core.h\"\n");
    fprintf(f, "static rr_rc_t drec_chunk_%d(void) {\n", chunk_id);
    fprintf(f, "c31_t *d = rrm->dsp;\n");
    fprintf(f, "uint32_t v0 = 0, v1 = 0;\n");
    fprintf(f, "c31reg_t f0, f1;\n");
    fprintf(f, "int dd_ = -1;\n");
    fprintf(f, "uint32_t dvv_ = 0;\n");
    fprintf(f, "f0.man = 0; f0.exp = -128; f1 = f0;\n");
    fprintf(f, "switch (d->pc) {\n");
    for (i = 0; i < p->nstarts; i++) {
        uint32_t s = p->starts[i];
        if (s < lo || s >= hi) continue;
        fprintf(f, "case 0x%xu: goto L_%x;\n", s, s);
        nblocks++;
    }
    fprintf(f, "default: return RR_EXIT;\n}\n");
    if (!nblocks) {
        fprintf(f, "(void)v0; (void)v1; (void)dd_; (void)dvv_;\n");
        fprintf(f, "return RR_EXIT;\n}\n");
        return 0;
    }
    for (i = 0; i < p->nstarts; i++) {
        uint32_t start = p->starts[i], cur;
        int cyc = 0, n = 0;
        if (start < lo || start >= hi) continue;
        fprintf(f, "L_%x:\n", start);
        cur = start;
        for (;;) {
            d_insn_t in;
            uint32_t npc;
            int cont;
            if (!in_img(p, cur)) {
                d_emit_fall(&x, cur, cyc, n);
                break;
            }
            if (cur != start && drec_plan_has(p, cur)) {
                d_emit_fall(&x, cur, cyc, n);
                break;
            }
            d_decode(p->img[cur - p->base], &in);
            npc = (cur + 1) & 0xffffffu;
            covered++;
            cont = d_emit_op(&x, &in, cur, &cyc, &n);
            if (!cont) break;
            if (in.flow != DFLW_NONE) {
                d_emit_fall(&x, npc, cyc, n);
                break;
            }
            cur = npc;
        }
    }
    fprintf(f, "return RR_EXIT;\n}\n");
    fprintf(f, "rec_fn_t drec_chunk_%d_fn(void);\n", chunk_id);
    fprintf(f, "rec_fn_t drec_chunk_%d_fn(void) { return drec_chunk_%d; }\n",
            chunk_id, chunk_id);
    return covered;
}
