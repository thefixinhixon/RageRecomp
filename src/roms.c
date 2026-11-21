// MAME ROM loading. See roms.h.
#include "roms.h"
#include "machine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "third_party/miniz/miniz.h"
#include "third_party/lzma/7z.h"
#include "third_party/lzma/7zAlloc.h"
#include "third_party/lzma/7zCrc.h"
#include "third_party/lzma/7zFile.h"

// One file inside an archive, as the archive's directory lists it.
typedef struct {
    char name[256];     // bare name (no folders)
    uint32_t crc;
    uint64_t size;
    uint32_t idx;       // the archive's own index for it
} entry_t;

// A MAME set as a .zip (miniz) or a .7z (LZMA SDK), told apart by content.
typedef struct {
    int is7z;
    entry_t *ents;
    int nents;
    mz_zip_archive zip;
    CFileInStream in7;
    CLookToRead2 look;
    CSzArEx db;
    UInt32 block;       // SzArEx_Extract's cache: the last solid block
    Byte *out;
    size_t outsize;
} arc_t;

struct roms {
    arc_t arcs[8];
    char paths[8][512];
    int narcs;
    char problems[64][256];
    int nproblems;
    int missing;
};

static const ISzAlloc lzma_alloc = { SzAlloc, SzFree };

static uint32_t crc32_of(const uint8_t *data, uint32_t len) {
    return (uint32_t)mz_crc32(MZ_CRC32_INIT, data, (size_t)len);
}

static const char *bare(const char *path) {
    const char *s = strrchr(path, '/');
    const char *b = strrchr(path, '\\');
    const char *b2 = (s && b) ? (s > b ? s : b) : (s ? s : b);
    return b2 ? b2 + 1 : path;
}

static int is_7z(const char *path) {
    static const uint8_t sig[6] = { '7', 'z', 0xbc, 0xaf, 0x27, 0x1c };
    uint8_t b[6];
    FILE *f = fopen(path, "rb");
    size_t n = f ? fread(b, 1, 6, f) : 0;
    if (f) fclose(f);
    return n == 6 && !memcmp(b, sig, 6);
}

static int open_zip(arc_t *a, const char *path) {
    mz_uint i, n;
    memset(&a->zip, 0, sizeof(a->zip));
    if (!mz_zip_reader_init_file(&a->zip, path, 0)) return 0;
    n = mz_zip_reader_get_num_files(&a->zip);
    a->ents = (entry_t *)calloc(n ? n : 1, sizeof(entry_t));
    for (i = 0; a->ents && i < n; i++) {
        mz_zip_archive_file_stat st;
        entry_t *e = &a->ents[a->nents];
        if (!mz_zip_reader_file_stat(&a->zip, i, &st) || st.m_is_directory) continue;
        snprintf(e->name, sizeof(e->name), "%s", bare(st.m_filename));
        e->crc = st.m_crc32;
        e->size = st.m_uncomp_size;
        e->idx = i;
        a->nents++;
    }
    return 1;
}

static int open_7z(arc_t *a, const char *path) {
    static int crc_ready;
    UInt32 i;
    if (!crc_ready) { CrcGenerateTable(); crc_ready = 1; }
    if (InFile_Open(&a->in7.file, path)) return 0;
    FileInStream_CreateVTable(&a->in7);
    LookToRead2_CreateVTable(&a->look, False);
    a->look.buf = (Byte *)ISzAlloc_Alloc(&lzma_alloc, (size_t)1 << 18);
    if (!a->look.buf) { File_Close(&a->in7.file); return 0; }
    a->look.bufSize = (size_t)1 << 18;
    a->look.realStream = &a->in7.vt;
    LookToRead2_INIT(&a->look)
    SzArEx_Init(&a->db);
    if (SzArEx_Open(&a->db, &a->look.vt, &lzma_alloc, &lzma_alloc) != SZ_OK) {
        SzArEx_Free(&a->db, &lzma_alloc);
        ISzAlloc_Free(&lzma_alloc, a->look.buf);
        File_Close(&a->in7.file);
        return 0;
    }
    a->is7z = 1;
    a->block = 0xffffffffu;
    a->ents = (entry_t *)calloc(a->db.NumFiles ? a->db.NumFiles : 1, sizeof(entry_t));
    for (i = 0; a->ents && i < a->db.NumFiles; i++) {
        UInt16 name16[256];
        entry_t *e = &a->ents[a->nents];
        size_t k, len = SzArEx_GetFileNameUtf16(&a->db, i, NULL);
        if (SzArEx_IsDir(&a->db, i) || len > 256) continue;
        SzArEx_GetFileNameUtf16(&a->db, i, name16);
        for (k = 0; k + 1 < len && k + 1 < sizeof(e->name); k++)
            e->name[k] = name16[k] < 0x80 ? (char)name16[k] : '?';
        e->name[k] = 0;
        memmove(e->name, bare(e->name), strlen(bare(e->name)) + 1);
        e->crc = SzBitWithVals_Check(&a->db.CRCs, i) ? a->db.CRCs.Vals[i] : 0;
        e->size = SzArEx_GetFileSize(&a->db, i);
        e->idx = i;
        a->nents++;
    }
    return 1;
}

