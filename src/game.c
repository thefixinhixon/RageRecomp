// Game profiles for the three Primal Rage sets. ROM names, CRCs and
// lengths follow MAME's atarigt.cpp driver (see NOTICE).
#include "game.h"
#include <string.h>

// Shared RLE ROMs (all sets use the primrageo numbering past the first
// pair; primrage/primrage20 only differ in that first pair).
#define RLE_SHARED \
    { "primrage", "136102-0332.mol1.0", 0x610cfcb4u, 0x100000u }, \
    { "primrage", "136102-0333.moh1.0", 0x3320448eu, 0x100000u }, \
    { "primrage", "136102-0334.mol1.1", 0xbe3acb6fu, 0x100000u }, \
    { "primrage", "136102-0335.moh1.1", 0xe4f6e87au, 0x100000u }, \
    { "primrage", "136102-0336.mol1.2", 0xa78a8525u, 0x100000u }, \
    { "primrage", "136102-0337.moh1.2", 0x73fdd050u, 0x100000u }, \
    { "primrage", "136102-0338.mol1.3", 0xfa19cae6u, 0x100000u }, \
    { "primrage", "136102-0339.moh1.3", 0xe0cd1393u, 0x100000u }, \
    { "primrage", "136102-0316.mol0.0", 0x9301c672u, 0x100000u }, \
    { "primrage", "136102-0317.moh0.0", 0x9e3b831au, 0x100000u }, \
    { "primrage", "136102-0318.mol0.1", 0x8523db5du, 0x100000u }, \
    { "primrage", "136102-0319.moh0.1", 0x42f22e4bu, 0x100000u }, \
    { "primrage", "136102-0320.mol0.2", 0x21369d13u, 0x100000u }, \
    { "primrage", "136102-0321.moh0.2", 0x3b7d498au, 0x100000u }, \
    { "primrage", "136102-0322.mol0.3", 0x05e9f407u, 0x100000u }, \
    { "primrage", "136102-0323.moh0.3", 0x603f3bb6u, 0x100000u }, \
    { "primrage", "136102-0324.mol0.4", 0x3c37769fu, 0x100000u }, \
    { "primrage", "136102-0325.moh0.4", 0xf43321e3u, 0x100000u }, \
    { "primrage", "136102-0326.mol0.5", 0x63d4cceau, 0x100000u }, \
    { "primrage", "136102-0327.moh0.5", 0x9f4806d5u, 0x100000u }, \
    { "primrage", "136102-0328.mol0.6", 0xa08d73e1u, 0x100000u }, \
    { "primrage", "136102-0329.moh0.6", 0xeff3d2cdu, 0x100000u }, \
    { "primrage", "136102-0330.mol0.7", 0x7bf6bb8fu, 0x100000u }, \
    { "primrage", "136102-0331.moh0.7", 0xc6a64dadu, 0x100000u }

