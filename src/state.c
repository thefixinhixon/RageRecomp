// Whole-machine save states. See state.h.
#include "state.h"
#include "machine.h"
#include "mem.h"
#include "rec_build.h"
#include "gtvid.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STATE_MAGIC 0x31525252u // "RRR1"

// Translated and interpreted code emulate identically, so only the state
// format (bumped with any emulation change) identifies compatible builds.
uint32_t state_compat_id(void) {
    return 0x52520000u | STATE_FORMAT;
}

static uint32_t crc32_buf(const uint8_t *data, uint32_t len) {
    uint32_t crc = 0xffffffffu, i, j;
    for (i = 0; i < len; i++) {
        crc ^= data[i];
        for (j = 0; j < 8; j++)
            crc = (crc & 1) ? (crc >> 1) ^ 0xedb88320u : crc >> 1;
    }
    return crc ^ 0xffffffffu;
}

// The program ROM's CRC, computed once (states record which ROM they're from).
static uint32_t rom_crc(void) {
    static uint32_t crc;
    static const uint8_t *of;
    if (!mach.rom) return 0;
    if (of != mach.rom) {
        crc = crc32_buf(mach.rom, 0x200000u);
        of = mach.rom;
    }
    return crc;
}

static void put32(uint8_t *dst, uint32_t v) {
    dst[0] = (uint8_t)v;
    dst[1] = (uint8_t)(v >> 8);
    dst[2] = (uint8_t)(v >> 16);
    dst[3] = (uint8_t)(v >> 24);
}

static uint32_t get32(const uint8_t *src) {
    return (uint32_t)src[0] | ((uint32_t)src[1] << 8) |
            ((uint32_t)src[2] << 16) | ((uint32_t)src[3] << 24);
}

// Fixed spans (all images are fixed-size by construction).
#define SP_CPU sizeof(m68k_t)
#define SP_DSP sizeof(c31_t)
#define SP_RAM RR_RAM_SIZE
#define SP_BANK RR_BANK_SIZE
#define SP_CR RR_CR_SIZE
#define SP_DRAM (RR_DSP_RAM_WORDS * 4)
#define SP_DIRAM (0x800u * 4)
#define SP_DIO (0x100u * 4)
#define SP_EE 2048u
#define SP_XGA sizeof(xga_t)

void machine_state_size(uint32_t *size) {
    *size = 16 + SP_CPU + SP_DSP + SP_RAM + SP_BANK + SP_CR + SP_DRAM +
            SP_DIRAM + SP_DIO + 580 + SP_EE + 4 + SP_XGA +
            104 + gt_state_size();
}