static void close_arc(arc_t *a) {
    if (a->is7z) {
        ISzAlloc_Free(&lzma_alloc, a->out);
        SzArEx_Free(&a->db, &lzma_alloc);
        ISzAlloc_Free(&lzma_alloc, a->look.buf);
        File_Close(&a->in7.file);
    } else {
        mz_zip_end(&a->zip);
    }
    free(a->ents);
}

// The file's data (malloc'd), or NULL.
static void *extract(arc_t *a, const entry_t *e, size_t *len) {
    size_t off = 0, got = 0;
    void *data;
    *len = 0;
    if (!a->is7z)
        return mz_zip_reader_extract_to_heap(&a->zip, e->idx, len, 0);
    if (SzArEx_Extract(&a->db, &a->look.vt, e->idx, &a->block, &a->out,
            &a->outsize, &off, &got, &lzma_alloc, &lzma_alloc) != SZ_OK)
        return NULL;
    data = malloc(got ? got : 1);
    if (data) {
        memcpy(data, a->out + off, got);
        *len = got;
    }
    return data;
}

roms_t *roms_open(char const *const *zips, int nzips) {
    roms_t *r = (roms_t *)calloc(1, sizeof(*r));
    int i;
    if (!r) return NULL;
    if (nzips > 8) nzips = 8;
    for (i = 0; i < nzips; i++) {
        size_t n = strlen(zips[i]);
        arc_t *a = &r->arcs[r->narcs];
        if (n >= sizeof(r->paths[i])) continue;
        memcpy(r->paths[i], zips[i], n + 1);
        memset(a, 0, sizeof(*a));
        if (is_7z(zips[i]) ? open_7z(a, zips[i]) : open_zip(a, zips[i]))
            r->narcs++;
    }
    return r;
}

void roms_close(roms_t *r) {
    int i;
    if (!r) return;
    for (i = 0; i < r->narcs; i++) close_arc(&r->arcs[i]);
    free(r);
}

// Where a file is: by name with the right CRC, else by CRC and length (the
// way MAME finds a merged set's shared files, stored once under the
// parent's name), else by name alone (a bad dump, reported). Returns 1
// with *ai/*ei set.
static int roms_find(roms_t *r, const rom_file_t *rf, int *ai_out, int *ei_out) {
    int ai, ei, pass;
    for (pass = 0; pass < 3; pass++) {
        for (ai = 0; ai < r->narcs; ai++) {
            for (ei = 0; ei < r->arcs[ai].nents; ei++) {
                const entry_t *e = &r->arcs[ai].ents[ei];
                int by_name = !strcmp(e->name, rf->name);
                int by_crc = rf->crc && e->crc == rf->crc && e->size == rf->length;
                if (pass == 0 ? by_name && (by_crc || !rf->crc) :
                        pass == 1 ? by_crc : by_name) {
                    *ai_out = ai;
                    *ei_out = ei;
                    return 1;
                }
            }
        }
    }
    return 0;
}

