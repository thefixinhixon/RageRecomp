// Atari GT video for Primal Rage. See gtvid.h and MAME's atarigt_v.cpp
// (NOTICE). Per-line playfield scrolls are captured during the frame and
// the whole picture is composited once at vblank.
#include "gtvid.h"
#include "machine.h"
#include <stdlib.h>
#include <string.h>

uint8_t *gt_pf = NULL;
uint8_t *gt_alpha = NULL;

// 0x8000 RGB888 pens (XRGB).
static uint32_t gt_pal[0x8000];
// Per-line playfield state captured by gt_scanline.
static uint16_t gt_xs[240], gt_ys[240];
static uint8_t gt_cb[240], gt_tb[240];
// Current values (persist across frames, as in MAME).
static uint16_t cur_xs = 0, cur_ys = 0;
static uint8_t cur_cb = 0, cur_tb = 0;
// RLE motion-object state.
#define MO_VRAM_W 336u
#define MO_VRAM_H 240u
static uint16_t mo_vram[2][2][MO_VRAM_W * MO_VRAM_H];
static uint8_t mo_control_bits = 0;
static uint8_t mo_command = 1; // COMMAND_DRAW
static int mo_partial = -1;
static uint16_t mo_checksums[256];

typedef struct {
    int16_t width, height, xoffs, yoffs;
    uint8_t bpp;
    const uint16_t *table;
    const uint16_t *data;
} mo_info_t;

static mo_info_t *mo_info = NULL;
static int mo_objectcount = 0;
static uint16_t rle_tab[8][256];

static uint32_t readbit(const uint8_t *src, uint32_t bit) {
    return src[bit >> 3] & (0x80u >> (bit & 7));
}

static void build_rle_tables(void);
static void mo_prescan_all(void);

void gtvid_decode(void) {
    // Playfield: 62500 8x8 blended 6-bit pixels + 4096 4-bit alpha pixels.
    uint32_t total = GT_PF_TILES, c;
    uint32_t x, y, p;
    static const uint32_t pf_plane[5] = { 0, 0, 1, 2, 3 };
    static const uint32_t pf_xoff[8] = { 0x800000u + 0, 0x800000u + 4, 0,
        4, 0x800000u + 8, 0x800000u + 12, 8, 12 };
    static const uint32_t ov_plane[6] = { 0x1000000u + 0, 0x1000000u + 4,
        0, 0, 0, 0 };
    static const uint32_t ov_xoff[8] = { 3, 2, 1, 0, 11, 10, 9, 8 };
    gtvid_free();
    gt_pf = (uint8_t *)calloc((size_t)total * 64, 1);
    gt_alpha = (uint8_t *)calloc((size_t)GT_ALPHA_TILES * 64, 1);
    if (!mach.tiles || !mach.chars) return;
    for (c = 0; c < total; c++) {
        uint8_t *dst = gt_pf + (size_t)c * 64;
        for (y = 0; y < 8; y++) {
            for (x = 0; x < 8; x++) {
                uint32_t v0 = 0, v2 = 0;
                uint32_t base = c * 128u + y * 16u;
                for (p = 0; p < 5; p++) {
                    if (readbit(mach.tiles,
                            base + pf_plane[p] + pf_xoff[x]))
                        v0 |= 0x10u >> p;
                }
                for (p = 0; p < 6; p++) {
                    if (readbit(mach.tiles,
                            base + ov_plane[p] + ov_xoff[x]))
                        v2 |= 0x20u >> p;
                }
                dst[y * 8 + x] = (uint8_t)((v0 & 0x0fu) | (v2 & 0x30u));
            }
        }
    }
    for (c = 0; c < GT_ALPHA_TILES; c++) {
        uint8_t *dst = gt_alpha + (size_t)c * 64;
        const uint8_t *src = mach.chars + (size_t)c * 32;
        for (y = 0; y < 8; y++) {
            for (x = 0; x < 8; x++) {
                uint8_t b = src[y * 4 + (x >> 1)];
                dst[y * 8 + x] = (x & 1) ? (b & 0x0fu) : (b >> 4);
            }
        }
    }
    gtvid_mo_init();
}

