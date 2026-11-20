// What the windowed frontend (frontend.c) needs from the program (main.c).
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "game.h"

#define PRODUCT "RageRecomp"

int frontend_main(void);
// Whether a set's files are all there.
bool app_set_there(game_id_t id);
// Save the cabinet's EEPROM (settings, high scores) to saves/<set>.eeprom.
void app_nvram_save(void);
// Start the program again (after choosing another game version).
void app_restart(void);
// Netplay: one frame with these inputs (BTN_* bits per player) and its
// checkpoint; resim: a rollback replaying a frame (its audio is dropped).
void machine_run_inputs(const uint16_t in[2], bool resim);
// Netplay: power-cycle and run the boot frames, identically on both sides.
void machine_netplay_reset(void);
// Show a message: a dialog in windowed mode, stderr always.
void app_report(bool fatal, const char *fmt, ...);
