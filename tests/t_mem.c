// Memory map and I/O dispatch: ROM/RAM/bank windows, input ports,
// coin lines idle high, EEPROM factory default. Regression tests for
// the coin-half base (was 0x3F, asserting phantom coins) and the
// EEPROM default (must be 0xFF like MAME's er2055).
#include "support.h"

int main(void) {
    test_init();
    m68k_t *c = rrm->cpu;
    (void)c;

    // Main RAM round-trips through the fast pages.
    t_w32(0xff1234u, 0xdeadbeefu);
    T_EQ_U32(t_r32(0xff1234u), 0xdeadbeefu, "RAM round trip");
    T_EQ_U32(rr_mrd16(0xff1234u, 0), 0xdeadu, "RAM high half");
    T_EQ_U32(rr_mrd8(0xff1237u), 0xefu, "RAM low byte");

    // Stack area in high RAM (where the game keeps SSP).
    t_w32(0xffff92u, 0x59c2u);
    T_EQ_U32(t_r32(0xffff92u), 0x59c2u, "high RAM round trip");

    // Banked work RAM is a slow path (MO command side effects).
    rr_mwr16(0xd71000u, 0x1234, 0);
    T_EQ_U32(rr_mrd16(0xd71000u, 0), 0x1234u, "bank RAM round trip");
    rr_mwr8(0xd71002u, 0x5a);
    T_EQ_U32(rr_mrd8(0xd71002u), 0x5au, "bank byte round trip");

    // Playfield RAM window.
    rr_mwr16(0xd72000u, 0xabcd, 0);
    T_EQ_U32(rr_mrd16(0xd72000u, 0), 0xabcdu, "playfield RAM round trip");

    // Colour RAM window (16-bit cells).
    gt_colorram_w(0xd80000u, 0x7fff);
    T_EQ_U32(gt_colorram_r(0xd80000u), 0x7fffu, "colour RAM round trip");
    T_EQ_U32(rr_mrd16(0xd80000u, 0), 0x7fffu, "colour RAM via map");

    // P1_P2 word: byte 0 = P1, byte 2 = P2, bytes 1/3 unconnected (1s).
    mach.in.b1 = 0xfe; // P2 start held (active low bit0)
    mach.in.b3 = 0xff; // P1 released
    {
        uint32_t w = ((uint32_t)mach.in.b3 << 24) | 0x00ff0000u |
                ((uint32_t)mach.in.b1 << 8) | 0xffu;
        T_EQ_U32(mem_mrd32(0xe80000u), w, "P1_P2 word layout");
        T_EQ_U32(mem_mrd8(0xe80000u), 0xffu, "P1_P2 byte 0 is P1");
        T_EQ_U32(mem_mrd8(0xe80001u), 0xffu, "P1_P2 byte 1 reads 1s");
        T_EQ_U32(mem_mrd8(0xe80002u), 0xfeu, "P1_P2 byte 2 is P2");
        T_EQ_U32(mem_mrd8(0xe80003u), 0xffu, "P1_P2 byte 3 reads 1s");
    }
    mach.in.b1 = 0xff;
    mach.in.b3 = 0xfe; // P1 start held
    T_EQ_U32(mem_mrd8(0xe80000u), 0xfeu, "P1 start bit visible");
    mach.in.b3 = 0xff;

    // Coin/service halves idle high; coin pulls its bit low.
    {
        uint16_t h = (uint16_t)mem_mrd16(0xe82004u);
        T_OK((h & 0x00c0u) == 0x00c0u, "coin lines idle high (no phantom coin)");
        mach.in.coin1 = 1;
        h = (uint16_t)mem_mrd16(0xe82004u);
        T_OK(!(h & 0x0080u), "coin1 pulls bit7 low");
        T_OK((h & 0x0040u), "coin2 stays high");
        mach.in.coin1 = 0;
        mach.in.coin2 = 1;
        h = (uint16_t)mem_mrd16(0xe82004u);
        T_OK(!(h & 0x0040u), "coin2 pulls bit6 low");
        mach.in.coin2 = 0;
    }
    {
        uint16_t h = (uint16_t)mem_mrd16(0xe82000u);
        T_OK(!(h & 0x0001u), "A2DRDY reads low without ADC");
        T_OK((h & 0x0040u), "self-test reads high when open");
        mach.in.service = 1;
        h = (uint16_t)mem_mrd16(0xe82000u);
        T_OK(!(h & 0x0040u), "self-test pulls bit6 low");
        mach.in.service = 0;
    }

    // EEPROM factory default is all 0xFF (MAME er2055 nvram_default).
    {
        int i, ok = 1;
        eeprom_reset();
        for (i = 0; i < 64; i++)
            if (eeprom_read((uint32_t)i) != 0xffu) ok = 0;
        T_OK(ok, "EEPROM defaults to 0xFF");
        eeprom_write(3, 0x42);
        T_EQ_U32(eeprom_read(3), 0xffu, "locked EEPROM ignores writes");
        eeprom_unlock();
        eeprom_write(3, 0x42);
        T_EQ_U32(eeprom_read(3), 0x42u, "unlocked EEPROM writes");
        eeprom_write(4, 0x11);
        T_EQ_U32(eeprom_read(4), 0xffu, "write relocks the chip");
    }

    // Unmapped holes read zero and ignore writes.
    T_EQ_U32(rr_mrd16(0x80b360u, 0), 0u, "hole reads zero");
    T_EQ_U32(mem_mrd32(0xc00004u), 0u, "gap reads zero");

    return test_finish();
}
