// 68EC020 static translator: recursive-traversal planning plus C emission.
// Covers ROM code reachable from the entry points (plus statically
// resolved direct jumps/calls); anything else runs on the interpreter.
#include "m68krec.h"
#include <stdlib.h>
#include <string.h>

void m68krec_plan_init(rec_plan_t *p, const uint8_t *rom, uint32_t size) {
    memset(p, 0, sizeof(*p));
    p->rom = rom;
    p->size = size;
    p->seen = (uint8_t *)calloc((size >> 1) + 1, 1);
    p->is_start = (uint8_t *)calloc((size >> 1) + 1, 1);
}

void m68krec_plan_free(rec_plan_t *p) {
    free(p->seen);
    free(p->is_start);
    free(p->starts);
    free(p->work);
    memset(p, 0, sizeof(*p));
}

void m68krec_plan_entry(rec_plan_t *p, uint32_t pc) {
    if (pc & 1) return;
    if (pc >= p->size) return;
    if (p->seen[pc >> 1]) return;
    if (p->nwork >= p->workcap) {
        p->workcap = p->workcap ? p->workcap * 2 : 1024;
        p->work = (uint32_t *)realloc(p->work,
                (size_t)p->workcap * sizeof(uint32_t));
    }
    p->work[p->nwork++] = pc;
}

static uint32_t rom16(rec_plan_t *p, uint32_t pc) {
    return ((uint32_t)p->rom[pc] << 8) | p->rom[pc + 1];
}

static void plan_add_start(rec_plan_t *p, uint32_t pc) {
    if (p->is_start[pc >> 1]) return;
    p->is_start[pc >> 1] = 1;
    if (p->nstarts >= p->cap) {
        p->cap = p->cap ? p->cap * 2 : 4096;
        p->starts = (uint32_t *)realloc(p->starts,
                (size_t)p->cap * sizeof(uint32_t));
    }
    p->starts[p->nstarts++] = pc;
}

static int cmp_u32(const void *a, const void *b) {
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}

// Resolve a direct jump/call EA to an absolute target. Returns 1 if static.
static int static_ea_target(const m_insn_t *in, uint32_t pc, uint32_t *tgt) {
    switch (in->ea.mode) {
    case MEA_ABSW: case MEA_ABSL:
        *tgt = (uint32_t)in->ea.disp & 0xffffffu;
        return 1;
    case MEA_PCDISP:
        *tgt = (pc + 2 + (uint32_t)in->ea.disp) & 0xffffffu;
        return 1;
    default:
        return 0;
    }
}

void m68krec_plan_run(rec_plan_t *p) {
    uint16_t words[12];
    int wi;
    while (p->nwork > 0) {
        uint32_t pc = p->work[--p->nwork], cur;
        if (pc >= p->size || (pc & 1)) continue;
        if (p->seen[pc >> 1]) continue;
        p->seen[pc >> 1] = 1; // this start is fully processed
        plan_add_start(p, pc);
        cur = pc;
        for (;;) {
            m_insn_t in;
            uint32_t npc, avail, tgt;
            int len;
            if (cur >= p->size || (cur & 1)) break;
            if (cur != pc && p->is_start[cur >> 1]) break; // splits a block
            avail = (p->size - cur) >> 1;
            if (avail > 11) avail = 11;
            if (avail < 1) break;
            for (wi = 0; wi < (int)avail; wi++)
                words[wi] = (uint16_t)rom16(p, cur + (uint32_t)wi * 2);
            len = m68k_decode(cur, words, (int)avail, &in);
            if (len <= 0) break; // data gap: end of the path
            npc = cur + (uint32_t)len * 2;
            switch (in.flow) {
            case MFLW_NONE:
                cur = npc;
                continue;
            case MFLW_BRANCH: case MFLW_DBCC:
                tgt = (cur + 2 + in.imm) & 0xffffffu;
                m68krec_plan_entry(p, tgt);
                m68krec_plan_entry(p, npc);
                break;
            case MFLW_JUMP:
                if (in.op == MOP_BRA) {
                    tgt = (cur + 2 + in.imm) & 0xffffffu;
                    m68krec_plan_entry(p, tgt);
                } else if (static_ea_target(&in, cur, &tgt)) {
                    m68krec_plan_entry(p, tgt);
                }
                break;
            case MFLW_CALL:
                if (in.op == MOP_BSR) {
                    tgt = (cur + 2 + in.imm) & 0xffffffu;
                    m68krec_plan_entry(p, tgt);
                    m68krec_plan_entry(p, npc);
                } else if (static_ea_target(&in, cur, &tgt)) {
                    m68krec_plan_entry(p, tgt);
                    m68krec_plan_entry(p, npc);
                } else {
                    m68krec_plan_entry(p, npc);
                }
                break;
            case MFLW_RET:
                break;
            default: // TRAP/EXIT: execution resumes at the fallthrough
                m68krec_plan_entry(p, npc);
                break;
            }
            break;
        }
    }
    qsort(p->starts, (size_t)p->nstarts, sizeof(uint32_t), cmp_u32);
}

int m68krec_plan_has(rec_plan_t *p, uint32_t pc) {
    if (pc & 1) return 0;
    if (pc >= p->size) return 0;
    return p->is_start[pc >> 1] != 0;
}

// ------------------------------------------------------------- emission

typedef struct {
    FILE *f;
    rec_plan_t *plan;
    int chunk;
    uint32_t lo, hi;
    int nea;
    m_ea_t *eas;    // deduped EA structs
    int *ea_ids;    // hash table (open addressing, -1 = empty)
    int ea_cap;
} emit_t;

