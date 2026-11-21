// Building the translated game code on the player's machine (see rec_build.h).
#include "rec_build.h"
#include "rec.h"
#include "machine.h"
#include "c31.h"
#include "m68krec.h"
#include "c31rec.h"
#include "cage.h"
#include "paths.h"
#include "rec_headers.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#define SO_EXT ".dll"
#define EXE_EXT ".exe"
#else
#include <dlfcn.h>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#define SO_EXT ".so"
#define EXE_EXT ""
extern char **environ;
#endif

static rec_progress_fn progress_cb;

static void progress(const char *stage, double f) {
    if (progress_cb) progress_cb(stage, f);
}

/* ---------------------------------------------------------------- */
/* Modules                                                          */
/* ---------------------------------------------------------------- */

static void *lib_open(const char *path) {
#ifdef _WIN32
    return (void *)LoadLibraryA(path);
#else
    return dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif
}

static void *lib_sym(void *lib, const char *name) {
#ifdef _WIN32
    return (void *)(uintptr_t)GetProcAddress((HMODULE)lib, name);
#else
    return dlsym(lib, name);
#endif
}

static const char *lib_error(void) {
#ifdef _WIN32
    static char buf[64];
    snprintf(buf, sizeof(buf), "Windows error %lu", (unsigned long)GetLastError());
    return buf;
#else
    const char *e = dlerror();
    return e ? e : "unknown error";
#endif
}

typedef void (*init_fn)(rr_mem_t *, const rr_hostfns_t *);

// Hand a module's two halves to the dispatchers. get(h, name) finds a symbol.
static int use_module(void *(*get)(void *, const char *), void *h,
        char *err, size_t errlen) {
    init_fn init = (init_fn)(uintptr_t)get(h, "rec_init_module");
    const rec_module_t *m = (const rec_module_t *)get(h, "rec_exported_m68k");
    const rec_module_t *d = (const rec_module_t *)get(h, "rec_exported_dsp");
    if (!init || !m || !d) {
        snprintf(err, errlen, "not a module of this program");
        return 0;
    }
    init(rrm, rrfns);
    // a DSP half with nothing in it: the sound program wasn't there to read
    if (rec_use_m68k(m) || (d->nlabels && rec_use_dsp(d))) {
        snprintf(err, errlen, "module from another version");
        return 0;
    }
    return 1;
}

bool rec_build_load_file(const char *path, char *err, size_t errlen) {
    void *lib;
    if (!file_readable(path)) {
        snprintf(err, errlen, "%s: not found", path);
        return false;
    }
    lib = lib_open(path);
    if (!lib) {
        snprintf(err, errlen, "%s: %s", path, lib_error());
        return false;
    }
    return use_module(lib_sym, lib, err, errlen) != 0;   // stays loaded
}

/* ---------------------------------------------------------------- */
/* Files and processes                                              */
/* ---------------------------------------------------------------- */

static int write_text(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");
    size_t n = strlen(text);
    int ok;
    if (!f) return 0;
    ok = fwrite(text, 1, n, f) == n;
    return (fclose(f) == 0) && ok;
}

static uint64_t fnv(uint64_t h, const void *p, size_t n) {
    const uint8_t *b = (const uint8_t *)p;
    while (n--) {
        h ^= *b++;
        h *= 0x100000001b3ull;
    }
    return h;
}

// Everything the translated code depends on: the program ROM, what the
// sound DSP boots from (boot loader, sound program ROMs), the translators,
// the module interface and the instruction semantics compiled into it (the
// embedded headers), so a fix there rebuilds every cached module.
static uint64_t build_key(void) {
    int v[2] = { RECOMP_VERSION, REC_ABI_VERSION }, i;
    uint64_t h = fnv(0xcbf29ce484222325ull, mach.rom, RR_ROM_SIZE);
    if (cage.boot) h = fnv(h, cage.boot, RR_DSP_BOOT_WORDS * 4);
    if (cage.srom) h = fnv(h, cage.srom, RR_DSP_ROM_WORDS * 4);
    if (cage.bootint) h = fnv(h, cage.bootint, 0x1000u * 4);
    for (i = 0; i < rr_hdr_count; i++)
        h = fnv(h, rr_hdr_data[i], strlen(rr_hdr_data[i]));
    return fnv(h, v, sizeof(v));
}