static const game_profile_t profiles[GAME_COUNT] = {
    [GAME_PRIMRAGE] = {
        .id = GAME_PRIMRAGE,
        .set = "primrage",
        .title = "Primal Rage (version 2.3, Jan 1995)",
        .has_start_buttons = 1,
        .dsp_speedup = 0x42f2u,
        .prog = {
            { "primrage", "rage_136102-2044a_pgmuu.29l", 0x85556b91u,
                0x80000u },
            { "primrage", "rage_136102-2043a_pgmum.28l", 0x4d3414d0u,
                0x80000u },
            { "primrage", "rage_136102-2042a_pgmlm.26l", 0x6b91e8eau,
                0x80000u },
            { "primrage", "rage_136102-2041a_pgmll.24l", 0x9958f715u,
                0x80000u },
        },
        .dspboot = { "primrage", "rage_136102-1045b_dspsub.11a",
            0x0656435fu, 0x80000u },
        .dspsnd = {
            { "primrage", "136102-0075_dspsub.11c", 0x02448be6u,
                0x200000u },
            { "primrage", "136102-0077_dspsub.11e", 0x057aff9au,
                0x200000u },
        },
        .tiles = {
            { "primrage", "rage_136102-0050a_pf0l.25n", 0x66896e8fu,
                0x80000u },
            { "primrage", "rage_136102-0051a_pf0m.27n", 0xfb5b3e7bu,
                0x80000u },
            { "primrage", "rage_136102-0052a_pf0h.28n", 0xcbe38670u,
                0x80000u },
        },
        .chars = { "primrage", "rage_136102-1078a_alpha.22p", 0x1d3260bfu,
            0x20000u },
        .rle = {
            { "primrage", "136102-2100a_mol0.2v", 0x433d4925u, 0x80000u },
            { "primrage", "136102-2101a_moh0.2w", 0x78c7bb2bu, 0x80000u },
            RLE_SHARED
        },
        .rle_count = 26,
        .credits = 0xffe1f6u, .timer = 0xffe8e5u, .fighters = 0xffe7bcu, .fighter_step = 0x94u,
        .players = 0xffe398u,
    },
    [GAME_PRIMRAGEO] = {
        .id = GAME_PRIMRAGEO,
        .set = "primrageo",
        .title = "Primal Rage (version 2.3, Dec 1994)",
        .has_start_buttons = 0,
        .dsp_speedup = 0x42f2u,
        .prog = {
            { "primrageo", "136102-1044b.29l", 0x35c9c34bu, 0x80000u },
            { "primrageo", "136102-1043b.28l", 0x86322829u, 0x80000u },
            { "primrageo", "136102-1042b.27l", 0x750e8095u, 0x80000u },
            { "primrageo", "136102-1041b.25l", 0x6a90d283u, 0x80000u },
        },
        .dspboot = { "primrageo", "136102-1078a.11a", 0x0656435fu,
            0x80000u },
        .dspsnd = {
            { "primrageo", "136102-0075", 0xb685a88eu, 0x200000u },
            { "primrageo", "136102-0077", 0x3283cea8u, 0x200000u },
        },
        .tiles = {
            { "primrageo", "136102-0050a.25n", 0x66896e8fu, 0x80000u },
            { "primrageo", "136102-0051a.27n", 0xfb5b3e7bu, 0x80000u },
            { "primrageo", "136102-0052a.28n", 0xcbe38670u, 0x80000u },
        },
        .chars = { "primrageo", "136102-1045b.23p", 0x1d3260bfu, 0x20000u },
        .rle = {
            { "primrageo", "136102-1100a.2v", 0x6e9c80b5u, 0x80000u },
            { "primrageo", "136102-1101a.2w", 0xbb7ee624u, 0x80000u },
            RLE_SHARED
        },
        .rle_count = 26,
        .credits = 0xffe1ceu, .timer = 0xffe8b9u, .fighters = 0xffe790u, .fighter_step = 0x94u,
        .players = 0,
    },
    [GAME_PRIMRAGE20] = {
        .id = GAME_PRIMRAGE20,
        .set = "primrage20",
        .title = "Primal Rage (version 2.0)",
        .has_start_buttons = 0,
        .dsp_speedup = 0x48a4u,
        .prog = {
            { "primrage20", "136102-0044b.29l", 0x26139575u, 0x80000u },
            { "primrage20", "136102-0043b.28l", 0x928d2447u, 0x80000u },
            { "primrage20", "136102-0042b.27l", 0xcd6062b9u, 0x80000u },
            { "primrage20", "136102-0041b.25l", 0x3008f6f0u, 0x80000u },
        },
        .dspboot = { "primrage20", "136102-0078a.11a", 0x91df8d8fu,
            0x80000u },
        .dspsnd = {
            { "primrage20", "136102-0075", 0xb685a88eu, 0x200000u },
            { "primrage20", "136102-0077", 0x3283cea8u, 0x200000u },
        },
        .tiles = {
            { "primrage20", "136102-0050a.25n", 0x66896e8fu, 0x80000u },
            { "primrage20", "136102-0051a.27n", 0xfb5b3e7bu, 0x80000u },
            { "primrage20", "136102-0052a.28n", 0xcbe38670u, 0x80000u },
        },
        .chars = { "primrage20", "136102-0045a.23p", 0xc8b39b1cu,
            0x20000u },
        .rle = {
            { "primrage20", "136102-0100a.2v", 0x5299fb2au, 0x80000u },
            { "primrage20", "136102-0101a.2w", 0x3e234711u, 0x80000u },
            RLE_SHARED
        },
        .rle_count = 26,
        .credits = 0xffdf00u, .timer = 0xffe5b8u, .fighters = 0xffe4a0u, .fighter_step = 0x8cu,
        .players = 0,
    },
};

const game_profile_t *game_profile(game_id_t id) {
    if ((int)id < 0 || id >= GAME_COUNT) return NULL;
    return &profiles[id];
}

const game_profile_t *game_by_set(const char *set) {
    int i;
    for (i = 0; i < GAME_COUNT; i++) {
        if (!strcmp(profiles[i].set, set)) return &profiles[i];
    }
    return NULL;
}

game_id_t game_detect(const char **names, int count) {
    // Score each set by distinctive program ROM names present.
    int best = -1, best_n = 0, i, j, k;
    for (i = 0; i < GAME_COUNT; i++) {
        int n = 0;
        for (j = 0; j < 4; j++) {
            const char *want = profiles[i].prog[j].name;
            const char *slash;
            // Match against bare names too (split sets keep subfolders).
            for (k = 0; k < count; k++) {
                slash = strrchr(names[k], '/');
                if (!strcmp(slash ? slash + 1 : names[k], want)) {
                    n++;
                    break;
                }
            }
        }
        if (n > best_n) {
            best_n = n;
            best = i;
        }
    }
    if (best < 0 || best_n == 0) return GAME_COUNT;
    return (game_id_t)best;
}
