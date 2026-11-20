// Game profiles: the three supported MAME sets (same driver, different
// ROM revisions). Per-set differences: program ROMs, CAGE boot ROM,
// sound ROMs, input layout (dedicated start vs button-start) and the
// DSP speedup address. Shared graphics/sound ROMs resolve from any set.
#pragma once
#include <stdint.h>

typedef enum {
    GAME_PRIMRAGE = 0,  // version 2.3, Jan 1995 (parent set)
    GAME_PRIMRAGEO,     // version 2.3, Dec 1994
    GAME_PRIMRAGE20,    // version 2.0
    GAME_COUNT
} game_id_t;

// One needed file inside a MAME zip (split or merged layout).
typedef struct {
    const char *zip;    // preferred zip ("primrage" also covers merged)
    const char *name;   // file name inside the zip
    uint32_t crc;       // expected CRC32 (0 = accept any, warn)
    uint32_t length;    // expected length
} rom_file_t;

typedef struct {
    game_id_t id;
    const char *set;        // MAME set name
    const char *title;      // for the picker and window title
    int has_start_buttons;  // dedicated P1/P2 start (else button 4 starts)
    uint32_t dsp_speedup;   // CAGE polled-loop address (0 = none)
    // Program ROMs: 4 x 512 KB, word-interleaved pairs.
    rom_file_t prog[4];
    // CAGE DSP boot ROM: 512 KB, one byte per DSP word.
    rom_file_t dspboot;
    // CAGE sound ROMs: 2 x 2 MB, word-interleaved halves.
    rom_file_t dspsnd[2];
    // Playfield tiles: 3 x 1 MB planes (shared across sets).
    rom_file_t tiles[3];
    // Alpha tiles: 128 KB (shared).
    rom_file_t chars;
    // Motion-object RLE ROMs (shared; some sets dump fewer — the loader
    // fills missing tail ROMs by number from primrage.zip).
    rom_file_t rle[32];
    int rle_count;          // ROMs listed for this set
    // Game RAM locations (68K addresses; found by tracing scripted runs,
    // the fighter records as in the research notes). 0 = not known.
    uint32_t credits;       // credit count (word)
    uint32_t timer;         // round clock, whole seconds (byte; 0 outside a fight)
    uint32_t fighters;      // fighter records, one per player...
    uint32_t fighter_step;  // ...this far apart; damage taken is the word at
                            // +0x5A, 8.8, a KO at 120 (0x7800)
    uint32_t players;       // bitmask of players in the game (bit 0 = P1)
} game_profile_t;

const game_profile_t *game_profile(game_id_t id);
const game_profile_t *game_by_set(const char *set);
// Detect the set from a zip's file list (returns GAME_COUNT if unknown).
game_id_t game_detect(const char **names, int count);