void machine_state_save(uint8_t *dst) {
    uint8_t *p = dst;
    put32(p, STATE_MAGIC);
    put32(p + 4, state_compat_id());
    {
        uint32_t size;
        machine_state_size(&size);
        put32(p + 8, size);
    }
    put32(p + 12, rom_crc());
    p += 16;
    memcpy(p, &mach.cpu, SP_CPU);
    p += SP_CPU;
    memcpy(p, &mach.dsp, SP_DSP);
    p += SP_DSP;
    memcpy(p, mach.ram, SP_RAM);
    p += SP_RAM;
    memcpy(p, mach.bank, SP_BANK);
    p += SP_BANK;
    memcpy(p, mach.colorram, SP_CR);
    p += SP_CR;
    memcpy(p, cage.ram, SP_DRAM);
    p += SP_DRAM;
    memcpy(p, cage.iram, SP_DIRAM);
    p += SP_DIRAM;
    memcpy(p, cage.io, SP_DIO);
    p += SP_DIO;
    put32(p, cage.from_main);
    put32(p + 4, cage.control);
    put32(p + 8, cage.latch);
    put32(p + 12, (uint32_t)cage.cpu_to_cage |
            ((uint32_t)cage.cage_to_cpu << 8) |
            ((uint32_t)cage.dma_enabled << 16) |
            ((uint32_t)cage.dma_timer_on << 24));
    put32(p + 16, (uint32_t)cage.dma_end_cyc);
    put32(p + 20, (uint32_t)(cage.dma_end_cyc >> 32));
    put32(p + 24, cage.dma_end_addr);
    put32(p + 28, (uint32_t)cage.timer_on[0] |
            ((uint32_t)cage.timer_on[1] << 8) |
            ((uint32_t)cage.dsp_held << 16));
    put32(p + 32, (uint32_t)cage.timer_end[0]);
    put32(p + 36, (uint32_t)(cage.timer_end[0] >> 32));
    put32(p + 40, (uint32_t)cage.timer_end[1]);
    put32(p + 44, (uint32_t)(cage.timer_end[1] >> 32));
    put32(p + 48, cage.dac_rate);
    put32(p + 52, (uint32_t)cage_abs_cyc);
    put32(p + 56, (uint32_t)(cage_abs_cyc >> 32));
    put32(p + 60, (uint32_t)cage_slice_budget);
    put32(p + 64, (uint32_t)cage.pend_hd | ((uint32_t)cage.pend_tl << 8) |
            ((uint32_t)cage.vis_hd << 16) | ((uint32_t)cage.vis_tl << 24));
    {
        uint32_t fi;
        for (fi = 0; fi < 64; fi++) put32(p + 68 + fi * 4, cage.pend[fi]);
        for (fi = 0; fi < 64; fi++)
            put32(p + 68 + 256 + fi * 4, cage.vis[fi]);
    }
    p += 580;
    // The DAC FIFOs are the sound on its way to the host, not emulated
    // state (nothing reads them back): left out, so two machines that
    // played their sound at different moments still compare equal.
    memcpy(p, eeprom.data, SP_EE);
    p += SP_EE;
    put32(p, eeprom.unlocked);
    p += 4;
    memcpy(p, &xga, SP_XGA);
    p += SP_XGA;
    put32(p, mach.irq3);
    put32(p + 4, mach.irq4);
    put32(p + 8, mach.irq6);
    put32(p + 12, mach.coin_counters);
    put32(p + 16, mach.led_state);
    put32(p + 20, mach.color_latch);
    put32(p + 24, mach.mo_control);
    put32(p + 28, mach.scanline);
    put32(p + 32, mach.vblank);
    put32(p + 36, (uint32_t)mach.scan_cyc);
    put32(p + 40, (uint32_t)mach.vblank_cyc);
    p += 44;
    put32(p, (uint32_t)mach.carry68k);
    put32(p + 4, (uint32_t)(mach.carry68k >> 32));
    put32(p + 8, (uint32_t)mach.carrydsp);
    put32(p + 12, (uint32_t)(mach.carrydsp >> 32));
    put32(p + 16, (uint32_t)mach.irq6_cyc);
    put32(p + 20, mach.playfield_tile_bank);
    put32(p + 24, mach.in.b1);
    put32(p + 28, mach.in.b3);
    put32(p + 32, mach.in.service);
    put32(p + 36, mach.in.coin1);
    put32(p + 40, mach.in.coin2);
    put32(p + 44, mach.in.tilt);
    put32(p + 48, mach.in.b0);
    put32(p + 52, (uint32_t)mach.frames);
    put32(p + 56, (uint32_t)(mach.frames >> 32));
    p += 60;
    gt_state_save(p);
}

// DSP RAM was replaced: translated functions are re-checked before use.
static void dsp_revalidate(void) {
    rrm->dsp_gen++;
}

// Values in a state that index tables: a damaged file (or one edited by
// hand) is refused before anything is restored, instead of reading or
// writing outside them later.
static int blob_sane(const uint8_t *src) {
    const uint8_t *cg = src + 16 + SP_CPU + SP_DSP + SP_RAM + SP_BANK + SP_CR +
            SP_DRAM + SP_DIRAM + SP_DIO;
    const uint8_t *mc = cg + 580 + SP_EE + 4 + SP_XGA;
    uint32_t fifo = get32(cg + 64), i;
    c31_t dsp;
    for (i = 0; i < 4; i++)                             // CAGE FIFO heads and tails
        if (((fifo >> (i * 8)) & 0xffu) >= 64) return 0;
    if (get32(mc + 28) >= GT_LINES) return 0;           // scanline
    memcpy(&dsp, src + 16 + SP_CPU, sizeof dsp);
    if (dsp.dcount < -1 || dsp.dcount > 3) return 0;    // delayed-branch countdown
    return gt_state_valid(mc + 104);
}

