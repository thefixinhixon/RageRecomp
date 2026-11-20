// Whole-machine save states. The blob is little-endian and self-sized;
// loads refuse wrong magic, size, compatibility id or program ROMs.
#pragma once
#include <stdint.h>

// Must differ between builds that emulate differently: bump the low word
// on any emulation change outside the translators (translator changes
// already move RECOMP_VERSION, which is folded in here).
#define STATE_FORMAT 3u
uint32_t state_compat_id(void);

// Save states as files (the whole machine). 1 on success; loading refuses
// states from other builds or game versions.
int state_save_file(const char *path);
int state_load_file(const char *path);
// Hash of the whole machine state, for determinism tests.
uint64_t state_hash(void);
