// What the windowed frontend (frontend.c) needs from the program (main.c).
#pragma once
#include <stdbool.h>
#include "game.h"

#define PRODUCT "RageRecomp"

int frontend_main(void);
// Whether a set's files are all there.
bool app_set_there(game_id_t id);
// Save the cabinet's EEPROM (settings, high scores) to saves/<set>.eeprom.
void app_nvram_save(void);
// Start the program again (after choosing another game version).
void app_restart(void);
// Show a message: a dialog in windowed mode, stderr always.
void app_report(bool fatal, const char *fmt, ...);
