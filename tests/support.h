// Shared test support: ROM-free machine init plus TAP assertions.
#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "machine.h"
#include "mem.h"
#include "m68k.h"
#include "m68k_core.h"
#include "m68kdis.h"
#include "rec_abi.h"
#include "cage.h"
#include "eeprom.h"
#include "gtvid.h"
#include "roms.h"
#include "game.h"

static int t_failures = 0;
static int t_count = 0;

#define T_OK(cond, ...) do { \
        t_count++; \
        if (cond) { printf("ok %d - ", t_count); printf(__VA_ARGS__); putchar('\n'); } \
        else { t_failures++; printf("not ok %d - ", t_count); printf(__VA_ARGS__); putchar('\n'); } \
    } while (0)

#define T_EQ_U32(a, e, ...) do { \
        uint32_t _a = (uint32_t)(a), _e = (uint32_t)(e); \
        t_count++; \
        if (_a == _e) { printf("ok %d - ", t_count); printf(__VA_ARGS__); putchar('\n'); } \
        else { t_failures++; printf("not ok %d - ", t_count); printf(__VA_ARGS__); \
            printf(" (got %08X want %08X)\n", _a, _e); } \
    } while (0)

#define T_EQ_STR(a, e, ...) do { \
        const char *_a = (a); const char *_e = (e); \
        t_count++; \
        if (strcmp(_a, _e) == 0) { printf("ok %d - ", t_count); printf(__VA_ARGS__); putchar('\n'); } \
        else { t_failures++; printf("not ok %d - ", t_count); printf(__VA_ARGS__); \
            printf(" (got '%s' want '%s')\n", _a, _e); } \
    } while (0)

#define T_SUBSTR(hay, needle, ...) do { \
        t_count++; \
        if (strstr((hay), (needle))) { printf("ok %d - ", t_count); printf(__VA_ARGS__); putchar('\n'); } \
        else { t_failures++; printf("not ok %d - ", t_count); printf(__VA_ARGS__); \
            printf(" ('%s' missing from '%s')\n", (needle), (hay)); } \
    } while (0)

// Bring up RAM-backed machine state without needing game ROMs.
static void test_init(void) {
    if (!mach.ram) mach.ram = (uint8_t *)calloc(1, RR_RAM_SIZE);
    if (!mach.bank) mach.bank = (uint8_t *)calloc(1, RR_BANK_SIZE);
    if (!mach.colorram)
        mach.colorram = (uint16_t *)calloc(RR_CR_SIZE / 2, 2);
    if (!cage.ram) cage.ram = (uint32_t *)calloc(RR_DSP_RAM_WORDS, 4);
    if (!cage.iram) cage.iram = (uint32_t *)calloc(0x800u, 4);
    machine_reset();
    mach.in.b1 = 0xff;
    mach.in.b3 = 0xff;
}

static int test_finish(void) {
    printf("# %d tests, %d failures\n", t_count, t_failures);
    return t_failures ? 1 : 0;
}

// Direct RAM helpers (main RAM window 0xF80000-0xFFFFFF).
static void t_w32(uint32_t addr, uint32_t v) {
    uint8_t *p = mach.ram + (addr - 0xf80000u);
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

static uint32_t t_r32(uint32_t addr) {
    return rr_mrd32(addr, 0);
}

// Decode one instruction from raw words; fails the test on decode error.
static int t_decode(uint32_t pc, const uint16_t *words, int nwords,
        m_insn_t *out, const char *what) {
    int len = m68k_decode(pc, words, nwords, out);
    T_OK(len > 0, "%s decodes", what);
    return len;
}
