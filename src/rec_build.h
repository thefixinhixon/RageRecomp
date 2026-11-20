// Building the translated game code on the player's machine.
//
// Distributed programs contain no game code. At first launch the program
// translates the 68K program ROM and the sound DSP's code (as its boot
// loader leaves it in RAM) to C, compiles that with the best compiler there
// is (a bundled Zig, the system's cc/gcc/clang, else TinyCC loaded as a
// library) into one module, and caches it in the data folder. Without any,
// the game runs on the interpreters.
#pragma once
#include <stdbool.h>
#include <stddef.h>

// Bump when the generated C changes (part of the cache key).
#define RECOMP_VERSION 9

// Progress for a "preparing" screen: stage text and 0..1.
typedef void (*rec_progress_fn)(const char *stage, double fraction);

// Load a module built earlier for these game files, if there is one.
bool rec_build_load_cached(char *msg, size_t msglen);
// Translate, compile and load. Slow: tens of seconds. Leaves the machine
// reset (the DSP's code is read by booting it).
bool rec_build(rec_progress_fn progress, char *msg, size_t msglen);
// Load a module file directly (testing).
bool rec_build_load_file(const char *path, char *err, size_t errlen);
// Write the sources for a linked-in build (`make recomp`) into dir:
// game-derived output, never to be shared. 0 on success.
int rec_emit_linked(const char *dir);
