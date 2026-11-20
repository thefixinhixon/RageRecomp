// TMS320C31 static translator: finds the CAGE sound program's code in DSP
// RAM (as the boot loader left it) and emits C.
//
// Code is found by recursive descent from the interrupt and trap vectors
// and where the warm-up boot stopped, plus values that look like code
// pointers (16-bit immediates and words in RAM). Every instruction is an
// entry label; call targets split the code into C functions.
//
// Each instruction runs exactly as one c31_step: repeat-block check,
// shared semantics (c31_core.h), cycles, delay slots, interrupt check.
// So translated and interpreted DSP runs are bit-identical.
#pragma once
#include <stddef.h>
#include "c31_core.h"

typedef struct {
    uint32_t img[DREC_WORDS];   // code as fetched after the warm-up boot
    uint8_t code[DREC_WORDS];   // per word: an instruction (the guard mask)
    uint8_t data[DREC_WORDS];   // per word: known not to be code
    uint8_t fstart[DREC_WORDS]; // per word: a function starts here
    uint32_t *work;
    int nwork, workcap;
    uint32_t ninsns, nfuncs, nptr;
} drec_plan_t;

// Plan from img (filled by the caller) and the entry points given.
void drec_plan(drec_plan_t *p, const uint32_t *entries, int nentries);

// Write rec_NNN.c plus exports.c into dir; append file names to files.
int drec_write(const drec_plan_t *p, const char *dir, const char *module,
        char *files, size_t files_cap);
