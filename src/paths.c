// Program, data and game-file locations (see paths.h).
#include "paths.h"
#include "launch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#else
#include <unistd.h>
#endif

#define PRODUCT_DIR "RageRecomp"

void make_dir(const char *path) {
#ifdef _WIN32
    _mkdir(path);
#else
    mkdir(path, 0755);
#endif
}

int file_readable(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f) fclose(f);
    return f != NULL;
}

static int is_dir(const char *path) {
    struct stat st;
    return !stat(path, &st) && S_ISDIR(st.st_mode);
}

const char *base_dir(void) {
    static char dir[4096];
    char *slash;
    if (*dir) return dir;
#ifdef _WIN32
    {
        DWORD n = GetModuleFileNameA(NULL, dir, sizeof(dir) - 1);
        dir[n < sizeof(dir) ? n : 0] = 0;
    }
    for (slash = dir; *slash; slash++)
        if (*slash == '\\') *slash = '/';
#else
    {
        ssize_t n = readlink("/proc/self/exe", dir, sizeof(dir) - 1);
        dir[n > 0 ? n : 0] = 0;
    }
#endif
    slash = strrchr(dir, '/');
    if (slash) slash[1] = 0;
    else strcpy(dir, "./");
    return dir;
}

const char *appimage_dir(void) {
    static char dir[4096];
    const char *p = getenv("APPIMAGE");
    if (!p || !*p || strlen(p) >= sizeof(dir)) return NULL;
    if (!*dir) {
        char *slash;
        strcpy(dir, p);
        slash = strrchr(dir, '/');
        if (slash) slash[1] = 0;
        else strcpy(dir, "./");
    }
    return dir;
}

static int writable_dir(const char *dir) {
    char probe[4200];
    FILE *f;
    snprintf(probe, sizeof(probe), "%s.rr-write-test", dir);
    f = fopen(probe, "wb");
    if (!f) return 0;
    fclose(f);
    remove(probe);
    return 1;
}

static char given[4096];

void set_data_dir(const char *p) {
    size_t n = strlen(p);
    snprintf(given, sizeof(given) - 1, "%s", p);
    if (n && p[n - 1] != '/' && p[n - 1] != '\\') strcat(given, "/");
    make_dir(p);
}

// The user's own data folder: %APPDATA%\RageRecomp or
// $XDG_DATA_HOME/RageRecomp (~/.local/share/RageRecomp), as SDL's pref path.
static void user_dir(char *out, size_t n) {
#ifdef _WIN32
    const char *a = getenv("APPDATA");
    snprintf(out, n, "%s/" PRODUCT_DIR "/", a ? a : ".");
#else
    const char *x = getenv("XDG_DATA_HOME"), *h = getenv("HOME");
    char parent[4096];
    if (x && *x) snprintf(parent, sizeof(parent), "%s", x);
    else {
        snprintf(parent, sizeof(parent), "%s/.local", h ? h : ".");
        make_dir(parent);
        snprintf(parent, sizeof(parent), "%s/.local/share", h ? h : ".");
    }
    make_dir(parent);
    snprintf(out, n, "%s/" PRODUCT_DIR "/", parent);
#endif
    {
        char d[4096];
        snprintf(d, sizeof(d), "%.*s", (int)strlen(out) - 1, out);
        make_dir(d);
    }
}

const char *data_dir(void) {
    static char dir[4096];
    if (*dir) return dir;
    if (*given) snprintf(dir, sizeof(dir), "%s", given);
    else if (writable_dir(base_dir())) snprintf(dir, sizeof(dir), "%s", base_dir());
    else {
        const char *ai = appimage_dir();
        char run[4200];
        if (ai) snprintf(run, sizeof(run), "%srun", ai);
        if (ai && is_dir(run) && writable_dir(ai)) snprintf(dir, sizeof(dir), "%s", ai);
        else user_dir(dir, sizeof(dir));
    }
    return dir;
}

const char *find_data(const char *name, const char *roms_dir, char *buf, size_t n) {
    if (file_readable(name)) return name;
    if (launch_dir()) {
        snprintf(buf, n, "%s%s", launch_dir(), name);
        if (file_readable(buf)) return buf;
    }
    if (roms_dir && *roms_dir) {
        size_t l = strlen(roms_dir);
        snprintf(buf, n, "%s%s%s", roms_dir, roms_dir[l - 1] == '/' || roms_dir[l - 1] == '\\' ? "" : "/", name);
        if (file_readable(buf)) return buf;
    }
    snprintf(buf, n, "%s%s", base_dir(), name);
    if (file_readable(buf)) return buf;
    snprintf(buf, n, "%srun/%s", base_dir(), name);
    if (file_readable(buf)) return buf;
    if (appimage_dir()) {
        snprintf(buf, n, "%srun/%s", appimage_dir(), name);
        if (file_readable(buf)) return buf;
    }
    snprintf(buf, n, "%srun/%s", data_dir(), name);
    return file_readable(buf) ? buf : name;
}
