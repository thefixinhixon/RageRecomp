// 68EC020 static translator (see m68krec.h).
//
// Each instruction is emitted as a call into the shared semantics in
// m68k_core.h (rr_exec) with its decoded form as a constant, mirroring
// the interpreter's m68k_step exactly: pc, execute, cycles, exception
// check. The translation removes fetch and decode and turns control flow
// into gotos; behaviour is defined in one place for both.
#include "m68krec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_WORDS 11
#define FILE_BYTES (1u << 20)   // split output so it compiles in parallel
#define MAX_TRIAL 256           // instructions a pointer candidate may run

static uint32_t rom16(const rec_plan_t *p, uint32_t pc) {
    return ((uint32_t)p->rom[pc] << 8) | p->rom[pc + 1];
}

static uint32_t rom32(const rec_plan_t *p, uint32_t pc) {
    return (rom16(p, pc) << 16) | rom16(p, pc + 2);
}

// Decode the instruction at pc. Returns its length in words, 0 if none.
static int dec(const rec_plan_t *p, uint32_t pc, m_insn_t *in) {
    uint16_t words[MAX_WORDS + 1];
    uint32_t avail, i;
    int len;
    if (pc & 1 || pc >= p->size) return 0;
    avail = (p->size - pc) >> 1;
    if (avail > MAX_WORDS) avail = MAX_WORDS;
    for (i = 0; i < avail; i++) words[i] = (uint16_t)rom16(p, pc + i * 2);
    len = m68k_decode(pc, words, (int)avail, in);
    return len > 0 ? len : 0;
}

// Instructions that can change the interrupt mask, trace bits or stack
// state: the block ends after them so a pending interrupt is taken at
// once, as the interpreter would.
static int writes_sr(const m_insn_t *in) {
    switch (in->op) {
    case MOP_MOVE:
        return in->r1 != 0;
    case MOP_ANDI: case MOP_ORI: case MOP_EORI:
        return in->r1 != 0;
    case MOP_MOVEC: case MOP_RESET: case MOP_STOP: case MOP_RTE:
        return 1;
    default:
        return 0;
    }
}

// Ends a run of straight-line code (control may go elsewhere).
static int ends_block(const m_insn_t *in) {
    return in->flow == MFLW_BRANCH || in->flow == MFLW_JUMP ||
            in->flow == MFLW_CALL || in->flow == MFLW_RET ||
            in->flow == MFLW_DBCC || in->flow == MFLW_EXIT ||
            writes_sr(in);
}

// Unconditional traps: what follows may be data.
static int always_traps(const m_insn_t *in) {
    return in->op == MOP_ILL || in->op == MOP_ILLEGAL;
}

// Static target of a branch, jump or call. Returns 1 if there is one.
static int static_target(const m_insn_t *in, uint32_t pc, uint32_t *tgt) {
    switch (in->op) {
    case MOP_BCC: case MOP_BRA: case MOP_BSR: case MOP_DBCC:
        *tgt = (pc + 2 + in->imm) & 0xffffffu;
        return 1;
    case MOP_JMP: case MOP_JSR:
        if (in->ea.mode == MEA_ABSW || in->ea.mode == MEA_ABSL) {
            *tgt = (uint32_t)in->ea.disp & 0xffffffu;
            return 1;
        }
        if (in->ea.mode == MEA_PCDISP) {
            *tgt = (pc + 2 + (uint32_t)in->ea.disp) & 0xffffffu;
            return 1;
        }
        return 0;
    default:
        return 0;
    }
}

static void push(rec_plan_t *p, uint32_t pc) {
    if (pc & 1 || pc >= p->size) return;
    if (p->start[pc >> 1]) return;
    if (p->nwork >= p->workcap) {
        p->workcap = p->workcap ? p->workcap * 2 : 4096;
        p->work = (uint32_t *)realloc(p->work,
                (size_t)p->workcap * sizeof(*p->work));
    }
    p->work[p->nwork++] = pc;
}

static void add_func(rec_plan_t *p, uint32_t pc) {
    if (pc & 1 || pc >= p->size) return;
    p->fstart[pc >> 1] = 1;
    push(p, pc);
}

// A compiled switch: `jmp (table,pc,dn.w)` over a table of word offsets
// from the table base. The table ends where the first case begins (the
// cases follow it) or at the first entry that cannot be code. Its words
// are marked as data so pointer candidates never start inside it.
static void switch_table(rec_plan_t *p, uint32_t base) {
    uint32_t i, end = p->size;
    for (i = 0; i < 1024; i++) {
        uint32_t a = base + i * 2, t;
        if (a + 2 > p->size || a >= end) break;
        if (p->start[a >> 1]) break;
        t = (base + (uint32_t)(int32_t)(int16_t)rom16(p, a)) & 0xffffffu;
        if (t & 1 || t >= p->size || p->inside[t >> 1]) break;
        if (t > base && t < end) end = t;
        p->inside[a >> 1] = 1;
        if (!p->guessing) p->strong[a >> 1] = 1;
        push(p, t);
    }
}

