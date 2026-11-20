// TMS320C31 static translator (see c31rec.h).
#include "c31rec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FILE_BYTES (1u << 20)
#define MAX_TRIAL 512

static void push(drec_plan_t *p, uint32_t a) {
    int i = drec_idx(a);
    if (i < 0 || p->code[i] || p->data[i]) return;
    if (p->nwork >= p->workcap) {
        p->workcap = p->workcap ? p->workcap * 2 : 1024;
        p->work = (uint32_t *)realloc(p->work,
                (size_t)p->workcap * sizeof(*p->work));
    }
    p->work[p->nwork++] = (uint32_t)i;
}

static void add_func(drec_plan_t *p, uint32_t a) {
    int i = drec_idx(a);
    if (i < 0 || p->data[i]) return;
    p->fstart[i] = 1;
    push(p, a);
}

// Static target of a branch or call at address a (word after it: a + 1).
static int static_target(const d_insn_t *in, uint32_t a, uint32_t *tgt) {
    switch (in->op) {
    case DOP_BR: case DOP_BRD: case DOP_CALL:
        *tgt = in->addr & 0xffffffu;
        return 1;
    case DOP_BCOND: case DOP_DBCOND: case DOP_CALLC:
        if (in->g != 1) return 0;
        *tgt = (a + 1 + in->addr) & 0xffffffu;
        return 1;
    case DOP_BDCOND: case DOP_DBDCOND:
        if (in->g != 1) return 0;
        *tgt = (a + 3 + in->addr) & 0xffffffu;
        return 1;
    default:
        return 0;
    }
}

// Unconditional transfer: what follows (after any delay slots) is not
// reached by falling through.
static int stops(const d_insn_t *in) {
    switch (in->op) {
    case DOP_BR: case DOP_BRD:
        return 1;
    case DOP_BCOND: case DOP_BDCOND: case DOP_RETS: case DOP_RETI:
        return in->cc == 0;
    default:
        return 0;
    }
}

static int delayed(const d_insn_t *in) {
    return in->op == DOP_BRD || in->op == DOP_BDCOND ||
            in->op == DOP_DBDCOND;
}

static void descend(drec_plan_t *p) {
    while (p->nwork > 0) {
        uint32_t i = p->work[--p->nwork];
        int slots = -1;     // instructions left before an unconditional
                            // delayed branch takes effect
        for (;;) {
            d_insn_t in;
            uint32_t a, tgt;
            if (p->code[i] || p->data[i]) break;
            d_decode(p->img[i], &in);
            if (in.op == DOP_ILL) break;
            p->code[i] = 1;
            p->ninsns++;
            a = drec_addr(i);
            if (static_target(&in, a, &tgt)) {
                if (in.op == DOP_CALL || in.op == DOP_CALLC) add_func(p, tgt);
                else push(p, tgt);
            }
            if (in.op == DOP_RPTB) push(p, (in.addr + 1) & 0xffffffu);
            if (slots > 0 && --slots == 0) break;
            if (stops(&in)) {
                if (!delayed(&in)) break;
                slots = 3;
            }
            {
                int n = drec_idx(a + 1);
                if (n < 0) break;
                i = (uint32_t)n;
            }
        }
    }
}

// Lowest and highest code word found by descent in i's region.
static int in_span(const drec_plan_t *p, uint32_t i) {
    uint32_t lo = i < 0x10000u ? 0 : 0x10000u;
    uint32_t hi = i < 0x10000u ? 0x10000u : DREC_WORDS, j;
    int below = 0, above = 0;
    for (j = lo; j < i && !below; j++) below = p->code[j];
    for (j = i + 1; j < hi && !above; j++) above = p->code[j];
    return below && above;
}

// Plausible code: decodes without illegal words up to an unconditional
// return or branch, or joins known code, within MAX_TRIAL words.
static int plausible(const drec_plan_t *p, uint32_t i) {
    int n;
    for (n = 0; n < MAX_TRIAL; n++) {
        d_insn_t in;
        int nx;
        if (p->code[i]) return 1;
        if (p->data[i]) return 0;
        d_decode(p->img[i], &in);
        if (in.op == DOP_ILL || in.op == DOP_SWI) return 0;
        if (stops(&in) && !delayed(&in)) return 1;
        nx = drec_idx(drec_addr(i) + 1);
        if (nx < 0) return 0;
        i = (uint32_t)nx;
    }
    return 0;
}

