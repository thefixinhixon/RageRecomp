// MAME ROM loading. See roms.h.
#include "roms.h"
#include "machine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "third_party/miniz/miniz.h"

struct roms {
    mz_zip_archive zips[8];
    char paths[8][512];
    int nzips;
    char problems[64][256];
    int nproblems;
    int missing;
};

static uint32_t crc32_of(const uint8_t *data, uint32_t len) {
    return (uint32_t)mz_crc32(MZ_CRC32_INIT, data, (size_t)len);
}

roms_t *roms_open(char const *const *zips, int nzips) {
    roms_t *r = (roms_t *)calloc(1, sizeof(*r));
    int i;
    if (!r) return NULL;
    if (nzips > 8) nzips = 8;
    for (i = 0; i < nzips; i++) {
        size_t n = strlen(zips[i]);
        if (n >= sizeof(r->paths[i])) continue;
        memcpy(r->paths[i], zips[i], n + 1);
        memset(&r->zips[r->nzips], 0, sizeof(mz_zip_archive));
        if (mz_zip_reader_init_file(&r->zips[r->nzips], zips[i], 0))
            r->nzips++;
    }
    return r;
}

void roms_close(roms_t *r) {
    int i;
    if (!r) return;
    for (i = 0; i < r->nzips; i++) mz_zip_end(&r->zips[i]);
    free(r);
}

static const char *bare(const char *path) {
    const char *s = strrchr(path, '/');
    const char *b = strrchr(path, '\\');
    const char *b2 = (s && b) ? (s > b ? s : b) : (s ? s : b);
    return b2 ? b2 + 1 : path;
}

uint8_t *roms_read(roms_t *r, const rom_file_t *rf, uint32_t *out_len) {
    int zi;
    char name[256];
    size_t uncomp = 0;
    void *data = NULL;
    *out_len = 0;
    for (zi = 0; zi < r->nzips; zi++) {
        mz_uint n = mz_zip_reader_get_num_files(&r->zips[zi]), fi;
        for (fi = 0; fi < n; fi++) {
            mz_zip_archive_file_stat st;
            if (!mz_zip_reader_file_stat(&r->zips[zi], fi, &st))
                continue;
            if (!strcmp(bare(st.m_filename), rf->name)) {
                data = mz_zip_reader_extract_to_heap(&r->zips[zi], fi,
                        &uncomp, 0);
                break;
            }
        }
        if (data) break;
    }
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

// Is a file of this name in any of the zips? (No reading or CRC check.)
static int roms_has(roms_t *r, const char *name) {
    int zi;
    for (zi = 0; zi < r->nzips; zi++) {
        mz_uint n = mz_zip_reader_get_num_files(&r->zips[zi]), fi;
        for (fi = 0; fi < n; fi++) {
            mz_zip_archive_file_stat st;
            if (mz_zip_reader_file_stat(&r->zips[zi], fi, &st) &&
                    !strcmp(bare(st.m_filename), name))
                return 1;
        }
    }
    return 0;
}

int roms_has_set(roms_t *r, const game_profile_t *p) {
    int i;
    for (i = 0; i < 4; i++)
        if (!roms_has(r, p->prog[i].name)) return 0;
    for (i = 0; i < 3; i++)
        if (!roms_has(r, p->tiles[i].name)) return 0;
    for (i = 0; i < p->rle_count; i++)
        if (!roms_has(r, p->rle[i].name)) return 0;
    return roms_has(r, p->dspboot.name) && roms_has(r, p->dspsnd[0].name) &&
            roms_has(r, p->dspsnd[1].name) && roms_has(r, p->chars.name);
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
        rom_file_t bi = { "tms32031", "c31boot.bin", 0xbddc2763u, 16384u };
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