// Follow every path from the worklist, marking instructions.
static void descend(rec_plan_t *p) {
    while (p->nwork > 0) {
        uint32_t pc = p->work[--p->nwork];
        for (;;) {
            m_insn_t in;
            uint32_t npc, tgt, w;
            int len;
            if (pc & 1 || pc >= p->size || p->start[pc >> 1]) break;
            len = dec(p, pc, &in);
            if (!len) break;
            p->start[pc >> 1] = 1;
            p->ninsns++;
            for (w = 0; w < (uint32_t)len; w++) {
                if ((pc >> 1) + w >= (p->size >> 1)) break;
                if (w) p->inside[(pc >> 1) + w] = 1;
                if (!p->guessing) p->strong[(pc >> 1) + w] = 1;
            }
            npc = pc + (uint32_t)len * 2;
            if (always_traps(&in)) break;
            if (static_target(&in, pc, &tgt)) {
                if (in.flow == MFLW_CALL) add_func(p, tgt);
                else push(p, tgt);
            }
            if (in.op == MOP_JMP && in.ea.mode == MEA_PCIDX &&
                    in.ea.bdsz == 0)
                switch_table(p, (pc + 2 + (uint32_t)in.ea.disp) & 0xffffffu);
            if (in.flow == MFLW_JUMP || in.flow == MFLW_RET) break;
            if (ends_block(&in) && in.flow != MFLW_NONE) {
                push(p, npc);   // branch/call fallthrough, resumption
                break;
            }
            pc = npc;           // straight on (incl. SR writes, traps)
        }
    }
}

// Does decoding from pc look like real code? It must decode cleanly,
// keep off the middle of code found by descent, and reach a return, a
// jump or known code within MAX_TRIAL instructions. Code found only by
// earlier guesses may overlap it (one of the two guesses is wrong; the
// emitter copes with overlapping streams).
static int plausible(const rec_plan_t *p, uint32_t pc) {
    int n;
    if (pc & 1 || pc >= p->limit) return 0;
    if (p->inside[pc >> 1] && p->strong[pc >> 1]) return 0;
    for (n = 0; n < MAX_TRIAL; n++) {
        m_insn_t in;
        uint32_t w;
        int len;
        if (pc >= p->limit) return 0;
        if (p->start[pc >> 1]) return 1;     // joins known code
        if (p->inside[pc >> 1] && p->strong[pc >> 1]) return 0;
        len = dec(p, pc, &in);
        if (!len || always_traps(&in) || in.op == MOP_TRAP ||
                in.op == MOP_RESET || in.op == MOP_STOP)
            return 0;
        for (w = 1; w < (uint32_t)len; w++)
            if (p->strong[(pc >> 1) + w]) return 0;
        if (in.flow == MFLW_JUMP || in.flow == MFLW_RET) return 1;
        pc += (uint32_t)len * 2;
    }
    return 0;
}

// A pointer candidate: new code becomes a function of its own.
static int try_pointer(rec_plan_t *p, uint32_t v) {
    v &= 0xffffffu;
    if (v & 1 || v >= p->limit || p->start[v >> 1]) return 0;
    if (!plausible(p, v)) return 0;
    add_func(p, v);
    p->guessing = 1;
    descend(p);
    p->guessing = 0;
    p->nptr++;
    return 1;
}

// Addresses the code itself names: absolute operands, long immediates and
// PC-relative LEA/PEA (tables of handlers next to the code using them).
static int scan_operands(rec_plan_t *p) {
    uint32_t pc;
    int found = 0;
    for (pc = 0; pc < p->limit; pc += 2) {
        m_insn_t in;
        const m_ea_t *e[2];
        int k;
        if (!p->start[pc >> 1] || !dec(p, pc, &in)) continue;
        e[0] = &in.ea;
        e[1] = &in.eb;
        for (k = 0; k < 2; k++) {
            if (e[k]->mode == MEA_ABSL)
                found += try_pointer(p, (uint32_t)e[k]->disp);
            else if (e[k]->mode == MEA_IMM && in.sz == MSZ_L)
                found += try_pointer(p, e[k]->imm);
            else if (e[k]->mode == MEA_PCDISP &&
                    (in.op == MOP_LEA || in.op == MOP_PEA))
                found += try_pointer(p, pc + 2 + (uint32_t)e[k]->disp);
        }
    }
    return found;
}

// Every long in ROM that points at plausible code (function pointer
// tables, state machines, handler lists).
static int scan_rom(rec_plan_t *p) {
    uint32_t a;
    int found = 0;
    for (a = 0; a + 4 <= p->size; a += 2)
        found += try_pointer(p, rom32(p, a));
    return found;
}

