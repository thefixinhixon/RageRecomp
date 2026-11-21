// Where things are: the program's folder, the writable data folder and the
// game's files. Plain C (no SDL), for the headless program too.
#pragma once
#include <stddef.h>

// The program's folder, with a trailing separator.
const char *base_dir(void);
// --data-dir: everything the program writes goes in this one folder (made if
// needed), and its run/ is searched for the game files. Before data_dir().
void set_data_dir(const char *dir);
// Where settings, the cache, save states and screenshots go: --data-dir,
// else beside the program when that folder is writable (portable use), else
// beside an AppImage that has a run/ folder next to it, else the user's
// data folder (RageRecomp). Trailing separator.
const char *data_dir(void);
// Running as an AppImage: the folder holding the .AppImage file, else NULL.
const char *appimage_dir(void);
// `name` if it exists, else the first of: the launcher's folder, the
// [game] roms setting, beside the program and run/ there, run/ beside an
// AppImage, run/ in the data folder. Returns name when none has it.
const char *find_data(const char *name, const char *roms_dir, char *buf, size_t n);
// The same for the first of several names found, searching each place for
// all of them before the next place.
const char *find_data_any(const char *const *names, int count,
        const char *roms_dir, char *buf, size_t n);
// Make a folder (one level); fine if it exists.
void make_dir(const char *path);
int file_readable(const char *path);
// A monotonic clock in microseconds, and a sleep (no SDL).
unsigned long long rr_ticks_us(void);
void rr_sleep_ms(int ms);