static void cache_paths(char *module, size_t mn, char *src, size_t sn) {
    char dir[4200];
    unsigned long long key = (unsigned long long)build_key();
    snprintf(dir, sizeof(dir), "%scache", data_dir());
    make_dir(dir);
    snprintf(module, mn, "%s/rec-%016llx" SO_EXT, dir, key);
    snprintf(src, sn, "%s/src-%016llx", dir, key);
}

// The source files (space separated) of a translation in src.
static int read_sources(const char *src, char *files, size_t n) {
    char path[4400];
    FILE *f;
    size_t got;
    snprintf(path, sizeof(path), "%s/sources.txt", src);
    f = fopen(path, "rb");
    if (!f) return 0;
    got = fread(files, 1, n - 1, f);
    fclose(f);
    files[got] = 0;
    return got > 0;
}

// Written when TinyCC was the best compiler there was: next time it is used
// straight away (it compiles the cached sources into memory). It names the
// compilers that were tried ("tried: zig gcc"), so one that turns up later
// (Zig copied in, a compiler installed) gets its chance once.
static void tcc_marker(const char *src, char *out, size_t n) {
    snprintf(out, n, "%s/use-tinycc", src);
}

static const char *const system_ccs[] = { "cc", "gcc", "clang" };
#define N_SYSTEM_CCS (sizeof(system_ccs) / sizeof(system_ccs[0]))

#define MAX_JOBS 16
#define CMD_ARGS 160

typedef struct {
    char *argv[CMD_ARGS];
    int argc, overflow;
} cmd_t;

static void cmd_add(cmd_t *c, const char *a) {
    if (c->argc < CMD_ARGS - 1) {
        size_t n = strlen(a) + 1;
        c->argv[c->argc] = (char *)malloc(n);
        if (c->argv[c->argc]) memcpy(c->argv[c->argc++], a, n);
    } else {
        c->overflow = 1;
    }
    c->argv[c->argc] = NULL;
}

static void cmd_free(cmd_t *c) {
    int i;
    for (i = 0; i < c->argc; i++) free(c->argv[i]);
    c->argc = 0;
}

static int cpu_count(void) {
#ifdef _WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (int)si.dwNumberOfProcessors;
#else
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (int)n : 2;
#endif
}

static double run_from, run_to;
static const char *run_stage;

