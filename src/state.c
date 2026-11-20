// Whole-machine save states. See state.h.
#include "state.h"
#include "machine.h"
#include "mem.h"
#include "rec_build.h"
#include "gtvid.h"
#include <stdlib.h>
#include <string.h>

#define STATE_MAGIC 0x31525252u // "RRR1"

uint32_t state_compat_id(void) {
    return ((uint32_t)REC_ABI_VERSION << 24) |
            ((uint32_t)RECOMP_VERSION << 16) | STATE_FORMAT;
}

static uint32_t crc32_buf(const uint8_t *data, uint32_t len) {
    uint32_t crc = 0xffffffffu, i, j;
    for (i = 0; i < len; i++) {
        crc ^= data[i];
        for (j = 0; j < 8; j++)
            crc = (crc & 1) ? (crc >> 1) ^ 0xedb88320u : crc >> 1;
    }
    return crc ^ 0xffffffffu;
}

static void put32(uint8_t *dst, uint32_t v) {
    dst[0] = (uint8_t)v;
    dst[1] = (uint8_t)(v >> 8);
    dst[2] = (uint8_t)(v >> 16);
    dst[3] = (uint8_t)(v >> 24);
}

static uint32_t get32(const uint8_t *src) {
    return (uint32_t)src[0] | ((uint32_t)src[1] << 8) |
            ((uint32_t)src[2] << 16) | ((uint32_t)src[3] << 24);
}

// Fixed spans (all images are fixed-size by construction).
#define SP_CPU sizeof(m68k_t)
#define SP_DSP sizeof(c31_t)
#define SP_RAM RR_RAM_SIZE
#define SP_BANK RR_BANK_SIZE
#define SP_CR RR_CR_SIZE
#define SP_DRAM (RR_DSP_RAM_WORDS * 4)
#define SP_DIRAM (0x800u * 4)
#define SP_DIO (0x100u * 4)
#define SP_DAC (CAGE_DAC_CHANNELS * CAGE_DAC_FIFO * 2)
#define SP_EE 2048u
#define SP_XGA sizeof(xga_t)

void machine_state_size(uint32_t *size) {
    *size = 16 + SP_CPU + SP_DSP + SP_RAM + SP_BANK + SP_CR + SP_DRAM +
            SP_DIRAM + SP_DIO + 64 + SP_DAC + 32 + SP_EE + 4 + SP_XGA +
            92 + gt_state_size();
}

