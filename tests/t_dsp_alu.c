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
        // 0x027CA3D7 is about +3.9 and 0x07683333 about +104.2 in
        // C31 float format, so R0 - R1 is negative: N set, BLT taken.
        // (This vector once pinned a sign-extension bug in d_subf's
        // exponent-alignment bias that cleared N here; MAME sets N.)
        T_OK((peek_st() & C31_ST_N) != 0, "cmpf less sets N");
        T_OK((peek_st() & C31_ST_Z) == 0, "cmpf less clears Z");
        T_OK(!d_cond(d, 9), "BGT not taken when less");
        T_OK(d_cond(d, 7), "BLT taken when less");
    }
    {
        // Crash vector from the sound program's voice mix entry:
        // CMPF R1=4.0, R4=10240.0 must set N (less) so the BGT at
        // $203B skips the stale-RS loop; clearing N wedges the DSP.
        c31_t *d = rrm->dsp;
        poke(0x1000, 0x09010000u); // 4.0
        poke(0x1001, 0x0F280000u); // 10240.0
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
        T_OK((peek_st() & C31_ST_N) != 0, "mix-entry cmpf sets N");
        T_OK(!d_cond(d, 9), "mix-entry BGT not taken");
    }
    {
        // FIX accepts any destination register (MAME's fix_reg
        // decodes a 5-bit dreg). The voice mixer relies on
        // FIX R4,RS ($20B5) bounding its loop and FIX R4,AR4
        // ($2090) deriving the sample pointer; masking the
        // destination to R0-R7 leaves RS/AR4 stale and clobbers
        // an R register, which wedges the DSP in a wild mix.
        c31_t *d = rrm->dsp;
        poke(0x1002, 0x0C2A0000u); // 5440.0
        // LDI $1002,AR0 / LDF *AR0,R4 / FIX R4,RS / FIX R4,AR4
        poke(0x1110, 0x08681002u);
        poke(0x1111, 0x0744C000u);
        poke(0x1112, 0x05190004u);
        poke(0x1113, 0x050C0004u);
        {
            char buf[128];
            d_insn_t o;
            d_decode(cage.ram[0x1112], &o);
            c31_format(buf, &o, 0x1112);
            T_SUBSTR(buf, "FIX", "fix test vector decodes");
            T_OK(o.dreg == 25, "fix decodes 5-bit RS dest");
        }
        d->pc = 0x1110;
        d->dcount = -1;
        d->st = 0;
        d->ie = 0;
        d->iff = 0;
        d->irq = 0;
        d->cycles = 1000000;
        d->r[1].man = 0x12345678;
        d->r[1].exp = 5;
        d->rs = 0xabcdefu;
        d->ar[4] = 0xdeadbeefu;
        c31_step(); // LDI AR0
        c31_step(); // LDF R4
        c31_step(); // FIX R4,RS
        T_OK(d->rs == 0x1540u, "fix to RS sets RS");
        T_OK(d->r[1].man == 0x12345678 && d->r[1].exp == 5,
                "fix to RS leaves R1 alone");
        T_OK(d->r[4].man == (int32_t)0x2A000000u,
                "fix source float intact");
        T_OK((peek_st() & (C31_ST_N | C31_ST_Z)) == 0,
                "fix sets N/Z for positive result");
        c31_step(); // FIX R4,AR4
        T_OK(d->ar[4] == 0x1540u, "fix to AR4 sets AR4");
        T_OK(d->r[4].man == (int32_t)0x2A000000u,
                "fix to AR4 leaves R4 float alone");
    }
    {
        // The music sequencer's wait test ($2833): CMPI 0,R0 / LDIU 0,R0 /
        // LDIGE 1,R0. Conditional loads leave the flags alone, so with R0
        // negative the LDIGE is not taken and R0 ends 0 (next event due).
        // When LDIU set Z, R0 became 1 and the music never advanced.
        c31_t *d = rrm->dsp;
        poke(0x1120, 0x04E00000u);  // CMPI  $0000,R0
        poke(0x1121, 0x50600000u);  // LDIU  $0000,R0
        poke(0x1122, 0x55600001u);  // LDIGE $0001,R0
        poke(0x1123, 0x40600000u);  // LDFU  $0000,R0
        d->pc = 0x1120;
        d->dcount = -1;
        d->st = 0;
        d->ie = 0;
        d->iff = 0;
        d->irq = 0;
        d->cycles = 1000000;
        d->r[0].man = -5;
        d->r[0].exp = 0;
        c31_step();
        T_OK((peek_st() & C31_ST_N) != 0, "cmpi negative sets N");
        c31_step();
        T_OK(d->r[0].man == 0, "ldiu loads");
        T_OK((peek_st() & (C31_ST_N | C31_ST_Z)) == C31_ST_N,
                "ldiu leaves the flags alone");
        c31_step();
        T_OK(d->r[0].man == 0, "ldige not taken after a negative compare");
        c31_step();
        T_OK((peek_st() & (C31_ST_N | C31_ST_Z)) == C31_ST_N,
                "ldfu leaves the flags alone");
    }
    printf("# %d tests, %d failures\n", t_count, t_failures);
    return t_failures != 0;
}