// Run the commands, `parallel` at a time, output appended to `log`.
static int run_all(cmd_t *cmds, int n, int parallel, const char *log) {
    int ok = 1, next = 0, done = 0, i;
    if (parallel < 1) parallel = 1;
    if (parallel > MAX_JOBS) parallel = MAX_JOBS;
    for (i = 0; i < n; i++)
        if (cmds[i].overflow) return 0;
#ifdef _WIN32
    {
        HANDLE running[MAX_JOBS];
        int nrun = 0;
        SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
        // a compiler that can't start or crashes must fail quietly, not stop
        // the build with a Windows message box (children inherit this)
        UINT mode = SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
        HANDLE out = CreateFileA(log, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                &sa, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        while (done < n) {
            while (nrun < parallel && next < n && ok) {
                static char line[32768];        // Windows' limit for a command line
                size_t len = 0;
                STARTUPINFOA si;
                PROCESS_INFORMATION pi;
                for (i = 0; i < cmds[next].argc; i++) {
                    size_t al = strlen(cmds[next].argv[i]);
                    if (len + al + 4 > sizeof(line)) { ok = 0; break; }
                    line[len++] = '"';
                    memcpy(line + len, cmds[next].argv[i], al);
                    len += al;
                    line[len++] = '"';
                    line[len++] = ' ';
                }
                line[len] = 0;
                if (!ok) { next = n; break; }
                memset(&si, 0, sizeof(si));
                si.cb = sizeof(si);
                si.dwFlags = STARTF_USESTDHANDLES;
                si.hStdOutput = si.hStdError = out;
                si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
                if (!CreateProcessA(NULL, line, NULL, NULL, TRUE, CREATE_NO_WINDOW,
                        NULL, NULL, &si, &pi)) { ok = 0; next = n; break; }
                CloseHandle(pi.hThread);
                running[nrun++] = pi.hProcess;
                next++;
            }
            if (!nrun) break;
            {
                DWORD r = WaitForMultipleObjects((DWORD)nrun, running, FALSE, 50), code = 1;
                int k;
                if (r == WAIT_TIMEOUT) {
                    progress(run_stage, run_from + (run_to - run_from) * done / n);
                    continue;
                }
                k = (int)(r - WAIT_OBJECT_0);
                if (k < 0 || k >= nrun) { ok = 0; break; }
                GetExitCodeProcess(running[k], &code);
                CloseHandle(running[k]);
                running[k] = running[--nrun];
                if (code) { ok = 0; next = n; }
                done++;
            }
        }
        for (i = 0; i < nrun; i++) {
            WaitForSingleObject(running[i], INFINITE);
            CloseHandle(running[i]);
        }
        if (out != INVALID_HANDLE_VALUE) CloseHandle(out);
        SetErrorMode(mode);
    }
#else
    {
        pid_t running[MAX_JOBS];
        int nrun = 0, fd = open(log, O_WRONLY | O_CREAT | O_APPEND, 0644);
        while (done < n) {
            while (nrun < parallel && next < n && ok) {
                posix_spawn_file_actions_t fa;
                pid_t pid;
                int e;
                posix_spawn_file_actions_init(&fa);
                if (fd >= 0) {
                    posix_spawn_file_actions_adddup2(&fa, fd, 1);
                    posix_spawn_file_actions_adddup2(&fa, fd, 2);
                }
                e = posix_spawnp(&pid, cmds[next].argv[0], &fa, NULL, cmds[next].argv, environ);
                posix_spawn_file_actions_destroy(&fa);
                if (e) { ok = 0; next = n; break; }
                running[nrun++] = pid;
                next++;
            }
            if (!nrun) break;
            {
                int status, k = 0;
                pid_t pid = waitpid(-1, &status, WNOHANG);
                if (pid == 0) {
                    progress(run_stage, run_from + (run_to - run_from) * done / n);
                    struct timespec ts = { 0, 50000000 };
                    nanosleep(&ts, NULL);
                    continue;
                }
                while (k < nrun && running[k] != pid) k++;
                if (k == nrun) continue;
                running[k] = running[--nrun];
                if (!WIFEXITED(status) || WEXITSTATUS(status)) { ok = 0; next = n; }
                done++;
            }
        }
        while (nrun) {
            int st;
            waitpid(running[--nrun], &st, 0);
        }
        if (fd >= 0) close(fd);
    }
#endif
    return ok;
}

// A program on PATH (or a path) that exists.
static int find_program(const char *name, char *out, size_t n) {
    const char *path = getenv("PATH");
    char buf[8192], *dir;
#ifdef _WIN32
    const char sep = ';';
#else
    const char sep = ':';
#endif
    if (strchr(name, '/') || strchr(name, '\\')) {
        snprintf(out, n, "%s", name);
        return file_readable(out);
    }
    if (!path) return 0;
    snprintf(buf, sizeof(buf), "%s", path);
    for (dir = buf; dir && *dir;) {
        char *e = strchr(dir, sep);
        if (e) *e = 0;
        snprintf(out, n, "%s/%s" EXE_EXT, dir, name);
        if (file_readable(out)) return 1;
        dir = e ? e + 1 : NULL;
    }
    return 0;
}

/* ---------------------------------------------------------------- */
/* Compilers                                                        */
/* ---------------------------------------------------------------- */

// Split the space-separated file list in place.
static int split(char *files, char **out, int max) {
    int n = 0;
    char *t;
    for (t = strtok(files, " \n"); t && n < max; t = strtok(NULL, " \n")) out[n++] = t;
    return n;
}

#define MAX_SOURCES 512

// zig: "zig cc" (bundled), else a gcc/clang driver.
static int build_with(const char *cc, int zig, const char *src, const char *list,
        const char *module, const char *log) {
    static cmd_t jobs[MAX_SOURCES];
    static char files[16384];
    char *names[MAX_SOURCES], file[4400], obj[4500];
    cmd_t link;
    int n, i, ok;
    snprintf(files, sizeof(files), "%s", list);
    n = split(files, names, MAX_SOURCES);
    for (i = 0; i < n; i++) {
        cmd_t *c = &jobs[i];
        memset(c, 0, sizeof(*c));
        cmd_add(c, cc);
        if (zig) cmd_add(c, "cc");
#ifdef _WIN32
        if (zig) { cmd_add(c, "-target"); cmd_add(c, "x86_64-windows-gnu"); }
#else
        cmd_add(c, "-fPIC");
#endif
        // -O1: the translated functions are huge and goto-heavy; -O2
        // costs much more compile time for little speed
        cmd_add(c, "-O1");
        cmd_add(c, "-fno-stack-protector");
        cmd_add(c, "-DREC_MODULE");
        cmd_add(c, "-w");
        cmd_add(c, "-I");
        cmd_add(c, src);
        cmd_add(c, "-c");
        snprintf(file, sizeof(file), "%s/%s", src, names[i]);
        cmd_add(c, file);
        cmd_add(c, "-o");
        snprintf(obj, sizeof(obj), "%s.o", file);
        cmd_add(c, obj);
    }
    run_stage = zig ? "Compiling the game code (Zig)" : "Compiling the game code";
    run_from = 0.3;
    run_to = 0.92;
    progress(run_stage, run_from);
    ok = run_all(jobs, n, cpu_count(), log);
    for (i = 0; i < n; i++) cmd_free(&jobs[i]);
    if (!ok) return 0;
    memset(&link, 0, sizeof(link));
    cmd_add(&link, cc);
    if (zig) cmd_add(&link, "cc");
#ifdef _WIN32
    if (zig) { cmd_add(&link, "-target"); cmd_add(&link, "x86_64-windows-gnu"); }
#endif
    cmd_add(&link, "-shared");
#ifndef _WIN32
    cmd_add(&link, "-nostdlib");            // the module needs nothing from the C library
#endif
    cmd_add(&link, "-o");
    cmd_add(&link, module);
    for (i = 0; i < n; i++) {
        snprintf(obj, sizeof(obj), "%s/%s.o", src, names[i]);
        cmd_add(&link, obj);
    }
    progress("Linking", 0.94);
    ok = run_all(&link, 1, 1, log);
    cmd_free(&link);
    return ok && file_readable(module);
}

// TinyCC, loaded at run time (the LGPL library shipped beside the program):
// compiles straight into memory, so only the C sources are cached.
typedef struct TCCState TCCState;
typedef struct {
    TCCState *(*new_)(void);
    void (*delete_)(TCCState *);
    int (*set_output_type)(TCCState *, int);
    int (*add_include_path)(TCCState *, const char *);
    void (*define_symbol)(TCCState *, const char *, const char *);
    int (*add_file)(TCCState *, const char *);
    void (*set_options)(TCCState *, const char *);
    void (*set_lib_path)(TCCState *, const char *);
    int (*relocate)(TCCState *);
    void *(*get_symbol)(TCCState *, const char *);
} tcc_api_t;

static tcc_api_t tcc;

static int load_tcc(char *libdir, size_t n) {
    static const char *const names[] = {
#ifdef _WIN32
        "tools/tcc/libtcc.dll", "libtcc.dll",
#else
        "tools/tcc/libtcc.so", "libtcc.so",
#endif
    };
    size_t i;
    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        char path[4200];
        void *lib;
        snprintf(path, sizeof(path), "%s%s", i == 0 ? base_dir() : "", names[i]);
        lib = lib_open(path);
        if (!lib) continue;
#define TSYM(f, name) *(void **)&tcc.f = lib_sym(lib, name)
        TSYM(new_, "tcc_new");
        TSYM(delete_, "tcc_delete");
        TSYM(set_output_type, "tcc_set_output_type");
        TSYM(add_include_path, "tcc_add_include_path");
        TSYM(define_symbol, "tcc_define_symbol");
        TSYM(add_file, "tcc_add_file");
        TSYM(set_options, "tcc_set_options");
        TSYM(set_lib_path, "tcc_set_lib_path");
        TSYM(relocate, "tcc_relocate");
        TSYM(get_symbol, "tcc_get_symbol");
#undef TSYM
        if (tcc.new_ && tcc.set_output_type && tcc.add_file && tcc.relocate &&
                tcc.get_symbol && tcc.add_include_path && tcc.define_symbol) {
            snprintf(libdir, n, "%stools/tcc", base_dir());
            return 1;
        }
    }
    return 0;
}

