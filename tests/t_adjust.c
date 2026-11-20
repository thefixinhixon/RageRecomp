// Operator settings in the EEPROM (adjust.c): the record code the game's
// driver checks, both copies, the option fields, pending changes applied
// only at power-on. Synthetic data: an all-zero record is a valid codeword.
#include "support.h"
#include "adjust.h"

int main(void) {
    test_init();

    // Codeword length grows as the game's driver grows it (len 2 -> 6).
    T_EQ_U32(adjust_codeword_len(2), 6, "codeword of 2 data bytes");
    T_EQ_U32(adjust_codeword_len(0x53), 0x5b, "settings record (v2.3): the second copy follows it");
    T_EQ_U32(adjust_codeword_len(0x51), 0x59, "settings record (v2.0)");

    // Round trip and error detection.
    uint8_t data[0x53], back[0x53], code[0x60];
    for (int i = 0; i < 0x53; i++) data[i] = (uint8_t)(i * 37 + 11);
    adjust_record_write(code, 0x53, data);
    T_OK(adjust_record_read(code, 0x53, back) && !memcmp(back, data, sizeof data), "record round trip");
    code[17] ^= 0x10;
    T_OK(!adjust_record_read(code, 0x53, back), "a flipped bit is caught");

    // A blank chip holds no settings: the game writes them at first boot.
    mach_profile = game_profile(GAME_PRIMRAGE);
    eeprom_factory();
    machine_reset();
    T_EQ_U32(adjust_get(ADJ_DIFFICULTY), (uint32_t)-1, "blank EEPROM: no settings");
    T_OK(!adjust_set(ADJ_DIFFICULTY, 3), "blank EEPROM: nothing to change");

    // Zeroed records are valid: every field reads 0.
    memset(eeprom.data, 0, sizeof eeprom.data);
    machine_reset();
    T_EQ_U32(adjust_get(ADJ_DIFFICULTY), 0, "zeroed record: difficulty 1");
    T_EQ_U32(adjust_running(ADJ_FREE_PLAY), 0, "running value read at power-on");

    // A change is pending until applied; the game reads it at power-on.
    T_OK(adjust_set(ADJ_DIFFICULTY, 15) && adjust_set(ADJ_FREE_PLAY, 1) && adjust_set(ADJ_GAME_COST, 1),
         "set difficulty 16, free play, 2 coins");
    T_EQ_U32(adjust_get(ADJ_DIFFICULTY), 15, "pending value shown");
    T_OK(adjust_pending(), "change pending");
    T_EQ_U32(eeprom.data[3], 0, "EEPROM untouched before apply");
    T_OK(!adjust_set(ADJ_ROUND_TIME, 11), "round time past 80 s refused");
    adjust_apply();
    machine_reset();
    T_OK(!adjust_pending(), "applied at power-on");
    T_EQ_U32(adjust_running(ADJ_DIFFICULTY), 15, "difficulty 16 after power-on");
    T_EQ_U32(adjust_running(ADJ_FREE_PLAY), 1, "free play after power-on");
    T_EQ_U32(adjust_running(ADJ_GAME_COST), 1, "2 coins per credit after power-on");
    T_EQ_U32(adjust_running(ADJ_GORE), 0, "untouched field kept");

    // The words where the game's descriptors put them (v2.3: coin options
    // at nibble 98, bytes 0x32 high and 0x31 low; game options at nibble
    // 102, bytes 0x36 down to 0x33).
    uint8_t rec[0x53];
    T_OK(adjust_record_read(eeprom.data, 0x53, rec), "first copy valid");
    T_EQ_U32(rec[0x32], 0x88, "coin options high byte: free play + 2 coins");
    T_EQ_U32(rec[0x33], 0xf0, "game options: difficulty 16 (index 15) in bits 4-7");
    T_OK(adjust_record_read(eeprom.data + 0x5b, 0x53, back) && !memcmp(back, rec, sizeof rec), "second copy matches");

    // Either intact copy is enough.
    eeprom.data[5] ^= 1;
    machine_reset();
    T_EQ_U32(adjust_running(ADJ_DIFFICULTY), 15, "first copy damaged: second copy read");

    // RR_ADJUST's syntax, in the menu's units.
    memset(eeprom.data, 0, sizeof eeprom.data);
    T_OK(adjust_parse("difficulty=7,round_time=80,gore=1"), "parse settings");
    machine_reset();
    T_EQ_U32(adjust_running(ADJ_DIFFICULTY), 6, "difficulty 7");
    T_EQ_U32(adjust_running(ADJ_ROUND_TIME), 10, "80 seconds");
    T_EQ_U32(adjust_running(ADJ_GORE), 1, "no gore");
    T_OK(!adjust_parse("speed=3"), "unknown name refused");
    T_EQ_STR(adjust_text(ADJ_GAME_COST, 0), "1 coin 1 credit", "coinage text");
    T_EQ_STR(adjust_text(ADJ_ROUND_TIME, 5), "55 seconds", "round time text");
    return test_finish();
}
