// Translated-code dispatchers. See rec.h.
#include "rec.h"
#include "m68k.h"
#include "c31.h"
#include "c31_core.h"
#include "cage.h"
#include "rec_build.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const rec_module_t *mod_m68k = NULL;
static const rec_module_t *mod_dsp = NULL;
static void *handle_m68k = NULL;
static void *handle_dsp = NULL;
static int force_interp_m68k = 0;
static int force_interp_dsp = 0;
static void map68_build(const rec_module_t *m);
static void mapdsp_build(const rec_module_t *m);

// RR_INTERP_HIST=file: count where the interpreters still run while
// translated code is active (missing entry points), written at shutdown.
#define HIST_68K (0x200000u / 2)    // per ROM word
#define HIST_DSP (0x10000u + 0x800u) // lowmem words, then IRAM
static uint32_t *hist68, *histdsp;
static uint64_t hist68_other, histdsp_other;

static void hist_m68k(uint32_t pc) {
    if (pc < 0x200000u) hist68[pc >> 1]++;
    else hist68_other++;
}

static void hist_dsp(uint32_t pc) {
    if (pc < 0x10000u) histdsp[pc]++;
    else if (pc >= 0x809800u && pc < 0x80a000u)
        histdsp[0x10000u + (pc - 0x809800u)]++;
    else histdsp_other++;
}

static void hist_write(void) {
    const char *name = getenv("RR_INTERP_HIST");
    FILE *f;
    uint32_t i;
    uint64_t t68 = 0, tdsp = 0;
    if (!name || !hist68) return;
    f = fopen(name, "w");
    if (!f) return;
    for (i = 0; i < HIST_68K; i++) t68 += hist68[i];
    for (i = 0; i < HIST_DSP; i++) tdsp += histdsp[i];
    fprintf(f, "# 68k interp %llu (+%llu outside ROM) of %llu; "
            "dsp interp %llu (+%llu outside RAM) of %llu\n",
            (unsigned long long)t68, (unsigned long long)hist68_other,
            (unsigned long long)rrm->cpu->total,
            (unsigned long long)tdsp, (unsigned long long)histdsp_other,
            (unsigned long long)rrm->dsp->total);
    for (i = 0; i < HIST_68K; i++)
        if (hist68[i]) fprintf(f, "m68k %06x %u\n", i * 2, hist68[i]);
    for (i = 0; i < HIST_DSP; i++)
        if (histdsp[i]) fprintf(f, "dsp %06x %u\n",
                i < 0x10000u ? i : 0x809800u + (i - 0x10000u), histdsp[i]);
    fclose(f);
}

void rec_init(void) {
    if (getenv("RR_INTERP_HIST") && !hist68) {
        hist68 = (uint32_t *)calloc(HIST_68K, sizeof(*hist68));
        histdsp = (uint32_t *)calloc(HIST_DSP, sizeof(*histdsp));
    }
    force_interp_m68k = getenv("RR_NOREC") && getenv("RR_NOREC")[0] != 0 &&
            strcmp(getenv("RR_NOREC"), "0") != 0;
    force_interp_dsp = getenv("RR_NODSP") && getenv("RR_NODSP")[0] != 0 &&
            strcmp(getenv("RR_NODSP"), "0") != 0;
    if (!force_interp_m68k && rec_linked_m68k.nlabels > 0 &&
            rec_linked_m68k.abi == REC_ABI_VERSION) {
        mod_m68k = &rec_linked_m68k;
        map68_build(mod_m68k);
    }
    if (!force_interp_dsp && rec_linked_dsp.nlabels > 0 &&
            rec_linked_dsp.abi == REC_ABI_VERSION)
        rec_use_dsp(&rec_linked_dsp, NULL);
}

void rec_shutdown(void) {
    hist_write();
    // Handles are closed by rec_build (which opened them); just forget.
    if (handle_m68k) rec_close_handle(handle_m68k);
    if (handle_dsp) rec_close_handle(handle_dsp);
    handle_m68k = handle_dsp = NULL;
    mod_m68k = mod_dsp = NULL;
    rrm->dsp_mask = NULL;
}

int rec_use_m68k(const rec_module_t *mod, void *handle) {
    if (!mod || mod->abi != REC_ABI_VERSION || mod->kind != 0 ||
            !mod->nlabels)
        return -1;
    if (handle_m68k) rec_close_handle(handle_m68k);
    mod_m68k = mod;
    handle_m68k = handle;
    map68_build(mod);
    return 0;
}

int rec_use_dsp(const rec_module_t *mod, void *handle) {
    if (!mod || mod->abi != REC_ABI_VERSION || mod->kind != 1 ||
            !mod->nlabels)
        return -1;
    if (!mod->label_fn || !mod->image || !mod->mask) return -1;
    if (handle_dsp) rec_close_handle(handle_dsp);
    mod_dsp = mod;
    handle_dsp = handle;
    mapdsp_build(mod);
    rrm->dsp_mask = mod->mask;
    rrm->dsp_gen++;
    return 0;
}

int rec_have_m68k(void) {
    return !force_interp_m68k && mod_m68k && mod_m68k->nlabels > 0;
}

int rec_have_dsp(void) {
    return !force_interp_dsp && mod_dsp && mod_dsp->nlabels > 0;
}

// DSP entry map: drec_idx(pc) -> function index (-1 = interpret), plus
// per function the dsp_gen at which it last matched (or failed to match)
// the image it was translated from.
static int32_t *mapdsp;
static uint32_t *dsp_ok_gen, *dsp_bad_gen;