static int try_pointer(drec_plan_t *p, uint32_t v) {
    int i = drec_idx(v);
    if (v > 0xffffffu || i < 0 || p->code[i] || p->data[i]) return 0;
    // 0-0xFFF fetches from the boot ROM shadow: code there never changes.
    if ((v >= 0x1000u && !in_span(p, (uint32_t)i)) ||
            !plausible(p, (uint32_t)i))
        return 0;
    add_func(p, v);
    descend(p);
    p->nptr++;
    return 1;
}

void drec_plan(drec_plan_t *p, const uint32_t *entries, int nentries) {
    int k, found;
    uint32_t i;
    p->nwork = 0;
    p->ninsns = p->nfuncs = p->nptr = 0;
    for (k = 0; k < nentries; k++) add_func(p, entries[k]);
    descend(p);
    // Pointers: 16-bit immediates loaded into registers (register
    // branches and calls), then words in RAM (handler tables).
    do {
        found = 0;
        for (i = 0; i < DREC_WORDS; i++) {
            d_insn_t in;
            if (!p->code[i]) continue;
            d_decode(p->img[i], &in);
            if ((in.op == DOP_LDI || in.op == DOP_LDIC) && in.g == 3)
                found += try_pointer(p, in.addr & 0xffffu);
        }
        for (i = 0; i < DREC_WORDS; i++)
            if (!p->code[i]) found += try_pointer(p, p->img[i]);
    } while (found);
    for (i = 0; i < DREC_WORDS; i++)
        if (p->code[i] && p->fstart[i]) p->nfuncs++;
}

// ------------------------------------------------------------- emission

static void insn_init(FILE *f, uint32_t a, const d_insn_t *in) {
    fprintf(f, "static const d_insn_t D%x = {%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,"
            "0x%xu,%u,%u};\n", a, in->op, in->cc, in->g, in->dreg, in->sreg,
            in->sreg2, in->dreg2, in->ma1, in->ma2, in->disp, in->addr,
            in->flow, in->xcycles);
}

// Function f covers code words in index range [lo, hi).
static void emit_func(FILE *f, const drec_plan_t *p, uint32_t lo,
        uint32_t hi) {
    uint32_t i;
    for (i = lo; i < hi; i++) {
        d_insn_t in;
        if (!p->code[i]) continue;
        d_decode(p->img[i], &in);
        insn_init(f, drec_addr(i), &in);
    }
    fprintf(f, "rr_rc_t df_%x(void) {\nc31_t *d = rrm->dsp;\n"
            "const uint32_t gen = rrm->dsp_gen;\ndispatch:\n"
            "switch (d->pc) {\n", drec_addr(lo));
    for (i = lo; i < hi; i++)
        if (p->code[i])
            fprintf(f, "case 0x%xu: goto L%x;\n", drec_addr(i), drec_addr(i));
    fprintf(f, "default: return RR_JUMP;\n}\n");
    for (i = lo; i < hi; i++) {
        d_insn_t in;
        uint32_t a, tgt;
        int ni;
        if (!p->code[i]) continue;
        d_decode(p->img[i], &in);
        a = drec_addr(i);
        fprintf(f, "L%x: DI(0x%xu, D%x, %u);\n", a, a, a, in.xcycles);
        // Anything but the next word (branch, delay slot ending, repeat,
        // interrupt) goes through the switch.
        ni = drec_idx(a + 1);
        if (static_target(&in, a, &tgt) && !delayed(&in)) {
            int ti = drec_idx(tgt);
            if (ti >= (int)lo && ti < (int)hi && p->code[ti])
                fprintf(f, "if (d->pc == 0x%xu) goto L%x;\n", tgt, tgt);
        }
        // Fixed width: the next word is emitted next if it is code here.
        if (ni >= (int)lo && ni < (int)hi && p->code[ni])
            fprintf(f, "if (d->pc != 0x%xu) goto dispatch;\n", a + 1);
        else
            fprintf(f, "goto dispatch;\n");
    }
    fprintf(f, "return RR_JUMP;\n}\n");
}

