// DSP boot: unshifted boot-word assembly (MAME ROM_LOAD32_BYTE) and
// boot-to-idle execution. Skips cleanly when the MAME ROM zips are
// absent (CI has no ROMs); set RR_TEST_ZIPS="a.zip;b.zip" to override
// the default search (./primrage.zip plus tms32031.zip beside it).
#include "support.h"
#include "c31.h"

static int have_file(const char *p) {
    FILE *f = fopen(p, "rb");
    if (f) fclose(f);
    return !!f;
}

int main(void) {
    char z0[1024], z1[1024];
    const char *zips[2];
    const char *env = getenv("RR_TEST_ZIPS");
    roms_t *r;
    const game_profile_t *prof;
    if (env && env[0]) {
        const char *sep = strchr(env, ';');
        size_t n = sep ? (size_t)(sep - env) : strlen(env);
        if (n >= sizeof(z0)) n = sizeof(z0) - 1;
        memcpy(z0, env, n);
        z0[n] = 0;
        if (sep) {
            strncpy(z1, sep + 1, sizeof(z1) - 1);
            z1[sizeof(z1) - 1] = 0;
        } else z1[0] = 0;
    } else {
        strcpy(z0, "primrage.zip");
        strcpy(z1, "/tmp/mameroms/tms32031.zip");
    }
    if (!have_file(z0)) {
        printf("# SKIP: game zip missing (%s)\n", z0);
        printf("# 0 tests, 0 failures\n");
        return 0;
    }
    zips[0] = z0;
    zips[1] = z1;
    prof = game_profile(GAME_PRIMRAGE);
    mach_profile = prof;
    r = roms_open(zips, have_file(z1) ? 2 : 1);
    if (roms_load_set(r, prof)) {
        printf("# SKIP: ROM set incomplete\n");
        printf("# 0 tests, 0 failures\n");
        return 0;
    }

    // Boot assembly must be unshifted (MAME ROM_LOAD32_BYTE rule).
    {
        uint8_t *f;
        uint32_t len, j, bad = 0;
        f = roms_read(r, &prof->dspboot, &len);
        T_OK(f && len >= 0x80000u, "boot file present");
        if (f && len >= 0x80000u) {
            for (j = 0; j < 0x80000u; j++) {
                if (cage.boot[j] != f[j]) {
                    if (bad < 3)
                        printf("# mismatch at %05X: got %08X want %02X\n",
                            j, cage.boot[j], f[j]);
                    bad++;
                }
            }
            T_EQ_U32(bad, 0u, "all 512K boot words unshifted");
            free(f);
        }
        T_EQ_U32(cage.boot[0], 8u, "boot header word");
    }

    // Boot execution completes into the idle loop (needs c31boot too).
    if (cage.bootint && cage.bootint[0]) {
        machine_reset();
        cage_reset_w(0);
        rrm->dsp->cycles = 3000000;
        c31_run();
        T_OK(rrm->dsp->total > 500000u, "DSP executes boot");
        T_OK(!rrm->dsp->idle, "DSP not stuck idle");
        T_EQ_U32(rrm->dsp->pc & 0xffffffu, 0x16afu, "DSP parks in idle loop");
    } else {
        printf("# SKIP: c31boot.bin missing, execution check omitted\n");
    }

    return test_finish();
}
