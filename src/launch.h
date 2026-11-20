// Started by a launcher (LaunchBox, Playnite, ES-DE, RecompLauncher, ...)
// with a game's path: a MAME set's zip, or a folder holding the zips.
#pragma once
#include <stddef.h>
#include "game.h"

// Note the path's folder (searched first for the game's files) and, for a
// zip, which set it is. 0 (and a message in err) if the path isn't there.
int launch_open(const char *path, char *err, size_t n);
int launch_active(void);            // launch_open succeeded
const char *launch_dir(void);       // its folder with a trailing separator, or NULL
const char *launch_set(void);       // the set a zip named (e.g. "primrageo"), or NULL
