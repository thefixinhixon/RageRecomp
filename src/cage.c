// Atari CAGE sound board. See cage.h and MAME's cage.cpp (NOTICE).
#include "cage.h"
#include "c31.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

cage_t cage;

static void cage_control_lines(void);
static void cage_update_dma(void);
static void cage_update_timer(int which);
static void cage_update_serial(void);

// IO register indices (word offsets from 0x808000).
#define DMA_GLOBAL_CTL 0x00
#define DMA_SOURCE_ADDR 0x04
#define DMA_DEST_ADDR 0x06
#define DMA_TRANSFER_COUNT 0x08
#define TIMER0_GLOBAL_CTL 0x20
#define TIMER0_COUNTER 0x24
#define TIMER0_PERIOD 0x28
#define TIMER1_GLOBAL_CTL 0x30
#define TIMER1_COUNTER 0x34
#define TIMER1_PERIOD 0x38
#define SPORT_GLOBAL_CTL 0x40
#define SPORT_TX_CTL 0x42
#define SPORT_RX_CTL 0x43
#define SPORT_TIMER_CTL 0x44
#define SPORT_TIMER_COUNTER 0x45
#define SPORT_TIMER_PERIOD 0x46
#define SPORT_DATA_TX 0x48
#define SPORT_DATA_RX 0x4c

int64_t cage_abs_cyc = 0;
int cage_slice_budget = 0;

// Monotonic DSP-cycle clock: absolute base plus elapsed time in the
// current slice (slices count down from their budget).
static int64_t dsp_now(void) {
    return cage_abs_cyc + (int64_t)(cage_slice_budget - rrm->dsp->cycles);
}

// Mark the start of a DSP slice (budget = cycles about to run).
void cage_slice_begin(int budget) {
    cage_slice_budget = budget;
}

void cage_slice_end(void) {
    cage_abs_cyc += (int64_t)(cage_slice_budget - rrm->dsp->cycles);
}

void cage_map_pages(void) {
    int i;
    for (i = 0; i < 256; i++) {
        rrm->drd[i] = rrm->dwr[i] = NULL;
    }
    if (cage.ram) {
        // Writes always hit RAM, but reads at 0-0xFFF hit the internal
        // boot ROM shadow (MAME's install_rom), so reads stay slow.
        rrm->drd[0x00] = NULL;
        rrm->dwr[0x00] = cage.ram;
    }
    if (cage.iram) {
        // 0x809800-0x809FFF: only the top 2K words of page 0x80 backing.
        // The page table maps whole 64K-word pages, so internal RAM must
        // go through the slow path (checked first in cage_drd/cage_dwr).
    }
    if (cage.boot) {
        for (i = 0; i < 8; i++)
            rrm->drd[0x40 + i] = cage.boot + (uint32_t)i * 0x10000u;
    }
    if (cage.srom) {
        for (i = 0; i < 64; i++)
            rrm->drd[0xc0 + i] = cage.srom + (uint32_t)i * 0x10000u;
    }
}

void cage_scanline_swap(void) {
    // Publish pending commands in FIFO order; the DSP drains them with
    // one latch read each, so bursts are never coalesced away.
    while (cage.pend_hd != cage.pend_tl) {
        uint8_t ntl = (uint8_t)((cage.vis_tl + 1) & 63);
        if (ntl == cage.vis_hd) break; // visible full: keep rest pending
        cage.vis[cage.vis_tl] = cage.pend[cage.pend_hd];
        cage.vis_tl = ntl;
        cage.pend_hd = (uint8_t)((cage.pend_hd + 1) & 63);
    }
    if (cage.vis_hd != cage.vis_tl) {
        cage.cpu_to_cage = 1;
        cage_control_lines();
        if (!cage.dsp_held) c31_set_irq(0, 1);
    }
}

void cage_reset(void) {
    int i;
    memset(cage.io, 0, sizeof(cage.io));
    cage.from_main = cage.control = cage.latch = 0;
    cage.cpu_to_cage = cage.cage_to_cpu = 0;
    cage.pend_hd = cage.pend_tl = cage.vis_hd = cage.vis_tl = 0;
    memset(cage.pend, 0, sizeof(cage.pend));
    memset(cage.vis, 0, sizeof(cage.vis));
    cage.dma_enabled = cage.dma_timer_on = 0;
    cage.dma_end_cyc = 0;
    cage.dma_end_addr = 0;
    cage.timer_on[0] = cage.timer_on[1] = 0;
    cage.timer_end[0] = cage.timer_end[1] = 0;
    cage.word_cyc = 0;
    cage.dac_rate = 22050;
    for (i = 0; i < (int)CAGE_DAC_CHANNELS; i++) {
        cage.dac_wr[i] = cage.dac_rd[i] = 0;
        if (!cage.dac[i])
            cage.dac[i] = (int16_t *)calloc(CAGE_DAC_FIFO,
                    sizeof(int16_t));
    }
    cage.dsp_held = 0;
    cage_abs_cyc = 0;
    cage_slice_budget = 0;
}

