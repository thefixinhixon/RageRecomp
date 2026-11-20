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

    // P1_P2 word: byte 0 = P1, byte 2 = P2, byte 1 unconnected (1s),
    // byte 3 the Jan 1995 set's button 4 (released here).
    mach.in.b0 = 0xff;
    mach.in.b1 = 0xfe; // P2 start held (active low bit0)
    mach.in.b3 = 0xff; // P1 released
    {
        uint32_t w = ((uint32_t)mach.in.b3 << 24) | 0x00ff0000u |
                ((uint32_t)mach.in.b1 << 8) | 0xffu;
        T_EQ_U32(mem_mrd32(0xe80000u), w, "P1_P2 word layout");
        T_EQ_U32(mem_mrd8(0xe80000u), 0xffu, "P1_P2 byte 0 is P1");
        T_EQ_U32(mem_mrd8(0xe80001u), 0xffu, "P1_P2 byte 1 reads 1s");
        T_EQ_U32(mem_mrd8(0xe80002u), 0xfeu, "P1_P2 byte 2 is P2");
        T_EQ_U32(mem_mrd8(0xe80003u), 0xffu, "P1_P2 byte 3 idle 1s");
        mach.in.b0 = 0xfd; // P1 button 4 (Jan 1995 layout)
        T_EQ_U32(mem_mrd8(0xe80003u), 0xfdu, "P1_P2 byte 3 holds button 4");
        mach.in.b0 = 0xff;
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
        eeprom_factory();
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
        eeprom_reset();     // power cycle: the cabinet keeps its settings
        T_EQ_U32(eeprom_read(3), 0x42u, "power-on reset keeps the contents");
        // On the bus the chip's bytes sit at even addresses (MAME's
        // umask32(0xff00ff00)): byte n at 0xD20000 + 2n. The game's driver
        // reads and writes those; odd addresses are not the chip.
        T_EQ_U32(rr_mrd8(0xd20006u), 0x42u, "EEPROM byte 3 at an even address");
        T_EQ_U32(rr_mrd8(0xd20007u), 0u, "odd address is not the chip");
        T_EQ_U32(rr_mrd16(0xd20006u, 0), 0x4200u, "16-bit read: chip byte in the high half");
        T_EQ_U32(rr_mrd32(0xd20004u, 0), 0xff004200u, "32-bit read: bytes 2 and 3");
        rr_mwr8(0xd40000u, 0);
        rr_mwr8(0xd20008u, 0x5a);
        T_EQ_U32(eeprom_read(4), 0x5au, "byte write at an even address reaches the chip");
        rr_mwr8(0xd40000u, 0);
        rr_mwr8(0xd2000bu, 0x77);
        T_EQ_U32(eeprom.data[5], 0xffu, "byte write at an odd address does not");
        rr_mwr8(0xd40000u, 0);
        rr_mwr16(0xd2000au, 0x6600, 0);
        T_EQ_U32(eeprom_read(5), 0x66u, "16-bit write stores its high byte");
    }

    // Unmapped holes read zero and ignore writes.
    T_EQ_U32(rr_mrd16(0x80b360u, 0), 0u, "hole reads zero");
    T_EQ_U32(mem_mrd32(0xc00004u), 0u, "gap reads zero");

    // 020: misaligned word/long data accesses are legal (the fight
    // entry code copies structs with odd move.w EAs); only odd
    // instruction fetches raise address error.
    t_w32(0xff1240u, 0x01020304u);
    T_EQ_U32(rr_mrd16(0xff1241u, 0), 0x0203u, "odd word read composes");
    T_EQ_U32(rr_mrd32(0xff1241u, 0), 0x02030400u, "odd long read composes");
    T_EQ_U32(c->exc, 0u, "odd reads raise no exception");
    rr_mwr16(0xff1241u, 0xa5b6, 0);
    T_EQ_U32(t_r32(0xff1240u), 0x01a5b604u, "odd word write stores");
    rr_mwr32(0xff1241u, 0x11223344u, 0);
    T_EQ_U32(t_r32(0xff1240u), 0x01112233u, "odd long write head");
    T_EQ_U32(rr_mrd8(0xff1244u), 0x44u, "odd long write tail");
    T_EQ_U32(c->exc, 0u, "odd writes raise no exception");

    return test_finish();
}
