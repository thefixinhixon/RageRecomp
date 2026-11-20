// First-run translation: plans/translates/loads the 68K and DSP modules,
// cached by ROM hash. Falls back to interpreters when no compiler works.
#pragma once

// Bump when the generated C changes (part of the cache key).
#define RECOMP_VERSION 1

// Build (or load from cache) the 68K and DSP modules for the loaded
// ROMs. The DSP build warms the image on the interpreter first.
int rec_build_m68k(const char *cache_dir);
int rec_build_dsp(const char *cache_dir);
// Close a dlopen/LoadLibrary handle from a successful build.
void rec_close_handle(void *h);
// Emit linked-in sources for `make recomp` (game-derived; never commit).
int rec_emit_linked(const char *dir);