int machine_state_load(const uint8_t *src, uint32_t size) {
    const uint8_t *p = src;
    uint32_t want;
    machine_state_size(&want);
    if (size < want) return -1;
    if (get32(p) != STATE_MAGIC) return -1;
    if (get32(p + 4) != state_compat_id()) return -1;
    if (get32(p + 8) != want) return -1;
    if (!mach.rom || get32(p + 12) != rom_crc())
        return -1;
    if (!blob_sane(src)) return -2;
    p += 16;
    memcpy(&mach.cpu, p, SP_CPU);
    p += SP_CPU;
    memcpy(&mach.dsp, p, SP_DSP);
    p += SP_DSP;
    memcpy(mach.ram, p, SP_RAM);
    p += SP_RAM;
    memcpy(mach.bank, p, SP_BANK);
    p += SP_BANK;
    memcpy(mach.colorram, p, SP_CR);
    p += SP_CR;
    memcpy(cage.ram, p, SP_DRAM);
    p += SP_DRAM;
    memcpy(cage.iram, p, SP_DIRAM);
    p += SP_DIRAM;
    memcpy(cage.io, p, SP_DIO);
    p += SP_DIO;
    cage.from_main = (uint16_t)get32(p);
    cage.control = (uint16_t)get32(p + 4);
    cage.latch = (uint16_t)get32(p + 8);
    cage.cpu_to_cage = (uint8_t)get32(p + 12);
    cage.cage_to_cpu = (uint8_t)(get32(p + 12) >> 8);
    cage.dma_enabled = (uint8_t)(get32(p + 12) >> 16);
    cage.dma_timer_on = (uint8_t)(get32(p + 12) >> 24);
    cage.dma_end_cyc = (int64_t)(((uint64_t)get32(p + 20) << 32) |
            get32(p + 16));
    cage.dma_end_addr = get32(p + 24);
    cage.timer_on[0] = (uint8_t)get32(p + 28);
    cage.timer_on[1] = (uint8_t)(get32(p + 28) >> 8);
    cage.dsp_held = (uint8_t)(get32(p + 28) >> 16);
    cage.timer_end[0] = (int64_t)(((uint64_t)get32(p + 36) << 32) |
            get32(p + 32));
    cage.timer_end[1] = (int64_t)(((uint64_t)get32(p + 44) << 32) |
            get32(p + 40));
    cage.dac_rate = get32(p + 48);
    cage_abs_cyc = (int64_t)(((uint64_t)get32(p + 56) << 32) |
            get32(p + 52));
    cage_slice_budget = (int)get32(p + 60);
    cage.pend_hd = (uint8_t)get32(p + 64);
    cage.pend_tl = (uint8_t)(get32(p + 64) >> 8);
    cage.vis_hd = (uint8_t)(get32(p + 64) >> 16);
    cage.vis_tl = (uint8_t)(get32(p + 64) >> 24);
    {
        uint32_t fi;
        for (fi = 0; fi < 64; fi++)
            cage.pend[fi] = (uint16_t)get32(p + 68 + fi * 4);
        for (fi = 0; fi < 64; fi++)
            cage.vis[fi] = (uint16_t)get32(p + 68 + 256 + fi * 4);
    }
    cage_retune();
    p += 580;
    memcpy(eeprom.data, p, SP_EE);
    p += SP_EE;
    eeprom.unlocked = (uint8_t)get32(p);
    p += 4;
    memcpy(&xga, p, SP_XGA);
    p += SP_XGA;
    mach.irq3 = (uint8_t)get32(p);
    mach.irq4 = (uint8_t)get32(p + 4);
    mach.irq6 = (uint8_t)get32(p + 8);
    mach.coin_counters = (uint8_t)get32(p + 12);
    mach.led_state = (uint8_t)get32(p + 16);
    mach.color_latch = (uint8_t)get32(p + 20);
    mach.mo_control = (uint8_t)get32(p + 24);
    mach.scanline = (uint16_t)get32(p + 28);
    mach.vblank = (uint8_t)get32(p + 32);
    mach.scan_cyc = (int32_t)get32(p + 36);
    mach.vblank_cyc = (int32_t)get32(p + 40);
    p += 44;
    mach.carry68k = (uint64_t)get32(p) | ((uint64_t)get32(p + 4) << 32);
    mach.carrydsp = (uint64_t)get32(p + 8) | ((uint64_t)get32(p + 12) << 32);
    mach.irq6_cyc = (int32_t)get32(p + 16);
    mach.playfield_tile_bank = (uint8_t)get32(p + 20);
    mach.in.b1 = (uint8_t)get32(p + 24);
    mach.in.b3 = (uint8_t)get32(p + 28);
    mach.in.service = (uint8_t)get32(p + 32);
    mach.in.coin1 = (uint8_t)get32(p + 36);
    mach.in.coin2 = (uint8_t)get32(p + 40);
    mach.in.tilt = (uint8_t)get32(p + 44);
    mach.in.b0 = (uint8_t)get32(p + 48);
    mach.frames = (uint64_t)get32(p + 52) | ((uint64_t)get32(p + 56) << 32);
    p += 60;
    gt_state_load(p);
    // Derived state: re-derive the serial word rate from the restored
    // registers, refresh the IRQ lines, and re-validate translated DSP chunks.
    mem_map_pages();
    cage_map_pages();
    cage_serial_restored();
    mem_update_irq();
    dsp_revalidate();
    return 0;
}