void gtvid_free(void) {
    free(gt_pf);
    gt_pf = NULL;
    free(gt_alpha);
    gt_alpha = NULL;
    free(mo_info);
    mo_info = NULL;
    mo_objectcount = 0;
}

// Build RLE tables, prescan objects and checksums (call after ROM load,
// before the first frame; also redo in gtvid_decode's tail).
void gtvid_mo_init(void) {
    build_rle_tables();
    mo_prescan_all();
}

// ------------------------------------------------------------- colour RAM

uint16_t gt_colorram_r(uint32_t addr) {
    if (!mach.colorram) return 0;
    return mach.colorram[(addr & 0x7ffffu) >> 1];
}

void gt_colorram_w(uint32_t addr, uint16_t val) {
    uint32_t w;
    if (!mach.colorram) return;
    w = (addr & 0x7ffffu) >> 1;
    mach.colorram[w] = val;
    if (w >= 0x20000u && w < 0x28000u) {
        uint32_t pen = w & 0x7fffu;
        gt_pal[pen] = (gt_pal[pen] & 0x0000ffu) |
                ((uint32_t)(val >> 8) << 16) |
                ((uint32_t)(val & 0xffu) << 8);
    } else if (w >= 0x30000u && w < 0x38000u) {
        uint32_t pen = w & 0x7fffu;
        gt_pal[pen] = (gt_pal[pen] & 0xffff00u) | (val & 0xffu);
    }
}

// ------------------------------------------------------------- scanlines

static uint16_t alpha_word(uint32_t index) {
    if (!mach.bank) return 0;
    index &= 0x7ffu; // 2K words of alpha RAM at 0xD76000
    return (uint16_t)(mach.bank[0x6000u + index * 2] << 8) |
            mach.bank[0x6000u + index * 2 + 1];
}

void gt_scanline(int line) {
    int offset = ((line & ~7) << 3) + 48, i;
    if (offset >= 0x800) return;
    for (i = 0; i < 8; i++) {
        uint16_t word = alpha_word((uint32_t)offset++);
        if (word & 0x8000u) {
            cur_xs = (uint16_t)((word >> 5) & 0x3ffu);
            cur_cb = (uint8_t)(word & 0x1fu);
        }
        word = alpha_word((uint32_t)offset++);
        if (word & 0x8000u) {
            cur_ys = (uint16_t)(((word >> 6) - (uint32_t)(line + i)) &
                    0x1ffu);
            cur_tb = (uint8_t)(word & 15u);
        }
        // Bands past the visible area still advance the current values.
        if (line + i < 240) {
            gt_xs[line + i] = cur_xs;
            gt_ys[line + i] = cur_ys;
            gt_cb[line + i] = cur_cb;
            gt_tb[line + i] = cur_tb;
        }
    }
}

// ------------------------------------------------------------- RLE tables

static void build_rle_tables(void) {
    int i;
    for (i = 0; i < 256; i++) // 4bpp
        rle_tab[0][i] = (uint16_t)((((i & 0xf0) + 0x10) << 4) | (i & 0x0f));
    for (i = 0; i < 256; i++) { // special 5bpp
        if ((i & 0x0f) == 0)
            rle_tab[1][i] = (uint16_t)((((i & 0xf0) + 0x10) << 4) |
                    (i & 0x0f));
        else
            rle_tab[1][i] = (uint16_t)((((i & 0xe0) + 0x20) << 3) |
                    (i & 0x1f));
    }
    for (i = 0; i < 256; i++) // 5bpp
        rle_tab[2][i] = rle_tab[3][i] =
                (uint16_t)((((i & 0xe0) + 0x20) << 3) | (i & 0x1f));
    for (i = 0; i < 256; i++) { // special 6bpp
        uint16_t v;
        if ((i & 0x0f) == 0)
            v = (uint16_t)((((i & 0xf0) + 0x10) << 4) | (i & 0x0f));
        else
            v = (uint16_t)((((i & 0xc0) + 0x40) << 2) | (i & 0x3f));
        rle_tab[4][i] = rle_tab[6][i] = v;
    }
    for (i = 0; i < 256; i++) // 6bpp
        rle_tab[5][i] = rle_tab[7][i] =
                (uint16_t)((((i & 0xc0) + 0x40) << 2) | (i & 0x3f));
}

