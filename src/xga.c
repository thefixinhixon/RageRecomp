// Atari 136094-0004A protection FPGA. See xga.h and MAME's atarixga.cpp.
#include "xga.h"

xga_t xga;

#define PR_SETKEY 0x44010u
#define PR_DECIPHER 0x44022u
#define PR_STATUS 0x44700u
#define PR_DONE0 0x4c7c0u
#define PR_RESULT 0x4c7c2u
#define PR_DONE4 0x4c7c4u
#define PR_DATA 0x47800u
#define PR_CHAR0 0x48700u
#define PR_CHAR1 0x64000u
#define PR_CHAR2 0x6c000u

static uint32_t popcnt(uint32_t x) {
    uint32_t n = 0;
    while (x) {
        n += x & 1;
        x >>= 1;
    }
    return n;
}

// Bit permutation helper: out[i] = in[bits[i]] (bits[0] = MSB).
static uint32_t bitswap(uint32_t v, const uint8_t *bits, int n) {
    uint32_t r = 0;
    int i;
    for (i = 0; i < n; i++) {
        r <<= 1;
        r |= (v >> bits[i]) & 1;
    }
    return r;
}

static uint8_t clock_table[128];
static int clock_table_done = 0;

static void build_clock_table(void) {
    // Seven-bit LFSR, feedback 0x53, states XORed with 0x28 (MAME).
    uint8_t state = 1;
    unsigned clocks;
    int i;
    for (i = 0; i < 128; i++) clock_table[i] = 14;
    for (clocks = 126; clocks >= 15; clocks--) {
        clock_table[state ^ 0x28u] = (uint8_t)clocks;
        state = (uint8_t)(((state << 1) | (popcnt(state & 0x53u) & 1)) &
                0x7fu);
        if (clocks == 15) break; // unsigned loop end
    }
    clock_table_done = 1;
}

uint32_t xga_clocks(uint8_t encoded_key) {
    uint8_t key = encoded_key;
    if (!clock_table_done) build_clock_table();
    if (key & 0x80) key ^= 0xa8u;
    return clock_table[key & 0x7fu];
}

uint16_t xga_lfsr(uint16_t state, uint16_t taps, uint32_t clocks) {
    uint16_t previous = state;
    int early = 0;
    uint32_t i;
    for (i = 0; i < clocks; i++) {
        previous = state;
        state = (uint16_t)((state << 1) | (popcnt(state & taps) & 1));
        early |= (state == 1);
    }
    if (!early) return state;
    return (state == 1) ? 0 : previous;
}

static uint32_t key_offset(uint32_t index) {
    static const uint8_t b[11] = { 10, 9, 8, 6, 7, 1, 0, 4, 2, 5, 3 };
    return bitswap(index, b, 11) ^ 0x096u;
}

static uint16_t decipher(uint32_t index, uint16_t c) {
    static const uint8_t kb[8] = { 7, 6, 0, 3, 4, 2, 5, 1 };
    uint8_t key = xga.key[key_offset(index) & 0x7ffu];
    uint8_t encoded = (uint8_t)bitswap(key, kb, 8);
    uint32_t clocks = xga_clocks(encoded);
    uint16_t state = c ? c : 1;
    if (!c) clocks--;
    return xga_lfsr(state, xga.taps, clocks);
}

void xga_reset(void) {
    int i;
    for (i = 0; i < 2048; i++) xga.key[i] = 0;
    xga.mode = 0;
    xga.taps = 0;
    xga.reply = 0;
}

void xga_write16(uint32_t offset, uint16_t data) {
    if (offset >= PR_DATA && offset < PR_DATA + 2048u * 2) {
        uint32_t index = (offset - PR_DATA) >> 1;
        if (xga.mode == 1) {
            xga.key[index] = (uint8_t)(data & 0xffu);
            return;
        }
        if (xga.mode == 2) {
            xga.reply = decipher(index, data);
            return;
        }
    }
    if (offset == PR_CHAR0 || offset == PR_CHAR1 || offset == PR_CHAR2) {
        // Trailing configuration word passes through untouched.
        if (data != 0x8016) {
            static const uint8_t tb[16] = { 15, 10, 9, 14, 13, 8, 11, 12,
                0, 3, 1, 5, 2, 4, 7, 6 };
            xga.taps = (uint16_t)(bitswap(data, tb, 16) ^ 0xd4c6u);
        }
    }
}

int xga_read16(uint32_t offset, uint16_t *data) {
    switch (offset) {
    case PR_SETKEY:
        xga.mode = 1;
        return 0;
    case PR_DECIPHER:
        xga.mode = 2;
        return 0;
    case PR_DONE0:
    case PR_DONE4:
        if (xga.mode == 1) xga.mode = 0;
        return 0;
    case PR_STATUS:
        *data = 0x8000u;
        return 1;
    case PR_RESULT:
        if (xga.mode != 2) return 0;
        *data = xga.reply;
        xga.mode = 0;
        return 1;
    default:
        return 0;
    }
}