// ------------------------------------------------------------- files

int state_save_file(const char *path) {
    uint32_t size;
    uint8_t *blob;
    FILE *f;
    int ok;
    machine_state_size(&size);
    blob = (uint8_t *)calloc(size, 1);
    if (!blob) return 0;
    machine_state_save(blob);
    f = fopen(path, "wb");
    ok = f && fwrite(blob, 1, size, f) == size;
    if (f && fclose(f) != 0) ok = 0;
    free(blob);
    return ok;
}

int state_load_file(const char *path) {
    uint32_t size;
    uint8_t *blob;
    FILE *f = fopen(path, "rb");
    int ok, rc = -1;
    if (!f) return 0;
    machine_state_size(&size);
    blob = (uint8_t *)malloc(size);
    // exactly one state: a short or longer file is damaged
    ok = blob && fread(blob, 1, size, f) == size && fgetc(f) == EOF;
    fclose(f);
    if (ok) rc = machine_state_load(blob, size);
    free(blob);
    if (!ok || rc == -2) {
        // the header names this build and game: if it matches, the rest is damaged
        fprintf(stderr, "state: %s is damaged or not a save state\n", path);
        return -1;
    }
    return rc == 0;
}

uint64_t state_hash(void) {
    uint32_t size, i;
    uint8_t *blob;
    uint64_t h = 1469598103934665603ull;
    machine_state_size(&size);
    blob = (uint8_t *)calloc(size, 1);
    if (!blob) return 0;
    machine_state_save(blob);
    for (i = 0; i < size; i++) {
        h ^= blob[i];
        h *= 1099511628211ull;
    }
    free(blob);
    return h;
}

// ------------------------------------------------------------- history
//
// Rewind, run-ahead and netplay rollback, as UltraCombo does them, but on
// the serialized state: every checkpoint serializes the machine, and the
// 4 KB pages of it that changed since the last one go into the history,
// XOR-packed against the newer copy. (The page tables give translated code
// direct writes, so there are no write hooks to mark pages dirty; comparing
// the ~1.4 MB serialized state is cheaper than any.)

#define PAGE 4096

static uint8_t *mirror;         // the state at the last checkpoint
static uint8_t *cur;            // scratch: the state now
static uint32_t blob_size, npages;

// Tokens of (u16 equal words, u16 literal words, literal words...): XOR of
// a against b, zero runs skipped. At most PAGE + 8 bytes.
static size_t pack_xor(uint8_t *out, const uint8_t *a, const uint8_t *b) {
    const uint8_t *x = a, *y = b;
    uint8_t *o = out;
    int i = 0, n = PAGE / 4;
    while (i < n) {
        int z = i, l, k;
        uint16_t zc, lc;
        while (i < n && !memcmp(x + 4 * i, y + 4 * i, 4)) i++;
        l = i;
        // a literal run ends at 3+ equal words in a row (cheaper as a zero run)
        while (i < n && !(!memcmp(x + 4 * i, y + 4 * i, 4) &&
                (i + 1 >= n || !memcmp(x + 4 * i + 4, y + 4 * i + 4, 4)) &&
                (i + 2 >= n || !memcmp(x + 4 * i + 8, y + 4 * i + 8, 4))))
            i++;
        zc = (uint16_t)(l - z);
        lc = (uint16_t)(i - l);
        memcpy(o, &zc, 2);
        memcpy(o + 2, &lc, 2);
        o += 4;
        for (k = l; k < i; k++) {
            int j;
            for (j = 0; j < 4; j++) *o++ = x[4 * k + j] ^ y[4 * k + j];
        }
    }
    return (size_t)(o - out);
}

// dst ^= unpacked data. Returns bytes consumed.
static size_t unpack_xor(uint8_t *dst, const uint8_t *in) {
    const uint8_t *p = in;
    int i = 0, n = PAGE / 4;
    while (i < n) {
        uint16_t zc, lc;
        int k, j;
        memcpy(&zc, p, 2);
        memcpy(&lc, p + 2, 2);
        p += 4;
        i += zc;
        for (k = 0; k < lc && i < n; k++, i++)
            for (j = 0; j < 4; j++) dst[4 * i + j] ^= *p++;
    }
    return (size_t)(p - in);
}

// Entry k takes the state from checkpoint k+1 back to checkpoint k: the
// pages that changed in between, each XOR its newer value.
typedef struct {
    size_t size;
    uint32_t npages;
    // followed by npages x (u32 page, packed xor)
} entry_t;