static uint16_t rle_word(uint32_t wi) {
    const uint8_t *r = mach.rle;
    uint32_t o = wi * 2;
    if (!r || o + 1 >= 0x2000000u) return 0;
    return (uint16_t)(r[o] << 8) | r[o + 1];
}

static void rle_word_xor(uint32_t wi, uint16_t v) {
    uint8_t *r = mach.rle;
    uint32_t o = wi * 2;
    uint16_t w;
    if (!r || o + 1 >= 0x2000000u) return;
    w = (uint16_t)(((uint16_t)r[o] << 8) | r[o + 1]) ^ v;
    r[o] = (uint8_t)(w >> 8);
    r[o + 1] = (uint8_t)w;
}

// Count objects and prescan them (once, at decode; mutates the image the
// same deterministic way MAME's prescan does).
static void mo_prescan_all(void) {
    uint32_t lowest = 0x1000000u, off;
    int n, i;
    if (!mach.rle) return;
    for (off = 0; off < lowest; off += 4) {
        uint32_t e = ((uint32_t)rle_word(off + 2) & 0xffu) << 16 |
                rle_word(off + 3);
        if (e > off && e < lowest) lowest = e;
    }
    n = (int)(lowest / 4);
    free(mo_info);
    mo_info = (mo_info_t *)calloc((size_t)(n ? n : 1), sizeof(mo_info_t));
    mo_objectcount = n;
    for (i = 0; i < n; i++) {
        mo_info_t *info = &mo_info[i];
        uint32_t b0 = (uint32_t)i * 4;
        uint32_t flags = rle_word(b0 + 2);
        uint32_t offset = ((flags & 0xffu) << 16) | rle_word(b0 + 3);
        uint32_t base = offset;
        int width = 0, height;
        info->xoffs = (int16_t)rle_word(b0);
        info->yoffs = (int16_t)rle_word(b0 + 1);
        info->table = rle_tab[(flags >> 8) & 7];
        info->bpp = (uint8_t)(((flags >> 8) & 7) == 0 ? 4 :
                (((flags >> 8) & 7) < 4 ? 5 : 6));
        if (offset < (uint32_t)i * 4 || offset >= 0x1000000u) {
            info->data = NULL;
            continue;
        }
        info->data = (const uint16_t *)(uintptr_t)offset; // word index
        for (height = 0; height < 1024; height++) {
            int tempwidth = 0, ec;
            if (base >= 0x1000000u) break;
            ec = rle_word(base++);
            if (ec & 0x8000) {
                ec ^= 0xffff;
                rle_word_xor(base - 1, 0xffffu);
            }
            if (!ec) break;
            while (ec-- > 0) {
                uint32_t wv;
                if (base >= 0x1000000u) break;
                wv = rle_word(base++);
                tempwidth += info->table[wv & 0xff] >> 8;
                tempwidth += info->table[wv >> 8] >> 8;
            }
            if (tempwidth > width) width = tempwidth;
        }
        info->width = (int16_t)width;
        info->height = (int16_t)height;
    }
    // Checksums over 0x20000-byte chunks (sums mod 0x10000).
    for (i = 0; i < 256; i++) {
        uint32_t sum = 0, w;
        for (w = 0; w < 0x10000u; w++)
            sum += rle_word((uint32_t)i * 0x10000u + w);
        mo_checksums[i] = (uint16_t)(sum & 0xffffu);
    }
}

// MO RAM access (u16 halves of bank[] at 0xD78000, MAME's "rle" share).
static uint16_t mo_ram_r(uint32_t wi) {
    uint32_t o = 0x8000u + wi * 2;
    if (!mach.bank || o + 1 >= 0x10000u) return 0;
    return (uint16_t)(mach.bank[o] << 8) | mach.bank[o + 1];
}

static void mo_ram_w(uint32_t wi, uint16_t val) {
    uint32_t o = 0x8000u + wi * 2;
    if (!mach.bank || o + 1 >= 0x10000u) return;
    mach.bank[o] = (uint8_t)(val >> 8);
    mach.bank[o + 1] = (uint8_t)val;
}

