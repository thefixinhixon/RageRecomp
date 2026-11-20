// Xicor 2816 parallel EEPROM (2048 bytes). Behaviour follows MAME's
// eeprompar (see NOTICE): writes work only right after an unlock write,
// each write locks the chip again, and reads while unlocked return 0.
#pragma once
#include <stdint.h>

typedef struct {
    uint8_t data[2048];
    uint8_t unlocked;   // 1 between an unlock write and the next access
} eeprom_t;

extern eeprom_t eeprom;

// Power-on: relock; the contents stay, as on the board.
void eeprom_reset(void);
// Blank chip (all 0xFF), as a cabinet without saved settings has.
void eeprom_factory(void);
// offset = (address - 0xD20000) >> 1 (bytes live at odd addresses).
uint8_t eeprom_read(uint32_t offset);
void eeprom_write(uint32_t offset, uint8_t data);
// Any write into 0xD40000-0xD4FFFF unlocks the chip.
void eeprom_unlock(void);