static FILE *open_part(const char *dir, int n, char *files, size_t cap) {
    char path[1024], name[32];
    FILE *f;
    snprintf(name, sizeof(name), "drec_%03d.c", n);
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    f = fopen(path, "w");
    if (!f) return NULL;
    if (strlen(files) + strlen(name) + 2 < cap) {
        strcat(files, name);
        strcat(files, " ");
    }
    fprintf(f, "// Translated from the game's own code. Do not edit or "
            "share.\n#define REC_MODULE 1\n#include \"rec_abi.h\"\n"
            "#include \"c31_core.h\"\n");
    return f;
}

int drec_write(const drec_plan_t *p, const char *dir, const char *module,
        char *files, size_t cap) {
    uint32_t *funcs, nf = 0, i, k, nlabels = 0;
    FILE *f = NULL, *x;
    char path[1024];
    int part = 0, rc = -1;
    funcs = (uint32_t *)malloc(DREC_WORDS * sizeof(*funcs));
    if (!funcs) return -1;
    // Functions split at function starts and at the region boundary.
    for (i = 0; i < DREC_WORDS; i++) {
        if (!p->code[i]) continue;
        if (p->fstart[i] || nf == 0 || (i >= 0x10000u &&
                funcs[nf - 1] < 0x10000u))
            funcs[nf++] = i;
        nlabels++;
    }
    for (k = 0; k < nf; k++) {
        uint32_t lo = funcs[k], hi = k + 1 < nf ? funcs[k + 1] : DREC_WORDS;
        if (lo < 0x10000u && hi > 0x10000u) hi = 0x10000u;
        if (!f) {
            f = open_part(dir, part++, files, cap);
            if (!f) goto out;
        }
        emit_func(f, p, lo, hi);
        if (ftell(f) > (long)FILE_BYTES) {
            fclose(f);
            f = NULL;
        }
    }
    if (f) fclose(f);
    snprintf(path, sizeof(path), "%s/dexports.c", dir);
    x = fopen(path, "w");
    if (!x) goto out;
    fprintf(x, "// Translated from the game's own code. Do not edit or "
            "share.\n#define REC_MODULE 1\n#include \"rec_abi.h\"\n");
    fprintf(x, "rr_mem_t *rrm;\nconst rr_hostfns_t *rrfns;\n"
            "void rec_init_module(rr_mem_t *m, const rr_hostfns_t *f) "
            "{ rrm = m; rrfns = f; }\n");
    for (k = 0; k < nf; k++)
        fprintf(x, "rr_rc_t df_%x(void);\n", drec_addr(funcs[k]));
    fprintf(x, "static const rec_func_t funcs[] = {\n");
    for (k = 0; k < nf; k++) {
        uint32_t hi = k + 1 < nf ? funcs[k + 1] : DREC_WORDS;
        if (funcs[k] < 0x10000u && hi > 0x10000u) hi = 0x10000u;
        fprintf(x, "{df_%x, 0x%xu, 0x%xu},\n", drec_addr(funcs[k]),
                drec_addr(funcs[k]), drec_addr(hi - 1) + 1);
    }
    fprintf(x, "};\nstatic const uint32_t labels[] = {\n");
    for (i = 0; i < DREC_WORDS; i++)
        if (p->code[i]) fprintf(x, "0x%xu,\n", drec_addr(i));
    fprintf(x, "};\nstatic const uint32_t label_fn[] = {\n");
    for (i = 0, k = 0; i < DREC_WORDS; i++) {
        if (!p->code[i]) continue;
        while (k + 1 < nf && funcs[k + 1] <= i) k++;
        fprintf(x, "%u,\n", k);
    }
    // The words translated, and which are code (the guard checks them).
    fprintf(x, "};\nstatic const uint32_t image[0x%xu] = {\n", DREC_WORDS);
    for (i = 0; i < DREC_WORDS; i++)
        fprintf(x, "0x%xu,%s", p->code[i] ? p->img[i] : 0,
                (i & 7) == 7 ? "\n" : "");
    fprintf(x, "};\nstatic const uint8_t mask[0x%xu] = {\n", DREC_WORDS);
    for (i = 0; i < DREC_WORDS; i++)
        fprintf(x, "%u,%s", p->code[i], (i & 31) == 31 ? "\n" : "");
    fprintf(x, "};\nconst rec_module_t %s = {%d, 1, %u, %u, funcs, labels, "
            "label_fn, image, mask};\n", module, REC_ABI_VERSION, nf,
            nlabels);
    fclose(x);
    if (strlen(files) + 14 < cap) strcat(files, "dexports.c ");
    rc = 0;
out:
    free(funcs);
    return rc;
}