// ------------------------------------------------------------- DMA/timers

static void cage_update_dma(void) {
    int enabled = ((cage.io[DMA_GLOBAL_CTL] & 3) == 3) &&
            cage.io[DMA_TRANSFER_COUNT] != 0;
    if (enabled && !cage.dma_enabled) {
        uint32_t addr = cage.io[DMA_SOURCE_ADDR];
        uint32_t total = cage.io[DMA_TRANSFER_COUNT];
        uint32_t count = total & ~3u;
        uint32_t inc = (cage.io[DMA_GLOBAL_CTL] >> 4) & 1;
        uint32_t i;
        // Transfer up front (MAME does the same), tracking the end addr.
        // A trailing partial channel group is dropped, as in MAME, but
        // the timer still runs for the full count.
        if (getenv("RR_LOG_DMA")) {
            int16_t s0 = (int16_t)(cage_drd(addr) & 0xffffu);
            int16_t s1 = (int16_t)(cage_drd(addr + inc) & 0xffffu);
            fprintf(stderr, "dma src=%06X cnt=%u s0=%d s1=%d\n",
                    addr, total, s0, s1);
        }
        for (i = 0; i < count; i++) {
            int16_t s = (int16_t)(cage_drd(addr) & 0xffffu);
            uint32_t ch = i & 3;
            cage.dac[ch][cage.dac_wr[ch] % CAGE_DAC_FIFO] = s;
            cage.dac_wr[ch]++;
            addr += inc;
        }
        if (!cage.dma_timer_on) {
            cage.dma_end_cyc = dsp_now() +
                    (int64_t)(cage.word_cyc * (double)total);
            cage.dma_end_addr = addr + (total - count) * inc;
            cage.dma_timer_on = 1;
        }
    } else if (!enabled && cage.dma_enabled) {
        cage.dma_timer_on = 0;
    }
    cage.dma_enabled = enabled ? 1 : 0;
}

static void cage_update_timer(int which) {
    uint32_t base = 0x10u * (uint32_t)which;
    int enabled = ((cage.io[base + TIMER0_GLOBAL_CTL] & 0xc0) == 0xc0);
    if (enabled && !cage.timer_on[which]) {
        cage.timer_end[which] = dsp_now() +
                (int64_t)2 * (int64_t)cage.io[base + TIMER0_PERIOD];
        cage.timer_on[which] = 1;
    } else if (!enabled && cage.timer_on[which]) {
        cage.timer_on[which] = 0;
    }
}

static void cage_update_serial(void) {
    double serial_cyc = 2.0; // half the H1 frequency, in DSP cycles
    double bit_cyc, word_cyc;
    uint32_t per, nbits;
    if (cage.io[SPORT_GLOBAL_CTL] & 4) serial_cyc *= 2;
    per = cage.io[SPORT_TIMER_PERIOD] & 0xffffu;
    if (!per) {
        // MAME: with no period the bit clock is the serial clock in
        // clock mode, else never (word rate 0).
        if (!(cage.io[SPORT_GLOBAL_CTL] & 4)) {
            cage.word_cyc = 0;
            return;
        }
        bit_cyc = serial_cyc;
    } else {
        bit_cyc = serial_cyc * (double)per;
    }
    nbits = 8u * (((cage.io[SPORT_GLOBAL_CTL] >> 18) & 3) + 1);
    word_cyc = bit_cyc * (double)nbits;
    cage.word_cyc = word_cyc;
    if (word_cyc > 0) {
        // DSP cycles run at half the clock (2 clocks each, as in MAME).
        uint32_t freq = (uint32_t)(((double)CAGE_DSP_HZ / 2.0) / word_cyc /
                (double)CAGE_DAC_CHANNELS);
        if (freq > 0 && freq < 100000) cage.dac_rate = freq;
    }
}

void cage_retune(void) {
    cage_update_serial();
}

