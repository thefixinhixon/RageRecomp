// Effective-address decode: brief/full extension words, displacements,
// PC-relative forms. Regression test for the inverted DA/WL bits that
// decoded (0,a0,d5.w*8) as (0,a0,a5.l*8) and crashed the game in 0x2E50.
#include "support.h"

static void check_ea(const char *what, uint32_t pc, const uint16_t *words,
        int nwords, int want_mode, int want_reg) {
    m_insn_t in;
    int len = t_decode(pc, words, nwords, &in, what);
    if (len <= 0) return;
    T_EQ_U32(in.ea.mode, (uint32_t)want_mode, "%s mode", what);
    T_EQ_U32(in.ea.reg, (uint32_t)want_reg, "%s reg", what);
}

int main(void) {
    test_init();

    // lea (0,a0,d5.w*8),a4 : the exact 0x2E50 case (ext word 0x5600).
    // DA=0 (Dn), reg=5, WL=0 (word), scale=3, disp8=0.
    {
        const uint16_t w[] = { 0x49f0, 0x5600 };
        m_insn_t in;
        char buf[128];
        int len = t_decode(0x2e50u, w, 2, &in, "lea indexed");
        if (len > 0) {
            T_EQ_U32(in.ea.mode, MEA_AIDX, "brief mode is AIDX");
            T_EQ_U32(in.ea.reg, 0u, "brief base is a0");
            T_EQ_U32(in.ea.ireg, 5u, "brief index is D5 (not A5)");
            T_EQ_U32(in.ea.ilong, 0u, "brief index is word (not long)");
            T_EQ_U32(in.ea.scale, 3u, "brief scale is 8");
            T_EQ_U32((uint32_t)in.ea.disp, 0u, "brief disp is 0");
            m68k_format(buf, &in, 0x2e50u);
            T_SUBSTR(buf, "d5.w", "dasm prints d5.w");
        }
    }

    // Brief word 0xD702 : DA=1 (An), reg=7 (a7), WL=0, scale=0, disp=2.
    {
        const uint16_t w[] = { 0x2070, 0xf002 }; // movea.l (2,a0,a7.w),a0
        m_insn_t in;
        int len = t_decode(0x100u, w, 2, &in, "brief a7.w");
        if (len > 0) {
            T_EQ_U32(in.ea.ireg, 15u, "DA=1 selects A reg (15=a7)");
            T_EQ_U32(in.ea.ilong, 0u, "WL=0 selects word");
            T_EQ_U32(in.ea.scale, 0u, "scale 0 selects *1");
            T_EQ_U32((uint32_t)in.ea.disp, 2u, "disp8 sign handling");
        }
    }

    // Brief word 0x9BFC : DA=1 reg=1 (a1), WL=1 (long), scale=3, disp=-4.
    {
        const uint16_t w[] = { 0x2070, 0x9efc }; // movea.l (-4,a0,a1.l*8),a0
        m_insn_t in;
        int len = t_decode(0x100u, w, 2, &in, "brief a1.l*8");
        if (len > 0) {
            T_EQ_U32(in.ea.ireg, 9u, "DA=1 reg=1 selects a1");
            T_EQ_U32(in.ea.ilong, 1u, "WL=1 selects long");
            T_EQ_U32(in.ea.scale, 3u, "scale 3 selects *8");
            T_EQ_U32((uint32_t)in.ea.disp, (uint32_t)-4, "disp8 is signed");
        }
    }

    // (d16,An) and absolute forms.
    {
        const uint16_t w[] = { 0x3028, 0xffe0 }; // move.w (-32,a0),d0
        m_insn_t in;
        int len = t_decode(0x100u, w, 2, &in, "d16(an)");
        if (len > 0) {
            T_EQ_U32(in.ea.mode, MEA_ADISP, "d16 mode");
            T_EQ_U32((uint32_t)in.ea.disp, (uint32_t)-32, "d16 sign-extended");
        }
    }
    {
        const uint16_t w[] = { 0x3039, 0xffff, 0x8420 }; // move.w ($ffff8420).l,d0
        m_insn_t in;
        int len = t_decode(0x100u, w, 3, &in, "abs.l");
        if (len > 0) {
            T_EQ_U32(in.ea.mode, MEA_ABSL, "abs.l mode");
            T_EQ_U32((uint32_t)in.ea.disp, 0xffff8420u, "abs.l address");
            T_EQ_U32((uint32_t)len, 3u, "abs.l length");
        }
    }
    {
        const uint16_t w[] = { 0x3038, 0x1234 }; // move.w ($1234).w,d0
        m_insn_t in;
        int len = t_decode(0x100u, w, 2, &in, "abs.w");
        if (len > 0) {
            T_EQ_U32(in.ea.mode, MEA_ABSW, "abs.w mode");
            T_EQ_U32((uint32_t)in.ea.disp, 0x1234u, "abs.w address");
        }
    }

    // (d16,PC): pc-relative disp resolves against pc+2 at exec time.
    {
        const uint16_t w[] = { 0x303a, 0x0010 }; // move.w (16,pc),d0
        m_insn_t in;
        int len = t_decode(0x1000u, w, 2, &in, "d16(pc)");
        if (len > 0) {
            T_EQ_U32(in.ea.mode, MEA_PCDISP, "pcdisp mode");
            T_EQ_U32(rr_ea_addr(rrm->cpu, &in.ea, 0x1000u), 0x1012u,
                "pcdisp resolves to pc+2+disp");
        }
    }

    // Full extension word: (d32,An) with null index/suppression bits.
    // xw = 1 0 0 11 0 000 : full fmt, BS=0, IS=0, BD=long, I/IS=0.
    {
        const uint16_t w[] = { 0x2030, 0x8330, 0x1234, 0x5678 };
        m_insn_t in;
        int len = t_decode(0x100u, w, 4, &in, "full ext d32");
        if (len > 0) {
            T_EQ_U32(in.ea.mode, MEA_AIDX, "full ext keeps AIDX");
            T_EQ_U32(in.ea.bdsz, 3u, "full ext base disp is long");
            T_EQ_U32((uint32_t)in.ea.disp, 0x12345678u, "full ext disp32");
            T_EQ_U32(in.ea.iis, 0u, "full ext no indirection");
        }
    }

    // Predecrement/postincrement lengths: byte on A7 still one word op.
    check_ea("postinc", 0x100u, (const uint16_t[]){ 0x1018 }, 1,
        MEA_AINC, 0);
    check_ea("predec", 0x100u, (const uint16_t[]){ 0x1027 }, 1,
        MEA_ADEC, 7);

    // Incremental decode: short reads must ask for more, not fault.
    {
        m_insn_t in;
        const uint16_t w1[] = { 0x3039 }; // move.w ($...).l needs 2 ext words
        int r = m68k_decode(0x100u, w1, 1, &in);
        T_OK(r == 0, "decode asks for more words when short");
    }

    // 020 cycle counts (MAME/Musashi parity, m68k_in.lst): cache-hit
    // costs pin game pacing (boot self-tests run ~20% slow otherwise).
    {
        // eor.l d0,d1 (reg-reg) = 2, eor.l d0,(a1) (mem-dst) = 4.
        const uint16_t e1[] = { 0xb181 }, e2[] = { 0xb191 };
        m_insn_t in;
        char buf[128];
        if (t_decode(0x100u, e1, 1, &in, "eor rr") > 0) {
            T_EQ_U32(in.cycles, 2u, "eor.l dn,dm costs 2");
            m68k_format(buf, &in, 0x100u);
            T_SUBSTR(buf, "eor", "eor rr vector decodes");
        }
        if (t_decode(0x100u, e2, 1, &in, "eor mem") > 0)
            T_EQ_U32(in.cycles, 4u, "eor.l dn,(an) costs 4");
        // cmp.l d0,d1 and cmp.l (a0),d1 are both 2 (mem-src free).
        {
            const uint16_t c1[] = { 0xb280 }, c2[] = { 0xb2a0 };
            if (t_decode(0x100u, c1, 1, &in, "cmp rr") > 0)
                T_EQ_U32(in.cycles, 2u, "cmp.l dn,dm costs 2");
            if (t_decode(0x100u, c2, 1, &in, "cmp mem") > 0)
                T_EQ_U32(in.cycles, 2u, "cmp.l (an),dn costs 2");
        }
        // lsr.w #1,d2 (reg) = 4.
        {
            const uint16_t w[] = { 0xe24a };
            if (t_decode(0x100u, w, 1, &in, "lsr reg") > 0) {
                T_EQ_U32(in.cycles, 4u, "lsr.w #1,dn costs 4");
                m68k_format(buf, &in, 0x100u);
                T_SUBSTR(buf, "lsr", "lsr vector decodes");
            }
        }
        // move.l d0,d1 (reg-reg) = 2; addq.w #1,d1 = 2.
        {
            const uint16_t m1[] = { 0x2200 }, a1[] = { 0x5141 };
            if (t_decode(0x100u, m1, 1, &in, "move rr") > 0) {
                char b2[128];
                m68k_format(b2, &in, 0x100u);
                T_SUBSTR(b2, "move.l", "move rr vector decodes");
                T_EQ_U32(in.cycles, 2u, "move.l dn,dm costs 2");
            }
            if (t_decode(0x100u, a1, 1, &in, "addq reg") > 0)
                T_EQ_U32(in.cycles, 2u, "addq.w #1,dn costs 2");
        }
        // link.w = 5, rts = 10.
        {
            const uint16_t l1[] = { 0x4e56, 0xfffc }, r1[] = { 0x4e75 };
            if (t_decode(0x100u, l1, 2, &in, "link") > 0)
                T_EQ_U32(in.cycles, 5u, "link.w costs 5");
            if (t_decode(0x100u, r1, 1, &in, "rts") > 0)
                T_EQ_U32(in.cycles, 10u, "rts costs 10");
        }
        // movem.l d0-d1,-(a7): 4 + 4*2 = 12 store.
        // movem.l (a7)+,d0-d1: 8 + 4*2 = 16 load.
        {
            const uint16_t s1[] = { 0x48e7, 0x0003 };
            const uint16_t l1[] = { 0x4cdf, 0x0003 };
            if (t_decode(0x100u, s1, 2, &in, "movem store") > 0) {
                T_EQ_U32(in.cycles, 12u, "movem.l 2-reg store costs 12");
                m68k_format(buf, &in, 0x100u);
                T_SUBSTR(buf, "movem", "movem vector decodes");
            }
            if (t_decode(0x100u, l1, 2, &in, "movem load") > 0)
                T_EQ_U32(in.cycles, 16u, "movem.l 2-reg load costs 16");
        }
    }

    return test_finish();
}
