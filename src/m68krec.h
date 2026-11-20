// 68EC020 static translator: plans reachable ROM code and emits C.
#pragma once
#include <stdio.h>
#include "m68k_core.h"

#define REC_CHUNK_SHIFT 16u
#define REC_CHUNK_SIZE (1u << REC_CHUNK_SHIFT)

typedef struct {
    const uint8_t *rom;
    uint32_t size;
    uint8_t *seen;          // per word: reached by traversal
    uint8_t *is_start;      // per word: block start
    uint32_t *starts;       // sorted block starts (filled by plan_run)
    int nstarts, cap;
    uint32_t *work;         // traversal worklist
    int nwork, workcap;
} rec_plan_t;

void m68krec_plan_init(rec_plan_t *p, const uint8_t *rom, uint32_t size);
void m68krec_plan_entry(rec_plan_t *p, uint32_t pc);
void m68krec_plan_run(rec_plan_t *p);
int m68krec_plan_has(rec_plan_t *p, uint32_t pc);
void m68krec_plan_free(rec_plan_t *p);

// Emit the C for one chunk [lo, hi). Returns translated bytes covered.
uint32_t m68krec_emit(FILE *f, rec_plan_t *p, uint32_t lo, uint32_t hi,
        int chunk_id);
