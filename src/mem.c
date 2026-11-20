// 68EC020 memory map and I/O dispatch. See mem.h and MAME's main_map.
#include "mem.h"
#include "m68k.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Provided by gtvid.c (colour RAM + video registers).
uint16_t gt_colorram_r(uint32_t addr);
void gt_colorram_w(uint32_t addr, uint16_t val);
void gt_mo_command(uint16_t cmd);
void gt_mo_control(uint8_t bits);



void mem_map_pages(void) {
    int i;
    for (i = 0; i < 256; i++) {
        rrm->mrd[i] = rrm->mwr[i] = NULL;
    }
    if (mach.rom) {
        for (i = 0; i < 32; i++)
            rrm->mrd[i] = mach.rom + (uint32_t)i * 0x10000u;
    }
    if (mach.ram) {
        for (i = 0; i < 8; i++) {
            rrm->mrd[0xf8 + i] = mach.ram + (uint32_t)i * 0x10000u;
            rrm->mwr[0xf8 + i] = mach.ram + (uint32_t)i * 0x10000u;
        }
    }
    // 0xD7xxxx (work RAM + MO command) stays slow so the MO command
    // write triggers rendering; main RAM holds the hot data (stack).
}

void mem_update_irq(void) {
    uint8_t irq = 0;
    if (mach.irq3) irq = 3;
    if (mach.irq4) irq = 4;
    if (mach.irq6) irq = 6;
    rrm->cpu->irq = irq;
}

void mem_set_scanline(int state) {
    mach.irq6 = state ? 1 : 0;
    mem_update_irq();
}

void mem_set_video(int state) {
    mach.irq4 = state ? 1 : 0;
    mem_update_irq();
}

// ------------------------------------------------------------- inputs

static uint32_t p1p2_word(void) {
    // Byte 2 is unconnected (reads 1s); bytes 3/1 are P1/P2, byte 0 the
    // Jan 1995 set's button 4.
    return ((uint32_t)mach.in.b3 << 24) | 0x00ff0000u |
            ((uint32_t)mach.in.b1 << 8) | mach.in.b0;
}

static uint16_t service_half(void) {
    uint16_t t = 0xff00u | 0x00feu; // unused high, bits 1-7 high
    if (mach.in.service & 1) t &= ~0x0040u; // self-test button, active low
    if (mach.in.tilt & 1) t &= ~0x0002u; // tilt, active low
    if (mach.vblank) t |= 0x0080u; // VBLANK is active high
    else t &= ~0x0080u;
    t &= ~0x0001u; // /A2DRDY reads low without the ADC board
    return t;
}

static uint16_t coin_half(void) {
    // All lines idle high (active low); MAME XORs the two IRQ status bits.
    uint16_t t = 0xffffu;
    if (mach.in.coin2 & 1) t &= ~0x0040u;
    if (mach.in.coin1 & 1) t &= ~0x0080u;
    if (mach.irq4) t ^= 0x0001u; // /VBIRQ follows the latch state
    if (mach.irq6) t ^= 0x0002u; // /4MSIRQ follows the latch state
    return t;
}

// ------------------------------------------------------------- slow paths

static uint8_t bank_rb(uint32_t addr) {
    if (!mach.bank) return 0;
    return mach.bank[addr - 0xd70000u];
}

static void bank_wb(uint32_t addr, uint8_t val) {
    uint32_t off;
    if (!mach.bank) return;
    off = addr - 0xd70000u;
    mach.bank[off] = val;
    // MO command at 0xD7A200 (low half write triggers).
    if ((addr & ~1u) == 0xd7a202u) {
        uint16_t cmd = (uint16_t)(mach.bank[0xa202u] << 8) |
                mach.bank[0xa203u];
        gt_mo_command(cmd);
    }
}

static uint16_t bank_rw(uint32_t addr) {
    if (!mach.bank) return 0;
    addr -= 0xd70000u;
    return (uint16_t)(mach.bank[addr] << 8) | mach.bank[addr + 1];
}

