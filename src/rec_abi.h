// Definitions shared by the program and the translated game code.
//
// The translated code is either linked into the program (developer builds,
// `make recomp`) or built on the player's machine into a separate module
// (.so/.dll, via the system compiler or TinyCC) that the program loads.
// Modules are compiled with REC_MODULE defined and without the C
// library's headers, so this file stands on its own in that case.
#pragma once

#ifdef REC_MODULE
typedef unsigned char uint8_t;
typedef signed char int8_t;
typedef unsigned short uint16_t;
typedef short int16_t;
typedef unsigned int uint32_t;
typedef int int32_t;
typedef unsigned long long uint64_t;
typedef long long int64_t;
typedef __SIZE_TYPE__ size_t;
#if !defined(__cplusplus) && (!defined(__STDC_VERSION__) || __STDC_VERSION__ < 202311L)
#define bool _Bool
#define true 1
#define false 0
#endif
#define INT32_MIN (-2147483647 - 1)
#else
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#endif

// Bumped whenever anything below (or what modules expect of the program) changes.
#define REC_ABI_VERSION 3

// Main CPU (68EC020, 24-bit bus) memory sizes.
#define RR_ROM_SIZE   0x200000u   // program ROM at 0x000000
#define RR_RAM_SIZE   0x080000u   // main RAM at 0xF80000
#define RR_BANK_SIZE  0x010000u   // work RAM bank at 0xD70000
#define RR_CR_SIZE    0x080000u   // colour RAM window at 0xD80000
#define RR_NPAGES     256u        // 64 KB pages over the 24-bit bus

// CAGE DSP (TMS320C31, word addresses) memory sizes, in 32-bit words.
#define RR_DSP_RAM_WORDS  0x10000u
#define RR_DSP_BOOT_WORDS 0x80000u
#define RR_DSP_ROM_WORDS  0x400000u
#define RR_DSP_NPAGES     256u    // 64K-word pages over the 24-bit word bus

// ---------------------------------------------------------------- 68K state

typedef struct {
    uint32_t d[8], a[8];
    uint32_t pc;            // address of the next instruction to execute
    uint32_t usp, isp, msp; // banked stacks; a[7] mirrors the active one
    uint16_t sr;            // T/S/M/I/CCR
    uint32_t vbr;
    uint8_t sfc, dfc;
    uint32_t cacr, caar;
    uint8_t irq;            // IPL lines, 0-7
    uint8_t stop;           // STOP state: wait for irq above the mask
    uint8_t halted;         // double fault
    int32_t cycles;         // slice budget left; <= 0 leaves translated code
    uint64_t total;         // instructions executed
    uint8_t exc;            // pending exception vector (0 = none)
    uint32_t exc_pc;        // fault pc recorded for the exception frame
    uint16_t exc_ssw;       // extra status for address/bus error frames
    uint32_t exc_ea;        // fault address for address/bus error frames
} m68k_t;

#define M68K_SR_T    0x8000u
#define M68K_SR_T0   0x4000u
#define M68K_SR_S    0x2000u
#define M68K_SR_M    0x1000u
#define M68K_SR_IMASK 0x0700u
#define M68K_SR_X    0x0010u
#define M68K_SR_N    0x0008u
#define M68K_SR_Z    0x0004u
#define M68K_SR_V    0x0002u
#define M68K_SR_C    0x0001u

// What a translated 68K block function returns.
typedef enum {
    RR_CONT = 0,    // ran out of instructions in this function; keep going
    RR_JUMP,        // pc changed; dispatch
    RR_EXIT         // slice over, exception pending, or stop; return to program
} rr_rc_t;

// ---------------------------------------------------------------- DSP state

// R0-R7 hold the DSP float as (mantissa, exponent) pairs, matching the
// hardware's extended precision: value = mantissa * 2^(exponent - 31),
// zero = (anything, -128) normalised to (0x80000000, -128) on store.
typedef struct {
    int32_t man;
    int32_t exp;
} c31reg_t;

typedef struct {
    c31reg_t r[8];
    uint32_t ar[8];
    uint32_t dp, ir0, ir1, bk, sp, st, ie, iff, iof;
    uint32_t rs, re, rc;    // repeat-block state (RPTS and RPTB share these)
    uint32_t xreg[4];       // register indices 28-31 (scratch)
    uint32_t pc;            // word address of the next instruction
    uint32_t djump;         // pending delayed-branch target
    int8_t dcount;          // instructions left before djump applies (-1 = none)
    uint8_t rpts_active;    // RPTS repeat in progress (defers interrupts)
    uint8_t lopower;        // low-power mode (16 clocks per cycle)
    uint32_t bkmask;        // circular-buffer mask derived from BK
    uint16_t irq;           // external interrupt line levels (bits 0-3 used)
    int32_t cycles;
    uint64_t total;
    uint8_t idle;           // IDLE state: wait for an enabled interrupt
} c31_t;

