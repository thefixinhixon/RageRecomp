// 68EC020 static translator: finds the game's code in ROM and emits C.
//
// Code is found by recursive descent from the exception vectors, plus any
// value that looks like a pointer to code: absolute operands and immediates
// in the code itself, and every long in ROM (function pointers, jump and
// state tables). A pointer only counts if decoding from it gives plausible
// code that does not overlap known instructions.
//
// Every instruction start becomes an entry label, so the dispatcher can
// enter translated code wherever the game resumes (returns, exception
// returns, interrupts taken at block ends). Call targets split the code
// into C functions; branches inside a function become gotos.
#pragma once
#include <stddef.h>
#include "m68k_core.h"

typedef struct {
    const uint8_t *rom;
    uint32_t size;          // ROM bytes searched
    uint32_t limit;         // code region end (pointer candidates below it)
    uint8_t *start;         // per word: an instruction starts here
    uint8_t *inside;        // per word: covered by an instruction (not its start)
    uint8_t *fstart;        // per word: a function starts here
    uint8_t *strong;        // per word: covered by code found by descent
                            // from the vectors (not by a pointer guess)
    int guessing;           // descending from a pointer candidate
    uint32_t *work;         // descent worklist
    int nwork, workcap;
    uint32_t ninsns, nfuncs, nptr;  // statistics
} rec_plan_t;

// Find the code: descent from the vectors, then pointer candidates.
void m68krec_plan(rec_plan_t *p, const uint8_t *rom, uint32_t size);
void m68krec_plan_free(rec_plan_t *p);

// Write the module sources (rec_NNN.c plus exports.c) into dir. `module`
// names the exported rec_module_t. Appends the file names, space
// separated, to `files`. Returns 0 on success.
int m68krec_write(rec_plan_t *p, const char *dir, const char *module,
        char *files, size_t files_cap);
