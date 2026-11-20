// The game's operator settings in the EEPROM. See adjust.h.
//
// The game keeps its settings as numbered values packed into a block of
// nibbles (a descriptor table in the program gives each value's nibble
// position and length). That block is one EEPROM record, stored twice and
// protected by the game's error-correcting code. Coin options (value 0x28,
// 0x26 on v2.0) and game options (0x29, 0x27) are bit fields as the test
// menu's own tables describe them: game options round time bits 0-3,
// difficulty 4-7, gore 8; coin options game cost bits 11-13, free play 15.
#include "adjust.h"
#include "eeprom.h"
#include "machine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint8_t coin; uint8_t shift, width, choices; } field_t;
static const field_t fields[ADJ_COUNT] = {
    [ADJ_DIFFICULTY] = { 0, 4, 4, 16 },
    [ADJ_ROUND_TIME] = { 0, 0, 4, 11 },
    [ADJ_FREE_PLAY]  = { 1, 15, 1, 2 },
    [ADJ_GAME_COST]  = { 1, 11, 3, 8 },
    [ADJ_GORE]       = { 0, 8, 1, 2 },
};

static int pending[ADJ_COUNT] = { -1, -1, -1, -1, -1 };
static int running[ADJ_COUNT] = { -1, -1, -1, -1, -1 };

int adjust_codeword_len(int len)
{
    // As the game's driver grows it: one parity byte per power of two.
    int n = len, p = 1;
    for (;;) {
        n++;
        if (n <= p) return n;
        p *= 2;
    }
}

static bool pow2_or_zero(int i) { return (i & (i - 1)) == 0; }

bool adjust_record_read(const uint8_t *code, int len, uint8_t *data)
{
    int n = adjust_codeword_len(len), j = 0;
    uint8_t acc[8] = { 0 };
    for (int i = 0; i < n; i++) {
        acc[0] ^= code[i];
        for (int k = 0; k < 8; k++)
            if (i >> k & 1) acc[k] ^= code[i];
        if (!pow2_or_zero(i) && j < len) data[j++] = code[i];
    }
    for (int k = 0; k < 8; k++)
        if (acc[k]) return false;       // the game would correct or reject it
    return true;
}

void adjust_record_write(uint8_t *code, int len, const uint8_t *data)
{
    int n = adjust_codeword_len(len), j = 0;
    memset(code, 0, (size_t)n);
    for (int i = 0; i < n; i++)
        if (!pow2_or_zero(i)) code[i] = j < len ? data[j++] : 0;
    for (int k = 0; (1 << k) < n; k++) {
        uint8_t p = 0;
        for (int i = 0; i < n; i++)
            if ((i >> k & 1) && i != (1 << k)) p ^= code[i];
        code[1 << k] = p;
    }
    uint8_t all = 0;
    for (int i = 1; i < n; i++) all ^= code[i];
    code[0] = all;
}

// The nibbles of a value as the game reads them, most significant first:
// a value of `cnt` nibbles ends at nibble pos+cnt, an odd end starting with
// that byte's low nibble, then whole bytes downwards, then a high nibble.
static uint32_t nib_get(const uint8_t *b, int pos, int cnt)
{
    int end = pos + cnt, at = end >> 1;
    uint32_t v = 0;
    if (end & 1) { v = b[at] & 0xfu; cnt--; }
    while (cnt > 0) {
        if (cnt == 1) { v = v << 4 | b[at - 1] >> 4; break; }
        v = v << 8 | b[--at];
        cnt -= 2;
    }
    return v;
}

static void nib_set(uint8_t *b, int pos, int cnt, uint32_t v)
{
    int end = pos + cnt, at = end >> 1, bits = cnt * 4;
    if (end & 1) { bits -= 4; b[at] = (uint8_t)((b[at] & 0xf0u) | (v >> bits & 0xfu)); cnt--; }
    while (cnt > 0) {
        if (cnt == 1) { bits -= 4; b[at - 1] = (uint8_t)((b[at - 1] & 0x0fu) | (v >> bits & 0xfu) << 4); break; }
        bits -= 8;
        b[--at] = (uint8_t)(v >> bits);
        cnt -= 2;
    }
}

// The settings record (from whichever copy is intact).
static bool record_load(uint8_t *data)
{
    const game_profile_t *p = mach_profile;
    if (!p || !p->opt_len) return false;
    if (adjust_record_read(eeprom.data, p->opt_len, data)) return true;
    return adjust_record_read(eeprom.data + p->opt_copy2, p->opt_len, data);
}