uint8_t *roms_read(roms_t *r, const rom_file_t *rf, uint32_t *out_len) {
    int ai, ei;
    char name[256];
    size_t uncomp = 0;
    void *data = NULL;
    *out_len = 0;
    if (roms_find(r, rf, &ai, &ei))
        data = extract(&r->arcs[ai], &r->arcs[ai].ents[ei], &uncomp);
    snprintf(name, sizeof(name), "%s", rf->name);
    if (!data) {
        if (r->nproblems < 64) {
            snprintf(r->problems[r->nproblems++], 256,
                    "missing %-34s (want %s, CRC %08X, %u bytes)", name,
                    rf->zip, rf->crc, rf->length);
        }
        r->missing = 1;
        return NULL;
    }
    *out_len = (uint32_t)uncomp;
    if (uncomp != rf->length && r->nproblems < 64) {
        snprintf(r->problems[r->nproblems++], 256,
                "length %-35s got %u want %u", name, (uint32_t)uncomp,
                rf->length);
    }
    if (rf->crc && crc32_of((uint8_t *)data, (uint32_t)uncomp) != rf->crc &&
            r->nproblems < 64) {
        snprintf(r->problems[r->nproblems++], 256,
                "CRC    %-35s got %08X want %08X", name,
                crc32_of((uint8_t *)data, (uint32_t)uncomp), rf->crc);
    }
    return (uint8_t *)data;
}

// Is the file in any of the zips (by name, or by CRC and length)? No
// reading or CRC check of the data.
static int roms_has(roms_t *r, const rom_file_t *rf) {
    int ai, ei;
    return roms_find(r, rf, &ai, &ei);
}

int roms_has_set(roms_t *r, const game_profile_t *p) {
    int i;
    for (i = 0; i < 4; i++)
        if (!roms_has(r, &p->prog[i])) return 0;
    for (i = 0; i < 3; i++)
        if (!roms_has(r, &p->tiles[i])) return 0;
    for (i = 0; i < p->rle_count; i++)
        if (!roms_has(r, &p->rle[i])) return 0;
    return roms_has(r, &p->dspboot) && roms_has(r, &p->dspsnd[0]) &&
            roms_has(r, &p->dspsnd[1]) && roms_has(r, &p->chars);
}

int roms_failed(roms_t *r) {
    return r->missing || r->nproblems > 0;
}

void roms_report(roms_t *r, char *buf, size_t cap) {
    size_t used = 0;
    int i;
    for (i = 0; i < r->nproblems && used + 1 < cap; i++) {
        int n = snprintf(buf + used, cap - used, "%s\n", r->problems[i]);
        if (n < 0) break;
        used += (size_t)n;
    }
    if (used >= cap && cap > 0) buf[cap - 1] = 0;
}