void cage_update(void) {
    c31_t *d = rrm->dsp;
    int64_t now = dsp_now();
    int fired = 0;
    if (getenv("RR_LOG_DMA") &&
            (cage.dma_timer_on || cage.io[DMA_TRANSFER_COUNT]))
        fprintf(stderr, "update now=%lld timer_on=%d end=%lld cnt=%u\n",
                (long long)now, cage.dma_timer_on,
                (long long)cage.dma_end_cyc,
                cage.io[DMA_TRANSFER_COUNT]);
    if (cage.dma_timer_on && now >= cage.dma_end_cyc) {
        cage.io[DMA_TRANSFER_COUNT] = 0;
        cage.io[DMA_SOURCE_ADDR] = cage.dma_end_addr;
        d->iff |= 1u << C31_DINT0;
        cage.dma_enabled = 0;
        cage.dma_timer_on = 0;
        fired = 1;
    }
    {
        int w;
        for (w = 0; w < 2; w++) {
            // Periodic: catch up in whole periods to hold the phase.
            while (cage.timer_on[w] && now >= cage.timer_end[w]) {
                int64_t per = (int64_t)2 *
                        (int64_t)cage.io[0x10u * (uint32_t)w +
                        TIMER0_PERIOD];
                d->iff |= 1u << (C31_TINT0 + w);
                if (per <= 0) {
                    cage.timer_on[w] = 0;
                    break;
                }
                cage.timer_end[w] += per;
                fired = 1;
            }
        }
    }
    (void)fired;
}

// ------------------------------------------------------------- DSP bus

uint32_t cage_drd(uint32_t addr) {
    addr &= 0xffffffu;
    if (addr < 0x1000 && cage.bootint) return cage.bootint[addr];
    if (addr < 0x10000 && cage.ram) return cage.ram[addr];
    if (addr >= 0x809800 && addr < 0x80a000 && cage.iram)
        return cage.iram[addr - 0x809800u];
    if (addr >= 0x808000 && addr < 0x808100) {
        // MAME reads these through a uint16_t (high bits always zero).
        uint32_t off = addr - 0x808000u, r = cage.io[off & 0xffu] & 0xffffu;
        if ((off & 0xffu) == DMA_GLOBAL_CTL)
            r = (r & ~0xcu) | (cage.dma_enabled ? 0xcu : 0u);
        return r;
    }
    if (addr == 0xa00000) {
        uint32_t v = cage.from_main;
        if (cage.vis_hd != cage.vis_tl) {
            v = cage.vis[cage.vis_hd];
            cage.vis_hd = (uint8_t)((cage.vis_hd + 1) & 63);
            cage.from_main = (uint16_t)v;
        }
        if (getenv("RR_LOG_SND"))
            fprintf(stderr, "snd dsp read %04X (dsp pc=%06X dt=%llu)\n",
                    v & 0xffffu, rrm->dsp->pc & 0xffffffu,
                    (unsigned long long)rrm->dsp->total);
        if (cage.vis_hd == cage.vis_tl) {
            cage.cpu_to_cage = 0;
            cage_control_lines();
            c31_set_irq(0, 0);
        }
        return v;
    }
    return 0;
}

void cage_dwr(uint32_t addr, uint32_t val) {
    addr &= 0xffffffu;
    if (addr >= 0x809800 && addr < 0x80a000 && cage.iram) {
        cage.iram[addr - 0x809800u] = val;
        return;
    }
    if (addr >= 0x808000 && addr < 0x808100) {
        uint32_t off = addr - 0x808000u;
        cage.io[off & 0xffu] = val;
        switch (off & 0xffu) {
        case DMA_GLOBAL_CTL:
        case DMA_SOURCE_ADDR:
        case DMA_DEST_ADDR:
        case DMA_TRANSFER_COUNT:
            if (getenv("RR_LOG_DMA") && (off & 0xffu) == DMA_TRANSFER_COUNT)
                fprintf(stderr, "dmawr cnt=%u ctl=%04X (dsp pc=%06X)\n",
                        val & 0xffffu, cage.io[DMA_GLOBAL_CTL] & 0xffffu,
                        rrm->dsp->pc & 0xffffffu);
            cage_update_dma();
            break;
        case TIMER0_GLOBAL_CTL:
        case TIMER0_COUNTER:
        case TIMER0_PERIOD:
            cage_update_timer(0);
            break;
        case TIMER1_GLOBAL_CTL:
        case TIMER1_COUNTER:
        case TIMER1_PERIOD:
            cage_update_timer(1);
            break;
        case SPORT_DATA_TX:
            if (cage.word_cyc > 0 &&
                    ((double)CAGE_DSP_HZ / 2.0) / cage.word_cyc ==
                    (double)(22050u * 4) &&
                    (cage.io[SPORT_RX_CTL] & 0xffu) == 0x62u)
                cage.io[SPORT_RX_CTL] ^= 0x800u;
            break;
        case SPORT_GLOBAL_CTL:
        case SPORT_TIMER_CTL:
        case SPORT_TIMER_PERIOD:
            cage_update_serial();
            break;
        default:
            break;
        }
        return;
    }
    if (addr == 0xa00000) {
        if (getenv("RR_LOG_SND"))
            fprintf(stderr, "snd dsp->main %04X (dsp pc=%06X)\n",
                    val & 0xffffu, rrm->dsp->pc & 0xffffffu);
        cage.latch = (uint16_t)(val & 0xffffu);
        cage.cage_to_cpu = 1;
        cage_control_lines();
        return;
    }
    if (addr == 0x200000) return; // nopw in MAME's map
}