static void bank_ww(uint32_t addr, uint16_t val) {
    uint32_t off;
    if (!mach.bank) return;
    off = addr - 0xd70000u;
    mach.bank[off] = (uint8_t)(val >> 8);
    mach.bank[off + 1] = (uint8_t)val;
    if (addr == 0xd7a202u) gt_mo_command(val);
}

uint32_t mem_mrd8(uint32_t addr) {
    addr &= 0xffffffu;
    if (addr < 0x200000u && mach.rom) return mach.rom[addr];
    if (addr >= 0xc00000u && addr < 0xc00004u) {
        // Gate main_r (it clears the latch) to high-half accesses.
        if ((addr & 2) == 0) {
            uint16_t main = cage_main_r();
            return (addr & 1) ? (main & 0xffu) : (main >> 8);
        } else {
            uint16_t ctl = cage_control_r();
            return (addr & 1) ? (ctl & 0xffu) : (ctl >> 8);
        }
    }
    if (addr >= 0xd00010u && addr < 0xd00020u)
        return (addr & 1) ? 0xffu : 0u; // no ADC board: odd bytes read 1s
    if (addr >= 0xd20000u && addr < 0xd21000u)
        return (addr & 1) ? eeprom_read((addr - 0xd20000u) >> 1) : 0u;
    if (addr >= 0xd70000u && addr < 0xd80000u) return bank_rb(addr);
    if (addr >= 0xd80000u && addr < 0xe00000u) {
        uint16_t h = gt_colorram_r(addr & ~1u), d = h;
        if (xga_read16(addr - 0xd80000u, &d)) h = d;
        return (addr & 1) ? (h & 0xffu) : (h >> 8);
    }
    if (addr >= 0xe80000u && addr < 0xe80004u) {
        uint32_t w = p1p2_word();
        return (w >> ((3 - (addr & 3)) * 8)) & 0xffu;
    }
    if (addr >= 0xe82000u && addr < 0xe82004u) {
        uint16_t h = service_half();
        return (addr & 1) ? (h & 0xffu) : (h >> 8);
    }
    if (addr >= 0xe82004u && addr < 0xe82008u) {
        uint16_t h = coin_half();
        return (addr & 1) ? (h & 0xffu) : (h >> 8);
    }
    return 0;
}

uint32_t mem_mrd16(uint32_t addr) {
    addr &= 0xffffffu;
    if (addr < 0x200000u && mach.rom)
        return ((uint32_t)mach.rom[addr] << 8) | mach.rom[addr + 1];
    if (addr == 0xc00000u) return cage_main_r();
    if (addr == 0xc00002u) return cage_control_r();
    if (addr >= 0xd00010u && addr < 0xd00020u)
        return (addr & 1) ? 0u : 0x00ffu;
    if (addr >= 0xd20000u && addr < 0xd21000u)
        return (addr & 1) ? 0u : eeprom_read((addr - 0xd20000u) >> 1);
    if (addr >= 0xd70000u && addr < 0xd80000u) return bank_rw(addr);
    if (addr >= 0xd80000u && addr < 0xe00000u) {
        uint16_t h = gt_colorram_r(addr), d = h;
        if (xga_read16(addr - 0xd80000u, &d)) h = d;
        return h;
    }
    if (addr >= 0xe80000u && addr < 0xe80004u) {
        uint32_t w = p1p2_word();
        return (addr & 2) ? (w & 0xffffu) : (w >> 16);
    }
    if (addr >= 0xe82000u && addr < 0xe82004u) return service_half();
    if (addr >= 0xe82004u && addr < 0xe82008u) return coin_half();
    return 0;
}