// Object field extraction (modesc masks).
static uint16_t mo_field(uint32_t entry, int which) {
    switch (which) {
    case 0: return mo_ram_r(entry) & 0x7fffu; // code
    case 1: return (mo_ram_r(entry + 1) & 0x0ff0u) >> 4; // color
    case 2: return (mo_ram_r(entry + 2) & 0xffc0u) >> 6; // xpos
    case 3: return (mo_ram_r(entry + 3) & 0xffc0u) >> 6; // ypos
    case 4: return mo_ram_r(entry + 4); // scale
    case 5: return (mo_ram_r(entry) & 0x8000u) >> 15; // hflip
    case 6: return mo_ram_r(entry + 6) & 0x00ffu; // order
    case 7: return (mo_ram_r(entry + 1) & 0x0e00u) >> 9; // priority
    default: return (mo_ram_r(entry + 1) & 0x8000u) >> 15; // vram
    }
}

static void mo_compute_checksum(void) {
    int reqsums = (int)mo_ram_r(0) + 1, i;
    if (reqsums > 256) reqsums = 256;
    for (i = 0; i < reqsums; i++) mo_ram_w((uint32_t)i, mo_checksums[i]);
}

// Draw one RLE object with zoom (normal orientation). Exact port of
// MAME's draw_rle_zoom (clip window 0,0,335,239).
static void mo_draw(uint16_t *bm, int code, uint32_t palette, int sx, int sy,
        int scalex, int scaley) {
    const mo_info_t *info = &mo_info[code];
    uint32_t data = (uint32_t)(uintptr_t)info->data;
    int scaled_w = (scalex * info->width + 0x7fff) >> 16;
    int scaled_h = (scaley * info->height + 0x7fff) >> 16;
    int dx, dy, ex, ey, sourcey, skip = 0, xclip = 0, row = 0;
    uint32_t row_start;
    int y;
    if (!scaled_w) scaled_w = 1;
    if (!scaled_h) scaled_h = 1;
    dx = (info->width << 16) / scaled_w;
    dy = (info->height << 16) / scaled_h;
    ex = sx + scaled_w - 1;
    ey = sy + scaled_h - 1;
    sourcey = dy / 2;
    if (sx < 0) {
        skip = -sx;
        xclip = 1;
    }
    if (sx > 335) return;
    if (ex > 335) {
        ex = 335;
        xclip = 1;
    } else if (ex < 0) return;
    if (sy < 0) {
        sourcey += -sy * dy;
        sy = 0;
    } else if (sy > 239) return;
    if (ey > 239) ey = 239;
    else if (ey < 0) return;
    row_start = data;
    for (y = sy; y <= ey; y++, sourcey += dy) {
        uint16_t *dest = bm + (uint32_t)y * MO_VRAM_W + (uint32_t)sx;
        int sourcex = dx / 2, rle_end = 0, entry, ec;
        uint32_t base;
        int target = sourcey >> 16;
        while (row != target) {
            row_start += 1 + rle_word(row_start);
            row++;
        }
        base = row_start;
        ec = rle_word(base++);
        if (!xclip) {
            for (entry = 0; entry < ec; entry++) {
                uint32_t wv = rle_word(base++);
                int count, value;
                count = info->table[wv & 0xff];
                value = count & 0xff;
                rle_end += (count & 0xff00) << 8;
                if (value) {
                    value |= (int)palette;
                    while (sourcex < rle_end)
                        *dest++ = (uint16_t)value, sourcex += dx;
                } else {
                    while (sourcex < rle_end)
                        dest++, sourcex += dx;
                }
                count = info->table[wv >> 8];
                value = count & 0xff;
                rle_end += (count & 0xff00) << 8;
                if (value) {
                    value |= (int)palette;
                    while (sourcex < rle_end)
                        *dest++ = (uint16_t)value, sourcex += dx;
                } else {
                    while (sourcex < rle_end)
                        dest++, sourcex += dx;
                }
            }
        } else {
            uint16_t *end = bm + (uint32_t)y * MO_VRAM_W + (uint32_t)ex;
            int skipped = skip;
            for (entry = 0; entry < ec && dest <= end; entry++) {
                uint32_t wv = rle_word(base++);
                int count = info->table[wv & 0xff];
                int value = count & 0xff;
                rle_end += (count & 0xff00) << 8;
                if (skipped) {
                    while (skipped && sourcex < rle_end)
                        dest++, sourcex += dx, skipped--;
                    if (skipped) goto mo_next3;
                }
                if (value) {
                    value |= (int)palette;
                    while (sourcex < rle_end && dest <= end)
                        *dest++ = (uint16_t)value, sourcex += dx;
                } else {
                    while (sourcex < rle_end)
                        dest++, sourcex += dx;
                }
            mo_next3:
                count = info->table[wv >> 8];
                value = count & 0xff;
                rle_end += (count & 0xff00) << 8;
                if (skipped) {
                    while (skipped && sourcex < rle_end)
                        dest++, sourcex += dx, skipped--;
                    if (skipped) goto mo_next4;
                }
                if (value) {
                    value |= (int)palette;
                    while (sourcex < rle_end && dest <= end)
                        *dest++ = (uint16_t)value, sourcex += dx;
                } else {
                    while (sourcex < rle_end)
                        dest++, sourcex += dx;
                }
            mo_next4:;
            }
        }
    }
}