// Assemble one set. Images are calloc'd here (MAME zero-fills regions).
int roms_load_set(roms_t *r, const game_profile_t *p) {
    int i, failed = 0;
    uint32_t len;
    uint8_t *f;
    // Program ROM: 4 x 512 KB byte lanes.
    mach.rom = (uint8_t *)calloc(1, 0x200000u);
    for (i = 0; i < 4; i++) {
        uint32_t j;
        f = roms_read(r, &p->prog[i], &len);
        if (!f || len < 0x80000u) {
            failed = 1;
            free(f);
            continue;
        }
        for (j = 0; j < 0x80000u; j++)
            mach.rom[j * 4u + (uint32_t)i] = f[j];
        free(f);
    }
    mach.ram = (uint8_t *)calloc(1, RR_RAM_SIZE);
    mach.bank = (uint8_t *)calloc(1, RR_BANK_SIZE);
    mach.colorram = (uint16_t *)calloc(RR_CR_SIZE / 2, 2);
    // Tiles: 3 x 512 KB planes at 1 MB strides (gaps read 0, as in MAME).
    mach.tiles = (uint8_t *)calloc(1, 0x300000u);
    for (i = 0; i < 3; i++) {
        f = roms_read(r, &p->tiles[i], &len);
        if (!f || len < 0x80000u) {
            failed = 1;
            free(f);
            continue;
        }
        memcpy(mach.tiles + (uint32_t)i * 0x100000u, f, 0x80000u);
        free(f);
    }
    // Alpha tiles: 128 KB direct.
    mach.chars = (uint8_t *)calloc(1, 0x20000u);
    f = roms_read(r, &p->chars, &len);
    if (!f || len < 0x20000u) {
        failed = 1;
        free(f);
    } else {
        memcpy(mach.chars, f, 0x20000u);
        free(f);
    }
    // RLE ROMs: even/odd byte lanes (moh/mol) into a 32 MB image.
    mach.rle = (uint8_t *)calloc(1, 0x2000000u);
    for (i = 0; i < p->rle_count; i += 2) {
        // Pairs are (mol @ odd, moh @ even) sharing a base stride.
        uint8_t *flo, *fhi;
        uint32_t llen, hlen, j, base;
        uint32_t pair = (uint32_t)i / 2;
        // First pair is 512 KB at 0; the rest are 1 MB at 0x800000+.
        base = (pair == 0) ? 0u : 0x800000u + (pair - 1) * 0x200000u;
        flo = roms_read(r, &p->rle[i], &llen);
        fhi = (i + 1 < p->rle_count) ? roms_read(r, &p->rle[i + 1], &hlen)
                                     : NULL;
        if (!flo || !fhi) {
            failed = 1;
            free(flo);
            free(fhi);
            continue;
        }
        {
            uint32_t n = (llen < hlen) ? llen : hlen;
            if (base + n * 2 > 0x2000000u) n = (0x2000000u - base) / 2;
            for (j = 0; j < n; j++) {
                mach.rle[base + j * 2] = fhi[j];
                mach.rle[base + j * 2 + 1] = flo[j];
            }
        }
        free(flo);
        free(fhi);
    }
    // CAGE DSP boot ROM: one byte per word (MAME's ROM_LOAD32_BYTE).
    cage.boot = (uint32_t *)calloc(RR_DSP_BOOT_WORDS, 4);
    f = roms_read(r, &p->dspboot, &len);
    if (!f || len < 0x80000u) {
        failed = 1;
        free(f);
    } else {
        uint32_t j;
        for (j = 0; j < 0x80000u; j++) cage.boot[j] = f[j];
        free(f);
    }
    // CAGE sound ROMs: 16-bit halves (LE pairing) at DSP 0xD00000+.
    cage.srom = (uint32_t *)calloc(RR_DSP_ROM_WORDS, 4);
    {
        uint8_t *s0, *s1;
        uint32_t l0, l1, j;
        s0 = roms_read(r, &p->dspsnd[0], &l0);
        s1 = roms_read(r, &p->dspsnd[1], &l1);
        if (!s0 || !s1 || l0 < 0x200000u || l1 < 0x200000u) {
            failed = 1;
            free(s0);
            free(s1);
        } else {
            for (j = 0; j < 0x100000u; j++) {
                uint32_t lo = (uint32_t)s0[j * 2] |
                        ((uint32_t)s0[j * 2 + 1] << 8);
                uint32_t hi = (uint32_t)s1[j * 2] |
                        ((uint32_t)s1[j * 2 + 1] << 8);
                cage.srom[0x100000u + j] = lo | (hi << 16);
            }
            free(s0);
            free(s1);
        }
    }
    cage.ram = (uint32_t *)calloc(RR_DSP_RAM_WORDS, 4);
    cage.iram = (uint32_t *)calloc(0x800u, 4);
    // Internal C31 boot ROM (c31boot.bin): player's own file, searched in
    // every opened zip (MAME also accepts it inside the game zip).
    {
        rom_file_t bi = { "tms32031.zip or tms320c31.zip", "c31boot.bin",
                0xbddc2763u, 16384u };
        uint8_t *b;
        uint32_t bl, j;
        cage.bootint = (uint32_t *)calloc(0x1000u, 4);
        b = roms_read(r, &bi, &bl);
        if (!b || bl < 16384u) {
            // External TI boot ROM is optional: warn and run silent.
            fprintf(stderr, "ragerecomp: c31boot.bin missing, "
                    "DSP audio disabled\n");
            free(b);
        } else {
            for (j = 0; j < 0x1000u; j++)
                cage.bootint[j] = (uint32_t)b[j * 4] |
                        ((uint32_t)b[j * 4 + 1] << 8) |
                        ((uint32_t)b[j * 4 + 2] << 16) |
                        ((uint32_t)b[j * 4 + 3] << 24);
            free(b);
        }
    }
    return failed;
}
