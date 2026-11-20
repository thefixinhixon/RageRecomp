// RMW addressing: read-modify-write ops must apply -(An)/(An)+ side
// effects exactly once and write back to the address that was read.
// Regression test for the or.l d0,(a2)+ double-increment bug that
// corrupted the 68K stack and crashed the boot (RTS to 0xFFFFBE).
#include "support.h"

static void run_op(m68k_t *c, const uint16_t *words, int n, uint32_t pc) {
    m_insn_t in;
    char buf[128];
    int len = t_decode(pc, words, n, &in, "rmw op");
    if (len <= 0) return;
    m68k_format(buf, &in, pc);
    rr_exec(c, &in, pc);
    T_OK(!c->exc, "no exception");
}

int main(void) {
    test_init();
    m68k_t *c = rrm->cpu;

    // or.l d0,(a2)+ : write [a2], bump once. (Boot crashed when this
    // wrote [a2+4] and bumped twice.)
    {
        const uint16_t w[] = { 0x819a };
        uint32_t base = 0xfff000u;
        m_insn_t in;
        char buf[128];
        int i;
        t_decode(base, w, 1, &in, "or.l d0,(a2)+");
        m68k_format(buf, &in, base);
        T_SUBSTR(buf, "or.l", "dasm says or.l");
        T_SUBSTR(buf, "(a2)+", "dasm says (a2)+");
        memset(c, 0, sizeof(*c));
        for (i = 0; i < 6; i++) t_w32(0xfff100u + i * 4, 0xaaaaaaaaU);
        t_w32(0xfff100u, 0x0f0f0f0fU);
        c->d[0] = 0xf0f0f0f0U;
        c->a[2] = 0xfff100u;
        c->sr = 0x2000;
        rr_exec(c, &in, base);
        T_EQ_U32(t_r32(0xfff100u), 0xffffffffU, "or.l result lands at [a2]");
        T_EQ_U32(t_r32(0xfff104u), 0xaaaaaaaaU, "or.l does not touch [a2+4]");
        T_EQ_U32(c->a[2], 0xfff104u, "or.l bumps a2 once");
        T_OK(!(c->sr & M68K_SR_Z), "or.l clears Z");
        T_OK((c->sr & M68K_SR_N), "or.l sets N");
    }

    // addq.l #1,(a3)+ with guards on both sides.
    {
        const uint16_t w[] = { 0x529b };
        m_insn_t in;
        char buf[128];
        t_decode(0xfff000u, w, 1, &in, "addq.l #1,(a3)+");
        m68k_format(buf, &in, 0xfff000u);
        T_SUBSTR(buf, "addq", "dasm says addq");
        memset(c, 0, sizeof(*c));
        t_w32(0xfff0fcu, 0xbbbbbbbbu);
        t_w32(0xfff100u, 0x00000041u);
        t_w32(0xfff104u, 0xbbbbbbbbu);
        c->a[3] = 0xfff100u;
        rr_exec(c, &in, 0xfff000u);
        T_EQ_U32(t_r32(0xfff100u), 0x42u, "addq result");
        T_EQ_U32(t_r32(0xfff0fcu), 0xbbbbbbbbu, "addq guard below intact");
        T_EQ_U32(t_r32(0xfff104u), 0xbbbbbbbbu, "addq guard above intact");
        T_EQ_U32(c->a[3], 0xfff104u, "addq bumps once");
    }

    // sub.l d1,-(a4) : predecrement once, write to decremented addr.
    {
        const uint16_t w[] = { 0x93a4 };
        m_insn_t in;
        char buf[128];
        t_decode(0xfff000u, w, 1, &in, "sub.l d1,-(a4)");
        m68k_format(buf, &in, 0xfff000u);
        T_SUBSTR(buf, "sub.l", "dasm says sub.l");
        memset(c, 0, sizeof(*c));
        t_w32(0xfff100u, 0xaaaaaaaaU);
        t_w32(0xfff104u, 0x00000100u);
        t_w32(0xfff108u, 0xaaaaaaaaU);
        c->d[1] = 1;
        c->a[4] = 0xfff108u;
        rr_exec(c, &in, 0xfff000u);
        T_EQ_U32(t_r32(0xfff104u), 0xffu, "sub predecrement result");
        T_EQ_U32(t_r32(0xfff100u), 0xaaaaaaaaU, "sub does not write below");
        T_EQ_U32(t_r32(0xfff108u), 0xaaaaaaaaU, "sub does not write above");
        T_EQ_U32(c->a[4], 0xfff104u, "sub predecrements once");
    }

    // neg.l (a5)+ and not.b -(a3) (unary RMW both directions).
    {
        const uint16_t wneg[] = { 0x449d };
        const uint16_t wnot[] = { 0x4623 };
        m_insn_t in;
        char buf[128];
        t_decode(0xfff000u, wneg, 1, &in, "neg.l (a5)+");
        m68k_format(buf, &in, 0xfff000u);
        T_SUBSTR(buf, "neg.l", "dasm says neg.l");
        memset(c, 0, sizeof(*c));
        t_w32(0xfff100u, 5);
        t_w32(0xfff104u, 0xccccccccU);
        c->a[5] = 0xfff100u;
        rr_exec(c, &in, 0xfff000u);
        T_EQ_U32(t_r32(0xfff100u), (uint32_t)-5, "neg result in place");
        T_EQ_U32(t_r32(0xfff104u), 0xccccccccU, "neg guard intact");
        T_EQ_U32(c->a[5], 0xfff104u, "neg bumps once");

        t_decode(0xfff000u, wnot, 1, &in, "not.b -(a3)");
        m68k_format(buf, &in, 0xfff000u);
        T_SUBSTR(buf, "not.b", "dasm says not.b");
        memset(c, 0, sizeof(*c));
        c->a[3] = 0xfff104u;
        rr_mwr8(0xfff103u, 0x3c);
        rr_exec(c, &in, 0xfff000u);
        T_EQ_U32(rr_mrd8(0xfff103u), 0xc3u, "not predecrement result");
        T_EQ_U32(c->a[3], 0xfff103u, "not predecrements once");
    }

    // asl.w (a0)+ : memory shift is RMW too.
    {
        const uint16_t w[] = { 0xe1d8 };
        m_insn_t in;
        char buf[128];
        t_decode(0xfff000u, w, 1, &in, "asl.w (a0)+");
        m68k_format(buf, &in, 0xfff000u);
        T_SUBSTR(buf, "asl.w", "dasm says asl.w");
        memset(c, 0, sizeof(*c));
        rr_mwr16(0xfff100u, 0x4001, 0xfff000u);
        rr_mwr16(0xfff102u, 0xdddd, 0xfff000u);
        c->a[0] = 0xfff100u;
        rr_exec(c, &in, 0xfff000u);
        T_EQ_U32(rr_mrd16(0xfff100u, 0), 0x8002u, "asl result in place");
        T_EQ_U32(rr_mrd16(0xfff102u, 0), 0xddddu, "asl guard intact");
        T_EQ_U32(c->a[0], 0xfff102u, "asl bumps once by word");
    }

    // addi.l #$1234,(a4)+ : immediate RMW.
    {
        const uint16_t w[] = { 0x069c, 0x0000, 0x1234 };
        m_insn_t in;
        char buf[128];
        t_decode(0xfff000u, w, 3, &in, "addi.l");
        m68k_format(buf, &in, 0xfff000u);
        T_SUBSTR(buf, "addi.l", "dasm says addi.l");
        memset(c, 0, sizeof(*c));
        t_w32(0xfff100u, 1);
        t_w32(0xfff104u, 0xeeeeeeeeU);
        c->a[4] = 0xfff100u;
        rr_exec(c, &in, 0xfff000u);
        T_EQ_U32(t_r32(0xfff100u), 0x1235u, "addi result in place");
        T_EQ_U32(t_r32(0xfff104u), 0xeeeeeeeeU, "addi guard intact");
        T_EQ_U32(c->a[4], 0xfff104u, "addi bumps once");
    }

    // clr.w -(a6) : CLR does not read; single predecrement write.
    {
        const uint16_t w[] = { 0x4266 };
        m_insn_t in;
        char buf[128];
        t_decode(0xfff000u, w, 1, &in, "clr.w -(a6)");
        m68k_format(buf, &in, 0xfff000u);
        T_SUBSTR(buf, "clr.w", "dasm says clr.w");
        memset(c, 0, sizeof(*c));
        rr_mwr16(0xfff102u, 0x7777, 0xfff000u);
        rr_mwr16(0xfff100u, 0x7777, 0xfff000u);
        c->a[6] = 0xfff104u;
        rr_exec(c, &in, 0xfff000u);
        T_EQ_U32(rr_mrd16(0xfff102u, 0), 0u, "clr zeroes target");
        T_EQ_U32(rr_mrd16(0xfff100u, 0), 0x7777u, "clr guard intact");
        T_EQ_U32(c->a[6], 0xfff102u, "clr predecrements once");
        T_OK((c->sr & M68K_SR_Z), "clr sets Z");
    }

    // tas (a7)+ : byte RMW on A7 steps by 2.
    {
        const uint16_t w[] = { 0x4adf };
        m_insn_t in;
        char buf[128];
        t_decode(0xfff000u, w, 1, &in, "tas (a7)+");
        m68k_format(buf, &in, 0xfff000u);
        T_SUBSTR(buf, "tas", "dasm says tas");
        memset(c, 0, sizeof(*c));
        c->a[7] = c->isp = 0xfff100u;
        c->sr = M68K_SR_S;
        rr_mwr8(0xfff100u, 0x12);
        rr_exec(c, &in, 0xfff000u);
        T_EQ_U32(rr_mrd8(0xfff100u), 0x92u, "tas sets bit 7 in place");
        T_EQ_U32(c->a[7], 0xfff102u, "tas keeps A7 even");
    }

    // move.l (a0)+,(a1)+ : pure MOVE bumps each side exactly once.
    {
        const uint16_t w[] = { 0x22d8 };
        m_insn_t in;
        char buf[128];
        t_decode(0xfff000u, w, 1, &in, "move.l (a0)+,(a1)+");
        m68k_format(buf, &in, 0xfff000u);
        T_SUBSTR(buf, "move.l", "dasm says move.l");
        memset(c, 0, sizeof(*c));
        t_w32(0xfff100u, 0xdeadbeefU);
        t_w32(0xfff200u, 0);
        c->a[0] = 0xfff100u;
        c->a[1] = 0xfff200u;
        rr_exec(c, &in, 0xfff000u);
        T_EQ_U32(t_r32(0xfff200u), 0xdeadbeefU, "move copies");
        T_EQ_U32(c->a[0], 0xfff104u, "move bumps source once");
        T_EQ_U32(c->a[1], 0xfff204u, "move bumps dest once");
    }

    (void)run_op;
    return test_finish();
}