uint32_t mem_mrd32(uint32_t addr) {
    addr &= 0xffffffu;
    if (addr + 3 < 0x200000u && mach.rom)
        return ((uint32_t)mach.rom[addr] << 24) |
                ((uint32_t)mach.rom[addr + 1] << 16) |
                ((uint32_t)mach.rom[addr + 2] << 8) | mach.rom[addr + 3];
    if (addr == 0xc00000u)
        return ((uint32_t)cage_main_r() << 16) | cage_control_r();
    if (addr >= 0xd00010u && addr + 3 < 0xd00020u) return 0x00ff00ffu;
    if (addr >= 0xd20000u && addr + 3 < 0xd21000u) {
        uint32_t b1 = eeprom_read((addr + 1 - 0xd20000u) >> 1);
        uint32_t b3 = eeprom_read((addr + 3 - 0xd20000u) >> 1);
        return (b1 << 16) | b3;
    }
    if (addr >= 0xd70000u && addr + 3 < 0xd80000u)
        return ((uint32_t)bank_rw(addr) << 16) | bank_rw(addr + 2);
    if (addr >= 0xd80000u && addr + 3 < 0xe00000u) {
        uint16_t hi = gt_colorram_r(addr), d = hi;
        uint16_t lo = gt_colorram_r(addr + 2), e = lo;
        if (xga_read16(addr - 0xd80000u, &d)) hi = d;
        if (xga_read16(addr + 2 - 0xd80000u, &e)) lo = e;
        return ((uint32_t)hi << 16) | lo;
    }
    if (addr == 0xe80000u) return p1p2_word();
    if (addr == 0xe82000u) {
        uint16_t h = service_half();
        return ((uint32_t)h << 16) | h;
    }
    if (addr == 0xe82004u) {
        uint16_t h = coin_half();
        return ((uint32_t)h << 16) | h;
    }
    return 0;
}

static void latch_w(uint32_t addr, uint32_t val, int bytes) {
    // 0xE08000: D13-D11 MO control (high byte), D5 /XRESET, D4 unused
    // here (primrage ties it high), D3/D0 coin counters.
    (void)bytes;
    if (addr == 0xe08000u) {
        gt_mo_control((uint8_t)((val >> 3) & 7));
    } else if (addr == 0xe08001u) {
        uint8_t b = (uint8_t)val;
        cage_reset_w(((~b) >> 5) & 1);
        if (b & 8) mach.coin_counters |= 2;
        else mach.coin_counters &= ~2;
        if (b & 1) mach.coin_counters |= 1;
        else mach.coin_counters &= ~1;
    }
}

void mem_mwr8(uint32_t addr, uint32_t val) {
    addr &= 0xffffffu;
    val &= 0xffu;
    if (addr >= 0xc00000u && addr < 0xc00004u) {
        // MAME calls the half handlers with the bus value (other lanes 0).
        if (addr == 0xc00000u) cage_main_w((uint16_t)(val << 8));
        else if (addr == 0xc00001u) cage_main_w((uint16_t)val);
        else if (addr == 0xc00002u) cage_control_w((uint16_t)(val << 8));
        else cage_control_w((uint16_t)val);
        return;
    }
    if (addr >= 0xd20000u && addr < 0xd21000u) {
        if (addr & 1) eeprom_write((addr - 0xd20000u) >> 1, (uint8_t)val);
        return;
    }
    if (addr >= 0xd40000u && addr < 0xd50000u) {
        eeprom_unlock();
        return;
    }
    if (addr >= 0xd70000u && addr < 0xd80000u) {
        bank_wb(addr, (uint8_t)val);
        return;
    }
    if (addr >= 0xd80000u && addr < 0xe00000u) {
        uint32_t half = addr & ~1u;
        uint16_t h = gt_colorram_r(half);
        if (addr & 1) h = (h & 0xff00u) | (uint8_t)val;
        else h = (h & 0x00ffu) | (uint16_t)(val << 8);
        gt_colorram_w(half, h);
        xga_write16(half - 0xd80000u, h);
        return;
    }
    if (addr >= 0xe08000u && addr < 0xe08002u) {
        latch_w(addr, val, 1);
        return;
    }
    if (addr >= 0xe0a000u && addr < 0xe0a004u) {
        mem_set_scanline(0);
        return;
    }
    if (addr >= 0xe0c000u && addr < 0xe0c004u) {
        mem_set_video(0);
        return;
    }
}