// Draw one RLE object with zoom, horizontally flipped. Exact port of
// MAME's draw_rle_zoom_hflip (index-based dest for defined behaviour at
// the edges; identical loop structure otherwise).
static void mo_draw_hflip(uint16_t *bm, int code, uint32_t palette, int sx,
        int sy, int scalex, int scaley) {
    const mo_info_t *info = &mo_info[code];
    uint32_t data = (uint32_t)(uintptr_t)info->data;
    int scaled_w = (scalex * info->width + 0x7fff) >> 16;
    int scaled_h = (scaley * info->height + 0x7fff) >> 16;
    int dx, dy, ex, ey, sourcey, skip = 0, xclip = 0, row = 0;
    uint32_t row_start;
    int y;
    if (!scaled_w) scaled_w = 1;
    if (!scaled_h) scaled_h = 1;
    dx = (info->width << 16) / scaled_w;
    dy = (info->height << 16) / scaled_h;
    ex = sx + scaled_w - 1;
    ey = sy + scaled_h - 1;
    sourcey = dy / 2;
    if (sx < 0) {
        sx = 0;
        xclip = 1;
    }
    if (sx > 335) return;
    if (ex > 335) {
        skip = ex - 335;
        xclip = 1;
    } else if (ex < 0) return;
    if (sy < 0) {
        sourcey += -sy * dy;
        sy = 0;
    } else if (sy > 239) return;
    if (ey > 239) ey = 239;
    else if (ey < 0) return;
    row_start = data;
    for (y = sy; y <= ey; y++, sourcey += dy) {
        int di = ex, sourcex = dx / 2, rle_end = 0, entry, ec;
        uint32_t base = row_start;
        int target = sourcey >> 16;
        uint16_t *rowbm = bm + (uint32_t)y * MO_VRAM_W;
        while (row != target) {
            row_start += 1 + rle_word(row_start);
            row++;
        }
        base = row_start;
        ec = (int)rle_word(base++);
        if (!xclip) {
            for (entry = 0; entry < ec; entry++) {
                uint32_t wv = rle_word(base++);
                int count = info->table[wv & 0xff];
                int value = count & 0xff;
                rle_end += (count & 0xff00) << 8;
                if (value) {
                    value |= (int)palette;
                    while (sourcex < rle_end)
                        rowbm[di--] = (uint16_t)value, sourcex += dx;
                } else {
                    while (sourcex < rle_end)
                        di--, sourcex += dx;
                }
                count = info->table[wv >> 8];
                value = count & 0xff;
                rle_end += (count & 0xff00) << 8;
                if (value) {
                    value |= (int)palette;
                    while (sourcex < rle_end)
                        rowbm[di--] = (uint16_t)value, sourcex += dx;
                } else {
                    while (sourcex < rle_end)
                        di--, sourcex += dx;
                }
            }
        } else {
            int skipped = skip;
            for (entry = 0; entry < ec && di >= sx; entry++) {
                uint32_t wv = rle_word(base++);
                int count = info->table[wv & 0xff];
                int value = count & 0xff;
                rle_end += (count & 0xff00) << 8;
                if (skipped) {
                    while (skipped && sourcex < rle_end)
                        di--, sourcex += dx, skipped--;
                    if (skipped) goto mh_next3;
                }
                if (value) {
                    value |= (int)palette;
                    while (sourcex < rle_end && di >= sx)
                        rowbm[di--] = (uint16_t)value, sourcex += dx;
                } else {
                    while (sourcex < rle_end)
                        di--, sourcex += dx;
                }
            mh_next3:
                count = info->table[wv >> 8];
                value = count & 0xff;
                rle_end += (count & 0xff00) << 8;
                if (skipped) {
                    while (skipped && sourcex < rle_end)
                        di--, sourcex += dx, skipped--;
                    if (skipped) goto mh_next4;
                }
                if (value) {
                    value |= (int)palette;
                    while (sourcex < rle_end && di >= sx)
                        rowbm[di--] = (uint16_t)value, sourcex += dx;
                } else {
                    while (sourcex < rle_end)
                        di--, sourcex += dx;
                }
            mh_next4:;
            }
        }
    }
}

