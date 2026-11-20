// Training mode (see training.h), adapted from UltraCombo's. RAM locations
// come from the game profile (game.h); each value is sanity-checked before
// use, and a version without them gets no training mode.
#include "training.h"
#include "config.h"
#include "machine.h"
#include "osd.h"
#include <string.h>

#define REFILL_DELAY 45         // frames without damage before health refills
#define KO 0x7800u              // damage at which a fighter is knocked out (120.0)
#define LOW_DAMAGE 0x6400u      // "low": 20 of 120 left

static uint8_t *ram_at(uint32_t a)
{
    a &= 0xffffffu;
    return a >= 0xf80000u ? &mach.ram[a - 0xf80000u] : NULL;
}

static uint32_t rd16(uint32_t a) { uint8_t *p = ram_at(a); return p ? (uint32_t)p[0] << 8 | p[1] : 0; }
static void wr16(uint32_t a, uint32_t v) { uint8_t *p = ram_at(a); if (p) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; } }

static uint32_t damage_addr(int p) { return mach_profile->fighters + (uint32_t)p * mach_profile->fighter_step + 0x5a; }

// A fight is on: the clock shows a plausible time.
static bool in_fight(void)
{
    uint8_t *t = mach_profile->timer ? ram_at(mach_profile->timer) : NULL;
    return t && *t >= 1 && *t <= 99;
}

static struct { uint32_t last; int still; } hp[2];
static uint8_t held_clock;

void training_apply(bool real)
{
    if (!cfg.training || !mach_profile->fighters || !in_fight()) { held_clock = 0; return; }
    // the clock: held where it was when training began (or the round started)
    uint8_t *t = ram_at(mach_profile->timer);
    if (!held_clock || *t > held_clock) held_clock = *t;
    else if (*t < held_clock) *t = held_clock;
    for (int p = 0; p < 2; p++) {
        uint32_t a = damage_addr(p), d = rd16(a);
        if (d >= KO) continue;                  // a KO is left to the game
        if (real) {
            if (d == hp[p].last) hp[p].still++;
            else { hp[p].last = d; hp[p].still = 0; }
        }
        // player 2 kept low, to practise finishing: set after each combo;
        // without refill it only ever lowers health, so damage still adds up
        uint32_t target = p == 1 && cfg.training_p2 ? LOW_DAMAGE : 0;
        if (hp[p].still < REFILL_DELAY) continue;
        if ((cfg.training_refill && d != target) || (!cfg.training_refill && d < target)) {
            wr16(a, target);
            if (real) hp[p].last = target;
        }
    }
}

#define REC_MAX (60 * 10)
static uint32_t rec[REC_MAX];
static int rec_len, rec_pos;
static enum { DUMMY_OFF, DUMMY_RECORD, DUMMY_PLAY } mode;

void training_record_toggle(void)
{
    if (mode == DUMMY_RECORD) {
        mode = DUMMY_OFF;
        osd_message("Dummy recorded: %.1f s (the play hotkey plays it)", rec_len / 60.0);
        return;
    }
    mode = DUMMY_RECORD;
    rec_len = 0;
    osd_message("Recording: you control player 2 (the record hotkey stops)");
}

void training_play_toggle(void)
{
    if (mode == DUMMY_PLAY) { mode = DUMMY_OFF; osd_message("Dummy stopped"); return; }
    if (!rec_len) { osd_message("Nothing recorded yet"); return; }
    mode = DUMMY_PLAY;
    rec_pos = 0;
    osd_message("Dummy playing (the play hotkey stops)");
}

void training_inputs(uint32_t held[2])
{
    const uint32_t mask = (1u << BTN_START) - 1;     // attacks and directions only
    if (mode == DUMMY_RECORD) {
        rec[rec_len++] = held[0] & mask;
        held[1] = (held[1] & ~mask) | (held[0] & mask);
        held[0] &= ~mask;
        if (rec_len == REC_MAX) training_record_toggle();
    } else if (mode == DUMMY_PLAY) {
        held[1] |= rec[rec_pos];
        rec_pos = (rec_pos + 1) % rec_len;
    }
}

const char *training_status(void)
{
    if (mode == DUMMY_RECORD) return "REC DUMMY";
    if (mode == DUMMY_PLAY) return cfg.training ? "TRAINING  DUMMY" : "DUMMY";
    return cfg.training ? "TRAINING" : NULL;
}

int game_health(int player)
{
    if (!mach_profile->fighters || !in_fight()) return -1;
    uint32_t d = rd16(damage_addr(player));
    return d <= KO ? (int)((KO - d) >> 8) : -1;
}

bool game_player_is_human(int player)
{
    uint8_t *m = mach_profile->players ? ram_at(mach_profile->players) : NULL;
    return !m || (*m >> player & 1);
}

int game_credits(void)
{
    if (!mach_profile->credits) return -1;
    uint32_t c = rd16(mach_profile->credits);
    return c < 100 ? (int)c : -1;
}