// ST bits.
#define C31_ST_C    0x00000001u
#define C31_ST_V    0x00000002u
#define C31_ST_Z    0x00000004u
#define C31_ST_N    0x00000008u
#define C31_ST_UF   0x00000010u
#define C31_ST_LV   0x00000020u
#define C31_ST_LUF  0x00000040u
#define C31_ST_OVM  0x00000080u
#define C31_ST_RM   0x00000100u
#define C31_ST_CF   0x00000400u
#define C31_ST_CE   0x00000800u
#define C31_ST_CC   0x00001000u
#define C31_ST_GIE  0x00002000u

// Interrupt lines (IF/IE bit positions and vector offsets from the table).
#define C31_INT0 0    // INT0: reset-ish external (unused here)
#define C31_INT1 1
#define C31_INT2 2
#define C31_INT3 3
#define C31_XINT0 4
#define C31_RINT0 5
#define C31_TINT0 8
#define C31_TINT1 9
#define C31_DINT0 10

// ---------------------------------------------------------------- host view

// Fast page tables. NULL = slow path through the host functions.
typedef struct {
    uint32_t abi, sizeof_m68k, sizeof_c31;
    m68k_t *cpu;
    c31_t *dsp;
    uint8_t *mrd[RR_NPAGES], *mwr[RR_NPAGES];   // 68K byte pages
    uint32_t *drd[RR_DSP_NPAGES], *dwr[RR_DSP_NPAGES]; // DSP word pages
    // CAGE speedup: DSP writes to this word burn 100 cycles (MAME's
    // write-hooked speedup). 0xFFFFFFFF = disabled.
    uint32_t dsp_speedup;
    // Translated-DSP validity: dsp_mask flags the code words the module
    // was built from (indexed as drec_idx: lowmem 0-0xFFFF, then IRAM).
    // A write that changes one bumps dsp_gen; translated DSP code then
    // returns to the dispatcher, which re-checks each function against
    // the module's image before entering it. NULL mask = no module.
    const uint8_t *dsp_mask;
    uint32_t dsp_gen;
} rr_mem_t;

typedef struct {
    // 68K slow paths (big-endian guest).
    uint32_t (*mrd8)(uint32_t addr);
    uint32_t (*mrd16)(uint32_t addr);
    uint32_t (*mrd32)(uint32_t addr);
    void (*mwr8)(uint32_t addr, uint32_t val);
    void (*mwr16)(uint32_t addr, uint32_t val);
    void (*mwr32)(uint32_t addr, uint32_t val);
    // 68K exceptions and interrupts.
    void (*mtrap)(uint32_t vec, uint32_t pc);   // take exception now
    void (*mirq)(void);                         // service a pending interrupt
    // DSP slow paths (word addresses).
    uint32_t (*drd)(uint32_t addr);
    void (*dwr)(uint32_t addr, uint32_t val);
    void (*dirq)(void);                         // service a pending DSP interrupt
    // Slice control.
    int (*poll)(void);                          // nonzero = leave translated code
} rr_hostfns_t;

// Translated 68K code: a function per chunk of ROM. Entered with cpu->pc at
// one of its labels, it runs until the cycles run out, the pc leaves what
// it can run, or an exception/interrupt/stop needs the program, and returns
// how it stopped.
typedef rr_rc_t (*rec_fn_t)(void);

typedef struct {
    rec_fn_t fn;
    uint32_t lo, hi;    // guest address range the C code was generated from
} rec_func_t;

// Translated DSP chunks have the same shape as 68K chunks (entered with
// dsp->pc at one of their labels, returning how they stopped).

// rrm/rrfns connect translated code to the program. Dynamically loaded
// modules define their own copies (in their exports file) which the
// program fills via rec_init_module after loading; linked-in translated
// code (built without REC_MODULE) uses the program's own globals.
extern rr_mem_t *rrm;
extern const rr_hostfns_t *rrfns;

// A translated module: functions with address ranges plus the sorted
// entry labels. 68K modules give each label's function in label_fn (the
// dispatcher maps pcs straight to functions). DSP modules also carry the
// image they were translated from, for the code guard.
typedef struct {
    uint32_t abi, kind; // REC_ABI_VERSION, 0 = 68K, 1 = DSP
    uint32_t nchunks, nlabels;
    const rec_func_t *chunks; // sorted by lo
    const uint32_t *labels;   // sorted pcs
    const uint32_t *label_fn; // per label: index into chunks
    const uint32_t *image;    // DSP: words translated (drec_idx layout)
    const uint8_t *mask;      // DSP: which of them are code
} rec_module_t;