// ------------------------------------------------------------- host side

static void cage_control_lines(void) {
    c31_t *d = rrm->dsp;
    uint32_t iof = d->iof & ~0x88u;
    if (cage.cpu_to_cage) iof |= 0x08u;
    if (cage.cage_to_cpu) iof |= 0x80u;
    d->iof = iof;
    // The IRQ3 reason is read separately (cage_irq_reason).
}

int cage_irq_reason(void) {
    int reason = 0;
    if ((cage.control & 3) == 3 && !cage.cpu_to_cage)
        reason |= 2;
    if ((cage.control & 2) && cage.cage_to_cpu)
        reason |= 1;
    return reason;
}

uint16_t cage_main_r(void) {
    uint16_t v = cage.latch;
    if (getenv("RR_LOG_SND") && cage.cage_to_cpu)
        fprintf(stderr, "snd main read %04X (68k pc=%06X)\n",
                v, rrm->cpu->pc & 0xffffffu);
    cage.cage_to_cpu = 0;
    cage_control_lines();
    return v;
}

void cage_main_w(uint16_t data) {
    uint8_t ntl;
    if (getenv("RR_LOG_SND"))
        fprintf(stderr, "snd main->dsp %04X (68k pc=%06X dt=%llu)\n",
                data, rrm->cpu->pc & 0xffffffu,
                (unsigned long long)rrm->dsp->total);
    ntl = (uint8_t)((cage.pend_tl + 1) & 63);
    if (ntl == cage.pend_hd) {
        // Pending overflow: drop the oldest (should never happen; the
        // 68K handshakes every command).
        cage.pend_hd = (uint8_t)((cage.pend_hd + 1) & 63);
    }
    cage.pend[cage.pend_tl] = data;
    cage.pend_tl = ntl;
}

uint16_t cage_control_r(void) {
    uint16_t r = 0;
    if (cage.cpu_to_cage) r |= 2;
    if (cage.cage_to_cpu) r |= 1;
    return r;
}

void cage_control_w(uint16_t data) {
    if (getenv("RR_LOG_SND"))
        fprintf(stderr, "snd control_w %04X (68k pc=%06X)\n",
                data, rrm->cpu->pc & 0xffffffu);
    cage.control = data;
    if (!(cage.control & 3)) {
        c31_set_irq(0, 0);
        c31_set_irq(1, 1);
        c31_reset();
        cage.dma_enabled = 0;
        cage.dma_timer_on = 0;
        cage.timer_on[0] = cage.timer_on[1] = 0;
        memset(cage.io, 0, 0x60u * sizeof(uint32_t));
        cage.cpu_to_cage = 0;
        cage.cage_to_cpu = 0;
        cage.pend_hd = cage.pend_tl = cage.vis_hd = cage.vis_tl = 0;
        cage.dsp_held = 1;
    } else {
        cage.dsp_held = 0;
        c31_set_irq(1, 0);
    }
    cage_control_lines();
}

void cage_reset_w(int state) {
    if (state) cage_control_w(0);
    cage.dsp_held = state ? 1 : 0;
}

// ------------------------------------------------------------- DAC mixing

uint32_t cage_queued(void) {
    uint32_t n0 = cage.dac_wr[0] - cage.dac_rd[0];
    uint32_t cap = CAGE_DAC_FIFO - 1;
    return (n0 < cap) ? n0 : cap;
}

uint32_t cage_mix(int16_t *dst_l, int16_t *dst_r, uint32_t frames) {
    uint32_t i, avail = cage_queued();
    uint32_t n = (frames < avail) ? frames : avail;
    for (i = 0; i < n; i++) {
        int32_t c0 = cage.dac[0][cage.dac_rd[0] % CAGE_DAC_FIFO];
        int32_t c1 = cage.dac[1][cage.dac_rd[1] % CAGE_DAC_FIFO];
        int32_t c2 = cage.dac[2][cage.dac_rd[2] % CAGE_DAC_FIFO];
        int32_t c3 = cage.dac[3][cage.dac_rd[3] % CAGE_DAC_FIFO];
        int32_t l = (c1 + c2) / 2, r = (c0 + c3) / 2;
        if (l > 32767) l = 32767;
        if (l < -32768) l = -32768;
        if (r > 32767) r = 32767;
        if (r < -32768) r = -32768;
        dst_l[i] = (int16_t)l;
        dst_r[i] = (int16_t)r;
        cage.dac_rd[0]++;
        cage.dac_rd[1]++;
        cage.dac_rd[2]++;
        cage.dac_rd[3]++;
    }
    for (; i < frames; i++) {
        dst_l[i] = dst_r[i] = 0;
    }
    return n;
}