static void *tcc_get(void *s, const char *name) {
    return tcc.get_symbol((TCCState *)s, name);
}

static int load_with_tcc(const char *src, const char *list, char *err, size_t errlen) {
    static char files[16384];
    char libdir[4200], *names[MAX_SOURCES], file[4400];
    TCCState *s;
    int n, i;
    if (!load_tcc(libdir, sizeof(libdir))) {
        snprintf(err, errlen, "TinyCC (libtcc) not found");
        return 0;
    }
    s = tcc.new_();
    if (!s) {
        snprintf(err, errlen, "TinyCC failed to start");
        return 0;
    }
    if (tcc.set_lib_path) tcc.set_lib_path(s, libdir);
    if (tcc.set_options) tcc.set_options(s, "-w -nostdlib");
    tcc.set_output_type(s, 1 /* TCC_OUTPUT_MEMORY */);
    tcc.add_include_path(s, src);
    tcc.define_symbol(s, "REC_MODULE", "1");
#ifdef _WIN32
    // TinyCC's prologue calls its own stack probe for big frames
    snprintf(file, sizeof(file), "%s/chkstk.S", libdir);
    if (tcc.add_file(s, file) < 0) {
        snprintf(err, errlen, "TinyCC could not assemble %s", file);
        return 0;
    }
#endif
    snprintf(files, sizeof(files), "%s", list);
    n = split(files, names, MAX_SOURCES);
    for (i = 0; i < n; i++) {
        snprintf(file, sizeof(file), "%s/%s", src, names[i]);
        if (tcc.add_file(s, file) < 0) {
            snprintf(err, errlen, "TinyCC could not compile %s", file);
            return 0;
        }
        progress("Compiling the game code (TinyCC)", 0.3 + 0.6 * (i + 1) / n);
    }
    if (tcc.relocate(s) < 0) {
        snprintf(err, errlen, "TinyCC could not link the game code");
        return 0;
    }
    return use_module(tcc_get, s, err, errlen);   // s stays alive: it holds the code
}

