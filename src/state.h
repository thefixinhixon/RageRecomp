// Whole-machine save states. The blob is little-endian and self-sized;
// loads refuse wrong magic, size, compatibility id or program ROMs.
#pragma once
#include <stddef.h>
#include <stdint.h>

// Must differ between builds that emulate differently: bump it on any
// emulation change or state layout change. (The translators don't count:
// translated code emulates exactly as the interpreters do.)
#define STATE_FORMAT 8u
uint32_t state_compat_id(void);

// Save states as files (the whole machine). 1 on success; loading refuses
// states from other builds or game versions.
int state_save_file(const char *path);
int state_load_file(const char *path);
// Hash of the whole machine state, for determinism tests.
uint64_t state_hash(void);

// Checkpoints, rewind history and rollback (run-ahead, netplay).
void state_init(void);              // the current state becomes the checkpoint; history cleared
void state_checkpoint(void);        // after each emulated frame
void state_rollback(void);          // back to the last checkpoint
void state_set_history(size_t bytes);   // rewind memory budget (0 = no history)
int state_rewind(void);             // back one checkpoint; 0 when the history is empty
double state_history_seconds(void);
size_t state_history_bytes(void);
uint64_t state_quick_hash(void);    // fast whole-state hash (netplay)