void m68krec_plan(rec_plan_t *p, const uint8_t *rom, uint32_t size) {
    uint32_t v, hi = 0, pc;
    memset(p, 0, sizeof(*p));
    p->rom = rom;
    p->size = size;
    p->start = (uint8_t *)calloc(size >> 1, 1);
    p->inside = (uint8_t *)calloc(size >> 1, 1);
    p->fstart = (uint8_t *)calloc(size >> 1, 1);
    p->strong = (uint8_t *)calloc(size >> 1, 1);
    add_func(p, rom32(p, 4) & 0xffffffu);
    for (v = 2; v < 256; v++) add_func(p, rom32(p, v * 4) & 0xffffffu);
    descend(p);
    // The code region: from 0 up to the last 64 KB block that reads like
    // code (many RTS and JSR opcodes), and at least what descent found.
    // Past it lies graphics and sound data, where false pointers abound.
    for (pc = 0; pc < size; pc += 0x10000u) {
        uint32_t a, rts = 0, jsr = 0;
        for (a = pc; a < pc + 0x10000u && a + 2 <= size; a += 2) {
            uint32_t w = rom16(p, a);
            rts += w == 0x4e75u;
            jsr += w == 0x4eb9u || w == 0x6100u;  // jsr abs.l, bsr.w
        }
        if (rts < 32 || jsr < 32) break;
        hi = pc + 0x10000u;
    }
    for (pc = hi; pc < size; pc += 2)
        if (p->start[pc >> 1]) hi = pc + 2;
    p->limit = (hi + 0xffffu) & ~0xffffu;
    if (p->limit > size) p->limit = size;
    // Operands first (more trustworthy), then ROM data; repeat while new
    // code turns up more operands.
    while (scan_operands(p) || scan_rom(p)) {
    }
    for (pc = 0; pc < size; pc += 2)
        if (p->fstart[pc >> 1] && p->start[pc >> 1]) p->nfuncs++;
}

void m68krec_plan_free(rec_plan_t *p) {
    free(p->start);
    free(p->inside);
    free(p->fstart);
    free(p->strong);
    free(p->work);
    memset(p, 0, sizeof(*p));
}

// ------------------------------------------------------------- emission

static void ea_init(FILE *f, const m_ea_t *e) {
    fprintf(f, "{%u,%u,%d,%u,%u,%u,%u,%u,%u,%u,%u,%d,%uu,%u}",
            e->mode, e->reg, e->disp, e->ireg, e->ilong, e->scale,
            e->bdsz, e->odsz, e->iis, e->bs, e->isup, e->od, e->imm, e->immw);
}

static void insn_init(FILE *f, uint32_t pc, const m_insn_t *in) {
    fprintf(f, "static const m_insn_t I%x = {%u,%u,%u,%u,%u,", pc, in->op,
            in->sz, in->cc, in->r1, in->r2);
    ea_init(f, &in->ea);
    fputc(',', f);
    ea_init(f, &in->eb);
    fprintf(f, ",%uu,%uu,%u,%u,%u};\n", in->imm, in->imm2, in->len,
            in->flow, in->cycles);
}

static int in_func(const rec_plan_t *p, uint32_t lo, uint32_t hi,
        uint32_t pc) {
    return pc >= lo && pc < hi && !(pc & 1) && p->start[pc >> 1];
}

// One function: instruction starts in [lo, hi).
static void emit_func(FILE *f, const rec_plan_t *p, uint32_t lo,
        uint32_t hi) {
    uint32_t pc;
    for (pc = lo; pc < hi; pc += 2) {
        m_insn_t in;
        if (p->start[pc >> 1] && dec(p, pc, &in)) insn_init(f, pc, &in);
    }
    fprintf(f, "rr_rc_t rf_%x(void) {\nm68k_t *c = rrm->cpu;\n"
            "dispatch:\nswitch (c->pc) {\n", lo);
    for (pc = lo; pc < hi; pc += 2)
        if (p->start[pc >> 1]) fprintf(f, "case 0x%xu: goto L%x;\n", pc, pc);
    fprintf(f, "default: return RR_JUMP;\n}\n");
    for (pc = lo; pc < hi; pc += 2) {
        m_insn_t in;
        uint32_t npc, tgt, next;
        int len;
        if (!p->start[pc >> 1]) continue;
        len = dec(p, pc, &in);
        npc = pc + (uint32_t)len * 2;
        fprintf(f, "L%x: RR_I(0x%xu, 0x%xu, I%x, %u);\n", pc, pc,
                npc & 0xffffffu, pc, in.cycles);
        if (ends_block(&in)) {
            fprintf(f, "if (rr_mpoll()) return RR_EXIT;\n");
            if (static_target(&in, pc, &tgt) && in_func(p, lo, hi, tgt))
                fprintf(f, "if (c->pc == 0x%xu) goto L%x;\n", tgt, tgt);
            if (in.flow != MFLW_JUMP && in.flow != MFLW_RET &&
                    in_func(p, lo, hi, npc))
                fprintf(f, "if (c->pc == 0x%xu) goto L%x;\n", npc, npc);
            fprintf(f, "goto dispatch;\n");
            continue;
        }
        // Straight on. The next instruction emitted is the next start in
        // address order, which is not npc when streams overlap (a start
        // inside this instruction) or code is missing: jump explicitly.
        for (next = pc + 2; next < hi && !p->start[next >> 1]; next += 2) {
        }
        if (next != npc || npc >= hi) {
            if (in_func(p, lo, hi, npc))
                fprintf(f, "goto L%x;\n", npc);
            else
                fprintf(f, "goto dispatch;\n");
        }
    }
    fprintf(f, "return RR_JUMP;\n}\n");
}