// The bundled Zig needs Windows 10 or later.
static int zig_runs_here(void) {
#ifdef _WIN32
    typedef LONG (WINAPI *get_version_fn)(OSVERSIONINFOW *);
    get_version_fn get = (get_version_fn)(uintptr_t)GetProcAddress(
            GetModuleHandleA("ntdll.dll"), "RtlGetVersion");
    OSVERSIONINFOW v;
    memset(&v, 0, sizeof(v));
    v.dwOSVersionInfoSize = sizeof(v);
    if (get && get(&v) == 0 && v.dwMajorVersion < 10) return 0;
#endif
    return 1;
}

static void set_env(const char *k, const char *v) {
#ifdef _WIN32
    _putenv_s(k, v);
#else
    setenv(k, v, 1);
#endif
}

static int compile_and_load(const char *src, const char *list, const char *module,
        const char *log, char *msg, size_t msglen) {
    char cc[4200], err[512] = "", tried[64] = "tried:";
    const char *only = getenv("RR_BUILD_WITH");     // testing: zig, cc, gcc, clang or tcc
    char marker[4400];
    tcc_marker(src, marker, sizeof(marker));
    // 1. a bundled Zig
    snprintf(cc, sizeof(cc), "%stools/zig/zig" EXE_EXT, base_dir());
    if (file_readable(cc) && (!only || !strcmp(only, "zig")) && zig_runs_here()) {
        char zc[4200];
        snprintf(zc, sizeof(zc), "%scache/zig", data_dir());   // kept across builds
        set_env("ZIG_GLOBAL_CACHE_DIR", zc);
        set_env("ZIG_LOCAL_CACHE_DIR", zc);
        strcat(tried, " zig");
        if (build_with(cc, 1, src, list, module, log) &&
                rec_build_load_file(module, err, sizeof(err))) {
            remove(marker);
            snprintf(msg, msglen, "translated code built with Zig");
            return 1;
        }
    }
    // 2. a C compiler already on the system
    {
        size_t i;
        for (i = 0; i < N_SYSTEM_CCS; i++) {
            if (only && strcmp(only, "cc") && strcmp(only, system_ccs[i])) continue;
            if (!find_program(system_ccs[i], cc, sizeof(cc))) continue;
            strcat(tried, " ");
            strcat(tried, system_ccs[i]);
            if (build_with(cc, 0, src, list, module, log) &&
                    rec_build_load_file(module, err, sizeof(err))) {
                remove(marker);
                snprintf(msg, msglen, "translated code built with %s", system_ccs[i]);
                return 1;
            }
        }
    }
    // 3. TinyCC
    if ((!only || !strcmp(only, "tcc")) && load_with_tcc(src, list, err, sizeof(err))) {
        char text[128];
        snprintf(text, sizeof(text), "TinyCC was the only compiler that worked\n%s\n", tried);
        write_text(marker, text);
        snprintf(msg, msglen, "translated code built with TinyCC (slower; see README)");
        return 1;
    }
    snprintf(msg, msglen, "no compiler worked (%s); running on the interpreters",
            *err ? err : "none found");
    return 0;
}

