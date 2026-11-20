// TMS320C31 static translator: plans reachable DSP code and emits C.
// Fixed-width ISA, so planning is exact (no data/code ambiguity beyond
// unreached words, which run on the interpreter). Delayed branches with
// straight-line slots are inlined; the rest use d_exec_delayed_slow.
#pragma once
#include <stdio.h>
#include "c31_core.h"

#define DREC_CHUNK_SHIFT 14u
#define DREC_CHUNK_SIZE (1u << DREC_CHUNK_SHIFT)

typedef struct {
    const uint32_t *img;    // translated image (word array)
    uint32_t base;          // word address of img[0]
    uint32_t words;         // image length in words
    uint8_t *is_start;      // per word: block start
    uint8_t *seen;          // per word: decoded as code (validity mask)
    uint32_t *starts;
    int nstarts, cap;
    uint32_t *work;
    int nwork, workcap;
    uint32_t *re_ends;      // RE+1 of each RPTB/RPTS range (for goto checks)
    int nre, recap;
} drec_plan_t;

void drec_plan_init(drec_plan_t *p, const uint32_t *img, uint32_t base,
        uint32_t words);
void drec_plan_entry(drec_plan_t *p, uint32_t pc);
void drec_plan_run(drec_plan_t *p);
int drec_plan_has(drec_plan_t *p, uint32_t pc);
int drec_is_re_end(drec_plan_t *p, uint32_t pc);
void drec_plan_free(drec_plan_t *p);

// Emit the C for one chunk [lo, hi). Returns words covered.
uint32_t drec_emit(FILE *f, drec_plan_t *p, uint32_t lo, uint32_t hi,
        int chunk_id);
