// Atari 136094-0004A protection FPGA (Primal Rage), overlaid on the
// colour-RAM window. Behaviour follows MAME's atarixga.cpp (see NOTICE):
// a key upload followed by LFSR decipher queries. The five per-character
// tap transforms in MAME's comments are our golden test vectors.
#pragma once
#include <stdint.h>

typedef struct {
    uint8_t mode;       // 0 = idle, 1 = setkey, 2 = decipher
    uint16_t taps;      // LFSR feedback mask from the character word
    uint16_t reply;     // last query result
    uint8_t key[2048];  // uploaded key bytes
} xga_t;

extern xga_t xga;

void xga_reset(void);
// 16-bit accesses at byte offsets from 0xD80000. Reads return nonzero when
// the FPGA drives the bus (overriding colour RAM); *data is only written
// in that case. All accesses have side effects, as on the chip.
void xga_write16(uint32_t offset, uint16_t data);
int xga_read16(uint32_t offset, uint16_t *data);
// Exposed for tests: clock-count table and the raw LFSR step.
uint32_t xga_clocks(uint8_t encoded_key);
uint16_t xga_lfsr(uint16_t state, uint16_t taps, uint32_t clocks);
