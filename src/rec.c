// Translated-code dispatchers. See rec.h.
#include "rec.h"
#include "m68k.h"
#include "c31.h"
#include "c31_core.h"
#include "cage.h"
#include "rec_build.h"
#include <stdlib.h>
#include <string.h>

static const rec_module_t *mod_m68k = NULL;
static const rec_module_t *mod_dsp = NULL;
static void *handle_m68k = NULL;
static void *handle_dsp = NULL;
static int force_interp_m68k = 0;
static int force_interp_dsp = 0;

void rec_init(void) {
    force_interp_m68k = getenv("RR_NOREC") && getenv("RR_NOREC")[0] != 0 &&
            strcmp(getenv("RR_NOREC"), "0") != 0;
    force_interp_dsp = getenv("RR_NODSP") && getenv("RR_NODSP")[0] != 0 &&
            strcmp(getenv("RR_NODSP"), "0") != 0;
    if (!force_interp_m68k && rec_linked_m68k.nlabels > 0 &&
            rec_linked_m68k.abi == REC_ABI_VERSION)
        mod_m68k = &rec_linked_m68k;
    if (!force_interp_dsp && rec_linked_dsp.nlabels > 0 &&
            rec_linked_dsp.abi == REC_ABI_VERSION) {
        mod_dsp = &rec_linked_dsp;
        rrm->dsp_snap = rec_linked_snap;
        rrm->dsp_mask = rec_linked_mask;
        memset(rrm->dsp_bad, 0, sizeof(rrm->dsp_bad));
    }
}

void rec_shutdown(void) {
    // Handles are closed by rec_build (which opened them); just forget.
    if (handle_m68k) rec_close_handle(handle_m68k);
    if (handle_dsp) rec_close_handle(handle_dsp);
    handle_m68k = handle_dsp = NULL;
    mod_m68k = mod_dsp = NULL;
}

int rec_use_m68k(const rec_module_t *mod, void *handle) {
    if (!mod || mod->abi != REC_ABI_VERSION || mod->kind != 0 ||
            !mod->nlabels)
        return -1;
    if (handle_m68k) rec_close_handle(handle_m68k);
    mod_m68k = mod;
    handle_m68k = handle;
    return 0;
}

int rec_use_dsp(const rec_module_t *mod, void *handle) {
    if (!mod || mod->abi != REC_ABI_VERSION || mod->kind != 1 ||
            !mod->nlabels)
        return -1;
    if (handle_dsp) rec_close_handle(handle_dsp);
    mod_dsp = mod;
    handle_dsp = handle;
    return 0;
}

int rec_have_m68k(void) {
    return !force_interp_m68k && mod_m68k && mod_m68k->nlabels > 0;
}

int rec_have_dsp(void) {
    return !force_interp_dsp && mod_dsp && mod_dsp->nlabels > 0;
}

static int has_label(const rec_module_t *m, uint32_t pc) {
    uint32_t lo = 0, hi = m->nlabels;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (m->labels[mid] == pc) return 1;
        if (m->labels[mid] < pc) lo = mid + 1;
        else hi = mid;
    }
    return 0;
}

static int find_chunk_idx(const rec_module_t *m, uint32_t pc) {
    uint32_t lo = 0, hi = m->nchunks;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (pc < m->chunks[mid].lo) hi = mid;
        else if (pc >= m->chunks[mid].hi) lo = mid + 1;
        else return (int)mid;
    }
    return -1;
}

static rec_fn_t find_kchunk(const rec_module_t *m, uint32_t pc) {
    int i = find_chunk_idx(m, pc);
    return (i < 0) ? NULL : m->chunks[i].fn;
}

// DSP chunk entry, honouring the translated-code guard (a chunk whose
// RAM changed runs on the interpreter instead).
static rec_fn_t find_dchunk(const rec_module_t *m, uint32_t pc) {
    int i = find_chunk_idx(m, pc);
    uint32_t chunk;
    if (i < 0) return NULL;
    if (pc < 0x10000u) chunk = pc >> 14;
    else if (pc >= 0x809800u && pc < 0x80a000u) chunk = 4;
    else return NULL;
    if (chunk < 8 && rrm->dsp_mask && rrm->dsp_bad[chunk]) return NULL;
    return m->chunks[i].fn;
}

void rec_run_main(void) {
    m68k_t *c = rrm->cpu;
    if (!rec_have_m68k() || (c->sr & (M68K_SR_T | M68K_SR_T0))) {
        m68k_run(); // interpreter (also covers trace mode)
        return;
    }
    for (;;) {
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
        {
            uint32_t pc = c->pc & 0xffffffu;
            rec_fn_t fn = NULL;
            if (mod_m68k && has_label(mod_m68k, pc))
                fn = find_kchunk(mod_m68k, pc);
            if (fn) {
                rr_rc_t rc = fn();
                if (rc == RR_EXIT) return;
                continue; // RR_JUMP / RR_CONT: re-dispatch
            }
            // Untranslated: interpret until translated code or slice end.
            {
                int guard = 1024;
                while (c->cycles > 0 && guard-- > 0) {
                    int st;
                    uint32_t pc2 = c->pc & 0xffffffu;
                    if (mod_m68k && has_label(mod_m68k, pc2)) break;
                    st = m68k_step();
                    if (st != 0) return;
                    if (c->exc || c->stop) break;
                }
            }
        }
    }
}

void rec_run_dsp(void) {
    c31_t *d = rrm->dsp;
    if (!rec_have_dsp()) {
        int budget = d->cycles;
        cage_slice_begin(budget);
        c31_run();
        cage_slice_end();
        cage_update();
        return;
    }
    {
        int budget = d->cycles;
        cage_slice_begin(budget);
        for (;;) {
            if (d->cycles <= 0) break;
            if (d->idle) {
                d_check_irqs(d);
                if (d->idle) { d->cycles = 0; break; }
            }
            // Repeat blocks loop back here (not from inside delay slots).
            if (d->dcount < 0 && (d->st & C31_ST_RM) && d->pc == d->re + 1) {
                if ((int32_t)--d->rc >= 0) d->pc = d->rs & 0xffffffu;
                else {
                    d->st &= ~C31_ST_RM;
                    d->rpts_active = 0;
                }
            }
            d_check_irqs(d);
            {
                uint32_t pc = d->pc & 0xffffffu;
                rec_fn_t fn = NULL;
                // Translated code never runs mid-delay (delay sequences
                // are atomic inside a chunk) or in a tripped chunk.
                if (d->dcount < 0 && mod_dsp && has_label(mod_dsp, pc))
                    fn = find_dchunk(mod_dsp, pc);
                if (fn) {
                    rr_rc_t rc = fn();
                    if (rc == RR_EXIT) break;
                    continue; // RR_JUMP / RR_CONT: re-dispatch
                }
                {
                    int guard = 1024;
                    while (d->cycles > 0 && guard-- > 0) {
                        uint32_t pc2 = d->pc & 0xffffffu;
                        if (d->dcount < 0 && mod_dsp &&
                                has_label(mod_dsp, pc2)) break;
                        if (c31_step()) {
                            guard = 0;
                            break;
                        }
                    }
                    if (d->idle) { d->cycles = 0; break; }
                }
            }
        }
        cage_slice_end();
        cage_update();
        (void)budget;
    }
}