/* ---------------------------------------------------------------- */
/* Translation                                                      */
/* ---------------------------------------------------------------- */

static int write_headers(const char *dir) {
    char path[4400];
    int i;
    for (i = 0; i < rr_hdr_count; i++) {
        snprintf(path, sizeof(path), "%s/%s", dir, rr_hdr_names[i]);
        if (!write_text(path, rr_hdr_data[i])) return -1;
    }
    return 0;
}

// The module's own glue: the program's view of memory and its callbacks.
static int write_glue(const char *dir) {
    char path[4400];
    snprintf(path, sizeof(path), "%s/module.c", dir);
    return write_text(path,
            "// Generated by rec_build. Do not edit.\n"
            "#define REC_MODULE 1\n#include \"rec_abi.h\"\n"
            "rr_mem_t *rrm;\nconst rr_hostfns_t *rrfns;\n"
            "void rec_init_module(rr_mem_t *m, const rr_hostfns_t *f) "
            "{ rrm = m; rrfns = f; }\n") ? 0 : -1;
}

static void dsp_warmup_run(void) {
    int i;
    machine_reset();
    cage_control_w(0);
    cage_control_w(3);
    for (i = 0; i < 60; i++) {
        rrm->dsp->cycles = 100000;
        cage_slice_begin(100000);
        c31_run();
        cage_slice_end();
        cage_update();
    }
}

