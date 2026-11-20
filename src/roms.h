// MAME ROM loading: finds files across split/merged zips, checks CRC32
// and length, assembles the interleaved images. Reports exactly which
// files are missing or bad so the picker can tell the player what to get.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "game.h"

typedef struct roms roms_t;

// Open zip files (up to 8 paths, tried in order for every file).
roms_t *roms_open(char const *const *zips, int nzips);
void roms_close(roms_t *r);

// Read one file by bare name (any folder inside the zips). Returns
// malloc'd data (*out_len set) or NULL. Missing files and CRC/length
// mismatches are recorded for roms_report (bad data still returned when
// present, so `--force-bad-dump` style flows can continue).
uint8_t *roms_read(roms_t *r, const rom_file_t *rf, uint32_t *out_len);

// True when every file the set needs is in the zips, by name (quick: no
// reading, no CRC check).
int roms_has_set(roms_t *r, const game_profile_t *p);
// True when anything was missing or mismatched.
int roms_failed(roms_t *r);
// Human-readable report (missing files with expected CRCs first).
void roms_report(roms_t *r, char *buf, size_t cap);

// Assemble the machine images for a profile. Returns 0 on success, nonzero
// when files were missing (images for present files are still filled).
int roms_load_set(roms_t *r, const game_profile_t *p);
