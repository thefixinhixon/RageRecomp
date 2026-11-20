// Cabinet controls to input ports (see controls.h).
#include "controls.h"
#include "machine.h"

void controls_apply(const uint16_t held[2], int test) {
    uint8_t *byte[2] = { &mach.in.b3, &mach.in.b1 };
    int p;
    mach.in.b0 = mach.in.b1 = mach.in.b3 = 0xff;
    for (p = 0; p < 2; p++) {
        uint32_t h = held[p];
        uint8_t v = 0;
        if (h >> BTN_RIGHT & 1) v |= 0x10;
        if (h >> BTN_LEFT & 1) v |= 0x20;
        if (h >> BTN_DOWN & 1) v |= 0x40;
        if (h >> BTN_UP & 1) v |= 0x80;
        if (mach_profile->has_start_buttons) {
            if (h >> BTN_START & 1) v |= 0x01;
            if (h >> BTN_B1 & 1) v |= 0x02;
            if (h >> BTN_B2 & 1) v |= 0x04;
            if (h >> BTN_B3 & 1) v |= 0x08;
            if (h >> BTN_B4 & 1) mach.in.b0 &= (uint8_t)~(p ? 0x08 : 0x02);
        } else {
            if ((h >> BTN_B1 & 1) || (h >> BTN_START & 1)) v |= 0x01;
            if (h >> BTN_B2 & 1) v |= 0x02;
            if (h >> BTN_B3 & 1) v |= 0x04;
            if (h >> BTN_B4 & 1) v |= 0x08;
        }
        *byte[p] = (uint8_t)~v;
    }
    mach.in.coin1 = held[0] >> BTN_COIN & 1;
    mach.in.coin2 = held[1] >> BTN_COIN & 1;
    mach.in.service = (uint8_t)(test != 0);
}
