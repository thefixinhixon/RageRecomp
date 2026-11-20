// Xicor 2816 parallel EEPROM. See eeprom.h and MAME's eeprompar.
#include "eeprom.h"
#include <string.h>

eeprom_t eeprom;

void eeprom_reset(void) {
    // A power cycle keeps the contents (it is the cabinet's NVRAM).
    eeprom.unlocked = 0;
}

void eeprom_factory(void) {
    // Factory default is all-0xFF (MAME's er2055 nvram_default).
    memset(eeprom.data, 0xff, sizeof(eeprom.data));
    eeprom.unlocked = 0;
}

uint8_t eeprom_read(uint32_t offset) {
    if (eeprom.unlocked) return 0; // reads while unlocked return unmapped
    return eeprom.data[offset & 0x7ffu];
}

void eeprom_write(uint32_t offset, uint8_t data) {
    if (!eeprom.unlocked) return; // locked: write ignored
    eeprom.data[offset & 0x7ffu] = data;
    eeprom.unlocked = 0; // each write locks the chip again
}

void eeprom_unlock(void) {
    eeprom.unlocked = 1;
}
