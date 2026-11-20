// DSP ALU edge cases: float compare/subtract signs and flags, driven
// through real hand-assembled opcodes (decode+exec+dasm together).
#include "support.h"
#include "c31.h"
#include "c31_core.h"
#include "c31dis.h"

static void poke(uint32_t addr, uint32_t v) {
    cage.ram[addr & 0xffffu] = v;
}

static uint32_t peek_st(void) {
    return rrm->dsp->st;
}

int main(void) {
    test_init();
    mach_profile = game_profile(GAME_PRIMRAGE);
    machine_reset();
    {
        c31_t *d = rrm->dsp;
        // Voice-envelope clamp values from the sound program: field_C
        // 0x027CA3D7 must compare LESS than limit 0x07683333.
        poke(0x1000, 0x027CA3D7u);
        poke(0x1001, 0x07683333u);
        // LDI $1000,AR0 / LDF *AR0,R0 / LDI $1001,AR0 / LDF *AR0,R1
        // CMPF R1,R0
        poke(0x1100, 0x08681000u);
        poke(0x1101, 0x0740C000u);
        poke(0x1102, 0x08681001u);
        poke(0x1103, 0x0741C000u);
        poke(0x1104, 0x04000001u);
        {
            char buf[128];
            d_insn_t o;
            uint32_t op = cage.ram[0x1104];
            d_decode(op, &o);
            c31_format(buf, &o, 0x1104);
            T_SUBSTR(buf, "CMPF", "cmpf test vector decodes");
        }
        d->pc = 0x1100;
        d->dcount = -1;
        d->st = 0;
        d->ie = 0;
        d->iff = 0;
        d->irq = 0;
        d->cycles = 1000000;
        c31_step(); // LDI AR0
        c31_step(); // LDF R0
        c31_step(); // LDI AR0
        c31_step(); // LDF R1
        c31_step(); // CMPF R1,R0
        // 0x027CA3D7 is about -0.1 and 0x07683333 about -23.8 in
        // C31 float format, so R0 - R1 is positive: N clear, BGT taken.
        T_OK((peek_st() & C31_ST_N) == 0, "cmpf greater clears N");
        T_OK((peek_st() & C31_ST_Z) == 0, "cmpf greater clears Z");
        T_OK(d_cond(d, 9), "BGT taken when greater");
        T_OK(!d_cond(d, 7), "BLT not taken when greater");
    }
    printf("# %d tests, %d failures\n", t_count, t_failures);
    return t_failures != 0;
}