#define MAX_ENTRIES 16384
static entry_t *hist[MAX_ENTRIES];
static int hist_head, hist_count;
static size_t hist_bytes, hist_budget;
static uint8_t *scratch;

static void hist_drop_oldest(void) {
    int i = (hist_head - hist_count + MAX_ENTRIES) % MAX_ENTRIES;
    hist_bytes -= hist[i]->size;
    free(hist[i]);
    hist[i] = NULL;
    hist_count--;
}

void state_set_history(size_t bytes) {
    hist_budget = bytes;
    while (hist_count && (hist_bytes > hist_budget || !bytes)) hist_drop_oldest();
}

double state_history_seconds(void) {
    return hist_count * (double)GT_FPS_DEN / GT_FPS_NUM;
}

size_t state_history_bytes(void) { return hist_bytes; }

// The blob is one this build just wrote, so a refusal is a bug in the checks
// (v1.1.1 refused any state taken with the partial-erase line at 240 or
// later: rewind, run-ahead and rollback then silently didn't restore).
static void restore(const uint8_t *src) {
    static int warned;
    uint32_t size;
    int rc;
    machine_state_size(&size);
    rc = machine_state_load(src, size);
    if (rc && !warned) {
        fprintf(stderr, "state: restoring a checkpoint failed (%d)\n", rc);
        warned = 1;
    }
}

void state_init(void) {
    uint32_t size;
    machine_state_size(&size);
    if (!mirror || blob_size != ((size + PAGE - 1) / PAGE) * PAGE) {
        free(mirror);
        free(cur);
        free(scratch);
        blob_size = ((size + PAGE - 1) / PAGE) * PAGE;
        npages = blob_size / PAGE;
        mirror = (uint8_t *)calloc(blob_size, 1);
        cur = (uint8_t *)calloc(blob_size, 1);
        scratch = (uint8_t *)malloc(sizeof(entry_t) + (size_t)npages * (PAGE + 16));
    }
    machine_state_save(mirror);
    while (hist_count) hist_drop_oldest();
}

void state_checkpoint(void) {
    uint32_t g;
    entry_t *e = NULL;
    uint8_t *p = NULL;
    if (!mirror) state_init();
    machine_state_save(cur);
    if (hist_budget) {
        e = (entry_t *)scratch;
        e->npages = 0;
        p = scratch + sizeof(*e);
    }
    for (g = 0; g < npages; g++) {
        uint8_t *c = cur + (size_t)g * PAGE, *m = mirror + (size_t)g * PAGE;
        if (!memcmp(c, m, PAGE)) continue;
        if (e) {
            memcpy(p, &g, 4);
            p += 4;
            p += pack_xor(p, c, m);
            e->npages++;
        }
        memcpy(m, c, PAGE);
    }
    if (!e) return;
    e->size = (size_t)(p - scratch);
    {
        entry_t *copy = (entry_t *)malloc(e->size);
        if (!copy) return;
        memcpy(copy, scratch, e->size);
        if (hist_count == MAX_ENTRIES) hist_drop_oldest();
        hist[hist_head] = copy;
        hist_head = (hist_head + 1) % MAX_ENTRIES;
        hist_count++;
        hist_bytes += copy->size;
        while (hist_bytes > hist_budget && hist_count > 1) hist_drop_oldest();
    }
}

void state_rollback(void) {
    if (!mirror) return;
    restore(mirror);
}

int state_rewind(void) {
    entry_t *e;
    const uint8_t *p;
    uint32_t i;
    if (!hist_count) return 0;
    hist_head = (hist_head - 1 + MAX_ENTRIES) % MAX_ENTRIES;
    e = hist[hist_head];
    hist[hist_head] = NULL;
    hist_count--;
    hist_bytes -= e->size;
    p = (const uint8_t *)(e + 1);
    for (i = 0; i < e->npages; i++) {
        uint32_t g;
        memcpy(&g, p, 4);
        p += 4;
        p += unpack_xor(mirror + (size_t)g * PAGE, p);  // mirror now holds the older page
    }
    free(e);
    restore(mirror);
    return 1;
}

// Netplay's per-second check: a fast hash (64-bit words) of the whole state.
uint64_t state_quick_hash(void) {
    uint64_t h = 0xcbf29ce484222325ull;
    uint32_t i;
    if (!cur) state_init();
    machine_state_save(cur);
    for (i = 0; i + 8 <= blob_size; i += 8) {
        uint64_t w;
        memcpy(&w, cur + i, 8);
        h = (h ^ w) * 0x100000001b3ull;
    }
    return h;
}