static void mapdsp_build(const rec_module_t *m) {
    uint32_t i;
    free(mapdsp);
    free(dsp_ok_gen);
    free(dsp_bad_gen);
    mapdsp = (int32_t *)malloc(DREC_WORDS * sizeof(*mapdsp));
    dsp_ok_gen = (uint32_t *)calloc(m->nchunks, sizeof(*dsp_ok_gen));
    dsp_bad_gen = (uint32_t *)calloc(m->nchunks, sizeof(*dsp_bad_gen));
    if (!mapdsp || !dsp_ok_gen || !dsp_bad_gen) {
        free(mapdsp);
        mapdsp = NULL;
        return;
    }
    for (i = 0; i < DREC_WORDS; i++) mapdsp[i] = -1;
    for (i = 0; i < m->nlabels; i++) {
        int k = drec_idx(m->labels[i]);
        if (k >= 0 && m->label_fn[i] < m->nchunks)
            mapdsp[k] = (int32_t)m->label_fn[i];
    }
}

// Does DSP memory (as fetched) still hold function f's code?
static int dsp_func_ok(uint32_t f) {
    const rec_func_t *fn = &mod_dsp->chunks[f];
    uint32_t a;
    uint32_t gen = rrm->dsp_gen;
    if (dsp_ok_gen[f] == gen) return 1;
    if (dsp_bad_gen[f] == gen) return 0;
    for (a = fn->lo; a < fn->hi; a++) {
        int i = drec_idx(a);
        if (i < 0) break;
        if (mod_dsp->mask[i] && rrfns->drd(a) != mod_dsp->image[i]) {
            dsp_bad_gen[f] = gen;
            return 0;
        }
    }
    dsp_ok_gen[f] = gen;
    return 1;
}

static rec_fn_t dsp_lookup(uint32_t pc) {
    int i = drec_idx(pc);
    int32_t f;
    if (i < 0 || !mapdsp || (f = mapdsp[i]) < 0) return NULL;
    return dsp_func_ok((uint32_t)f) ? mod_dsp->chunks[f].fn : NULL;
}

// 68K entry map: ROM pc / 2 -> translated function (NULL = interpret).
#define MAP68_SIZE (RR_ROM_SIZE / 2)
static rec_fn_t *map68;

static void map68_build(const rec_module_t *m) {
    uint32_t i;
    if (!map68) map68 = (rec_fn_t *)calloc(MAP68_SIZE, sizeof(*map68));
    if (!map68) return;
    memset(map68, 0, MAP68_SIZE * sizeof(*map68));
    if (!m || !m->label_fn) return;
    for (i = 0; i < m->nlabels; i++) {
        uint32_t pc = m->labels[i];
        if (pc < RR_ROM_SIZE && !(pc & 1) && m->label_fn[i] < m->nchunks)
            map68[pc >> 1] = m->chunks[m->label_fn[i]].fn;
    }
}

static rec_fn_t map68_get(uint32_t pc) {
    return (pc < RR_ROM_SIZE && !(pc & 1) && map68) ? map68[pc >> 1] : NULL;
}

void rec_run_main(void) {
    m68k_t *c = rrm->cpu;
    if (!rec_have_m68k() || (c->sr & (M68K_SR_T | M68K_SR_T0))) {
        m68k_run(); // interpreter (also covers trace mode)
        return;
    }
    for (;;) {
        rec_fn_t fn;
        if (c->cycles <= 0 || c->halted) return;
        if (c->stop) {
            uint32_t mask = (c->sr & M68K_SR_IMASK) >> 8;
            if (c->irq > mask || c->irq == 7) {
                c->stop = 0;
            } else {
                return;
            }
        }
        if (m68k_service()) {
            c->cycles -= 20;
            c->total++;
            continue;
        }
        if (c->sr & (M68K_SR_T | M68K_SR_T0)) {
            m68k_run();
            return;
        }
        fn = map68_get(c->pc & 0xffffffu);
        if (fn) {
            // Translated code returns at block ends; the loop above takes
            // pending exceptions and interrupts, as the interpreter does
            // between instructions.
            uint64_t before = c->total;
            uint32_t pc0 = c->pc;
            fn();
            if (c->total != before || c->pc != pc0 || c->exc || c->stop)
                continue;
        }
        // Not translated (or the function could not enter): interpret one
        // instruction, then look again.
        {
            int st;
            if (hist68) hist_m68k(c->pc & 0xffffffu);
            st = m68k_step();
            if (st != 0) return;
        }
    }
}

// Mirrors c31_run: translated functions run whole steps (see DI in
// c31_core.h), the interpreter covers the rest, so both give the same
// result to the bit.
void rec_run_dsp(void) {
    c31_t *d = rrm->dsp;
    cage_slice_begin(d->cycles);
    if (!rec_have_dsp()) {
        c31_run();
    } else {
        for (;;) {
            rec_fn_t fn;
            if (d->cycles <= 0) break;
            if (d->idle) {
                d_check_irqs(d);
                if (d->idle) { d->cycles = 0; break; }
            }
            fn = dsp_lookup(d->pc & 0xffffffu);
            if (fn) {
                // Progress: instructions ran, or a repeat block looped
                // back into another function.
                uint64_t before = d->total;
                uint32_t pc0 = d->pc;
                fn();
                if (d->total != before || d->pc != pc0 || d->idle) continue;
            }
            if (histdsp) hist_dsp(d->pc & 0xffffffu);
            if (c31_step()) { d->cycles = 0; break; }
        }
    }
    cage_slice_end();
    cage_update();
}