// The warmup booted if the DSP left the boot ROM for RAM/IRAM code and
// the vector section landed (matches MAME's boot image bit-for-bit).
static int dsp_warm_ok(void) {
    uint32_t pc = rrm->dsp->pc & 0xffffffu;
    int in_ram = (pc < 0x10000u) || (pc >= 0x809800u && pc < 0x80a000u);
    return in_ram && cage.iram && cage.iram[0x7c0] != 0;
}

// Boot the sound program on the interpreter and find its code. Leaves the
// machine reset. Returns 0 with the plan filled.
static int dsp_plan_warm(drec_plan_t *plan) {
    uint32_t entries[72], i;
    int n = 0;
    dsp_warmup_run();
    if (!dsp_warm_ok()) {
        fprintf(stderr, "rec: DSP warm-up boot failed\n");
        machine_reset();
        return -1;
    }
    memset(plan, 0, sizeof(*plan));
    // The code as the DSP fetches it (0-0xFFF reads the boot ROM shadow).
    for (i = 0; i < DREC_WORDS; i++) plan->img[i] = rrfns->drd(drec_addr(i));
    if (getenv("RR_DUMP_DSP")) {  // debug: the image planned from
        FILE *f = fopen(getenv("RR_DUMP_DSP"), "wb");
        if (f) {
            fwrite(plan->img, 4, DREC_WORDS, f);
            fclose(f);
        }
    }
    // Where the boot left off, then the interrupt and trap vectors.
    entries[n++] = rrm->dsp->pc & 0xffffffu;
    for (i = 1; i < 64; i++) entries[n++] = rrfns->drd(i) & 0xffffffu;
    drec_plan(plan, entries, n);
    fprintf(stderr, "rec: DSP code %u instructions, %u functions (%u found "
            "by pointers)\n", plan->ninsns, plan->nfuncs, plan->nptr);
    machine_reset();
    return 0;
}

// Write both CPUs' sources into dir and their file names into files;
// m68k and dsp name the exported modules.
// An empty DSP half: the sound program couldn't be booted (no tms32031.zip),
// so the DSP stays on the interpreter while the 68K is still translated.
static int write_empty_dsp(const char *dir, const char *dsp, char *files, size_t cap) {
    char path[4400], text[512];
    snprintf(path, sizeof(path), "%s/dexports.c", dir);
    snprintf(text, sizeof(text), "// No sound program was read: nothing translated.\n"
            "#define REC_MODULE 1\n#include \"rec_abi.h\"\n"
            "const rec_module_t %s = {%d, 1, 0, 0, 0, 0, 0, 0, 0};\n",
            dsp, REC_ABI_VERSION);
    if (!write_text(path, text)) return -1;
    if (strlen(files) + 12 < cap) strcat(files, "dexports.c ");
    return 0;
}

static int translate(const char *dir, const char *m68k, const char *dsp,
        char *files, size_t cap) {
    rec_plan_t plan;
    drec_plan_t *dplan = NULL;
    int rc;
    files[0] = 0;
    progress("Reading the sound program", 0.02);
    if (cage.srom && cage.boot && cage.bootint) {
        dplan = (drec_plan_t *)calloc(1, sizeof(*dplan));
        if (!dplan) return -1;
        if (dsp_plan_warm(dplan)) {
            free(dplan);
            dplan = NULL;
        }
    }
    if (!dplan) fprintf(stderr, "rec: no sound program to translate; the DSP "
            "runs on the interpreter\n");
    progress("Translating the game's code", 0.08);
    m68krec_plan(&plan, mach.rom, RR_ROM_SIZE);
    fprintf(stderr, "rec: 68K code %u instructions below %06X, %u functions "
            "(%u found by pointers)\n", plan.ninsns, plan.limit, plan.nfuncs,
            plan.nptr);
    rc = m68krec_write(&plan, dir, m68k, files, cap);
    m68krec_plan_free(&plan);
    progress("Translating the sound code", 0.25);
    if (!rc) rc = dplan ? drec_write(dplan, dir, dsp, files, cap) :
            write_empty_dsp(dir, dsp, files, cap);
    if (dplan) {
        free(dplan->work);
        free(dplan);
    }
    return rc;
}