static FILE *open_part(const char *dir, int n, char *files, size_t cap) {
    char path[1024], name[32];
    FILE *f;
    snprintf(name, sizeof(name), "rec_%03d.c", n);
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    f = fopen(path, "w");
    if (!f) return NULL;
    if (strlen(files) + strlen(name) + 2 < cap) {
        strcat(files, name);
        strcat(files, " ");
    }
    fprintf(f, "// Translated from the game's own code. Do not edit or "
            "share.\n#define REC_MODULE 1\n#include \"rec_abi.h\"\n"
            "#include \"m68k_core.h\"\n#include \"rec_gen.h\"\n");
    return f;
}

int m68krec_write(rec_plan_t *p, const char *dir, const char *module,
        char *files, size_t cap) {
    uint32_t *funcs, pc, nf = 0, i, nlabels = 0;
    FILE *f = NULL, *x;
    char path[1024];
    int part = 0;
    funcs = (uint32_t *)malloc((size_t)(p->size >> 1) * sizeof(*funcs));
    if (!funcs) return -1;
    // Functions: from each function start to the next. Code before the
    // first one starts a function of its own.
    for (pc = 0; pc < p->size; pc += 2) {
        if (!p->start[pc >> 1]) continue;
        if (p->fstart[pc >> 1] || nf == 0) funcs[nf++] = pc;
        nlabels++;
    }
    for (i = 0; i < nf; i++) {
        uint32_t lo = funcs[i], hi = i + 1 < nf ? funcs[i + 1] : p->size;
        if (!f) {
            f = open_part(dir, part++, files, cap);
            if (!f) { free(funcs); return -1; }
        }
        emit_func(f, p, lo, hi);
        if (ftell(f) > (long)FILE_BYTES) {
            fclose(f);
            f = NULL;
        }
    }
    if (f) fclose(f);
    // Exports: functions and every entry label with its function.
    snprintf(path, sizeof(path), "%s/exports.c", dir);
    x = fopen(path, "w");
    if (!x) { free(funcs); return -1; }
    fprintf(x, "// Translated from the game's own code. Do not edit or "
            "share.\n#define REC_MODULE 1\n#include \"rec_abi.h\"\n");
    fprintf(x, "rr_mem_t *rrm;\nconst rr_hostfns_t *rrfns;\n"
            "void rec_init_module(rr_mem_t *m, const rr_hostfns_t *f) "
            "{ rrm = m; rrfns = f; }\n");
    for (i = 0; i < nf; i++) fprintf(x, "rr_rc_t rf_%x(void);\n", funcs[i]);
    fprintf(x, "static const rec_func_t funcs[] = {\n");
    for (i = 0; i < nf; i++)
        fprintf(x, "{rf_%x, 0x%xu, 0x%xu},\n", funcs[i], funcs[i],
                i + 1 < nf ? funcs[i + 1] : p->size);
    fprintf(x, "};\nstatic const uint32_t labels[] = {\n");
    for (pc = 0; pc < p->size; pc += 2)
        if (p->start[pc >> 1]) fprintf(x, "0x%xu,\n", pc);
    fprintf(x, "};\nstatic const uint32_t label_fn[] = {\n");
    for (pc = 0, i = 0; pc < p->size; pc += 2) {
        if (!p->start[pc >> 1]) continue;
        while (i + 1 < nf && funcs[i + 1] <= pc) i++;
        fprintf(x, "%u,\n", i);
    }
    fprintf(x, "};\nconst rec_module_t %s = {%d, 0, %u, %u, funcs, labels, "
            "label_fn};\n", module, REC_ABI_VERSION, nf, nlabels);
    fclose(x);
    if (strlen(files) + 12 < cap) strcat(files, "exports.c ");
    free(funcs);
    return 0;
}
