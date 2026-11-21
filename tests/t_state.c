// Save-state files: a state round-trips; damaged files (wrong length,
// out-of-range values) are refused with -1 and leave the machine as it was;
// a state from another build or ROM is refused with 0. As UltraCombo's
// v1.0.4 (#3): a damaged file must never be half-loaded or read past.
#include "support.h"
#include "state.h"
#include "xga.h"

#define PATH "t_state.rrs"

static uint8_t *blob;
static long blob_len;

// Offsets in the blob (state.c's layout), for corrupting single fields.
static long cage_off(void) {
    return 16 + (long)sizeof(m68k_t) + (long)sizeof(c31_t) + RR_RAM_SIZE + RR_BANK_SIZE +
            RR_CR_SIZE + RR_DSP_RAM_WORDS * 4 + 0x800 * 4 + 0x100 * 4;
}
static long mach_off(void) { return cage_off() + 580 + 2048 + 4 + (long)sizeof(xga_t); }
static long gt_off(void) { return mach_off() + 104; }

static void write_blob(long len) {
    FILE *f = fopen(PATH, "wb");
    fwrite(blob, 1, (size_t)len, f);
    fclose(f);
}

static void read_saved(void) {
    FILE *f = fopen(PATH, "rb");
    fseek(f, 0, SEEK_END);
    blob_len = ftell(f);
    fseek(f, 0, SEEK_SET);
    blob = (uint8_t *)malloc((size_t)blob_len + 1);
    if (fread(blob, 1, (size_t)blob_len, f) != (size_t)blob_len) blob_len = 0;
    fclose(f);
}

// Load a copy of the saved state with one byte changed; the RAM marker must survive a refusal.
static int load_with(long off, uint8_t v) {
    uint8_t was = blob[off];
    int rc;
    blob[off] = v;
    write_blob(blob_len);
    blob[off] = was;
    mach.ram[0x100] = 0x5a;
    rc = state_load_file(PATH);
    T_OK(rc == 1 || mach.ram[0x100] == 0x5a, "refused state leaves RAM alone (offset %ld)", off);
    return rc;
}

int main(void) {
    (void)t_w32; (void)t_r32; (void)t_decode;   // support.h helpers this test doesn't use
    test_init();
    mach.rom = (uint8_t *)calloc(1, RR_ROM_SIZE);

    mach.ram[0x100] = 0xa5;
    T_OK(state_save_file(PATH) == 1, "save");
    read_saved();
    T_OK(blob_len > 0, "saved file read back (%ld bytes)", blob_len);
    mach.ram[0x100] = 0;
    T_OK(state_load_file(PATH) == 1, "load");
    T_EQ_U32(mach.ram[0x100], 0xa5, "RAM restored");

    // length: one byte short, one byte over
    write_blob(blob_len - 1);
    T_OK(state_load_file(PATH) == -1, "truncated file is damaged");
    blob[blob_len] = 0;
    write_blob(blob_len + 1);
    T_OK(state_load_file(PATH) == -1, "file with trailing data is damaged");

    // values that index tables
    T_OK(load_with(cage_off() + 64, 64) == -1, "CAGE FIFO index 64 is damaged");
    T_OK(load_with(cage_off() + 67, 200) == -1, "CAGE FIFO tail 200 is damaged");
    T_OK(load_with(mach_off() + 29, 0x10) == -1, "scanline 4096 is damaged");
    T_OK(load_with(gt_off() + 960 + 17, 0x40) == -1, "colour bank 0x40 is damaged");
    T_OK(load_with(gt_off() + 960 + 240 + 3, 16) == -1, "tile bank 16 is damaged");
    T_OK(load_with(16 + (long)sizeof(m68k_t) + (long)offsetof(c31_t, dcount), 9) == -1,
         "delayed-branch count 9 is damaged");

    // in range: still loads
    T_OK(load_with(gt_off() + 960 + 17, 0x1f) == 1, "colour bank 0x1f loads");

    // another build or ROM: 0, not damaged
    T_OK(load_with(4, 0x77) == 0, "another state format is refused as another version");
    {
        uint8_t *mine = mach.rom;
        mach.rom = (uint8_t *)calloc(1, RR_ROM_SIZE);   // a different program ROM
        mach.rom[5] = 1;
        write_blob(blob_len);
        T_OK(state_load_file(PATH) == 0, "state from another ROM is refused");
        free(mach.rom);
        mach.rom = mine;
    }

    remove(PATH);
    T_OK(state_load_file(PATH) == 0, "missing file");
    free(blob);
    return test_finish();
}