// A compiler that is here now but wasn't when TinyCC was chosen (the marker's
// "tried:" line; markers from before v1.1.2 have none).
static int untried_compiler(const char *marker) {
    char text[256] = "", path[4200], *t;
    FILE *f = fopen(marker, "rb");
    size_t i, got;
    if (f) {
        got = fread(text, 1, sizeof(text) - 1, f);
        text[got] = 0;
        fclose(f);
    }
    t = strstr(text, "tried:");
    if (!t) t = "";
    snprintf(path, sizeof(path), "%stools/zig/zig" EXE_EXT, base_dir());
    if (file_readable(path) && zig_runs_here() && !strstr(t, " zig")) return 1;
    for (i = 0; i < N_SYSTEM_CCS; i++) {
        char word[16];
        const char *w;
        size_t len;
        int seen = 0;
        if (!find_program(system_ccs[i], path, sizeof(path))) continue;
        snprintf(word, sizeof(word), " %s", system_ccs[i]);
        len = strlen(word);
        for (w = strstr(t, word); w; w = strstr(w + 1, word))
            if (w[len] == 0 || w[len] == ' ' || w[len] == '\n' || w[len] == '\r') seen = 1;
        if (!seen) return 1;
    }
    return 0;
}

bool rec_build_load_cached(char *msg, size_t msglen) {
    static char files[16384];
    char module[4400], src[4400], err[512], marker[4500];
    if (!mach.rom) return false;
    cache_paths(module, sizeof(module), src, sizeof(src));
    if (file_readable(module) && rec_build_load_file(module, err, sizeof(err))) {
        snprintf(msg, msglen, "translated code loaded");
        return true;
    }
    tcc_marker(src, marker, sizeof(marker));
    if (file_readable(marker) && untried_compiler(marker)) return false;   // rebuild with it
    if (file_readable(marker) && read_sources(src, files, sizeof(files)) &&
            load_with_tcc(src, files, err, sizeof(err))) {
        snprintf(msg, msglen, "translated code built with TinyCC");
        return true;
    }
    return false;
}

bool rec_build(rec_progress_fn cb, char *msg, size_t msglen) {
    static char files[16384];
    char module[4400], src[4400], log[4500], path[4500];
    int ok;
    progress_cb = cb;
    if (!mach.rom) {
        snprintf(msg, msglen, "no game loaded");
        return false;
    }
    cache_paths(module, sizeof(module), src, sizeof(src));
    make_dir(src);
    snprintf(log, sizeof(log), "%s/build.log", src);
    remove(log);
    if (write_headers(src) || write_glue(src) ||
            translate(src, "rec_exported_m68k", "rec_exported_dsp", files,
                    sizeof(files))) {
        snprintf(msg, msglen, "translation failed (see the log)");
        progress_cb = NULL;
        return false;
    }
    if (strlen(files) + 12 < sizeof(files)) strcat(files, "module.c");
    snprintf(path, sizeof(path), "%s/sources.txt", src);
    write_text(path, files);
    ok = compile_and_load(src, files, module, log, msg, msglen);
    progress("Done", 1.0);
    progress_cb = NULL;
    return ok != 0;
}

int rec_emit_linked(const char *dir) {
    static char files[16384];
    make_dir(dir);
    if (write_headers(dir)) return -1;
    return translate(dir, "rec_linked_m68k", "rec_linked_dsp", files, sizeof(files));
}