void machine_state_save(uint8_t *dst) {
    uint8_t *p = dst;
    put32(p, STATE_MAGIC);
    put32(p + 4, state_compat_id());
    {
        uint32_t size;
        machine_state_size(&size);
        put32(p + 8, size);
    }
    put32(p + 12, mach.rom ? crc32_buf(mach.rom, 0x200000u) : 0);
    p += 16;
    memcpy(p, &mach.cpu, SP_CPU);
    p += SP_CPU;
    memcpy(p, &mach.dsp, SP_DSP);
    p += SP_DSP;
    memcpy(p, mach.ram, SP_RAM);
    p += SP_RAM;
    memcpy(p, mach.bank, SP_BANK);
    p += SP_BANK;
    memcpy(p, mach.colorram, SP_CR);
    p += SP_CR;
    memcpy(p, cage.ram, SP_DRAM);
    p += SP_DRAM;
    memcpy(p, cage.iram, SP_DIRAM);
    p += SP_DIRAM;
    memcpy(p, cage.io, SP_DIO);
    p += SP_DIO;
    put32(p, cage.from_main);
    put32(p + 4, cage.control);
    put32(p + 8, cage.latch);
    put32(p + 12, (uint32_t)cage.cpu_to_cage |
            ((uint32_t)cage.cage_to_cpu << 8) |
            ((uint32_t)cage.dma_enabled << 16) |
            ((uint32_t)cage.dma_timer_on << 24));
    put32(p + 16, (uint32_t)cage.dma_end_cyc);
    put32(p + 20, (uint32_t)(cage.dma_end_cyc >> 32));
    put32(p + 24, cage.dma_end_addr);
    put32(p + 28, (uint32_t)cage.timer_on[0] |
            ((uint32_t)cage.timer_on[1] << 8) |
            ((uint32_t)cage.dsp_held << 16));
    put32(p + 32, (uint32_t)cage.timer_end[0]);
    put32(p + 36, (uint32_t)(cage.timer_end[0] >> 32));
    put32(p + 40, (uint32_t)cage.timer_end[1]);
    put32(p + 44, (uint32_t)(cage.timer_end[1] >> 32));
    put32(p + 48, cage.dac_rate);
    put32(p + 52, (uint32_t)cage_abs_cyc);
    put32(p + 56, (uint32_t)(cage_abs_cyc >> 32));
    put32(p + 60, (uint32_t)cage_slice_budget);
    p += 64;
    {
        uint32_t ch;
        for (ch = 0; ch < CAGE_DAC_CHANNELS; ch++) {
            memcpy(p, cage.dac[ch], CAGE_DAC_FIFO * 2);
            p += CAGE_DAC_FIFO * 2;
        }
        for (ch = 0; ch < CAGE_DAC_CHANNELS; ch++) {
            put32(p, cage.dac_wr[ch]);
            put32(p + 4, cage.dac_rd[ch]);
            p += 8;
        }
    }
    memcpy(p, eeprom.data, SP_EE);
    p += SP_EE;
    put32(p, eeprom.unlocked);
    p += 4;
    memcpy(p, &xga, SP_XGA);
    p += SP_XGA;
    put32(p, mach.irq3);
    put32(p + 4, mach.irq4);
    put32(p + 8, mach.irq6);
    put32(p + 12, mach.coin_counters);
    put32(p + 16, mach.led_state);
    put32(p + 20, mach.color_latch);
    put32(p + 24, mach.mo_control);
    put32(p + 28, mach.scanline);
    put32(p + 32, mach.vblank);
    put32(p + 36, (uint32_t)mach.scan_cyc);
    put32(p + 40, (uint32_t)mach.vblank_cyc);
    p += 44;
    put32(p, (uint32_t)mach.carry68k);
    put32(p + 4, (uint32_t)(mach.carry68k >> 32));
    put32(p + 8, (uint32_t)mach.carrydsp);
    put32(p + 12, (uint32_t)(mach.carrydsp >> 32));
    put32(p + 16, (uint32_t)mach.irq6_cyc);
    put32(p + 20, mach.playfield_tile_bank);
    put32(p + 24, mach.in.b1);
    put32(p + 28, mach.in.b3);
    put32(p + 32, mach.in.service);
    put32(p + 36, mach.in.coin1);
    put32(p + 40, mach.in.coin2);
    put32(p + 44, mach.in.tilt);
    p += 48;
    gt_state_save(p);
}

// Re-validate the translated DSP chunks against the session snapshot.
static void dsp_revalidate(void) {
    uint32_t c;
    if (!rrm->dsp_snap || !rrm->dsp_mask) return;
    memset(rrm->dsp_bad, 0, sizeof(rrm->dsp_bad));
    for (c = 0; c < 4; c++) {
        uint32_t a;
        for (a = c * 0x4000u; a < (c + 1) * 0x4000u; a++) {
            if (rrm->dsp_mask[a] && cage.ram[a] != rrm->dsp_snap[a]) {
                rrm->dsp_bad[c] = 1;
                break;
            }
        }
    }
    {
        uint32_t a;
        for (a = 0; a < 0x800u; a++) {
            if (rrm->dsp_mask[0x10000u + a] &&
                    cage.iram[a] != rrm->dsp_snap[0x10000u + a]) {
                rrm->dsp_bad[4] = 1;
                break;
            }
        }
    }
}