static void mo_render(void) {
    // Sort 256 objects by order (stable buckets, ascending, skipping 0).
    int head[256], next[256], o, order;
    int back = (~mo_control_bits & 4) >> 2;
    for (o = 0; o < 256; o++) head[o] = -1;
    for (o = 0; o < 256; o++) {
        int e = o * 8;
        int ord = mo_field((uint32_t)e, 6) & 0xff;
        next[o] = head[ord];
        head[ord] = o;
    }
    for (order = 1; order < 256; order++) {
        int cur;
        for (cur = head[order]; cur >= 0; cur = next[cur]) {
            int e = cur * 8;
            int scale = mo_field((uint32_t)e, 4);
            int code = mo_field((uint32_t)e, 0);
            if (scale > 0 && code < mo_objectcount && mo_info) {
                int hflip = mo_field((uint32_t)e, 5);
                int color = mo_field((uint32_t)e, 1);
                int pri = mo_field((uint32_t)e, 7);
                int x = mo_field((uint32_t)e, 2);
                int y = mo_field((uint32_t)e, 3);
                int which = mo_field((uint32_t)e, 8) & 1;
                const mo_info_t *info = &mo_info[code];
                int sxo, syo, palette;
                uint16_t *bm;
                if (!info->data) continue;
                if (x & 0x200) x |= ~0x3ff;
                if (y & 0x200) y |= ~0x3ff;
                sxo = (scale * info->xoffs) >> 12;
                syo = (scale * info->yoffs) >> 12;
                if (hflip)
                    sxo = ((scale * info->width) >> 12) - sxo;
                x -= sxo;
                y -= syo;
                palette = (color << 4) | (pri << 12);
                bm = mo_vram[which][back];
                if (!hflip)
                    mo_draw(bm, code, (uint32_t)palette, x, y,
                            scale << 4, scale << 4);
                else
                    mo_draw_hflip(bm, code, (uint32_t)palette, x, y,
                            scale << 4, scale << 4);
            }
        }
    }
}

static void mo_erase_lines(int top, int bottom, int frame) {
    int y;
    if (top < 0) top = 0;
    if (bottom > 239) bottom = 239;
    for (y = top; y <= bottom; y++) {
        memset(mo_vram[0][frame] + (uint32_t)y * MO_VRAM_W, 0,
                MO_VRAM_W * sizeof(uint16_t));
        memset(mo_vram[1][frame] + (uint32_t)y * MO_VRAM_W, 0,
                MO_VRAM_W * sizeof(uint16_t));
    }
}

void gt_mo_control(uint8_t bits) {
    int old = mo_control_bits;
    bits &= 7;
    // No early-out: like MAME, every write advances the erase frontier.
    if (old & 2) // CONTROL_ERASE was set: raster erase to here
        mo_erase_lines(mo_partial + 1, (int)mach.scanline,
                (old >> 2) & 1);
    mo_control_bits = bits;
    if (!(old & 1) && (bits & 1)) { // CONTROL_MOGO rising edge
        if (mo_command == 2) mo_compute_checksum();
        else mo_render();
    }
    mo_partial = (int)mach.scanline;
}

void gt_mo_command(uint16_t cmd) {
    mo_command = (cmd == 2) ? 2 : 1;
}

