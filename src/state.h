// Whole-machine save states. The blob is little-endian and self-sized;
// loads refuse wrong magic, size, compatibility id or program ROMs.
#pragma once
#include <stdint.h>

// Must differ between builds that emulate differently: bump the low word
// on any emulation change outside the translators (translator changes
// already move RECOMP_VERSION, which is folded in here).
#define STATE_FORMAT 2u
uint32_t state_compat_id(void);