static unsigned ea_hash(const m_ea_t *e) {
    // FNV-1a over the struct bytes.
    const unsigned char *p = (const unsigned char *)e;
    unsigned h = 2166136261u, i;
    for (i = 0; i < sizeof(*e); i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

static void emit_init(emit_t *x, FILE *f, rec_plan_t *p, int chunk,
        uint32_t lo, uint32_t hi) {
    memset(x, 0, sizeof(*x));
    x->f = f;
    x->plan = p;
    x->chunk = chunk;
    x->lo = lo;
    x->hi = hi;
    x->ea_cap = 1024;
    x->eas = (m_ea_t *)calloc((size_t)x->ea_cap, sizeof(m_ea_t));
    x->ea_ids = (int *)malloc((size_t)x->ea_cap * sizeof(int));
    {
        int i;
        for (i = 0; i < x->ea_cap; i++) x->ea_ids[i] = -1;
    }
}

static void emit_free(emit_t *x) {
    free(x->eas);
    free(x->ea_ids);
}

// Intern an EA struct, emitting its definition on first use. Returns its id.
static int emit_ea(emit_t *x, const m_ea_t *e) {
    unsigned h = ea_hash(e) % (unsigned)x->ea_cap;
    for (;;) {
        int id = x->ea_ids[h];
        if (id < 0) break;
        if (!memcmp(&x->eas[id], e, sizeof(*e))) return id;
        h = (h + 1) % (unsigned)x->ea_cap;
    }
    if (x->nea * 2 >= x->ea_cap) { // grow (rehash by re-insertion)
        int old_cap = x->ea_cap, i;
        int *old_ids = x->ea_ids;
        x->ea_cap *= 2;
        x->eas = (m_ea_t *)realloc(x->eas,
                (size_t)x->ea_cap * sizeof(m_ea_t));
        x->ea_ids = (int *)malloc((size_t)x->ea_cap * sizeof(int));
        for (i = 0; i < x->ea_cap; i++) x->ea_ids[i] = -1;
        for (i = 0; i < old_cap; i++) {
            if (old_ids[i] < 0) continue;
            h = ea_hash(&x->eas[old_ids[i]]) % (unsigned)x->ea_cap;
            while (x->ea_ids[h] >= 0) h = (h + 1) % (unsigned)x->ea_cap;
            x->ea_ids[h] = old_ids[i];
        }
        free(old_ids);
        return emit_ea(x, e);
    }
    {
        int id = x->nea++;
        x->eas[id] = *e;
        h = ea_hash(e) % (unsigned)x->ea_cap;
        while (x->ea_ids[h] >= 0) h = (h + 1) % (unsigned)x->ea_cap;
        x->ea_ids[h] = id;
        fprintf(x->f, "static const m_ea_t E_%d_%d = {%u,%u,%d,%u,%u,%u,%u,"
                "%u,%u,%u,%u,%d,%uu,%u};\n", x->chunk, id,
                e->mode, e->reg, e->disp, e->ireg, e->ilong, e->scale,
                e->bdsz, e->odsz, e->iis, e->bs, e->isup, e->od, e->imm,
                e->immw);
        return id;
    }
}

static int in_chunk(emit_t *x, uint32_t pc) {
    return pc >= x->lo && pc < x->hi && m68krec_plan_has(x->plan, pc);
}

static void emit_ea_init(FILE *f, const m_ea_t *e) {
    fprintf(f, "{%u,%u,%d,%u,%u,%u,%u,%u,%u,%u,%u,%d,%uu,%u}",
            e->mode, e->reg, e->disp, e->ireg, e->ilong, e->scale,
            e->bdsz, e->odsz, e->iis, e->bs, e->isup, e->od, e->imm, e->immw);
}

// Slow path: run the shared semantics for one instruction.
static void emit_slow(emit_t *x, const m_insn_t *in, uint32_t pc,
        uint32_t npc, int *slow_n) {
    FILE *f = x->f;
    int id = (*slow_n)++;
    fprintf(f, "static const m_insn_t I_%d_%d = {%u,%u,%u,%u,%u,",
            x->chunk, id, in->op, in->sz, in->cc, in->r1, in->r2);
    emit_ea_init(f, &in->ea);
    fprintf(f, ",");
    emit_ea_init(f, &in->eb);
    fprintf(f, ",%uu,%uu,%u,%u,%u};\n", in->imm, in->imm2, in->len,
            in->flow, in->cycles);
    fprintf(f, "c->pc = 0x%xu;\n", npc);
    fprintf(f, "rr_exec(c, &I_%d_%d, 0x%xu);\n", x->chunk, id, pc);
    fprintf(f, "if (c->exc) return RR_EXIT;\n");
}

// Emit one instruction inline. Returns 0 if the block ends here (control
// flow emitted its own exit), else 1. *cyc accumulates cycles, *nins insns.
static int emit_op(emit_t *x, const m_insn_t *in, uint32_t pc, uint32_t npc,
        int *slow_n, int *cyc, int *nins, int blk_cyc, int blk_n) {
    FILE *f = x->f;
    uint32_t sz = in->sz;
    const char *nz = (sz == MSZ_B) ? "rr_set_nz8" :
            (sz == MSZ_W) ? "rr_set_nz16" : "rr_set_nz32";
    (void)blk_cyc; (void)blk_n;
    *cyc += in->cycles;
    (*nins)++;
    switch (in->op) {
    case MOP_MOVE:
        if (in->r1 != 0) { // SR/CCR/USP forms: slow path
            emit_slow(x, in, pc, npc, slow_n);
            return 1;
        } {
            int a = emit_ea(x, &in->ea), b = emit_ea(x, &in->eb);
            fprintf(f, "c->pc = 0x%xu;\n", npc);
            fprintf(f, "v0 = rr_ear(c, &E_%d_%d, %u, 0x%xu);\n", x->chunk,
                    a, sz, pc);
            fprintf(f, "if (c->exc) return RR_EXIT;\n");
            fprintf(f, "%s(c, v0);\n", nz);
            fprintf(f, "rr_eaw(c, &E_%d_%d, %u, 0x%xu, v0);\n", x->chunk,
                    b, sz, pc);
            fprintf(f, "if (c->exc) return RR_EXIT;\n");
            return 1;
        }
    case MOP_MOVEA: {
        int a = emit_ea(x, &in->ea);
        fprintf(f, "c->pc = 0x%xu;\n", npc);
        fprintf(f, "v0 = rr_ear(c, &E_%d_%d, %u, 0x%xu);\n", x->chunk, a,
                sz, pc);
        fprintf(f, "if (c->exc) return RR_EXIT;\n");
        if (sz == MSZ_W)
            fprintf(f, "c->a[%u] = (uint32_t)(int32_t)(int16_t)v0;\n",
                    in->r1);
        else
            fprintf(f, "c->a[%u] = v0;\n", in->r1);
        return 1;
    }
    case MOP_MOVEQ:
        fprintf(f, "c->d[%u] = 0x%xu;\n", in->r1, in->imm);
        fprintf(f, "rr_set_nz32(c, 0x%xu);\n", in->imm);
        return 1;
    case MOP_LEA: {
        int a = emit_ea(x, &in->ea);
        fprintf(f, "c->pc = 0x%xu;\n", npc);
        fprintf(f, "c->a[%u] = rr_ea_addr(c, &E_%d_%d, 0x%xu);\n", in->r1,
                x->chunk, a, pc);
        fprintf(f, "if (c->exc) return RR_EXIT;\n");
        return 1;
    }
    case MOP_PEA: {
        int a = emit_ea(x, &in->ea);
        fprintf(f, "c->pc = 0x%xu;\n", npc);
        fprintf(f, "v0 = rr_ea_addr(c, &E_%d_%d, 0x%xu);\n", x->chunk, a,
                pc);
        fprintf(f, "if (c->exc) return RR_EXIT;\n");
        fprintf(f, "rr_push32(c, v0, 0x%xu);\n", pc);
        fprintf(f, "if (c->exc) return RR_EXIT;\n");
        return 1;
    }
    case MOP_ADD: case MOP_SUB: case MOP_CMP: case MOP_AND: case MOP_OR:
    case MOP_EOR: case MOP_ADDI: case MOP_SUBI: case MOP_CMPI:
    case MOP_ANDI: case MOP_ORI: case MOP_EORI: case MOP_ADDQ:
    case MOP_SUBQ: {
        int a;
        if (in->r1 != 0 && (in->op == MOP_ORI || in->op == MOP_ANDI ||
                in->op == MOP_EORI)) {
            emit_slow(x, in, pc, npc, slow_n); // ...to CCR/SR
            return 1;
        }
        if ((in->op == MOP_ADDQ || in->op == MOP_SUBQ) &&
                in->ea.mode == MEA_A) {
            // Address quick ops are full-width and leave CCR alone.
            fprintf(f, "c->pc = 0x%xu;\n", npc);
            fprintf(f, "c->a[%u] %s= %uu;\n", in->ea.reg,
                    in->op == MOP_ADDQ ? "+" : "-", in->r1);
            return 1;
        }
        a = emit_ea(x, &in->ea);
        int re = (in->imm & 0x80000000u) != 0;
        int is_imm = (in->op == MOP_ADDI || in->op == MOP_SUBI ||
                in->op == MOP_CMPI || in->op == MOP_ANDI ||
                in->op == MOP_ORI || in->op == MOP_EORI);
        int is_q = (in->op == MOP_ADDQ || in->op == MOP_SUBQ);
        int is_cmp = (in->op == MOP_CMP || in->op == MOP_CMPI);
        const char *alu = NULL, *fn = NULL;
        char sfx = (sz == MSZ_B) ? '8' : (sz == MSZ_W) ? '6' : '2';
        // helper suffix: rr_add8/16/32
        const char *w = (sz == MSZ_B) ? "8" : (sz == MSZ_W) ? "16" : "32";
        if (in->op == MOP_ADD || in->op == MOP_ADDI || in->op == MOP_ADDQ)
            fn = "rr_add";
        else if (in->op == MOP_SUB || in->op == MOP_SUBI ||
                in->op == MOP_SUBQ)
            fn = "rr_sub";
        else if (is_cmp)
            fn = "rr_cmp";
        else
            alu = (in->op == MOP_AND || in->op == MOP_ANDI) ? "&" :
                    (in->op == MOP_OR || in->op == MOP_ORI) ? "|" : "^";
        (void)sfx; (void)alu;
        fprintf(f, "c->pc = 0x%xu;\n", npc);
        if (is_imm)
            fprintf(f, "v0 = 0x%xu;\n", in->imm);
        else if (is_q)
            fprintf(f, "v0 = %uu;\n", in->r1);
        else if (re)
            fprintf(f, "v0 = c->d[%u];\n", in->r1);
        else
            fprintf(f, "v0 = rr_ear(c, &E_%d_%d, %u, 0x%xu);\n", x->chunk,
                    a, sz, pc);
        if (!is_imm && !is_q && !re) fprintf(f, "if (c->exc) return RR_EXIT;\n");
        if (re || is_imm || is_q) {
            fprintf(f, "v1 = rr_ear(c, &E_%d_%d, %u, 0x%xu);\n", x->chunk,
                    a, sz, pc);
            fprintf(f, "if (c->exc) return RR_EXIT;\n");
        } else if (sz == MSZ_B) {
            fprintf(f, "v1 = c->d[%u] & 0xffu;\n", in->r1);
        } else if (sz == MSZ_W) {
            fprintf(f, "v1 = c->d[%u] & 0xffffu;\n", in->r1);
        } else {
            fprintf(f, "v1 = c->d[%u];\n", in->r1);
        }
        if (fn && !is_cmp)
            fprintf(f, "r = %s%s(c, v1, v0);\n", fn, w);
        else if (is_cmp) {
            fprintf(f, "%s%s(c, v1, v0);\n", fn, w);
            return 1;
        } else {
            const char *op = (in->op == MOP_AND || in->op == MOP_ANDI) ?
                    "&" : (in->op == MOP_OR || in->op == MOP_ORI) ? "|" :
                    "^";
            fprintf(f, "r = v1 %s v0;\n", op);
            fprintf(f, "%s(c, r);\n", nz);
        }
        if (re || is_imm || is_q) {
            fprintf(f, "rr_eaw_rmw(c, &E_%d_%d, %u, 0x%xu, r);\n", x->chunk,
                    a, sz, pc);
            fprintf(f, "if (c->exc) return RR_EXIT;\n");
        } else if (sz == MSZ_B) {
            fprintf(f, "c->d[%u] = (c->d[%u] & ~0xffu) | r;\n", in->r1,
                    in->r1);
        } else if (sz == MSZ_W) {
            fprintf(f, "c->d[%u] = (c->d[%u] & ~0xffffu) | r;\n", in->r1,
                    in->r1);
        } else {
            fprintf(f, "c->d[%u] = r;\n", in->r1);
        }
        return 1;
    }
    case MOP_ADDA: case MOP_SUBA: case MOP_CMPA: {
        int a = emit_ea(x, &in->ea);
        fprintf(f, "c->pc = 0x%xu;\n", npc);
        fprintf(f, "v0 = rr_ear(c, &E_%d_%d, %u, 0x%xu);\n", x->chunk, a,
                sz, pc);
        fprintf(f, "if (c->exc) return RR_EXIT;\n");
        if (sz == MSZ_W) fprintf(f, "v0 = (uint32_t)(int32_t)(int16_t)v0;\n");
        if (in->op == MOP_ADDA)
            fprintf(f, "c->a[%u] += v0;\n", in->r1);
        else if (in->op == MOP_SUBA)
            fprintf(f, "c->a[%u] -= v0;\n", in->r1);
        else
            fprintf(f, "rr_cmp32(c, c->a[%u], v0);\n", in->r1);
        return 1;
    }
    case MOP_ADDX: case MOP_SUBX: {
        const char *w = (sz == MSZ_B) ? "8" : (sz == MSZ_W) ? "16" : "32";
        const char *fn = (in->op == MOP_ADDX) ? "rr_addx" : "rr_subx";
        uint32_t rx = in->r2 & 7, ry = in->r1;
        if (in->r2 & 0x10) {
            uint32_t st = (sz == MSZ_B) ? 1 : (sz == MSZ_W) ? 2 : 4;
            uint32_t sst = (sz == MSZ_B && rx == 7) ? 2 : st;
            uint32_t dst = (sz == MSZ_B && ry == 7) ? 2 : st;
            fprintf(f, "c->pc = 0x%xu;\n", npc);
            fprintf(f, "c->a[%u] -= %uu; c->a[%u] -= %uu;\n", rx, sst, ry,
                    dst);
            fprintf(f, "v0 = rr_mrd%s(c->a[%u], 0x%xu);\n",
                    sz == MSZ_B ? "8" : sz == MSZ_W ? "16" : "32", rx, pc);
            fprintf(f, "if (c->exc) return RR_EXIT;\n");
            fprintf(f, "v1 = rr_mrd%s(c->a[%u], 0x%xu);\n",
                    sz == MSZ_B ? "8" : sz == MSZ_W ? "16" : "32", ry, pc);
            fprintf(f, "if (c->exc) return RR_EXIT;\n");
            fprintf(f, "r = %s%s(c, v1, v0);\n", fn, w);
            fprintf(f, "rr_mwr%s(c->a[%u], r, 0x%xu);\n",
                    sz == MSZ_B ? "8" : sz == MSZ_W ? "16" : "32", ry, pc);
            fprintf(f, "if (c->exc) return RR_EXIT;\n");
        } else {
            const char *m = (sz == MSZ_B) ? " & 0xffu" :
                    (sz == MSZ_W) ? " & 0xffffu" : "";
            const char *k = (sz == MSZ_B) ? " ~0xffu" :
                    (sz == MSZ_W) ? " ~0xffffu" : NULL;
            fprintf(f, "r = %s%s(c, c->d[%u]%s, c->d[%u]%s);\n", fn, w, ry,
                    m, rx, m);
            if (k)
                fprintf(f, "c->d[%u] = (c->d[%u] &%s) | r;\n", ry, ry, k);
            else
                fprintf(f, "c->d[%u] = r;\n", ry);
        }
        return 1;
    }
    case MOP_NEGX: case MOP_CLR: case MOP_NEG: case MOP_NOT: case MOP_TST:
    case MOP_TAS: {
        int a = emit_ea(x, &in->ea);
        const char *w = (sz == MSZ_B) ? "8" : (sz == MSZ_W) ? "16" : "32";
        fprintf(f, "c->pc = 0x%xu;\n", npc);
        fprintf(f, "v0 = rr_ear(c, &E_%d_%d, %u, 0x%xu);\n", x->chunk, a,
                sz, pc);
        fprintf(f, "if (c->exc) return RR_EXIT;\n");
        if (in->op == MOP_NEGX)
            fprintf(f, "r = rr_subx%s(c, 0, v0);\n", w);
        else if (in->op == MOP_CLR)
            fprintf(f, "r = 0;\n");
        else if (in->op == MOP_NEG)
            fprintf(f, "r = rr_sub%s(c, 0, v0);\n", w);
        else if (in->op == MOP_NOT) {
            fprintf(f, "r = ~v0;\n");
            fprintf(f, "%s(c, r);\n", nz);
        } else if (in->op == MOP_TAS) {
            fprintf(f, "rr_set_nz8(c, v0);\n");
            fprintf(f, "rr_eaw_rmw(c, &E_%d_%d, 0, 0x%xu, v0 | 0x80u);\n",
                    x->chunk, a, pc);
            fprintf(f, "if (c->exc) return RR_EXIT;\n");
            return 1;
        } else {
            fprintf(f, "%s(c, v0);\n", nz);
            return 1;
        }
        if (in->op == MOP_CLR) {
            fprintf(f, "rr_eaw_rmw(c, &E_%d_%d, %u, 0x%xu, 0);\n", x->chunk, a,
                    sz, pc);
            fprintf(f, "if (c->exc) return RR_EXIT;\n");
            fprintf(f, "c->sr &= (uint16_t)~(0x8u | 0x2u | 0x1u);\n");
            fprintf(f, "c->sr |= 0x4u;\n");
        } else {
            fprintf(f, "rr_eaw_rmw(c, &E_%d_%d, %u, 0x%xu, r);\n", x->chunk, a,
                    sz, pc);
            fprintf(f, "if (c->exc) return RR_EXIT;\n");
        }
        return 1;
    }
    case MOP_SWAP:
        fprintf(f, "v0 = c->d[%u];\n", in->r1);
        fprintf(f, "c->d[%u] = (v0 >> 16) | (v0 << 16);\n", in->r1);
        fprintf(f, "rr_set_nz32(c, c->d[%u]);\n", in->r1);
        return 1;
    case MOP_EXT:
        if (sz == MSZ_W) {
            fprintf(f, "c->d[%u] = (c->d[%u] & ~0xffffu) | "
                    "(uint32_t)(uint16_t)(int16_t)(int8_t)c->d[%u];\n",
                    in->r1, in->r1, in->r1);
            fprintf(f, "rr_set_nz16(c, c->d[%u]);\n", in->r1);
        } else {
            fprintf(f, "c->d[%u] = (uint32_t)(int32_t)(int16_t)c->d[%u];\n",
                    in->r1, in->r1);
            fprintf(f, "rr_set_nz32(c, c->d[%u]);\n", in->r1);
        }
        return 1;
    case MOP_EXTB:
        fprintf(f, "c->d[%u] = (uint32_t)(int32_t)(int8_t)c->d[%u];\n",
                in->r1, in->r1);
        fprintf(f, "rr_set_nz32(c, c->d[%u]);\n", in->r1);
        return 1;
    case MOP_EXG: {
        uint32_t rx = in->r1, ry = in->r2 & 7;
        if ((in->r2 & 0x30) == 0x00)
            fprintf(f, "r = c->d[%u]; c->d[%u] = c->d[%u]; c->d[%u] = r;\n",
                    rx, rx, ry, ry);
        else if ((in->r2 & 0x30) == 0x10)
            fprintf(f, "r = c->a[%u]; c->a[%u] = c->a[%u]; c->a[%u] = r;\n",
                    rx, rx, ry, ry);
        else
            fprintf(f, "r = c->d[%u]; c->d[%u] = c->a[%u]; c->a[%u] = r;\n",
                    rx, rx, ry, ry);
        return 1;
    }
    case MOP_CMPM: {
        uint32_t xa = in->r2 & 7, ya = in->r1;
        uint32_t st = (sz == MSZ_B) ? 1 : (sz == MSZ_W) ? 2 : 4;
        uint32_t stx = (sz == MSZ_B && xa == 7) ? 2 : st;
        uint32_t sty = (sz == MSZ_B && ya == 7) ? 2 : st;
        const char *m = (sz == MSZ_B) ? "8" : (sz == MSZ_W) ? "16" : "32";
        const char *w = (sz == MSZ_B) ? "8" : (sz == MSZ_W) ? "16" : "32";
        fprintf(f, "c->pc = 0x%xu;\n", npc);
        fprintf(f, "v0 = rr_mrd%s(c->a[%u], 0x%xu);\n", m, xa, pc);
        fprintf(f, "c->a[%u] += %uu;\n", xa, stx);
        fprintf(f, "if (c->exc) return RR_EXIT;\n");
        fprintf(f, "v1 = rr_mrd%s(c->a[%u], 0x%xu);\n", m, ya, pc);
        fprintf(f, "c->a[%u] += %uu;\n", ya, sty);
        fprintf(f, "if (c->exc) return RR_EXIT;\n");
        fprintf(f, "rr_cmp%s(c, v1, v0);\n", w);
        return 1;
    }
    case MOP_MULS: case MOP_MULU: {
        int a = emit_ea(x, &in->ea);
        fprintf(f, "c->pc = 0x%xu;\n", npc);
        fprintf(f, "v0 = rr_ear(c, &E_%d_%d, 1, 0x%xu);\n", x->chunk, a,
                pc);
        fprintf(f, "if (c->exc) return RR_EXIT;\n");
        if (in->op == MOP_MULS)
            fprintf(f, "c->d[%u] = (uint32_t)((int32_t)(int16_t)v0 * "
                    "(int32_t)(int16_t)c->d[%u]);\n", in->r1, in->r1);
        else
            fprintf(f, "c->d[%u] = (uint32_t)(v0 & 0xffffu) * "
                    "(c->d[%u] & 0xffffu);\n", in->r1, in->r1);
        fprintf(f, "rr_set_nz32(c, c->d[%u]);\n", in->r1);
        return 1;
    }
    case MOP_DIVS: case MOP_DIVU: {
        int a = emit_ea(x, &in->ea);
        fprintf(f, "c->pc = 0x%xu;\n", npc);
        fprintf(f, "v0 = rr_ear(c, &E_%d_%d, 1, 0x%xu);\n", x->chunk, a,
                pc);
        fprintf(f, "if (c->exc) return RR_EXIT;\n");
        if (in->op == MOP_DIVS) {
            fprintf(f, "if (!(v0 & 0xffffu)) { rr_exc(5, 0x%xu); "
                    "return RR_EXIT; }\n", pc);
            fprintf(f, "c->sr &= (uint16_t)~0x1u;\n");
            fprintf(f, "if (c->d[%u] == 0x80000000u && "
                    "(int16_t)v0 == -1) {\n", in->r1);
            fprintf(f, "c->d[%u] = 0;\n", in->r1);
            fprintf(f, "c->sr &= (uint16_t)~(0x8u|0x4u|0x2u);\n");
            fprintf(f, "c->sr |= 0x4u;\n");
            fprintf(f, "} else {\n");
            fprintf(f, "int32_t q = (int32_t)c->d[%u] / "
                    "(int32_t)(int16_t)v0;\n", in->r1);
            fprintf(f, "int32_t mr = (int32_t)c->d[%u] %% "
                    "(int32_t)(int16_t)v0;\n", in->r1);
            fprintf(f, "if (q == (int32_t)(int16_t)q) {\n");
            fprintf(f, "c->d[%u] = ((uint32_t)(uint16_t)mr << 16) | "
                    "(uint32_t)(uint16_t)q;\n", in->r1);
            fprintf(f, "c->sr &= (uint16_t)~(0x8u|0x4u|0x2u);\n");
            fprintf(f, "if (q & 0x8000) c->sr |= 0x8u;\n");
            fprintf(f, "if (!(q & 0xffff)) c->sr |= 0x4u;\n");
            fprintf(f, "} else c->sr |= 0x2u;\n");
            fprintf(f, "}\n");
        } else {
            fprintf(f, "if (!(v0 & 0xffffu)) { rr_exc(5, 0x%xu); "
                    "return RR_EXIT; }\n", pc);
            fprintf(f, "c->sr &= (uint16_t)~0x1u;\n");
            fprintf(f, "v1 = c->d[%u] / (v0 & 0xffffu);\n", in->r1);
            fprintf(f, "r = c->d[%u] %% (v0 & 0xffffu);\n", in->r1);
            fprintf(f, "if (v1 <= 0xffffu) {\n");
            fprintf(f, "c->d[%u] = (r << 16) | v1;\n", in->r1);
            fprintf(f, "c->sr &= (uint16_t)~(0x8u|0x4u|0x2u);\n");
            fprintf(f, "if (v1 & 0x8000u) c->sr |= 0x8u;\n");
            fprintf(f, "if (!v1) c->sr |= 0x4u;\n");
            fprintf(f, "} else c->sr |= 0x2u;\n");
        }
        return 1;
    }
    case MOP_ASL: case MOP_ASR: case MOP_LSL: case MOP_LSR:
    case MOP_ROL: case MOP_ROR: case MOP_ROXL: case MOP_ROXR: {
        const char *fn = (in->op == MOP_ASL) ? "rr_asl" :
                (in->op == MOP_ASR) ? "rr_asr" :
                (in->op == MOP_LSL) ? "rr_lsl" :
                (in->op == MOP_LSR) ? "rr_lsr" :
                (in->op == MOP_ROL) ? "rr_rol" :
                (in->op == MOP_ROR) ? "rr_ror" :
                (in->op == MOP_ROXL) ? "rr_roxl" : "rr_roxr";
        uint32_t w = (sz == MSZ_B) ? 8 : (sz == MSZ_W) ? 16 : 32;
        if (in->r1 & 0x80) {
            int a = emit_ea(x, &in->ea);
            fprintf(f, "c->pc = 0x%xu;\n", npc);
            fprintf(f, "v0 = rr_ear(c, &E_%d_%d, 1, 0x%xu);\n", x->chunk,
                    a, pc);
            fprintf(f, "if (c->exc) return RR_EXIT;\n");
            fprintf(f, "r = %s(c, v0, 1, 16);\n", fn);
            fprintf(f, "rr_eaw_rmw(c, &E_%d_%d, 1, 0x%xu, r);\n", x->chunk, a,
                    pc);
            fprintf(f, "if (c->exc) return RR_EXIT;\n");
            return 1;
        }
        if (in->imm & 0x100)
            fprintf(f, "v1 = c->d[%u] & 0x3fu;\n", in->imm & 7);
        else
            fprintf(f, "v1 = %uu;\n", in->imm);
        if (sz == MSZ_B)
            fprintf(f, "v0 = c->d[%u] & 0xffu;\n", in->r1);
        else if (sz == MSZ_W)
            fprintf(f, "v0 = c->d[%u] & 0xffffu;\n", in->r1);
        else
            fprintf(f, "v0 = c->d[%u];\n", in->r1);
        fprintf(f, "r = %s(c, v0, v1, %u);\n", fn, w);
        if (sz == MSZ_B)
            fprintf(f, "c->d[%u] = (c->d[%u] & ~0xffu) | r;\n", in->r1,
                    in->r1);
        else if (sz == MSZ_W)
            fprintf(f, "c->d[%u] = (c->d[%u] & ~0xffffu) | r;\n", in->r1,
                    in->r1);
        else
            fprintf(f, "c->d[%u] = r;\n", in->r1);
        return 1;
    }
    case MOP_BTST: case MOP_BCHG: case MOP_BCLR: case MOP_BSET: {
        if (in->ea.mode == MEA_D) {
            if (in->r1 & 0x80)
                fprintf(f, "v1 = %uu;\n", in->r1 & 0x1f);
            else
                fprintf(f, "v1 = c->d[%u] & 0x1fu;\n", in->r1);
            fprintf(f, "if (c->d[%u] & (1u << v1)) "
                    "c->sr &= (uint16_t)~0x4u;\n", in->ea.reg);
            fprintf(f, "else c->sr |= 0x4u;\n");
            if (in->op == MOP_BCHG)
                fprintf(f, "c->d[%u] ^= (1u << v1);\n", in->ea.reg);
            else if (in->op == MOP_BCLR)
                fprintf(f, "c->d[%u] &= ~(1u << v1);\n", in->ea.reg);
            else if (in->op == MOP_BSET)
                fprintf(f, "c->d[%u] |= (1u << v1);\n", in->ea.reg);
            return 1;
        } else {
            int a = emit_ea(x, &in->ea);
            if (in->r1 & 0x80)
                fprintf(f, "v1 = %uu;\n", (in->r1 & 0x1f) & 7);
            else
                fprintf(f, "v1 = c->d[%u] & 7u;\n", in->r1);
            fprintf(f, "c->pc = 0x%xu;\n", npc);
            if (in->ea.mode == MEA_AINC) {
                uint32_t st = (in->ea.reg == 7) ? 2 : 1;
                fprintf(f, "v0 = rr_ear(c, &E_%d_%d, 0, 0x%xu);\n",
                        x->chunk, a, pc);
                fprintf(f, "if (c->exc) return RR_EXIT;\n");
                fprintf(f, "r = c->a[%u] - %uu;\n", in->ea.reg, st);
            } else if (in->ea.mode == MEA_ADEC) {
                fprintf(f, "v0 = rr_ear(c, &E_%d_%d, 0, 0x%xu);\n",
                        x->chunk, a, pc);
                fprintf(f, "if (c->exc) return RR_EXIT;\n");
                fprintf(f, "r = c->a[%u];\n", in->ea.reg);
            } else {
                fprintf(f, "r = rr_ea_addr(c, &E_%d_%d, 0x%xu);\n",
                        x->chunk, a, pc);
                fprintf(f, "if (c->exc) return RR_EXIT;\n");
                fprintf(f, "v0 = rr_mrd8(r);\n");
            }
            fprintf(f, "if (v0 & (1u << v1)) c->sr &= "
                    "(uint16_t)~0x4u;\n");
            fprintf(f, "else c->sr |= 0x4u;\n");
            if (in->op == MOP_BCHG)
                fprintf(f, "rr_mwr8(r, v0 ^ (1u << v1));\n");
            else if (in->op == MOP_BCLR)
                fprintf(f, "rr_mwr8(r, v0 & ~(1u << v1));\n");
            else if (in->op == MOP_BSET)
                fprintf(f, "rr_mwr8(r, v0 | (1u << v1));\n");
            return 1;
        }
    }
    case MOP_SCC: {
        int a = emit_ea(x, &in->ea);
        fprintf(f, "c->pc = 0x%xu;\n", npc);
        fprintf(f, "rr_eaw(c, &E_%d_%d, 0, 0x%xu, "
                "rr_cond(c->sr, %u) ? 0xffu : 0u);\n", x->chunk, a, pc,
                in->cc);
        fprintf(f, "if (c->exc) return RR_EXIT;\n");
        return 1;
    }
    case MOP_CHK: {
        int a = emit_ea(x, &in->ea);
        fprintf(f, "c->pc = 0x%xu;\n", npc);
        if (sz == MSZ_W) {
            fprintf(f, "v0 = (uint32_t)(int32_t)(int16_t)c->d[%u];\n",
                    in->r1);
            fprintf(f, "v1 = (uint32_t)(int32_t)(int16_t)rr_ear(c, "
                    "&E_%d_%d, 1, 0x%xu);\n", x->chunk, a, pc);
        } else {
            fprintf(f, "v0 = c->d[%u];\n", in->r1);
            fprintf(f, "v1 = rr_ear(c, &E_%d_%d, 2, 0x%xu);\n", x->chunk,
                    a, pc);
        }
        fprintf(f, "if (c->exc) return RR_EXIT;\n");
        fprintf(f, "c->sr &= (uint16_t)~(0x4u|0x2u|0x1u);\n");
        fprintf(f, "if (!v0) c->sr |= 0x4u;\n");
        fprintf(f, "if ((int32_t)v0 < 0 || (int32_t)v0 > (int32_t)v1) {\n");
        fprintf(f, "if ((int32_t)v0 < 0) c->sr |= 0x8u;\n");
        fprintf(f, "else c->sr &= (uint16_t)~0x8u;\n");
        fprintf(f, "rr_exc(6, 0x%xu); return RR_EXIT;\n", pc);
        fprintf(f, "}\n");
        return 1;
    }
    case MOP_LINK: {
        fprintf(f, "c->pc = 0x%xu;\n", npc);
        fprintf(f, "rr_push32(c, c->a[%u], 0x%xu);\n", in->r1, pc);
        fprintf(f, "if (c->exc) return RR_EXIT;\n");
        fprintf(f, "c->a[%u] = c->a[7];\n", in->r1);
        fprintf(f, "c->a[7] = (c->a[7] + 0x%xu) & 0xffffffu;\n", in->imm);
        return 1;
    }
    case MOP_UNLK:
        fprintf(f, "c->pc = 0x%xu;\n", npc);
        fprintf(f, "c->a[7] = c->a[%u];\n", in->r1);
        fprintf(f, "c->a[%u] = rr_pop32(c, 0x%xu);\n", in->r1, pc);
        fprintf(f, "if (c->exc) return RR_EXIT;\n");
        return 1;
    case MOP_NOP:
        return 1;
    case MOP_BCC: case MOP_BRA: case MOP_BSR: case MOP_DBCC:
    case MOP_JMP: case MOP_JSR: case MOP_RTS: case MOP_RTR:
    case MOP_RTD: case MOP_TRAP: case MOP_TRAPV: case MOP_TRAPCC:
        break; // handled below (block-final)
    default:
        // Complex and system ops run the shared semantics.
        emit_slow(x, in, pc, npc, slow_n);
        if (in->op == MOP_RTE) {
            fprintf(f, "return RR_JUMP;\n");
            return 0;
        }
        if (in->op == MOP_STOP) {
            fprintf(f, "return RR_EXIT;\n");
            return 0;
        }
        return 1;
    }
    // Block-final control flow. *cyc/*nins are final here.
    {
        int bcyc = *cyc, bn = *nins;
        uint32_t tgt = 0;
        int has_tgt = 0, is_call = 0;
        if (in->op == MOP_BCC || in->op == MOP_BRA || in->op == MOP_BSR) {
            tgt = (pc + 2 + in->imm) & 0xffffffu;
            has_tgt = 1;
            is_call = (in->op == MOP_BSR);
        } else if (in->op == MOP_DBCC) {
            tgt = (pc + 2 + in->imm) & 0xffffffu;
            has_tgt = 1;
        } else if (in->op == MOP_JMP || in->op == MOP_JSR) {
            is_call = (in->op == MOP_JSR);
            has_tgt = static_ea_target(in, pc, &tgt);
        }
        if (in->op == MOP_BCC) {
            fprintf(f, "if (rr_cond(c->sr, %u)) {\n", in->cc);
            if (in_chunk(x, tgt))
                fprintf(f, "if (rr_blk_end(0x%xu, %d, %d) != RR_CONT) "
                        "return RR_EXIT;\n", npc, bcyc, bn);
            if (in_chunk(x, tgt))
                fprintf(f, "goto L_%x;\n", tgt);
            else
                fprintf(f, "return rr_blk_jump(0x%xu, %d, %d);\n", tgt,
                        bcyc, bn);
            fprintf(f, "}\n");
            // fallthrough
            if (in_chunk(x, npc)) {
                fprintf(f, "if (rr_blk_end(0x%xu, %d, %d) != RR_CONT) "
                        "return RR_EXIT;\n", npc, bcyc, bn);
                fprintf(f, "goto L_%x;\n", npc);
            } else {
                fprintf(f, "return rr_blk_jump(0x%xu, %d, %d);\n", npc,
                        bcyc, bn);
            }
            return 0;
        }
        if (in->op == MOP_BRA) {
            if (in_chunk(x, tgt)) {
                fprintf(f, "if (rr_blk_end(0x%xu, %d, %d) != RR_CONT) "
                        "return RR_EXIT;\n", npc, bcyc, bn);
                fprintf(f, "goto L_%x;\n", tgt);
            } else {
                fprintf(f, "return rr_blk_jump(0x%xu, %d, %d);\n", tgt,
                        bcyc, bn);
            }
            return 0;
        }
        if (in->op == MOP_BSR || (in->op == MOP_JSR && has_tgt)) {
            fprintf(f, "c->pc = 0x%xu;\n", npc);
            fprintf(f, "rr_push32(c, 0x%xu, 0x%xu);\n", npc, pc);
            fprintf(f, "if (c->exc) return RR_EXIT;\n");
            if (in_chunk(x, tgt)) {
                fprintf(f, "if (rr_blk_end(0x%xu, %d, %d) != RR_CONT) "
                        "return RR_EXIT;\n", npc, bcyc, bn);
                fprintf(f, "goto L_%x;\n", tgt);
            } else {
                fprintf(f, "return rr_blk_jump(0x%xu, %d, %d);\n", tgt,
                        bcyc, bn);
            }
            return 0;
        }
        if (in->op == MOP_JMP && has_tgt) {
            if (in_chunk(x, tgt)) {
                fprintf(f, "if (rr_blk_end(0x%xu, %d, %d) != RR_CONT) "
                        "return RR_EXIT;\n", npc, bcyc, bn);
                fprintf(f, "goto L_%x;\n", tgt);
            } else {
                fprintf(f, "return rr_blk_jump(0x%xu, %d, %d);\n", tgt,
                        bcyc, bn);
            }
            return 0;
        }
        if (in->op == MOP_DBCC) {
            fprintf(f, "if (!rr_cond(c->sr, %u)) {\n", in->cc);
            fprintf(f, "v0 = (c->d[%u] - 1) & 0xffffu;\n", in->r1);
            fprintf(f, "c->d[%u] = (c->d[%u] & ~0xffffu) | v0;\n", in->r1,
                    in->r1);
            fprintf(f, "if (v0 != 0xffffu) {\n");
            if (in_chunk(x, tgt)) {
                fprintf(f, "if (rr_blk_end(0x%xu, %d, %d) != RR_CONT) "
                        "return RR_EXIT;\n", npc, bcyc, bn);
                fprintf(f, "goto L_%x;\n", tgt);
            } else {
                fprintf(f, "return rr_blk_jump(0x%xu, %d, %d);\n", tgt,
                        bcyc, bn);
            }
            fprintf(f, "}\n}\n");
            if (in_chunk(x, npc)) {
                fprintf(f, "if (rr_blk_end(0x%xu, %d, %d) != RR_CONT) "
                        "return RR_EXIT;\n", npc, bcyc, bn);
                fprintf(f, "goto L_%x;\n", npc);
            } else {
                fprintf(f, "return rr_blk_jump(0x%xu, %d, %d);\n", npc,
                        bcyc, bn);
            }
            return 0;
        }
        if ((in->op == MOP_JMP || in->op == MOP_JSR) && !has_tgt) {
            int a = emit_ea(x, &in->ea);
            fprintf(f, "c->pc = 0x%xu;\n", npc);
            fprintf(f, "v0 = rr_ea_addr(c, &E_%d_%d, 0x%xu);\n", x->chunk,
                    a, pc);
            fprintf(f, "if (c->exc) return RR_EXIT;\n");
            if (is_call) {
                fprintf(f, "rr_push32(c, 0x%xu, 0x%xu);\n", npc, pc);
                fprintf(f, "if (c->exc) return RR_EXIT;\n");
            }
            fprintf(f, "return rr_blk_jump(v0 & 0xffffffu, %d, %d);\n",
                    bcyc, bn);
            return 0;
        }
        if (in->op == MOP_RTS) {
            fprintf(f, "c->pc = 0x%xu;\n", npc);
            fprintf(f, "v0 = rr_pop32(c, 0x%xu);\n", pc);
            fprintf(f, "if (c->exc) return RR_EXIT;\n");
            fprintf(f, "return rr_blk_jump(v0 & 0xffffffu, %d, %d);\n",
                    bcyc, bn);
            return 0;
        }
        if (in->op == MOP_RTR) {
            fprintf(f, "c->pc = 0x%xu;\n", npc);
            fprintf(f, "v0 = rr_pop16(c, 0x%xu);\n", pc);
            fprintf(f, "if (c->exc) return RR_EXIT;\n");
            fprintf(f, "c->sr = (uint16_t)((c->sr & 0xff00u) | "
                    "(v0 & 0xffu));\n");
            fprintf(f, "v0 = rr_pop32(c, 0x%xu);\n", pc);
            fprintf(f, "if (c->exc) return RR_EXIT;\n");
            fprintf(f, "return rr_blk_jump(v0 & 0xffffffu, %d, %d);\n",
                    bcyc, bn);
            return 0;
        }
        if (in->op == MOP_RTD) {
            fprintf(f, "c->pc = 0x%xu;\n", npc);
            fprintf(f, "v0 = rr_pop32(c, 0x%xu);\n", pc);
            fprintf(f, "if (c->exc) return RR_EXIT;\n");
            fprintf(f, "c->a[7] = (c->a[7] + 0x%xu) & 0xffffffu;\n",
                    in->imm);
            fprintf(f, "return rr_blk_jump(v0 & 0xffffffu, %d, %d);\n",
                    bcyc, bn);
            return 0;
        }
        if (in->op == MOP_TRAP) {
            fprintf(f, "c->pc = 0x%xu;\n", npc);
            fprintf(f, "rr_exc(%u, 0x%xu);\n", 32 + in->r1, pc);
            fprintf(f, "return RR_EXIT;\n");
            return 0;
        }
        if (in->op == MOP_TRAPV || in->op == MOP_TRAPCC) {
            if (in->op == MOP_TRAPV)
                fprintf(f, "if (c->sr & 0x2u) {\n");
            else
                fprintf(f, "if (rr_cond(c->sr, %u)) {\n", in->cc);
            fprintf(f, "c->pc = 0x%xu;\n", npc);
            fprintf(f, "rr_exc(7, 0x%xu);\n", pc);
            fprintf(f, "return RR_EXIT;\n}\n");
            if (in_chunk(x, npc)) {
                fprintf(f, "if (rr_blk_end(0x%xu, %d, %d) != RR_CONT) "
                        "return RR_EXIT;\n", npc, bcyc, bn);
                fprintf(f, "goto L_%x;\n", npc);
            } else {
                fprintf(f, "return rr_blk_jump(0x%xu, %d, %d);\n", npc,
                        bcyc, bn);
            }
            return 0;
        }
    }
    return 1;
}

// Fallthrough exit for blocks ending in a trapping op (DIV/CHK), a split,
// or a decode gap.
static void emit_fall(emit_t *x, uint32_t npc, int cyc, int n) {
    FILE *f = x->f;
    if (in_chunk(x, npc)) {
        fprintf(f, "if (rr_blk_end(0x%xu, %d, %d) != RR_CONT) "
                "return RR_EXIT;\n", npc, cyc, n);
        fprintf(f, "goto L_%x;\n", npc);
    } else {
        fprintf(f, "return rr_blk_jump(0x%xu, %d, %d);\n", npc, cyc, n);
    }
}

uint32_t m68krec_emit(FILE *f, rec_plan_t *p, uint32_t lo, uint32_t hi,
        int chunk_id) {
    emit_t x;
    int i, nblocks = 0, slow_n = 0;
    uint32_t covered = 0;
    emit_init(&x, f, p, chunk_id, lo, hi);
    fprintf(f, "// Generated by m68krec. Do not edit.\n");
    fprintf(f, "#define REC_MODULE 1\n");
    fprintf(f, "#include \"rec_abi.h\"\n");
    fprintf(f, "#include \"m68k_core.h\"\n");
    fprintf(f, "#include \"rec_gen.h\"\n");
    fprintf(f, "static rr_rc_t rec_chunk_%d(void) {\n", chunk_id);
    fprintf(f, "m68k_t *c = rrm->cpu;\n");
    fprintf(f, "uint32_t v0 = 0, v1 = 0, r = 0;\n");
    fprintf(f, "switch (c->pc) {\n");
    for (i = 0; i < p->nstarts; i++) {
        uint32_t s = p->starts[i];
        if (s < lo || s >= hi) continue;
        fprintf(f, "case 0x%xu: goto L_%x;\n", s, s);
        nblocks++;
    }
    fprintf(f, "default: return RR_EXIT;\n}\n");
    if (!nblocks) {
        fprintf(f, "(void)v0; (void)v1; (void)r;\n");
        fprintf(f, "return RR_EXIT;\n}\n");
        emit_free(&x);
        return 0;
    }
    for (i = 0; i < p->nstarts; i++) {
        uint32_t start = p->starts[i], cur;
        int cyc = 0, n = 0;
        uint16_t words[12];
        int wi;
        if (start < lo || start >= hi) continue;
        fprintf(f, "L_%x:\n", start);
        cur = start;
        for (;;) {
            m_insn_t in;
            uint32_t npc, avail;
            int len, cont;
            if (cur >= p->size || (cur & 1)) {
                emit_fall(&x, cur, cyc, n);
                break;
            }
            if (cur != start && m68krec_plan_has(p, cur)) {
                emit_fall(&x, cur, cyc, n); // split at the next block
                break;
            }
            avail = (p->size - cur) >> 1;
            if (avail > 11) avail = 11;
            if (avail < 1) {
                emit_fall(&x, cur, cyc, n);
                break;
            }
            for (wi = 0; wi < (int)avail; wi++)
                words[wi] = (uint16_t)rom16(p, cur + (uint32_t)wi * 2);
            len = m68k_decode(cur, words, (int)avail, &in);
            if (len <= 0) { // data gap: leave to the interpreter
                emit_fall(&x, cur, cyc, n);
                break;
            }
            npc = cur + (uint32_t)len * 2;
            covered += (uint32_t)len * 2;
            cont = emit_op(&x, &in, cur, npc, &slow_n, &cyc, &n, 0, 0);
            if (!cont) break; // exit already emitted
            if (in.flow != MFLW_NONE) { // DIV/CHK/slow-trap fallthrough
                emit_fall(&x, npc, cyc, n);
                break;
            }
            cur = npc;
        }
    }
    // Should be unreachable, but a corrupt plan must not run off the end.
    fprintf(f, "return RR_EXIT;\n}\n");
    fprintf(f, "rec_fn_t rec_chunk_%d_fn(void);\n", chunk_id);
    fprintf(f, "rec_fn_t rec_chunk_%d_fn(void) { return rec_chunk_%d; }\n",
            chunk_id, chunk_id);
    emit_free(&x);
    return covered;
}