// Vblank hook: finish a pending erase, reset the partial line.
void gt_mo_vblank(void) {
    if (mo_control_bits & 2)
        mo_erase_lines(mo_partial + 1, 239, (mo_control_bits >> 2) & 1);
    mo_partial = -1;
}

// ------------------------------------------------------------- composite

static uint16_t pf_word(uint32_t col, uint32_t row) {
    // playfield_scan: ((((col&0x40)^0x40)<<6) | ((row&0x3F)<<6) | (col&0x3F))
    uint32_t idx = ((((col & 0x40u) ^ 0x40u) << 6) |
            ((row & 0x3fu) << 6) | (col & 0x3fu)) &
            0x1fffu;
    uint32_t o = 0x2000u + idx * 2;
    if (!mach.bank) return 0;
    return (uint16_t)(mach.bank[o] << 8) | mach.bank[o + 1];
}

void gt_render(void) {
    int x, y;
    uint8_t latch;
    const uint16_t *cram;
    const uint32_t *mram;
    const uint16_t *mo;
    int frame;
    if (!mach.frame_rgb || !gt_pf || !gt_alpha || !mach.colorram) return;
    latch = (uint8_t)(mach.colorram[0x18000u] & 0xffu);
    mach.color_latch = latch;
    cram = mach.colorram + (((uint32_t)latch & 0x08u) << 10);
    mram = gt_pal + (((uint32_t)latch & 0xc0u) << 7);
    frame = (mo_control_bits >> 2) & 1;
    mo = mo_vram[0][frame];
    for (y = 0; y < 240; y++) {
        uint32_t *dst = mach.frame_rgb + (uint32_t)y * 336u;
        uint16_t xs = gt_xs[y], ys = gt_ys[y];
        uint32_t cb = (uint32_t)gt_cb[y] << 8;
        uint32_t tb = (uint32_t)gt_tb[y] << 12;
        for (x = 0; x < 336; x++) {
            uint32_t pcol = ((uint32_t)x + xs) & 1023u;
            uint32_t prow = ((uint32_t)y + ys) & 511u;
            uint32_t tcol = pcol >> 3, trow = prow >> 3;
            uint16_t tw = pf_word(tcol, trow);
            uint32_t code = tb | (tw & 0xfffu);
            uint32_t color = (tw >> 12) & 7u;
            uint32_t flipx = (tw >> 15) & 1u;
            uint32_t px = flipx ? 7u - (pcol & 7u) : (pcol & 7u);
            uint32_t pen = gt_pf[(code & 0x7fffu) * 64u + (prow & 7u) * 8u +
                    px];
            uint32_t pf = pen + color * 32u + cb;
            uint32_t aw = alpha_word(((uint32_t)y >> 3) * 64u +
                    ((uint32_t)x >> 3));
            uint32_t acode = aw & 0xfffu, acolor = (aw >> 12) & 0xfu;
            uint32_t apen = gt_alpha[(acode & 0xfffu) * 64u +
                    ((uint32_t)y & 7u) * 8u + ((uint32_t)x & 7u)];
            uint32_t an = apen + acolor * 16u;
            uint16_t mov = mo[(uint32_t)y * 336u + (uint32_t)x];
            uint32_t pfpri = (pf >> 10) & 7u, mopri = mov >> 12;
            uint32_t mgep = (mopri >= pfpri) && !(pfpri & 4);
            uint32_t cra;
            uint32_t rgb;
            if (an & 0x8fu) {
                cra = an & 0xffu;
            } else if ((mov & 0x3fu) &&
                    ((mov & 0x800u) || mgep || !(pf & 0x3fu))) {
                cra = 0x1000u | (mov & 0x7ffu);
            } else {
                cra = pf & 0xfffu;
            }
            cra = cram[cra & 0x3fffu];
            rgb = (mram[(cra >> 10) & 0x1fu] & 0xff0000u) |
                    (mram[(cra >> 5) & 0x1fu] & 0x00ff00u) |
                    (mram[cra & 0x1fu] & 0x0000ffu);
            // GAL 22E lightning flash (MAME's exact two-stage effect).
            if (latch & 7u) {
                uint32_t lcr = latch & 7u;
                uint32_t far_fx = (lcr == 1) ? 1 : (lcr == 3) ? 2 : 0;
                uint32_t all_fx = (lcr == 2) ? 1 : (lcr >= 4) ? 2 : 0;
                // far_fx: only where the playfield is the visible source.
                if (far_fx && !(an & 0x8fu) &&
                        !((mov & 0x3fu) &&
                                ((mov & 0x800u) || mgep ||
                                        !(pf & 0x3fu)))) {
                    if (!(pf & 0x3fu) || !(pf & 0x2000u)) {
                        if (far_fx == 1) {
                            rgb = 0;
                        } else {
                            uint32_t r = ((rgb >> 16) & 0xffu) / 3 + 149;
                            uint32_t g = ((rgb >> 8) & 0xffu) / 3 + 163;
                            uint32_t b = (rgb & 0xffu) / 3 + 144;
                            rgb = (r << 16) | (g << 8) | b;
                        }
                    }
                }
                // all_fx: every pixel.
                if (all_fx) {
                    if (all_fx == 1) {
                        rgb = 0;
                    } else {
                        uint32_t r = ((rgb >> 16) & 0xffu) / 3 + 149;
                        uint32_t g = ((rgb >> 8) & 0xffu) / 3 + 163;
                        uint32_t b = (rgb & 0xffu) / 3 + 144;
                        rgb = (r << 16) | (g << 8) | b;
                    }
                }
            }
            dst[x] = rgb;
        }
    }
}


