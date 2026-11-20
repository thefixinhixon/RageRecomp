// The cabinet's controls as the frontend, headless scripts and netplay see
// them: one bit per BTN_* per player, turned into the input ports by the
// game version's layout. No SDL.
#pragma once
#include <stdint.h>

enum {
    BTN_B1, BTN_B2, BTN_B3, BTN_B4,
    BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_START, BTN_COIN,
    NBUTTONS
};

// Set the ports for this frame. test: the self-test switch is held.
// The Jan 1995 set has start buttons and reads button 4 from the port's
// low byte; the older sets put buttons 1-4 where start was, and start
// with an attack button (button 1 here).
void controls_apply(const uint16_t held[2], int test);
