// Started by a launcher with a game's path (see launch.h). MAME keeps one zip
// per set, so the path names the set (primrageo.zip) or a folder holding
// them; the other zips it needs (primrage.zip for a clone's shared files,
// tms32031.zip) are looked for beside it first.
#include "launch.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static char dir[4200];
static char set[32];
static int active;

static const char *base_name(const char *p) {
    const char *s = strrchr(p, '/'), *b = strrchr(p, '\\');
    if (b > s) s = b;
    return s ? s + 1 : p;
}

static int is_dir(const char *p) {
    struct stat st;
    return !stat(p, &st) && S_ISDIR(st.st_mode);
}

int launch_open(const char *path, char *err, size_t n) {
    struct stat st;
    const char *b = base_name(path);
    size_t len = strlen(b);
    if (stat(path, &st)) {
        snprintf(err, n, "%s: not found", path);
        return 0;
    }
    if (is_dir(path)) {
        snprintf(dir, sizeof(dir), "%s", path);
        len = strlen(dir);
        if (len && dir[len - 1] != '/' && dir[len - 1] != '\\')
            snprintf(dir + len, sizeof(dir) - len, "/");
        set[0] = 0;                 // the configured version, from there
    } else {
        int i;
        snprintf(dir, sizeof(dir), "%.*s", (int)(b - path), path);
        // the set by its zip's name; anything else plays the configured one
        if (len > 4 && !strcmp(b + len - 4, ".zip"))
            snprintf(set, sizeof(set), "%.*s", (int)(len - 4), b);
        for (i = 0; i < GAME_COUNT && set[0]; i++)
            if (!strcmp(game_profile((game_id_t)i)->set, set)) break;
        if (i == GAME_COUNT) set[0] = 0;
    }
    active = 1;
    return 1;
}

int launch_active(void) { return active; }

const char *launch_dir(void) { return active ? dir : NULL; }

const char *launch_set(void) { return active && set[0] ? set : NULL; }