int machine_state_load(const uint8_t *src, uint32_t size) {
    const uint8_t *p = src;
    uint32_t want;
    machine_state_size(&want);
    if (size < want) return -1;
    if (get32(p) != STATE_MAGIC) return -1;
    if (get32(p + 4) != state_compat_id()) return -1;
    if (get32(p + 8) != want) return -1;
    if (!mach.rom || get32(p + 12) != crc32_buf(mach.rom, 0x200000u))
        return -1;
    p += 16;
    memcpy(&mach.cpu, p, SP_CPU);
    p += SP_CPU;
    memcpy(&mach.dsp, p, SP_DSP);
    p += SP_DSP;
    memcpy(mach.ram, p, SP_RAM);
    p += SP_RAM;
    memcpy(mach.bank, p, SP_BANK);
    p += SP_BANK;
    memcpy(mach.colorram, p, SP_CR);
    p += SP_CR;
    memcpy(cage.ram, p, SP_DRAM);
    p += SP_DRAM;
    memcpy(cage.iram, p, SP_DIRAM);
    p += SP_DIRAM;
    memcpy(cage.io, p, SP_DIO);
    p += SP_DIO;
    cage.from_main = (uint16_t)get32(p);
    cage.control = (uint16_t)get32(p + 4);
    cage.latch = (uint16_t)get32(p + 8);
    cage.cpu_to_cage = (uint8_t)get32(p + 12);
    cage.cage_to_cpu = (uint8_t)(get32(p + 12) >> 8);
    cage.dma_enabled = (uint8_t)(get32(p + 12) >> 16);
    cage.dma_timer_on = (uint8_t)(get32(p + 12) >> 24);
    cage.dma_end_cyc = (int64_t)(((uint64_t)get32(p + 20) << 32) |
            get32(p + 16));
    cage.dma_end_addr = get32(p + 24);
    cage.timer_on[0] = (uint8_t)get32(p + 28);
    cage.timer_on[1] = (uint8_t)(get32(p + 28) >> 8);
    cage.dsp_held = (uint8_t)(get32(p + 28) >> 16);
    cage.timer_end[0] = (int64_t)(((uint64_t)get32(p + 36) << 32) |
            get32(p + 32));
    cage.timer_end[1] = (int64_t)(((uint64_t)get32(p + 44) << 32) |
            get32(p + 40));
    cage.dac_rate = get32(p + 48);
    cage_abs_cyc = (int64_t)(((uint64_t)get32(p + 56) << 32) |
            get32(p + 52));
    cage_slice_budget = (int)get32(p + 60);
    cage_retune();
    p += 64;
    {
        uint32_t ch;
        for (ch = 0; ch < CAGE_DAC_CHANNELS; ch++) {
            memcpy(cage.dac[ch], p, CAGE_DAC_FIFO * 2);
            p += CAGE_DAC_FIFO * 2;
        }
        for (ch = 0; ch < CAGE_DAC_CHANNELS; ch++) {
            cage.dac_wr[ch] = get32(p);
            cage.dac_rd[ch] = get32(p + 4);
            p += 8;
        }
    }
    memcpy(eeprom.data, p, SP_EE);
    p += SP_EE;
    eeprom.unlocked = (uint8_t)get32(p);
    p += 4;
    memcpy(&xga, p, SP_XGA);
    p += SP_XGA;
    mach.irq3 = (uint8_t)get32(p);
    mach.irq4 = (uint8_t)get32(p + 4);
    mach.irq6 = (uint8_t)get32(p + 8);
    mach.coin_counters = (uint8_t)get32(p + 12);
    mach.led_state = (uint8_t)get32(p + 16);
    mach.color_latch = (uint8_t)get32(p + 20);
    mach.mo_control = (uint8_t)get32(p + 24);
    mach.scanline = (uint16_t)get32(p + 28);
    mach.vblank = (uint8_t)get32(p + 32);
    mach.scan_cyc = (int32_t)get32(p + 36);
    mach.vblank_cyc = (int32_t)get32(p + 40);
    p += 44;
    mach.carry68k = (uint64_t)get32(p) | ((uint64_t)get32(p + 4) << 32);
    mach.carrydsp = (uint64_t)get32(p + 8) | ((uint64_t)get32(p + 12) << 32);
    mach.irq6_cyc = (int32_t)get32(p + 16);
    mach.playfield_tile_bank = (uint8_t)get32(p + 20);
    mach.in.b1 = (uint8_t)get32(p + 24);
    mach.in.b3 = (uint8_t)get32(p + 28);
    mach.in.service = (uint8_t)get32(p + 32);
    mach.in.coin1 = (uint8_t)get32(p + 36);
    mach.in.tilt = (uint8_t)get32(p + 40);
    p += 48;
    gt_state_load(p);
    // Derived state: re-derive the DSP float word rate (done above),
    // refresh the IRQ lines, and re-validate translated DSP chunks.
    mem_map_pages();
    cage_map_pages();
    mem_update_irq();
    dsp_revalidate();
    return 0;
}
