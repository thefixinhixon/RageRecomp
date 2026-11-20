// Address-register semantics: full-width An handling, ADDQ/SUBQ on An
// leaving CCR alone, MOVEA variants, TST PC exclusion. Regression tests
// for the ADDQ.W-An truncation bug and the missing MOVEA/LEA decode rows.
#include "support.h"

int main(void) {
    test_init();
    m68k_t *c = rrm->cpu;
    m_insn_t in;
    char buf[128];

    // addq.w #4,a2 : full-width add, CCR untouched.
    {
        const uint16_t w[] = { 0x584a }; // 0101 100 01 001010
        t_decode(0x100u, w, 1, &in, "addq.w #4,a2");
        m68k_format(buf, &in, 0x100u);
        T_SUBSTR(buf, "addq", "dasm says addq");
        memset(c, 0, sizeof(*c));
        c->a[2] = 0xffff0000u;
        c->sr = 0x2000 | 0x1f;
        rr_exec(c, &in, 0x100u);
        T_EQ_U32(c->a[2], 0xffff0004u, "addq.w An is full width");
        T_EQ_U32(c->sr & 0x1f, 0x1fu, "addq.w An leaves CCR alone");
    }

    // subq.w #8,a7 : 8 (encoded 0) wraps the stack pointer.
    {
        const uint16_t w[] = { 0x514f }; // 0101 000 01 001111 (#8)
        t_decode(0x100u, w, 1, &in, "subq.w #8,a7");
        memset(c, 0, sizeof(*c));
        c->a[7] = c->isp = 4;
        c->sr = M68K_SR_S;
        rr_exec(c, &in, 0x100u);
        T_EQ_U32(c->a[7], (uint32_t)-4, "subq.w wraps An");
    }

    // movea.l d3,a5 and movea.w (a0),a1 (sign extension).
    {
        const uint16_t wl[] = { 0x2a43 }; // movea.l d3,a5
        t_decode(0x100u, wl, 1, &in, "movea.l d3,a5");
        m68k_format(buf, &in, 0x100u);
        T_SUBSTR(buf, "movea.l", "dasm says movea.l");
        memset(c, 0, sizeof(*c));
        c->d[3] = 0xdeadbeefu;
        rr_exec(c, &in, 0x100u);
        T_EQ_U32(c->a[5], 0xdeadbeefu, "movea.l copies full long");
    }
    {
        const uint16_t ww[] = { 0x3250 }; // movea.w (a0),a1
        t_decode(0x100u, ww, 1, &in, "movea.w (a0),a1");
        memset(c, 0, sizeof(*c));
        c->a[0] = 0xfff100u;
        rr_mwr16(0xfff100u, 0x8001, 0x100u);
        rr_exec(c, &in, 0x100u);
        T_EQ_U32(c->a[1], 0xffff8001u, "movea.w sign-extends");
    }

    // lea (d8,a2,d1.w*2),a4 : indexed LEA incl. odd high regs.
    {
        const uint16_t w[] = { 0x49f2, 0x1228 };
        t_decode(0x100u, w, 2, &in, "lea indexed");
        m68k_format(buf, &in, 0x100u);
        T_SUBSTR(buf, "lea", "dasm says lea");
        memset(c, 0, sizeof(*c));
        c->a[2] = 0x1000;
        c->d[1] = 0x10;
        rr_exec(c, &in, 0x100u);
        // disp 0x28 + a2 + d1.w*2 = 0x28+0x1000+0x20
        T_EQ_U32(c->a[4], 0x1048u, "lea indexed computes address");
    }

    // tst.b d0 sets flags from a data register.
    {
        const uint16_t w[] = { 0x4a00 };
        t_decode(0x100u, w, 1, &in, "tst.b d0");
        memset(c, 0, sizeof(*c));
        c->d[0] = 0x80;
        c->sr = 0x2000;
        rr_exec(c, &in, 0x100u);
        T_OK((c->sr & M68K_SR_N), "tst.b sets N");
        T_OK(!(c->sr & M68K_SR_Z), "tst.b clears Z");
    }

    // cmpa.w d0,a1 : compare without storing.
    {
        const uint16_t w[] = { 0xb2c0 }; // cmpa.w d0,a1
        t_decode(0x100u, w, 1, &in, "cmpa.w d0,a1");
        memset(c, 0, sizeof(*c));
        c->d[0] = 5;
        c->a[1] = 5;
        rr_exec(c, &in, 0x100u);
        T_EQ_U32(c->a[1], 5u, "cmpa does not store");
        T_OK((c->sr & M68K_SR_Z), "cmpa sets Z on equal");
    }

    return test_finish();
}
