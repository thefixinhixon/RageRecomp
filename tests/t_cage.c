// CAGE board: DMA transfer/timing, timers, serial port edge cases,
// host mailbox and reset lines. Drives the board through its DSP-side
// (cage_dwr) and host-side (cage_main/control) registers without
// executing DSP code; time is advanced via the DSP-cycle clock.
#include "support.h"

#define IO(x) (0x808000u + (x))

static void test_time(int64_t now) {
    cage_abs_cyc = now;
    cage_slice_budget = 0;
    rrm->dsp->cycles = 0;
}

int main(void) {
    test_init();
    cage_reset();
    test_time(0);

    // Serial port first (DMA timer needs a word rate).
    // Bit 2 = clock mode, bits 18-19 = word length (1 = 16-bit).
    cage_dwr(IO(0x40), 0x00040004u);
    cage_dwr(IO(0x46), 100); // period
    T_OK(cage.word_cyc > 0, "serial programs a word rate");
    T_OK(cage.dac_rate > 0 && cage.dac_rate < 100000, "DAC rate sane");

    // Serial edge: period 0 in clock mode still ticks (MAME parity).
    cage_dwr(IO(0x46), 0);
    T_OK(cage.word_cyc > 0, "period-0 clock mode still ticks");
    // Period 0 outside clock mode: word rate 0, never fires.
    cage_dwr(IO(0x40), 0x00040000u);
    cage_dwr(IO(0x46), 0);
    T_OK(cage.word_cyc == 0, "period-0 data mode is idle");
    // Restore a working serial setup for the DMA tests.
    cage_dwr(IO(0x40), 0x00040004u);
    cage_dwr(IO(0x46), 100);
    {
        double wc = cage.word_cyc;

        // DMA transfer: 8 words deinterleave across 4 FIFOs.
        for (int i = 0; i < 8; i++) cage.ram[0x1000 + i] = 0x1000 + i;
        cage_dwr(IO(0x04), 0x1000); // source
        cage_dwr(IO(0x08), 8);      // count
        cage_dwr(IO(0x00), 3 | (1u << 4)); // enable, incrementing
        T_OK(cage.dma_enabled, "DMA enables");
        T_EQ_U32(cage.dac_wr[0], 2u, "ch0 got words 0,4");
        T_EQ_U32(cage.dac_wr[1], 2u, "ch1 got words 1,5");
        T_EQ_U32(cage.dac_wr[2], 2u, "ch2 got words 2,6");
        T_EQ_U32(cage.dac_wr[3], 2u, "ch3 got words 3,7");
        T_EQ_U32((uint32_t)cage.dac[0][0], 0x1000u, "ch0 first sample");
        T_EQ_U32((uint32_t)cage.dac[1][0], 0x1001u, "ch1 first sample");
        T_EQ_U32((uint32_t)cage.dac[2][1], 0x1006u, "ch2 second sample");

        // Completion fires at now + word_cyc * full count.
        {
            int64_t end = cage.dma_end_cyc;
            T_OK(end == (int64_t)(wc * 8.0), "DMA timer spans full count");
            test_time(end - 1);
            cage_update();
            T_OK(cage.dma_enabled, "DMA not done early");
            T_OK(!(rrm->dsp->iff & (1u << C31_DINT0)), "no DMA irq early");
            test_time(end);
            cage_update();
            T_OK(!cage.dma_enabled, "DMA completes");
            T_OK(rrm->dsp->iff & (1u << C31_DINT0), "DMA raises DINT0");
            T_EQ_U32(cage.io[0x08], 0u, "DMA count clears");
            T_EQ_U32(cage.io[0x04], 0x1008u, "DMA source advances");
        }
        rrm->dsp->iff = 0;
    }

    // Partial tail: count 6 transfers 4, drops 2, timer runs for 6.
    {
        double wc;
        cage_reset();
        test_time(0);
        uint32_t w0 = cage.dac_wr[0];
        cage_dwr(IO(0x40), 0x00040004u);
        cage_dwr(IO(0x46), 100);
        wc = cage.word_cyc;
        for (int i = 0; i < 8; i++) cage.ram[0x2000 + i] = 0x2000 + i;
        cage_dwr(IO(0x04), 0x2000);
        cage_dwr(IO(0x08), 6);
        cage_dwr(IO(0x00), 3 | (1u << 4));
        T_EQ_U32(cage.dac_wr[0] - w0, 1u, "tail group dropped (ch0 +1)");
        T_OK(cage.dma_end_cyc == (int64_t)(wc * 6.0),
            "timer still spans full count with tail");
        T_EQ_U32(cage.dma_end_addr, 0x2006u, "end addr skips tail");
    }

    // Timer 0: enable, fire once per period, periodic while enabled.
    {
        cage_reset();
        test_time(0);
        cage_dwr(IO(0x28), 500);   // period
        cage_dwr(IO(0x20), 0xc0);  // enable
        T_OK(cage.timer_on[0], "timer arms");
        test_time(999);
        cage_update();
        T_OK(!(rrm->dsp->iff & (1u << C31_TINT0)), "timer quiet early");
        test_time(1000);
        cage_update();
        T_OK(rrm->dsp->iff & (1u << C31_TINT0), "timer fires TINT0");
        T_OK(cage.timer_on[0], "timer stays armed (periodic)");
        rrm->dsp->iff = 0;
        test_time(2000);
        cage_update();
        T_OK(rrm->dsp->iff & (1u << C31_TINT0), "timer refires next period");
        cage_dwr(IO(0x20), 0x00); // disable
        T_OK(!cage.timer_on[0], "timer disarms");
    }

    // Host mailbox and reset lines (both directions through 0xA00000).
    // Delivery is deferred a scanline (MAME's synchronize parity): the
    // DSP sees a command only after cage_scanline_swap publishes it.
    {
        cage_reset();
        cage_main_w(0xbeef);
        T_OK(!cage.cpu_to_cage, "main write stays pending pre-swap");
        T_OK(!(cage_control_r() & 2u), "control hides pending data");
        T_EQ_U32(cage_drd(0xa00000u), 0u, "DSP reads empty pre-swap");
        cage_scanline_swap();
        T_OK(cage.cpu_to_cage, "swap raises cpu_to_cage");
        T_OK(cage_control_r() & 2u, "control shows data ready");
        T_EQ_U32(cage_drd(0xa00000u), 0xbeefu, "DSP reads mailbox");
        T_OK(!cage.cpu_to_cage, "DSP read drains mailbox");
        // Bursts stay ordered and lossless across the swap.
        cage_main_w(0x1111);
        cage_main_w(0x2222);
        cage_scanline_swap();
        T_EQ_U32(cage_drd(0xa00000u), 0x1111u, "burst first kept");
        T_EQ_U32(cage_drd(0xa00000u), 0x2222u, "burst second kept");
        T_OK(!cage.cpu_to_cage, "burst drain clears flag");
        cage_dwr(0xa00000u, 0x5a5a);
        T_OK(cage.cage_to_cpu, "DSP write raises cage_to_cpu");
        T_OK(cage_control_r() & 1u, "control shows reply ready");
        T_EQ_U32(cage_main_r(), 0x5a5au, "main reads reply");
        T_OK(!cage.cage_to_cpu, "main read drains reply");
        cage_reset_w(1);
        T_OK(cage.dsp_held, "XRESET holds DSP");
        cage_reset_w(0);
        T_OK(!cage.dsp_held, "release frees DSP");
        cage_control_w(0);
        T_OK(cage.dsp_held, "control 0 holds DSP");
        cage_control_w(3);
        T_OK(!cage.dsp_held, "control 3 releases DSP");
    }

    return test_finish();
}