// ------------------------------------------------------------- save states

uint32_t gt_state_size(void) {
    return (uint32_t)(sizeof(gt_xs) + sizeof(gt_ys) + sizeof(gt_cb) +
            sizeof(gt_tb) + 8 + sizeof(mo_vram) + 4 + sizeof(gt_pal));
}

void gt_state_save(uint8_t *dst) {
    memcpy(dst, gt_xs, sizeof(gt_xs));
    dst += sizeof(gt_xs);
    memcpy(dst, gt_ys, sizeof(gt_ys));
    dst += sizeof(gt_ys);
    memcpy(dst, gt_cb, sizeof(gt_cb));
    dst += sizeof(gt_cb);
    memcpy(dst, gt_tb, sizeof(gt_tb));
    dst += sizeof(gt_tb);
    dst[0] = (uint8_t)(cur_xs >> 8);
    dst[1] = (uint8_t)cur_xs;
    dst[2] = (uint8_t)(cur_ys >> 8);
    dst[3] = (uint8_t)cur_ys;
    dst[4] = cur_cb;
    dst[5] = cur_tb;
    dst[6] = mo_control_bits;
    dst[7] = mo_command;
    dst += 8;
    memcpy(dst, mo_vram, sizeof(mo_vram));
    dst += sizeof(mo_vram);
    dst[0] = (uint8_t)((uint32_t)mo_partial >> 24);
    dst[1] = (uint8_t)((uint32_t)mo_partial >> 16);
    dst[2] = (uint8_t)((uint32_t)mo_partial >> 8);
    dst[3] = (uint8_t)(uint32_t)mo_partial;
    dst += 4;
    memcpy(dst, gt_pal, sizeof(gt_pal));
}

void gt_state_load(const uint8_t *src) {
    memcpy(gt_xs, src, sizeof(gt_xs));
    src += sizeof(gt_xs);
    memcpy(gt_ys, src, sizeof(gt_ys));
    src += sizeof(gt_ys);
    memcpy(gt_cb, src, sizeof(gt_cb));
    src += sizeof(gt_cb);
    memcpy(gt_tb, src, sizeof(gt_tb));
    src += sizeof(gt_tb);
    cur_xs = (uint16_t)((uint16_t)src[0] << 8 | src[1]);
    cur_ys = (uint16_t)((uint16_t)src[2] << 8 | src[3]);
    cur_cb = src[4];
    cur_tb = src[5];
    mo_control_bits = src[6];
    mo_command = src[7];
    src += 8;
    memcpy(mo_vram, src, sizeof(mo_vram));
    src += sizeof(mo_vram);
    mo_partial = (int)(((uint32_t)src[0] << 24) | ((uint32_t)src[1] << 16) |
            ((uint32_t)src[2] << 8) | src[3]);
    src += 4;
    memcpy(gt_pal, src, sizeof(gt_pal));
}