void mem_mwr16(uint32_t addr, uint32_t val) {
    addr &= 0xffffffu;
    val &= 0xffffu;
    if (addr == 0xc00000u) {
        cage_main_w((uint16_t)val);
        return;
    }
    if (addr == 0xc00002u) {
        cage_control_w((uint16_t)val);
        return;
    }
    if (addr >= 0xd20000u && addr < 0xd21000u) {
        // A 16-bit write lands its low byte on the odd lane.
        if ((addr & 1) == 0)
            eeprom_write((addr - 0xd20000u) >> 1, (uint8_t)val);
        return;
    }
    if (addr >= 0xd40000u && addr < 0xd50000u) {
        eeprom_unlock();
        return;
    }
    if (addr >= 0xd70000u && addr < 0xd80000u) {
        bank_ww(addr, (uint16_t)val);
        return;
    }
    if (addr >= 0xd80000u && addr < 0xe00000u) {
        gt_colorram_w(addr, (uint16_t)val);
        xga_write16(addr - 0xd80000u, (uint16_t)val);
        return;
    }
    if (addr == 0xe08000u) {
        latch_w(0xe08000u, val >> 8, 1);
        latch_w(0xe08001u, val, 1);
        return;
    }
    if (addr >= 0xe0a000u && addr < 0xe0a004u) {
        mem_set_scanline(0);
        return;
    }
    if (addr >= 0xe0c000u && addr < 0xe0c004u) {
        mem_set_video(0);
        return;
    }
}

void mem_mwr32(uint32_t addr, uint32_t val) {
    addr &= 0xffffffu;
    if (addr == 0xc00000u) {
        cage_control_w((uint16_t)val);
        cage_main_w((uint16_t)(val >> 16));
        return;
    }
    if (addr >= 0xd40000u && addr < 0xd50000u) {
        eeprom_unlock();
        return;
    }
    if (addr >= 0xd20000u && addr + 3 < 0xd21000u) {
        eeprom_write((addr + 1 - 0xd20000u) >> 1, (uint8_t)(val >> 16));
        eeprom_write((addr + 3 - 0xd20000u) >> 1, (uint8_t)val);
        return;
    }
    if (addr >= 0xd70000u && addr + 3 < 0xd80000u) {
        bank_ww(addr, (uint16_t)(val >> 16));
        bank_ww(addr + 2, (uint16_t)val);
        return;
    }
    if (addr >= 0xd80000u && addr + 3 < 0xe00000u) {
        gt_colorram_w(addr, (uint16_t)(val >> 16));
        xga_write16(addr - 0xd80000u, (uint16_t)(val >> 16));
        gt_colorram_w(addr + 2, (uint16_t)val);
        xga_write16(addr + 2 - 0xd80000u, (uint16_t)val);
        return;
    }
    if (addr == 0xe08000u) {
        latch_w(0xe08000u, val >> 24, 1);
        latch_w(0xe08001u, val >> 16, 1);
        return;
    }
    if (addr == 0xe0a000u) {
        mem_set_scanline(0);
        return;
    }
    if (addr == 0xe0c000u) {
        mem_set_video(0);
        return;
    }
    // 0xE04000 LED, 0xE0E000 watchdog, ROM: no-ops.
}

void mem_mtrap(uint32_t vec, uint32_t pc) {
    m68k_t *c = rrm->cpu;
    c->exc = (uint8_t)vec;
    c->exc_pc = pc;
    m68k_service();
}

void mem_mirq(void) {
    m68k_service();
}

int mem_poll(void) {
    return 0;
}
