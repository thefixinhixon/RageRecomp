// The game's own operator settings (the test menu's Game Options and Coin
// Options), read from and written to the cabinet's EEPROM the way the game
// stores them. The game reads them at power-on, so a change is held back
// and written by adjust_apply() just before "Reset game" (or on exit).
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    ADJ_DIFFICULTY,     // 0-15: 1 (easiest) to 16 (most difficult); default 7
    ADJ_ROUND_TIME,     // 0-10: 30 to 80 seconds; default 55
    ADJ_FREE_PLAY,      // 0 no, 1 yes
    ADJ_GAME_COST,      // 0-7: 1 to 8 coins per credit; default 2
    ADJ_GORE,           // 0 full gore, 1 no gore
    ADJ_COUNT
} adjust_id_t;

// The setting as it will be after the next reset (a pending change, else
// the EEPROM's); -1 when the EEPROM holds no valid settings yet (a blank
// chip: the game writes its defaults during its first boot).
int adjust_get(adjust_id_t id);
// The value the game is running with (read at the last power-on).
int adjust_running(adjust_id_t id);
// Change a setting (held until adjust_apply). False if out of range or the
// EEPROM holds no settings yet.
bool adjust_set(adjust_id_t id, int value);
int adjust_choices(adjust_id_t id);
// Menu text for a value, e.g. "7 (medium)", "55 seconds".
const char *adjust_text(adjust_id_t id, int value);
// True if some setting differs from what the game is running with.
bool adjust_pending(void);
// Write pending changes into the EEPROM image (before a power-on).
void adjust_apply(void);
// Note what the game is running with: call right after each power-on.
void adjust_powered_on(void);
// Apply "name=value,..." (names: difficulty 1-16, round_time 30-80,
// free_play 0/1, game_cost 1-8, gore 0/1) straight to the EEPROM image;
// for RR_ADJUST in tests. Returns false on a bad name or value.
bool adjust_parse(const char *spec);

// The record format, exposed for the unit test. A record of `len` data
// bytes is stored as a codeword of adjust_codeword_len(len) bytes: data at
// the indices that are not powers of two, a parity byte at each power of
// two (the XOR of the bytes whose index has that bit) and an overall parity
// byte at 0, as the game's EEPROM driver checks them.
int adjust_codeword_len(int len);
bool adjust_record_read(const uint8_t *code, int len, uint8_t *data);
void adjust_record_write(uint8_t *code, int len, const uint8_t *data);