static int field_of(const uint8_t *data, adjust_id_t id)
{
    const game_profile_t *p = mach_profile;
    const field_t *f = &fields[id];
    uint32_t w = f->coin ? nib_get(data, p->coin_nibble, 4) : nib_get(data, p->game_nibble, 8);
    int v = (int)(w >> f->shift & ((1u << f->width) - 1));
    return v < f->choices ? v : -1;
}

static int stored(adjust_id_t id)
{
    uint8_t data[256];
    return record_load(data) ? field_of(data, id) : -1;
}

int adjust_get(adjust_id_t id)
{
    if (id >= ADJ_COUNT) return -1;
    return pending[id] >= 0 ? pending[id] : stored(id);
}

int adjust_running(adjust_id_t id) { return id < ADJ_COUNT ? running[id] : -1; }

int adjust_choices(adjust_id_t id) { return id < ADJ_COUNT ? fields[id].choices : 0; }

bool adjust_set(adjust_id_t id, int value)
{
    if (id >= ADJ_COUNT || value < 0 || value >= fields[id].choices || stored(id) < 0) return false;
    pending[id] = value == stored(id) ? -1 : value;
    return true;
}

bool adjust_pending(void)
{
    for (int i = 0; i < ADJ_COUNT; i++)
        if (adjust_get((adjust_id_t)i) != running[i]) return true;
    return false;
}

void adjust_apply(void)
{
    const game_profile_t *p = mach_profile;
    uint8_t data[256];
    bool any = false;
    for (int i = 0; i < ADJ_COUNT; i++) any |= pending[i] >= 0;
    if (!any || !record_load(data)) return;
    uint32_t coin = nib_get(data, p->coin_nibble, 4), game = nib_get(data, p->game_nibble, 8);
    for (int i = 0; i < ADJ_COUNT; i++) {
        if (pending[i] < 0) continue;
        const field_t *f = &fields[i];
        uint32_t mask = ((1u << f->width) - 1) << f->shift, v = (uint32_t)pending[i] << f->shift;
        if (f->coin) coin = (coin & ~mask) | v;
        else game = (game & ~mask) | v;
        pending[i] = -1;
    }
    nib_set(data, p->coin_nibble, 4, coin);
    nib_set(data, p->game_nibble, 8, game);
    adjust_record_write(eeprom.data, p->opt_len, data);         // both copies, as the game keeps them
    adjust_record_write(eeprom.data + p->opt_copy2, p->opt_len, data);
}

void adjust_powered_on(void)
{
    for (int i = 0; i < ADJ_COUNT; i++) running[i] = stored((adjust_id_t)i);
}

const char *adjust_text(adjust_id_t id, int v)
{
    static char buf[32];
    if (v < 0) return "(set on first boot)";
    switch (id) {
    case ADJ_DIFFICULTY:
        snprintf(buf, sizeof buf, "%d%s", v + 1, v == 0 ? " (easiest)" : v == 6 ? " (medium)" : v == 15 ? " (hardest)" : "");
        break;
    case ADJ_ROUND_TIME: snprintf(buf, sizeof buf, "%d seconds", 30 + 5 * v); break;
    case ADJ_FREE_PLAY: return v ? "On" : "Off";
    case ADJ_GAME_COST: snprintf(buf, sizeof buf, "%d coin%s 1 credit", v + 1, v ? "s" : ""); break;
    case ADJ_GORE: return v ? "No gore" : "Full gore";
    default: return "?";
    }
    return buf;
}

bool adjust_parse(const char *spec)
{
    static const char *const names[ADJ_COUNT] = { "difficulty", "round_time", "free_play", "game_cost", "gore" };
    char buf[256];
    snprintf(buf, sizeof buf, "%s", spec);
    for (char *tok = strtok(buf, ","); tok; tok = strtok(NULL, ",")) {
        char *eq = strchr(tok, '=');
        if (!eq) return false;
        *eq = 0;
        int i, n = atoi(eq + 1);
        for (i = 0; i < ADJ_COUNT && strcmp(tok, names[i]); i++) {}
        if (i == ADJ_COUNT) return false;
        int v = i == ADJ_DIFFICULTY || i == ADJ_GAME_COST ? n - 1 : i == ADJ_ROUND_TIME ? (n - 30) / 5 : n;
        if (!adjust_set((adjust_id_t)i, v) && stored((adjust_id_t)i) != v) return false;
    }
    adjust_apply();
    return true;
}
