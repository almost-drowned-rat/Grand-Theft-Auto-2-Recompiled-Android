/* launcher.c -- GTA 2 Recompiled's Linux launcher (see launcher.h). From Zone of the Enders Recompiled's
 * engine/launcher/launcher.c (ps2recomp-option-p, ad3b491, the same author's), adapted: the GTA 2 freeware installer
 * instead of a disc image (no CHD, no disc reading), the game started with WR_ROOT. GPL-3.0-or-later (LICENSE). */

#include "launcher.h"

#include "game_setup_ui.h"
#include "sha256.h"
#include "zip_read.h"

#include <SDL3/SDL.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#define LP 1024
#define MAX_ARGS 256
#define MAX_PROCS 72
#define TRAILER_MAGIC "XRPAYLD1"            /* the Xbox launcher's trailer: one release script for both */
#define TRAILER_SIZE 48                     /* magic (8), payload offset (8, LE), release (32, NUL padded) */
#define WRONG_INPUT 3                       /* setup_build.py's exit code: not the GTA 2 freeware installer */
#define PACKAGE_INCOMPLETE "The package is incomplete - please download it again."

typedef struct {
    const GtaLauncherConfig *cfg;
    /* root = the game's folder, WR_ROOT (the AppImage's folder - writable: game/, saves/, data/, media/ live here);
     * tools = data/tools, the payload unpacked; work = data/build/work, the lifted C; obj = data/build/obj;
     * game = root/game, the game's files from the installer */
    char root[LP], tools[LP], data[LP], build[LP], work[LP], obj[LP], game[LP];
    char exe[LP];                           /* this exe (the payload trailer) */
    char appimage[LP];                      /* Linux: the AppImage file started ("" none) - copied to a target folder */
    char payload[LP];                       /* the file the payload trailer was found on (exe, or an older AppImage) */
    char out[LP];                           /* the game: root/data/game_exe ({OUT}) */
    char input[LP];                         /* {INPUT}: the player's GTA 2 installer or installed GTA 2 folder (only read) */
    char release[33];
    char clang[LP], python[LP];             /* toolchain.txt: absolute */
    char jobs[16];                          /* {JOBS} */
    XrSetupUi *ui;
    SDL_Mutex *log_mx;
    SDL_IOStream *log;
    double base, span;
    char step[128];
    /* the player quit during the installation (the window's close box / Alt+F4, "Quit anyway?" answered Yes - the
     * maintainer 2026-11-23): the build's running tools stopped, no further step, the half-made game removed */
    SDL_AtomicInt cancel;
    SDL_Mutex *proc_mx;
    SDL_Process *procs[MAX_PROCS];          /* the build's tools running now (compilers: up to 64 at once) */
#ifdef _WIN32
    HANDLE job;                             /* they and the processes they start: stopped together */
#endif
} L;

/* ── small helpers ─────────────────────────────────────────────────────── */

static void lg(L *l, const char *fmt, ...)
{
    char line[1400];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    fprintf(stderr, "[LAUNCHER] %s\n", line);
    if (l->log_mx)
        SDL_LockMutex(l->log_mx);
    if (l->log) {
        SDL_WriteIO(l->log, line, strlen(line));
        SDL_WriteIO(l->log, "\n", 1);
        SDL_FlushIO(l->log);
    }
    if (l->log_mx)
        SDL_UnlockMutex(l->log_mx);
}

static void join(char *out, size_t n, const char *a, const char *b)
{
    size_t k = strlen(a);
    snprintf(out, n, "%s%s%s", a, k && (a[k - 1] == '/' || a[k - 1] == '\\') ? "" : "/", b);
}

static int is_file(const char *p)
{
    SDL_PathInfo pi;
    return SDL_GetPathInfo(p, &pi) && pi.type == SDL_PATHTYPE_FILE;
}

static int is_dir(const char *p)
{
    SDL_PathInfo pi;
    return SDL_GetPathInfo(p, &pi) && pi.type == SDL_PATHTYPE_DIRECTORY;
}

/* the player's copy: the installer (a file) or an installed GTA 2 (a folder) */
static int is_input(const char *p) { return p && *p && (is_file(p) || is_dir(p)); }

static Sint64 file_size(const char *p)
{
    SDL_PathInfo pi;
    return SDL_GetPathInfo(p, &pi) && pi.type == SDL_PATHTYPE_FILE ? (Sint64)pi.size : -1;
}

static char *read_text(const char *p)
{
    return (char *)SDL_LoadFile(p, NULL);   /* SDL_LoadFile adds a NUL */
}

static int write_text(const char *p, const char *text)
{
    return SDL_SaveFile(p, text, strlen(text));
}

typedef struct { int ok; } RmCtx;
static int rm_tree(const char *p);
static SDL_EnumerationResult SDLCALL rm_cb(void *u, const char *dir, const char *name)
{
    char p[LP];
    join(p, sizeof p, dir, name);
    if (!rm_tree(p))
        ((RmCtx *)u)->ok = 0;
    return SDL_ENUM_CONTINUE;
}

/* the player's own files by their names: never removed (Orta GitHub #6, 2026-11-17: an installer deleted a player's
 * only image that lay inside its folder - the rule for every launcher). Here: a large .exe (the GTA 2 installer,
 * ~362 MB - the game's own gta2.exe is 1.6 MB) and the translations pack. */
static int is_player_file(const char *p, Sint64 size)
{
    size_t n = strlen(p);
    if (n > 4 && !SDL_strcasecmp(p + n - 4, ".exe") && size >= ((Sint64)64 << 20))
        return 1;
    return n > 4 && !SDL_strcasecmp(p + n - 4, ".pak");
}

/* removes p and everything below it - only ever the launcher's own folders (data/build, data/tools/bundle/unpack);
 * a player's file found there stays (and so does its folder) */
static int rm_tree(const char *p)
{
    SDL_PathInfo pi;
    RmCtx c = { 1 };
    if (!SDL_GetPathInfo(p, &pi))
        return 1;
    if (pi.type == SDL_PATHTYPE_FILE && is_player_file(p, (Sint64)pi.size)) {
        SDL_Log("kept %s: a player's file is never removed", p);
        return 0;
    }
    if (pi.type == SDL_PATHTYPE_DIRECTORY)
        SDL_EnumerateDirectory(p, rm_cb, &c);
    return SDL_RemovePath(p) && c.ok;
}

/* "{TOOLS}/x" with the placeholders filled in */
static void expand(L *l, const char *in, char *out, size_t n)
{
    size_t o = 0;
    while (*in && o + 1 < n) {
        const char *v = NULL;
        size_t skip = 0;
        int dir = 0;                        /* a folder: its trailing separator dropped */
        if (!strncmp(in, "{TOOLS}", 7)) v = l->tools, skip = 7, dir = 1;
        else if (!strncmp(in, "{ROOT}", 6)) v = l->root, skip = 6, dir = 1;
        else if (!strncmp(in, "{WORK}", 6)) v = l->work, skip = 6, dir = 1;
        else if (!strncmp(in, "{GAME}", 6)) v = l->game, skip = 6, dir = 1;
        else if (!strncmp(in, "{DATA}", 6)) v = l->data, skip = 6, dir = 1;
        else if (!strncmp(in, "{OUT}", 5)) v = l->out, skip = 5;
        else if (!strncmp(in, "{INPUT}", 7)) v = l->input, skip = 7;
        else if (!strncmp(in, "{JOBS}", 6)) v = l->jobs, skip = 6;
        if (v) {
            size_t k = strlen(v);
            if (dir && k && (v[k - 1] == '/' || v[k - 1] == '\\'))
                k--;
            if (o + k >= n)
                break;
            memcpy(out + o, v, k);
            o += k;
            in += skip;
        } else {
            out[o++] = *in++;
        }
    }
    out[o] = 0;
}

static int split_tabs(char *line, char **argv, int max)
{
    int n = 0;
    char *s = line;
    while (n < max - 1) {
        char *t = strchr(s, '\t');
        argv[n++] = s;
        if (!t)
            break;
        *t = 0;
        s = t + 1;
    }
    argv[n] = NULL;
    return n;
}

/* ── quitting during the installation: the build's tools stopped (the maintainer 2026-11-23) ───────────── */

static int cancelled(L *l) { return SDL_GetAtomicInt(&l->cancel) != 0; }

/* a started tool into the list (and on Windows into the job, so the processes it starts - clang's linker - are
 * stopped with it); 0: the player quit meanwhile - the caller stops it */
static int proc_add(L *l, SDL_Process *proc)
{
    int i, ok = 0;
    SDL_LockMutex(l->proc_mx);
#ifdef _WIN32
    if (l->job) {
        HANDLE h = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE,
                               (DWORD)SDL_GetNumberProperty(SDL_GetProcessProperties(proc), SDL_PROP_PROCESS_PID_NUMBER, 0));
        if (h) {
            if (!AssignProcessToJobObject(l->job, h))
                lg(l, "a tool not in the launcher's job (error %lu)", (unsigned long)GetLastError());
            CloseHandle(h);
        }
    }
#endif
    for (i = 0; i < MAX_PROCS; i++)
        if (!l->procs[i]) {
            l->procs[i] = proc;
            ok = 1;
            break;
        }
    if (!ok)
        lg(l, "more than %d tools at once: one not in the list", MAX_PROCS);
    ok = !cancelled(l);
    SDL_UnlockMutex(l->proc_mx);
    return ok;
}

static void proc_remove(L *l, SDL_Process *proc)
{
    int i;
    SDL_LockMutex(l->proc_mx);
    for (i = 0; i < MAX_PROCS; i++)
        if (l->procs[i] == proc)
            l->procs[i] = NULL;
    SDL_UnlockMutex(l->proc_mx);
}

#ifndef _WIN32
/* Linux: pid and every process below it (by /proc/<n>/stat's parent field) - the parent held first (SIGSTOP), so
 * it starts no new one, then the children, then it */
static void kill_tree(pid_t pid, int depth)
{
    DIR *d;
    struct dirent *e;
    if (pid <= 1 || depth > 16)
        return;
    kill(pid, SIGSTOP);
    if ((d = opendir("/proc"))) {
        while ((e = readdir(d))) {
            char p[64], buf[512], *close_paren;
            FILE *f;
            long child = strtol(e->d_name, NULL, 10);
            if (child <= 1)
                continue;
            snprintf(p, sizeof p, "/proc/%ld/stat", child);
            if (!(f = fopen(p, "r")))
                continue;
            buf[0] = 0;
            if (!fgets(buf, sizeof buf, f))
                buf[0] = 0;
            fclose(f);
            /* "pid (comm) state ppid ...": comm may hold spaces and parentheses - after the last ')' */
            if ((close_paren = strrchr(buf, ')'))) {
                char state;
                long ppid;
                if (sscanf(close_paren + 1, " %c %ld", &state, &ppid) == 2 && ppid == (long)pid)
                    kill_tree((pid_t)child, depth + 1);
            }
        }
        closedir(d);
    }
    kill(pid, SIGKILL);
}
#endif

/* the player quit: every running tool (and what it started) stopped; the steps see cancelled() and end */
static void stop_build_tools(L *l)
{
    int i, n = 0;
    SDL_SetAtomicInt(&l->cancel, 1);
    SDL_LockMutex(l->proc_mx);
#ifdef _WIN32
    if (l->job)
        TerminateJobObject(l->job, 1);
#endif
    for (i = 0; i < MAX_PROCS; i++)
        if (l->procs[i]) {
#ifndef _WIN32
            kill_tree((pid_t)SDL_GetNumberProperty(SDL_GetProcessProperties(l->procs[i]), SDL_PROP_PROCESS_PID_NUMBER, 0), 0);
#endif
            SDL_KillProcess(l->procs[i], true);
            n++;
        }
    SDL_UnlockMutex(l->proc_mx);
    lg(l, "quit during \"%s\" (the player): %d running tool(s) stopped", l->step, n);
}

/* runs argv in cwd, stdout + stderr appended to log_path (NULL: dropped); the exit code, -1 when it could not
 * start (or the player quit) */
static int run_proc(L *l, char **argv, const char *cwd, const char *log_path)
{
    SDL_PropertiesID p;
    SDL_IOStream *out;
    SDL_Process *proc;
    int code = -1;
    if (cancelled(l))
        return -1;
    p = SDL_CreateProperties();
    out = log_path ? SDL_IOFromFile(log_path, "ab") : NULL;
    SDL_SetPointerProperty(p, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, argv);
    if (cwd)
        SDL_SetStringProperty(p, SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING, cwd);
    SDL_SetNumberProperty(p, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
    if (out) {
        SDL_SetNumberProperty(p, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_REDIRECT);
        SDL_SetPointerProperty(p, SDL_PROP_PROCESS_CREATE_STDOUT_POINTER, out);
        SDL_SetBooleanProperty(p, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
    } else {
        SDL_SetNumberProperty(p, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_NULL);
        SDL_SetNumberProperty(p, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_NULL);
    }
#ifdef _WIN32
    SDL_SetBooleanProperty(p, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);   /* (no console window) */
#endif
    /* (not in the background elsewhere: SDL gives a background process's exit code as 0 - a failed compile and the
       wrong disc's code 3 would pass unseen; on Windows the background flag stays for "no console window" and the
       code is read from our own handle below) */
    proc = SDL_CreateProcessWithProperties(p);
    SDL_DestroyProperties(p);
    if (!proc) {
        lg(l, "cannot start %s: %s", argv[0], SDL_GetError());
    } else {
        if (!proc_add(l, proc))
            SDL_KillProcess(proc, true);    /* (the player quit while it started) */
#ifdef _WIN32
        /* SDL gives a background process's exit code as 0 on Windows too (SDL_process.c SDL_WaitProcess: found
           2026-11-21 by a forced failure - a crashed translation passed as "done"). The real code from our own handle:
           the pid stays valid while SDL keeps its handle, until SDL_DestroyProcess. */
        HANDLE own = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE,
                                 (DWORD)SDL_GetNumberProperty(SDL_GetProcessProperties(proc), SDL_PROP_PROCESS_PID_NUMBER, 0));
        SDL_WaitProcess(proc, true, &code);
        if (own) {
            DWORD rc;
            if (WaitForSingleObject(own, INFINITE) == WAIT_OBJECT_0 && GetExitCodeProcess(own, &rc))
                code = (int)rc;
            else
                code = -1;
            CloseHandle(own);
        } else {
            lg(l, "%s: no handle for its exit code (error %lu)", argv[0], (unsigned long)GetLastError());
            code = -1;
        }
#else
        SDL_WaitProcess(proc, true, &code);
#endif
        proc_remove(l, proc);
        SDL_DestroyProcess(proc);
        if (cancelled(l))
            code = -1;                      /* stopped by the player: never "done" */
    }
    if (out)
        SDL_CloseIO(out);
    return code;
}

/* ── a step on a worker thread, the window kept alive meanwhile ────────── */

typedef struct {
    int (*fn)(L *, void *);
    L *l;
    void *arg;
    SDL_AtomicInt done;
    int rc;
} Job;

static int SDLCALL job_main(void *p)
{
    Job *j = (Job *)p;
    j->rc = j->fn(j->l, j->arg);
    SDL_SetAtomicInt(&j->done, 1);
    return 0;
}

/* Without dialogs (RECOMP_NO_DIALOGS=1: tests, RecompLauncher's windowless setup step): "progress 0.42 <step>" lines
 * on stdout - when the percentage or the step changes, and every 30 s on long work */
static void progress_line(double f, const char *step)
{
    static int last_pct = -1;
    static char last_step[128];
    static Uint64 last_t;
    const int pct = (int)(f * 100.0);
    const Uint64 now = SDL_GetTicks();
    if (!xr_no_dialogs())
        return;
    if (pct == last_pct && !strcmp(step, last_step) && now - last_t < 30000)
        return;
    last_pct = pct;
    last_t = now;
    snprintf(last_step, sizeof last_step, "%s", step);
    printf("progress %.2f %s\n", f < 0 ? 0.0 : f > 1 ? 1.0 : f, step);
    fflush(stdout);
}

static void ui_say(L *l, double within, const char *extra)
{
    char t[256];
    progress_line(l->base + l->span * (within < 0 ? 0 : within > 1 ? 1 : within), l->step);
    snprintf(t, sizeof t, "%s%s%s", l->step, extra && *extra ? " - " : "", extra ? extra : "");
    if (l->ui)
        xr_setup_ui_progress(l->ui, l->base + l->span * (within < 0 ? 0 : within > 1 ? 1 : within), t);
}

typedef struct { SDL_AtomicInt done_n; int total; } Prog;

static double prog_frac(void *a)
{
    Prog *p = (Prog *)a;
    return p->total ? (double)SDL_GetAtomicInt(&p->done_n) / p->total : 0.0;
}

/* runs fn on a thread; its code (0 = done). progress: 0..1 within the step, or NULL */
static int run_job(L *l, const char *step, double base, double span, int (*fn)(L *, void *), void *arg,
                   double (*progress)(void *))
{
    Job j;
    SDL_Thread *t;
    Uint64 t0 = SDL_GetTicks();
    memset(&j, 0, sizeof j);
    j.fn = fn; j.l = l; j.arg = arg;
    snprintf(l->step, sizeof l->step, "%s", step);
    l->base = base;
    l->span = span;
    /* the player quit between two steps: no further one */
    if (cancelled(l) || (l->ui && xr_setup_ui_cancelled(l->ui))) {
        if (!cancelled(l))
            stop_build_tools(l);
        lg(l, "%s: not started (the player quit)", step);
        return 1;
    }
    lg(l, "%s", step);
    t = SDL_CreateThread(job_main, "launcher step", &j);
    if (!t)
        return fn(l, arg);
    while (!SDL_GetAtomicInt(&j.done)) {
        char e[64];
        unsigned s = (unsigned)((SDL_GetTicks() - t0) / 1000);
        snprintf(e, sizeof e, "%u:%02u", s / 60, s % 60);
        ui_say(l, progress ? progress(arg) : 0.0, cancelled(l) ? "stopping" : e);   /* (the window kept alive) */
        /* the close box answered Yes ("Quit anyway?"): the running tools stopped, the step ends by itself */
        if (!cancelled(l) && l->ui && xr_setup_ui_cancelled(l->ui))
            stop_build_tools(l);
        SDL_Delay(100);
    }
    SDL_WaitThread(t, NULL);
    if (!cancelled(l) && l->ui && xr_setup_ui_cancelled(l->ui))
        stop_build_tools(l);                /* (answered while the step ended) */
    if (cancelled(l) && !j.rc)
        j.rc = 1;
    lg(l, "%s: %s (code %d) after %u s", step, cancelled(l) ? "STOPPED (the player quit)" : j.rc ? "FAILED" : "done",
       j.rc, (unsigned)((SDL_GetTicks() - t0) / 1000));
    return j.rc;
}
/* ── the payload ───────────────────────────────────────────────────────── */

/* the trailer on this program, or on the AppImage file: l->payload names the file it was found on */
static int read_trailer_of(L *l, const char *file, Uint64 *offset);

static int read_trailer(L *l, Uint64 *offset)
{
    if (read_trailer_of(l, l->exe, offset))
        return 1;
    return l->appimage[0] && read_trailer_of(l, l->appimage, offset);
}

static int read_trailer_of(L *l, const char *file, Uint64 *offset)
{
    SDL_IOStream *f = SDL_IOFromFile(file, "rb");
    unsigned char t[TRAILER_SIZE];
    Sint64 size;
    int ok = 0, i;
    if (!f)
        return 0;
    size = SDL_GetIOSize(f);
    if (size > TRAILER_SIZE && SDL_SeekIO(f, size - TRAILER_SIZE, SDL_IO_SEEK_SET) >= 0
        && SDL_ReadIO(f, t, sizeof t) == sizeof t && !memcmp(t, TRAILER_MAGIC, 8)) {
        *offset = 0;
        for (i = 7; i >= 0; i--)
            *offset = (*offset << 8) | t[8 + i];
        memcpy(l->release, t + 16, 32);
        l->release[32] = 0;
        ok = *offset < (Uint64)size;
    }
    SDL_CloseIO(f);
    if (ok)
        snprintf(l->payload, sizeof l->payload, "%s", file);
    return ok;
}

/* every entry of the zip at zp below dest, through the zip-slip guard, streamed (no size cap) */
static int unzip_into(L *l, const char *zp, const char *dest, Prog *prog, const char *what)
{
    char err[256];
    ZipFile *z = zip_open(zp, err, sizeof err);
    int i, bad = 0;
    if (!z) {
        lg(l, "%s: %s", what, err);
        return 1;
    }
    prog->total = zip_count(z);
    for (i = 0; i < zip_count(z) && !bad; i++) {
        const ZipEntry *e = zip_entry(z, i);
        char rel[LP], dst[LP], *slash;
        if (!zip_safe_path(e->name, rel, sizeof rel)) {
            lg(l, "%s: refused entry %s", what, e->name);
            bad = 1;
            break;
        }
        join(dst, sizeof dst, dest, rel);
        if (e->dir) {
            SDL_CreateDirectory(dst);
        } else {
            if ((slash = strrchr(dst, '/')) != NULL) {
                *slash = 0;
                SDL_CreateDirectory(dst);
                *slash = '/';
            }
            if (!zip_extract_to_file(z, i, dst, err, sizeof err)) {
                lg(l, "%s: %s: %s", what, e->name, err);
                bad = 1;
            }
#ifndef _WIN32
            /* Linux: a program in the zip (the compiler, Python) runnable again - its Unix mode kept */
            else if (e->mode & 0111)
                chmod(dst, 0755);
#endif
        }
        SDL_AddAtomicInt(&prog->done_n, 1);
    }
    zip_close(z);
    return bad;
}

typedef struct { Prog p; Uint64 offset; } UnpackArg;

static int unpack_job(L *l, void *a)
{
    UnpackArg *u = (UnpackArg *)a;
    char zp[LP], marker[LP];
    SDL_IOStream *in = SDL_IOFromFile(l->payload, "rb"), *out;
    Sint64 end;
    int bad = 0;
    void *buf;
    if (!in)
        return 1;
    end = SDL_GetIOSize(in) - TRAILER_SIZE;
    join(zp, sizeof zp, l->root, "data/payload.zip");
    out = SDL_IOFromFile(zp, "wb");
    if (!out || SDL_SeekIO(in, (Sint64)u->offset, SDL_IO_SEEK_SET) < 0) {
        SDL_CloseIO(in);
        if (out)
            SDL_CloseIO(out);
        return 1;
    }
    buf = malloc(1 << 20);
    while (buf && SDL_TellIO(in) < end) {
        Sint64 left = end - SDL_TellIO(in);
        size_t want = left > (1 << 20) ? (1 << 20) : (size_t)left, got = SDL_ReadIO(in, buf, want);
        if (!got || SDL_WriteIO(out, buf, got) != got) {
            bad = 1;
            break;
        }
    }
    free(buf);
    SDL_CloseIO(in);
    SDL_CloseIO(out);
    if (bad)
        return 1;
    rm_tree(l->tools);
    SDL_CreateDirectory(l->tools);
    bad = unzip_into(l, zp, l->tools, &u->p, "payload");
    SDL_RemovePath(zp);
    if (bad)
        return 1;
    join(marker, sizeof marker, l->tools, "release.txt");
    return write_text(marker, l->release) ? 0 : 1;
}

/* ── the compiler (toolchain.txt format 2, as the Xbox launcher's) ─────── */
/*
 *   format  2
 *   clang   llvm/bin/clang++                the compiler driver (C with -x c)
 *   python  python/bin/python3.12           the lifter's Python
 *   bundle  bundle/<zip> <sha256> <size> <folder in the zip> <folder under tools>
 *                                           optional: a shipped zip unpacked at the first start
 */
typedef struct {
    int format;
    char clang[LP], python[LP];
    char zip[LP], sha[65], folder[256], dest[256];
    Sint64 size;
    Prog unpack;
} Toolchain;

static int safe_rel(const char *p)
{
    return p[0] && p[0] != '/' && p[0] != '\\' && !strchr(p, ':') && !strstr(p, "..");
}

static int read_toolchain(L *l, Toolchain *t)
{
    char p[LP], *text, *line, *save = NULL;
    memset(t, 0, sizeof *t);
    join(p, sizeof p, l->tools, "toolchain.txt");
    if (!(text = read_text(p)))
        return 0;
    for (line = SDL_strtok_r(text, "\r\n", &save); line; line = SDL_strtok_r(NULL, "\r\n", &save)) {
        char *argv[8];
        int n = split_tabs(line, argv, 8);
        if (n < 2 || argv[0][0] == '#')
            continue;
        if (!strcmp(argv[0], "format")) t->format = SDL_atoi(argv[1]);
        else if (!strcmp(argv[0], "clang")) snprintf(t->clang, sizeof t->clang, "%s", argv[1]);
        else if (!strcmp(argv[0], "python")) snprintf(t->python, sizeof t->python, "%s", argv[1]);
        else if (!strcmp(argv[0], "bundle") && n >= 6) {
            snprintf(t->zip, sizeof t->zip, "%s", argv[1]);
            snprintf(t->sha, sizeof t->sha, "%s", argv[2]);
            t->size = (Sint64)SDL_strtoll(argv[3], NULL, 10);
            snprintf(t->folder, sizeof t->folder, "%s", argv[4]);
            snprintf(t->dest, sizeof t->dest, "%s", argv[5]);
        }
    }
    SDL_free(text);
    if (t->format != 2 || !safe_rel(t->clang) || !safe_rel(t->python))
        return 0;
    if (t->zip[0] && (!safe_rel(t->zip) || !safe_rel(t->dest) || strlen(t->sha) != 64 || t->size <= 0))
        return 0;
    join(l->clang, sizeof l->clang, l->tools, t->clang);
    join(l->python, sizeof l->python, l->tools, t->python);
    return 1;
}

static int file_sha256(const char *p, char hex[65])
{
    SDL_IOStream *f = SDL_IOFromFile(p, "rb");
    XrSha256 s;
    uint8_t d[32];
    void *buf = malloc(1 << 20);
    size_t got;
    if (!f || !buf) {
        if (f)
            SDL_CloseIO(f);
        free(buf);
        return 0;
    }
    xr_sha256_init(&s);
    while ((got = SDL_ReadIO(f, buf, 1 << 20)) > 0)
        xr_sha256_update(&s, buf, got);
    SDL_CloseIO(f);
    free(buf);
    xr_sha256_final(&s, d);
    xr_sha256_hex(d, hex);
    return 1;
}

static int check_job(L *l, void *a)
{
    Toolchain *t = (Toolchain *)a;
    char hex[65];
    if (!file_sha256(t->zip, hex))
        return 1;
    lg(l, "compiler bundle sha256 %s (expected %s)", hex, t->sha);
    return SDL_strcasecmp(hex, t->sha) ? 2 : 0;
}

static double unpack_frac(void *a)
{
    return prog_frac(&((Toolchain *)a)->unpack);
}

static int unpack_tc_job(L *l, void *a)
{
    Toolchain *t = (Toolchain *)a;
    char tmp[LP], from[LP], to[LP], marker[LP];
    join(tmp, sizeof tmp, l->tools, "bundle/unpack");
    rm_tree(tmp);
    SDL_CreateDirectory(tmp);
    if (unzip_into(l, t->zip, tmp, &t->unpack, "compiler zip"))
        return 1;
    join(from, sizeof from, tmp, t->folder);
    join(to, sizeof to, l->tools, t->dest);
    rm_tree(to);
    if (!SDL_RenamePath(from, to)) {
        lg(l, "cannot move %s to %s: %s", from, to, SDL_GetError());
        return 1;
    }
    rm_tree(tmp);
    join(marker, sizeof marker, to, ".sha256");
    return write_text(marker, t->sha) ? 0 : 1;
}

static int ensure_toolchain(L *l)
{
    Toolchain t;
    char marker[LP], *have;
    if (!read_toolchain(l, &t)) {
        lg(l, "toolchain.txt missing, damaged or not format 2 in %s", l->tools);
        xr_setup_ui_message(l->ui, 1, PACKAGE_INCOMPLETE);
        return 0;
    }
    if (t.zip[0]) {
        char dest[LP], zrel[LP];
        join(dest, sizeof dest, l->tools, t.dest);
        join(marker, sizeof marker, dest, ".sha256");
        have = read_text(marker);
        if (have && !SDL_strcasecmp(have, t.sha) && is_file(l->clang) && is_file(l->python)) {
            SDL_free(have);
            return 1;
        }
        SDL_free(have);
        snprintf(zrel, sizeof zrel, "%s", t.zip);
        join(t.zip, sizeof t.zip, l->tools, zrel);
        if (file_size(t.zip) != t.size) {
            lg(l, "compiler bundle %s: size %lld, expected %lld", t.zip, (long long)file_size(t.zip),
               (long long)t.size);
            xr_setup_ui_message(l->ui, 1, PACKAGE_INCOMPLETE);
            return 0;
        }
        if (run_job(l, "Checking the compiler", 0.02, 0.02, check_job, &t, NULL)) {
            xr_setup_ui_message(l->ui, 1, PACKAGE_INCOMPLETE);
            return 0;
        }
        if (run_job(l, "Unpacking the compiler (once)", 0.04, 0.06, unpack_tc_job, &t, unpack_frac)) {
            xr_setup_ui_message(l->ui, 1, "The compiler could not be unpacked - see data\\launcher.log.");
            return 0;
        }
    }
    if (!is_file(l->clang) || !is_file(l->python)) {
        lg(l, "compiler %s or python %s missing", l->clang, l->python);
        xr_setup_ui_message(l->ui, 1, PACKAGE_INCOMPLETE);
        return 0;
    }
    return 1;
}

/* ── lift.txt, each line "<name> <script> <args...>" run with the shipped Python (check, extract, lift ...) ── */

/* only: just that line; else every line but skip[] ("check", "extract": run by themselves) */
typedef struct { Prog p; const char *only; const char *skip[4]; } LiftArg;

static int lift_skipped(const LiftArg *la, const char *name)
{
    int i;
    if (la->only)
        return strcmp(name, la->only) != 0;
    for (i = 0; i < 4; i++)
        if (la->skip[i] && !strcmp(name, la->skip[i]))
            return 1;
    return 0;
}

static int lift_job(L *l, void *a)
{
    LiftArg *la = (LiftArg *)a;
    char p[LP], *text, *line, *save = NULL, logp[LP];
    int lines = 0, rc = 0;
    join(p, sizeof p, l->tools, "lift.txt");
    if (!(text = read_text(p)))
        return 1;
    {   /* the progress: the lines this run runs */
        char *copy = SDL_strdup(text), *sv = NULL, *ln;
        for (ln = SDL_strtok_r(copy, "\r\n", &sv); ln; ln = SDL_strtok_r(NULL, "\r\n", &sv)) {
            const char *tab = strchr(ln, '\t');
            char name[64];
            if (!tab || ln[0] == '#' || (size_t)(tab - ln) >= sizeof name)
                continue;
            memcpy(name, ln, (size_t)(tab - ln));
            name[tab - ln] = 0;
            if (!lift_skipped(la, name))
                lines++;
        }
        SDL_free(copy);
    }
    la->p.total = lines ? lines : 1;
    join(logp, sizeof logp, l->build, "lift.log");
    for (line = SDL_strtok_r(text, "\r\n", &save); line && !rc && !cancelled(l); line = SDL_strtok_r(NULL, "\r\n", &save)) {
        static char exp[MAX_ARGS][LP];
        char *raw[MAX_ARGS], *argv[MAX_ARGS + 4];
        int n = split_tabs(line, raw, MAX_ARGS), i, k = 0;
        if (n < 2 || raw[0][0] == '#')
            continue;
        if (lift_skipped(la, raw[0]))
            continue;
        argv[k++] = l->python;
        argv[k++] = "-I";
        argv[k++] = "-B";
        for (i = 1; i < n && k < MAX_ARGS + 2; i++) {
            expand(l, raw[i], exp[i], LP);
            argv[k++] = exp[i];
        }
        argv[k] = NULL;
        lg(l, "lift: %s", raw[0]);
        rc = run_proc(l, argv, l->tools, logp);
        if (rc)
            lg(l, "lift step %s: exit %d - see %s", raw[0], rc, logp);
        SDL_AddAtomicInt(&la->p.done_n, 1);
    }
    SDL_free(text);
    return rc;
}

/* ── compile + link: build.txt ─────────────────────────────────────────── */
/*
 *   cc    <arg>...           the compile line's arguments (before -c <src> -o <obj>)
 *   glob  <folder> <ext>     every file of that extension in the folder is compiled
 *   src   <file>             one more file to compile
 *   obj   <file>             a prebuilt object (the port's own code) - linked, not compiled
 *   link  <arg>...           the link line; {OBJECTS} = the compiled and the prebuilt objects
 *   copy  <from> <to>        a file the game needs beside it (libSDL3.so.0), copied after the link
 */
#define MAX_UNITS 512
typedef struct {
    Prog p;
    char *cc[MAX_ARGS];
    int ncc;
    char src[MAX_UNITS][LP], objp[MAX_UNITS][LP];
    Sint64 srcsize[MAX_UNITS];
    int nsrc;
    char pre[32][LP];
    int npre;
    SDL_AtomicInt next, failed;
    char *link[MAX_ARGS];
    int nlink;
    char cpf[16][LP], cpt[16][LP];
    int ncp;
    int jobs;
    char *text;
    char argbuf[MAX_ARGS * 3][LP];
    int nargbuf;
} Build;

static char *bstore(L *l, Build *b, const char *raw)
{
    char *d;
    if (b->nargbuf >= MAX_ARGS * 3)
        return (char *)raw;
    d = b->argbuf[b->nargbuf++];
    expand(l, raw, d, LP);
    return d;
}

typedef struct { Build *b; const char *ext; } GlobCtx;

static SDL_EnumerationResult SDLCALL glob_cb(void *u, const char *dir, const char *name)
{
    GlobCtx *g = (GlobCtx *)u;
    size_t k = strlen(name), e = strlen(g->ext);
    if (k > e && !SDL_strcasecmp(name + k - e, g->ext) && g->b->nsrc < MAX_UNITS) {
        join(g->b->src[g->b->nsrc], LP, dir, name);
        g->b->srcsize[g->b->nsrc] = file_size(g->b->src[g->b->nsrc]);
        g->b->nsrc++;
    }
    return SDL_ENUM_CONTINUE;
}

static int read_build(L *l, Build *b)
{
    char p[LP], *line, *save = NULL;
    join(p, sizeof p, l->tools, "build.txt");
    if (!(b->text = read_text(p)))
        return 0;
    for (line = SDL_strtok_r(b->text, "\r\n", &save); line; line = SDL_strtok_r(NULL, "\r\n", &save)) {
        char *raw[MAX_ARGS], dir[LP];
        int n = split_tabs(line, raw, MAX_ARGS), i;
        if (n < 1 || raw[0][0] == '#')
            continue;
        if (!strcmp(raw[0], "cc")) {
            for (i = 1; i < n && b->ncc < MAX_ARGS - 1; i++)
                b->cc[b->ncc++] = bstore(l, b, raw[i]);
        } else if (!strcmp(raw[0], "glob") && n > 2) {
            GlobCtx g = { b, raw[2] };
            expand(l, raw[1], dir, LP);
            SDL_EnumerateDirectory(dir, glob_cb, &g);
        } else if (!strcmp(raw[0], "src") && n > 1 && b->nsrc < MAX_UNITS) {
            expand(l, raw[1], b->src[b->nsrc], LP);
            b->srcsize[b->nsrc] = file_size(b->src[b->nsrc]);
            b->nsrc++;
        } else if (!strcmp(raw[0], "obj") && n > 1 && b->npre < 32) {
            expand(l, raw[1], b->pre[b->npre++], LP);
        } else if (!strcmp(raw[0], "copy") && n > 2 && b->ncp < 16) {
            expand(l, raw[1], b->cpf[b->ncp], LP);
            expand(l, raw[2], b->cpt[b->ncp], LP);
            b->ncp++;
        } else if (!strcmp(raw[0], "link")) {
            for (i = 1; i < n && b->nlink < MAX_ARGS - 1; i++)
                b->link[b->nlink++] = bstore(l, b, raw[i]);
        }
    }
    return b->ncc > 0 && b->nsrc > 0 && b->nlink > 0;
}

typedef struct { L *l; Build *b; int *order; } Worker;

static int SDLCALL compile_worker(void *p)
{
    Worker *w = (Worker *)p;
    Build *b = w->b;
    for (;;) {
        int n = SDL_AddAtomicInt(&b->next, 1), i, k = 0, j;
        char *argv[MAX_ARGS + 8], logp[LP];
        if (n >= b->nsrc || cancelled(w->l))
            break;
        i = w->order[n];
        argv[k++] = w->l->clang;
        for (j = 0; j < b->ncc; j++)
            argv[k++] = b->cc[j];
        argv[k++] = "-c";
        argv[k++] = b->src[i];
        argv[k++] = "-o";
        argv[k++] = b->objp[i];
        argv[k] = NULL;
        snprintf(logp, sizeof logp, "%s.log", b->objp[i]);
        SDL_RemovePath(logp);
        if (run_proc(w->l, argv, w->l->root, logp) != 0) {
            if (!cancelled(w->l))
                lg(w->l, "compile failed: %s - see %s", b->src[i], logp);
            SDL_AddAtomicInt(&b->failed, 1);
        }
        SDL_AddAtomicInt(&b->p.done_n, 1);
    }
    return 0;
}

/* Compilers at once: the logical processors, but a compiler of the largest lifted files takes 2 GB and more - at
 * most one per 2.5 GB of memory (port.py's rule); GTA2_LAUNCHER_JOBS caps it further. */
static int compile_jobs(void)
{
    int n = SDL_GetNumLogicalCPUCores(), by_mem = (int)(SDL_GetSystemRAM() / 2560);
    const char *cap = SDL_getenv("GTA2_LAUNCHER_JOBS");
    if (by_mem > 0 && by_mem < n)
        n = by_mem;
    if (cap && SDL_atoi(cap) > 0 && SDL_atoi(cap) < n)
        n = SDL_atoi(cap);
    return n < 1 ? 1 : n > 64 ? 64 : n;
}

static int compile_job(L *l, void *a)
{
    Build *b = (Build *)a;
    int n = b->jobs, i, j;
    static int order[MAX_UNITS];
    SDL_Thread *t[64];
    Worker w;
    if (n > b->nsrc)
        n = b->nsrc;
    b->p.total = b->nsrc;
    for (i = 0; i < b->nsrc; i++) {
        const char *s = strrchr(b->src[i], '/'), *bs = strrchr(b->src[i], '\\');
        char name[LP];
        if (bs > s)
            s = bs;
        snprintf(name, sizeof name, "%03d_%s.o", i, s ? s + 1 : b->src[i]);
        join(b->objp[i], LP, l->obj, name);
        order[i] = i;
    }
    /* the largest files first: the longest compiles start at once, the small ones fill in after */
    for (i = 1; i < b->nsrc; i++)
        for (j = i; j > 0 && b->srcsize[order[j]] > b->srcsize[order[j - 1]]; j--) {
            int x = order[j];
            order[j] = order[j - 1];
            order[j - 1] = x;
        }
    w.l = l; w.b = b; w.order = order;
    lg(l, "compiling %d files on %d threads", b->nsrc, n);
    for (i = 0; i < n; i++)
        t[i] = SDL_CreateThread(compile_worker, "compile", &w);
    for (i = 0; i < n; i++)
        if (t[i])
            SDL_WaitThread(t[i], NULL);
    return SDL_GetAtomicInt(&b->failed) ? 1 : 0;
}

static int link_job(L *l, void *a)
{
    Build *b = (Build *)a;
    static char *argv[MAX_UNITS + MAX_ARGS + 40];
    char logp[LP];
    int k = 0, i, j;
    join(logp, sizeof logp, l->build, "link.log");
    SDL_RemovePath(logp);
    argv[k++] = l->clang;
    for (i = 0; i < b->nlink; i++) {
        if (strcmp(b->link[i], "{OBJECTS}")) {
            argv[k++] = b->link[i];
            continue;
        }
        for (j = 0; j < b->npre; j++)
            argv[k++] = b->pre[j];
        for (j = 0; j < b->nsrc; j++)
            argv[k++] = b->objp[j];
    }
    argv[k] = NULL;
    return run_proc(l, argv, l->root, logp) != 0;
}
/* ── the target folder (issue 12) ─────── */

/* A folder's path without the separators at its end, for comparing. */
static void plain_dir(const char *in, char *out, size_t n)
{
    size_t k;
    snprintf(out, n, "%s", in);
    k = strlen(out);
    while (k > 1 && (out[k - 1] == '/' || out[k - 1] == '\\'))
        out[--k] = 0;
}

static int same_dir(const char *a, const char *b)
{
    char x[LP], y[LP];
    size_t i;
    plain_dir(a, x, sizeof x);
    plain_dir(b, y, sizeof y);
    for (i = 0; x[i]; i++) if (x[i] == '\\') x[i] = '/';
    for (i = 0; y[i]; i++) if (y[i] == '\\') y[i] = '/';
#ifdef _WIN32
    return !SDL_strcasecmp(x, y);
#else
    return !strcmp(x, y);
#endif
}

static const char *base_name(const char *p)
{
    const char *a = strrchr(p, '/'), *b = strrchr(p, '\\');
    const char *s = a > b ? a : b;
    return s ? s + 1 : p;
}

typedef struct { const char *from[3]; const char *to[3]; Sint64 total; SDL_AtomicInt done_mb; } CopyArg;

/* The files copied (never moved: the player's own files stay where they are - the maintainer's rule), chunk by
 * chunk; one already there with the same size is kept. */
static int copy_job(L *l, void *a)
{
    CopyArg *c = (CopyArg *)a;
    Sint64 done = 0;
    int i;
    char *buf = (char *)SDL_malloc(4 << 20);
    if (!buf)
        return 1;
    for (i = 0; i < 3 && c->from[i]; i++) {
        SDL_IOStream *in, *out;
        size_t got;
        char part[LP];
        if (file_size(c->to[i]) == file_size(c->from[i])) {
            done += file_size(c->from[i]);
            lg(l, "%s is there already", c->to[i]);
            continue;
        }
        snprintf(part, sizeof part, "%s.part", c->to[i]);
        in = SDL_IOFromFile(c->from[i], "rb");
        out = SDL_IOFromFile(part, "wb");
        if (!in || !out) {
            lg(l, "cannot copy %s to %s: %s", c->from[i], c->to[i], SDL_GetError());
            if (in) SDL_CloseIO(in);
            if (out) SDL_CloseIO(out);
            SDL_free(buf);
            return 1;
        }
        while ((got = SDL_ReadIO(in, buf, 4 << 20)) > 0) {
            if (SDL_WriteIO(out, buf, got) != got || (l->ui && xr_setup_ui_cancelled(l->ui))) {
                SDL_CloseIO(in);
                SDL_CloseIO(out);
                SDL_RemovePath(part);
                SDL_free(buf);
                return 1;
            }
            done += (Sint64)got;
            SDL_SetAtomicInt(&c->done_mb, (int)(done >> 20));
        }
        SDL_CloseIO(in);
        if (!SDL_CloseIO(out) || !SDL_RenamePath(part, c->to[i])) {
            lg(l, "cannot finish %s: %s", c->to[i], SDL_GetError());
            SDL_free(buf);
            return 1;
        }
        lg(l, "copied %s to %s", c->from[i], c->to[i]);
    }
    SDL_free(buf);
    return 0;
}

static double copy_frac(void *a)
{
    CopyArg *c = (CopyArg *)a;
    const Sint64 mb = c->total >> 20;
    return (double)SDL_GetAtomicInt(&c->done_mb) / (double)(mb > 0 ? mb : 1);
}


/* The port's own optional file (GtaLauncherConfig.extra_file_*: GTA2-translations.pak) copied into `folder` (the
 * game's folder, WR_ROOT) under its name, where the game looks for it - the player's download stays where it is.
 * 0: failed. */
static int place_file(L *l, const char *folder, const char *file)
{
    char to[LP];
    CopyArg c;
    if (!l->cfg->extra_file_name || !file || !file[0])
        return 1;
    join(to, sizeof to, folder, l->cfg->extra_file_name);
    if (!strcmp(file, to))
        return 1;
    memset(&c, 0, sizeof c);
    c.from[0] = file;
    c.to[0] = to;
    c.total = file_size(file);
    SDL_RemovePath(to);                 /* (a newer one of the same size too) */
    if (run_job(l, "Copying the translations", 0.99, 0.01, copy_job, &c, copy_frac)) {
        xr_setup_ui_message(l->ui, 1, "Copying the file failed - see data\\launcher.log.");
        return 0;
    }
    lg(l, "%s copied to %s", file, to);
    return 1;
}

/* Another target folder than the exe's: this exe copied there and started there with the choices (--go), this one
 * ends. 1: started. */
static int move_to_target(L *l, const char *target, const char *file)
{
    char exe_to[LP], *argv[12];
    int k = 0;
    SDL_PropertiesID p;
    SDL_Process *proc;
    CopyArg c;
    if (!SDL_CreateDirectory(target)) {
        lg(l, "cannot make %s: %s", target, SDL_GetError());
        xr_setup_ui_message(l->ui, 1, "The target folder cannot be made - choose another one.");
        return 0;
    }
    {   /* the file the player has: the AppImage when one was started (this exe is inside its mount), else this exe */
        const char *from = l->appimage[0] ? l->appimage : l->exe;
        join(exe_to, sizeof exe_to, target, base_name(from));
        memset(&c, 0, sizeof c);
        c.from[0] = from;
        c.to[0] = exe_to;
        c.total = file_size(from);
    }
    if (run_job(l, "Copying the game into the target folder", 0.0, 0.02, copy_job, &c, copy_frac)) {
        xr_setup_ui_message(l->ui, 1, "The game cannot be copied into the target folder - see data\\launcher.log.");
        return 0;
    }
#ifndef _WIN32
    chmod(exe_to, 0755);                /* (the copy a program again: the AppImage) */
#endif
    argv[k++] = exe_to;
    argv[k++] = "--go";
    if (l->input[0]) {
        argv[k++] = "--installer";
        argv[k++] = l->input;
    }
    if (file && file[0]) {
        argv[k++] = "--file";
        argv[k++] = (char *)file;
    }
    argv[k] = NULL;
    p = SDL_CreateProperties();
    SDL_SetPointerProperty(p, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, argv);
    SDL_SetStringProperty(p, SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING, target);
    SDL_SetBooleanProperty(p, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
    proc = SDL_CreateProcessWithProperties(p);
    SDL_DestroyProperties(p);
    if (!proc) {
        lg(l, "cannot start %s: %s", exe_to, SDL_GetError());
        return 0;
    }
    lg(l, "installing in %s: started %s", target, exe_to);
    SDL_DestroyProcess(proc);
    return 1;
}
/* ── the installer ─────────────────────────────────────────────────────── */

/* data/launcher.ini (the launcher's own, key=value lines): "installer", the copy the last build used - only the setup
 * page's pre-fill (the maintainer's rule: after the first start nothing refers to the player's installer or folder;
 * every file the game and a rebuild need is a real copy in game/) */
static int ini_get(L *l, const char *key, char *out, size_t n)
{
    char p[LP], *t, *line, *save = NULL;
    const size_t k = strlen(key);
    int found = 0;
    join(p, sizeof p, l->data, "launcher.ini");
    if (!(t = read_text(p)))
        return 0;
    for (line = SDL_strtok_r(t, "\r\n", &save); line; line = SDL_strtok_r(NULL, "\r\n", &save))
        if (!strncmp(line, key, k) && line[k] == '=' && line[k + 1]) {
            snprintf(out, n, "%s", line + k + 1);
            found = 1;
        }
    SDL_free(t);
    return found;
}

static void ini_set(L *l, const char *key, const char *value)
{
    char p[LP], *t, *line, *save = NULL, *outbuf;
    const size_t k = strlen(key);
    size_t cap;
    join(p, sizeof p, l->data, "launcher.ini");
    t = read_text(p);
    cap = (t ? strlen(t) : 0) + k + strlen(value) + 16;
    if (!(outbuf = (char *)calloc(1, cap))) {
        SDL_free(t);
        return;
    }
    if (t)
        for (line = SDL_strtok_r(t, "\r\n", &save); line; line = SDL_strtok_r(NULL, "\r\n", &save))
            if (strncmp(line, key, k) || line[k] != '=') {
                strcat(outbuf, line);
                strcat(outbuf, "\n");
            }
    if (*value) {
        strcat(outbuf, key);
        strcat(outbuf, "=");
        strcat(outbuf, value);
        strcat(outbuf, "\n");
    }
    if (!write_text(p, outbuf))
        lg(l, "cannot write %s", p);
    free(outbuf);
    SDL_free(t);
}

typedef struct { const char *name; char found[LP]; } FindFile;

static SDL_EnumerationResult SDLCALL find_cb(void *u, const char *dir, const char *name)
{
    FindFile *f = (FindFile *)u;
    char p[LP];
    if (SDL_strcasecmp(name, f->name))
        return SDL_ENUM_CONTINUE;
    join(p, sizeof p, dir, name);
    if (!is_file(p))
        return SDL_ENUM_CONTINUE;
    snprintf(f->found, sizeof f->found, "%s", p);
    return SDL_ENUM_SUCCESS;
}

/* an installed GTA 2 beside the AppImage: a folder there (not the game's own game/ data/ saves/ media/) with
 * gta2.exe in it or one level below (setup_build.py looks up to 3 levels down and checks it) */
typedef struct { L *l; int depth; char found[LP]; } FindGame;

static SDL_EnumerationResult SDLCALL game_dir_cb(void *u, const char *dir, const char *name)
{
    static const char *const own[] = { "game", "data", "saves", "media" };
    FindGame *g = (FindGame *)u;
    FindFile f;
    char p[LP];
    size_t i;
    join(p, sizeof p, dir, name);
    if (!is_dir(p))
        return SDL_ENUM_CONTINUE;
    if (g->depth == 0)
        for (i = 0; i < sizeof own / sizeof *own; i++)
            if (!SDL_strcasecmp(name, own[i]))
                return SDL_ENUM_CONTINUE;
    memset(&f, 0, sizeof f);
    f.name = "gta2.exe";
    SDL_EnumerateDirectory(p, find_cb, &f);
    if (f.found[0]) {
        snprintf(g->found, sizeof g->found, "%s", p);
        return SDL_ENUM_SUCCESS;
    }
    if (g->depth == 0) {
        g->depth = 1;
        SDL_EnumerateDirectory(p, game_dir_cb, g);
        g->depth = 0;
        if (g->found[0])
            return SDL_ENUM_SUCCESS;
    }
    return SDL_ENUM_CONTINUE;
}

/* the player's copy: --installer / --folder, the one the last build used (prefill: only for the setup page, never
 * taken by itself), GTA2.exe beside the AppImage (any case), an installed GTA 2 folder beside it */
static int find_installer(L *l, const char *arg, int prefill)
{
    FindFile f;
    FindGame g;
    char p[LP];
    if (arg && is_input(arg)) {
        snprintf(l->input, sizeof l->input, "%s", arg);
        return 1;
    }
    if (prefill && ini_get(l, "installer", p, sizeof p) && is_input(p)) {
        snprintf(l->input, sizeof l->input, "%s", p);
        return 1;
    }
    memset(&f, 0, sizeof f);
    f.name = l->cfg->input_file;
    SDL_EnumerateDirectory(l->root, find_cb, &f);
    if (f.found[0]) {
        snprintf(l->input, sizeof l->input, "%s", f.found);
        return 1;
    }
    memset(&g, 0, sizeof g);
    g.l = l;
    SDL_EnumerateDirectory(l->root, game_dir_cb, &g);
    if (g.found[0]) {
        snprintf(l->input, sizeof l->input, "%s", g.found);
        return 1;
    }
    return 0;
}

typedef struct { const char *path; char hex[65]; } ShaArg;

static int sha_job(L *l, void *a)
{
    ShaArg *s = (ShaArg *)a;
    (void)l;
    return file_sha256(s->path, s->hex) ? 0 : 1;
}

/* the build's stamp: the release and the game it was built from - game/gta2.exe's SHA-256 (the copy in the game's
 * folder, the same whether it came from the installer, a folder or an earlier build) */
static void stamp_text(L *l, const char *from, char *out, size_t n)
{
    snprintf(out, n, "%s\ngta2.exe %s\n", l->release, from && *from ? from : "?");
}

/* built: the stamp's release is this one, the game and its files there */
static int built_already(L *l)
{
    char p[LP], *have, *nl;
    int ok;
    join(p, sizeof p, l->build, "stamp.txt");
    if (!(have = read_text(p)))
        return 0;
    if ((nl = strchr(have, '\n')))
        *nl = 0;
    join(p, sizeof p, l->game, "gta2.exe");
    ok = !strcmp(have, l->release) && is_file(l->out) && is_file(p);
    SDL_free(have);
    return ok;
}

/* ── the game ──────────────────────────────────────────────────────────── */

/* build.txt's copy lines: the files the game needs beside it in data/ (libSDL3.so.0) */
static int copy_libs(L *l, Build *b)
{
    int i;
    for (i = 0; i < b->ncp; i++) {
        void *data;
        size_t n;
        if (!(data = SDL_LoadFile(b->cpf[i], &n)) || !SDL_SaveFile(b->cpt[i], data, n)) {
            lg(l, "cannot copy %s to %s: %s", b->cpf[i], b->cpt[i], SDL_GetError());
            SDL_free(data);
            return 0;
        }
        SDL_free(data);
#ifndef _WIN32
        chmod(b->cpt[i], 0755);
#endif
        lg(l, "copied %s", b->cpt[i]);
    }
    return 1;
}

/* the game, started in its folder with WR_ROOT = that folder (it is built into data/: its own folder is not the
 * game's); the launcher ends, the game goes on */
static int start_game(L *l)
{
    char *argv[] = { l->out, NULL }, root[LP];
    SDL_PropertiesID p = SDL_CreateProperties();
    SDL_Environment *env = SDL_CreateEnvironment(true);
    SDL_Process *proc;
    plain_dir(l->root, root, sizeof root);
    if (env) {
        SDL_SetEnvironmentVariable(env, "WR_ROOT", root, true);
        SDL_SetPointerProperty(p, SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER, env);
    }
    SDL_SetPointerProperty(p, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, argv);
    SDL_SetStringProperty(p, SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING, root);
    SDL_SetNumberProperty(p, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(p, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(p, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetBooleanProperty(p, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
    proc = SDL_CreateProcessWithProperties(p);
    SDL_DestroyProperties(p);
    if (env)
        SDL_DestroyEnvironment(env);
    if (!proc) {
        lg(l, "cannot start %s: %s", l->out, SDL_GetError());
        return 0;
    }
    lg(l, "started %s (WR_ROOT=%s)", l->out, root);
    SDL_DestroyProcess(proc);
    return 1;
}

/* ── main ──────────────────────────────────────────────────────────────── */

int gta_launcher_main(const GtaLauncherConfig *cfg, int argc, char **argv)
{
    static L l;
    const char *input_arg = NULL, *file_arg = NULL, *base;
    int repair = 0, no_start = 0, setup = 0, i, ok = 0, rc, found = 0, form = -1, go = 0, copy = 1, building = 0;
    int game_ready;
    char target[LP] = "", file[LP] = "", none[4] = "";
    char p[LP], stamp_want[LP + 128], from_sha[80] = "", logp[LP];
    Uint64 payload_ofs = 0;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--repair")) repair = 1;
        else if (!strcmp(argv[i], "--setup")) setup = 1;      /* the setup page even when built */
        else if (!strcmp(argv[i], "--no-start")) no_start = 1;
        else if ((!strcmp(argv[i], "--installer") || !strcmp(argv[i], "--folder") || !strcmp(argv[i], "--image")) &&
                 i + 1 < argc)
            input_arg = argv[++i];     /* (a file or a folder either way: told apart by what it is) */
        /* (the setup page's choices, given on by the copy of the program in the target folder) */
        else if (!strcmp(argv[i], "--go")) go = 1;
        else if (!strcmp(argv[i], "--file") && i + 1 < argc) file_arg = argv[++i];
        /* the same as one argument each (RecompLauncher passes an optional file so: the argument dropped whole when
           the player picked none) */
        else if (!strncmp(argv[i], "--installer=", 12)) input_arg = argv[i] + 12;
        else if (!strncmp(argv[i], "--folder=", 9)) input_arg = argv[i] + 9;
        else if (!strncmp(argv[i], "--image=", 8)) input_arg = argv[i] + 8;
        else if (!strncmp(argv[i], "--file=", 7)) file_arg = argv[i] + 7;
    }
    memset(&l, 0, sizeof l);
    l.cfg = cfg;
    SDL_Init(SDL_INIT_VIDEO);
    base = SDL_GetBasePath();
    snprintf(l.root, sizeof l.root, "%s", base ? base : "./");
    {
        /* the root, the game's folder, in the three ways the launcher runs (as ZOE's):
         *   the AppImage started (its runtime sets an absolute APPIMAGE): the AppImage's folder - the launcher itself
         *     runs from the AppImage's read-only mount;
         *   the AppImage unpacked and its AppRun started without APPIMAGE (RecompLauncher): an absolute APPDIR with
         *     this program under it;
         *   the tar.gz's launcher started: its own folder (SDL_GetBasePath).
         * The payload is on this program itself (usr/bin/gta2-launcher in the AppDir, the tar.gz's gta2_launcher). */
        char self[LP] = "", real[LP];
        const char *exe_name = SDL_getenv("GTA2_LAUNCHER_SELF");    /* tests: the program the payload is on */
        const char *appimage = SDL_getenv("APPIMAGE"), *appdir = SDL_getenv("APPDIR");
        ssize_t n = readlink("/proc/self/exe", real, sizeof real - 1);
        real[n > 0 ? n : 0] = 0;
        snprintf(self, sizeof self, "%s", real[0] ? real : "/proc/self/exe");
        if (appimage && appimage[0] == '/') {
            char *slash;
            snprintf(l.appimage, sizeof l.appimage, "%s", appimage);
            snprintf(l.root, sizeof l.root, "%s", appimage);
            slash = strrchr(l.root, '/');
            if (slash)
                slash[1] = 0;
            else
                snprintf(l.root, sizeof l.root, "./");
        } else if (appdir && appdir[0] == '/' && real[0]) {
            size_t k = strlen(appdir);
            while (k > 1 && appdir[k - 1] == '/')
                --k;
            if (!strncmp(real, appdir, k) && real[k] == '/')
                snprintf(l.root, sizeof l.root, "%.*s/", (int)k, appdir);
        }
        snprintf(l.exe, sizeof l.exe, "%s", exe_name && *exe_name ? exe_name : self);
    }
    join(l.data, sizeof l.data, l.root, "data");
    join(l.out, sizeof l.out, l.data, cfg->game_exe);   /* the game in data/: only the launcher on top */
    join(l.tools, sizeof l.tools, l.data, "tools");
    join(l.build, sizeof l.build, l.data, "build");
    join(l.work, sizeof l.work, l.build, "work");
    join(l.obj, sizeof l.obj, l.build, "obj");
    join(l.game, sizeof l.game, l.root, "game");
    snprintf(l.jobs, sizeof l.jobs, "%d", compile_jobs());
    SDL_CreateDirectory(l.data);
    l.log_mx = SDL_CreateMutex();
    l.proc_mx = SDL_CreateMutex();
    join(logp, sizeof logp, l.data, "launcher.log");
    l.log = SDL_IOFromFile(logp, "wb");
    lg(&l, "%s: launcher in %s", cfg->window_title, l.root);
    l.ui = xr_setup_ui_open(cfg->window_title);
    xr_setup_ui_icon(l.ui, cfg->icon_bmp, cfg->icon_bmp_size);
    xr_setup_ui_main_line(l.ui, cfg->input_label, cfg->input_filter_name, cfg->input_filter, "installer or folder", 1);

    if (!read_trailer(&l, &payload_ofs)) {
        xr_setup_ui_message(l.ui, 1, PACKAGE_INCOMPLETE);
        goto out;
    }
    lg(&l, "release %s", l.release);
    found = find_installer(&l, input_arg, 0);
    if (input_arg && !found)
        lg(&l, "--installer / --folder %s: no such file or folder", input_arg);
    join(p, sizeof p, l.game, "gta2.exe");
    game_ready = is_file(p);                /* the game's files of an earlier build (checked again below) */
    /* the port's own optional file (the translations): its line pre-filled with the copy in the game's folder */
    if (cfg->extra_file_label && cfg->extra_file_name) {
        char have[LP];
        join(have, sizeof have, l.root, cfg->extra_file_name);
        if (is_file(have))
            snprintf(file, sizeof file, "%s", have);
        if (file_arg)
            snprintf(file, sizeof file, "%s", file_arg);
        xr_setup_ui_file_line(l.ui, cfg->extra_file_label, cfg->extra_file_filter_name, cfg->extra_file_filter, file,
                              sizeof file);
    }
    if (go) {                               /* the choices made on the setup page of the copy this one came from */
        setup = 1;
        form = 1;
    }
    if (!repair && !setup && built_already(&l)) {     /* built for this release: straight into the game */
        lg(&l, "built already");
        progress_line(1.0, "Already built");
        ok = no_start || start_game(&l);
        goto out;
    }
    /* to be built: the setup page (the installer's line, the translations', the target folder, Go / Quit) - not when
       the game's files are there from an earlier build (a new release: built from them, no installer needed) */
    if (!go && (setup || !game_ready)) {
        if (!found)
            find_installer(&l, NULL, 1);    /* (the last one used: the line pre-filled, nothing more) */
        plain_dir(l.root, target, sizeof target);
        form = xr_setup_ui_form(l.ui, cfg->input, l.input, sizeof l.input, NULL, NULL, none, 0, target, sizeof target,
                                &copy);
        if (form == 0)
            goto out;
        if (form > 0) {
            found = is_input(l.input);
            lg(&l, "copy chosen on the setup page: %s; target folder %s", l.input, target);
            if (target[0] && !same_dir(target, l.root)) {     /* installed there: that copy of this program goes on */
                ok = move_to_target(&l, target, file);
                goto out;
            }
        }
    }
    if (found && is_dir(l.input))
        lg(&l, "installed GTA 2 folder %s", l.input);
    else if (found)
        lg(&l, "installer %s (%lld bytes)", l.input, (long long)file_size(l.input));
    else if (!game_ready) {
        lg(&l, "no copy: give --installer <GTA2.exe> or --folder <an installed GTA 2>, or put %s beside the program",
           cfg->input_file);
        xr_setup_ui_message(l.ui, 1, "No copy of GTA 2 was chosen: the GTA 2 freeware installer (GTA2.exe, the 2004 "
                                     "Rockstar release) or a folder with that GTA 2 installed.");
        goto out;
    }
    if (repair)
        lg(&l, "repair: building again");

    /* the payload */
    {
        char *have;
        join(p, sizeof p, l.tools, "release.txt");
        have = read_text(p);
        if (repair || !have || strcmp(have, l.release)) {
            UnpackArg u;
            memset(&u, 0, sizeof u);
            u.offset = payload_ofs;
            if (run_job(&l, "Unpacking the launcher's files", 0.0, 0.02, unpack_job, &u, prog_frac)) {
                SDL_free(have);
                xr_setup_ui_message(l.ui, 1, "The launcher's files could not be unpacked - see data/launcher.log.");
                goto out;
            }
        }
        SDL_free(have);
    }
    if (!ensure_toolchain(&l))
        goto out;
    building = 1;                           /* (from here a quit removes the half-made game) */
    rm_tree(l.build);
    SDL_CreateDirectory(l.build);
    SDL_CreateDirectory(l.obj);
    SDL_CreateDirectory(l.work);
    /* the game's files: game/ when it holds the supported gta2.exe (a new release, --repair: built from game/ alone,
       the installer or folder not read again - the maintainer's rule), else unpacked from the installer / copied
       from the folder - the right one, else asked for again */
    {
        LiftArg la;
        rc = 1;
        if (game_ready) {
            memset(&la, 0, sizeof la);
            la.only = "check";
            rc = run_job(&l, "Checking the game's files", 0.10, 0.01, lift_job, &la, NULL);
            if (rc == 0) {
                ShaArg sa;
                memset(&sa, 0, sizeof sa);
                join(p, sizeof p, l.game, "gta2.exe");
                sa.path = p;
                if (run_job(&l, "Checking the game's files", 0.11, 0.01, sha_job, &sa, NULL) == 0)
                    snprintf(from_sha, sizeof from_sha, "%s", sa.hex);
            }
        }
        while (rc != 0) {
            ShaArg sa;
            if (!found) {
                xr_setup_ui_message(l.ui, 1, "The game's files are missing or damaged - choose your GTA 2 installer "
                                             "(GTA2.exe) or GTA 2 folder on the setup page (start with --setup).");
                goto out;
            }
            const int folder = is_dir(l.input);
            memset(&la, 0, sizeof la);
            la.only = folder ? "extract-folder" : "extract";
            rc = run_job(&l, folder ? "Copying the game's files from your GTA 2 folder"
                                    : "Unpacking the game's files from the installer", 0.10, 0.04, lift_job, &la, NULL);
            if (rc == 0) {
                char exe[LP];
                (void)folder;
                memset(&sa, 0, sizeof sa);
                join(exe, sizeof exe, l.game, "gta2.exe");
                sa.path = exe;                        /* the stamp: game/gta2.exe's hash (its copy, checked) */
                if (run_job(&l, "Checking the game's files", 0.14, 0.01, sha_job, &sa, NULL) == 0)
                    snprintf(from_sha, sizeof from_sha, "%s", sa.hex);
                ini_set(&l, "installer", l.input);
                break;
            }
            if (rc != WRONG_INPUT || cancelled(&l)) {
                xr_setup_ui_message(l.ui, 1, "Setting up the game's files failed - see data/build/lift.log.");
                goto out;
            }
            lg(&l, "%s: %s", l.input, cfg->wrong_input);
            if (xr_no_dialogs()) {
                xr_setup_ui_message(l.ui, 1, cfg->wrong_input);
                goto out;
            }
            plain_dir(l.root, target, sizeof target);
            form = xr_setup_ui_form(l.ui, cfg->input, l.input, sizeof l.input, cfg->wrong_input, NULL, none, 0, target,
                                    sizeof target, &copy);
            if (form <= 0)
                goto out;
            found = is_input(l.input);
            rc = 1;
        }
    }
    {
        LiftArg la;
        memset(&la, 0, sizeof la);
        la.skip[0] = "check";
        la.skip[1] = "extract";
        la.skip[2] = "extract-folder";
        /* (no time estimates in the steps' texts: they only worry - the maintainer 2026-11-20) */
        if (run_job(&l, "Translating the game's code", 0.15, 0.13, lift_job, &la, prog_frac)) {
            xr_setup_ui_message(l.ui, 1, "Translating the game's code failed - see data/build/lift.log.");
            goto out;
        }
    }
    {
        static Build b;
        memset(&b, 0, sizeof b);
        b.jobs = SDL_atoi(l.jobs);
        if (!read_build(&l, &b)) {
            xr_setup_ui_message(l.ui, 1, "The launcher's build.txt is missing or damaged - start it with --repair.");
            goto out;
        }
        if (run_job(&l, "Compiling", 0.28, 0.69, compile_job, &b, prog_frac)) {
            xr_setup_ui_message(l.ui, 1, "Compiling failed - see data/build/obj/*.log.");
            SDL_free(b.text);
            goto out;
        }
        if (run_job(&l, "Linking", 0.97, 0.02, link_job, &b, NULL) || !copy_libs(&l, &b)) {
            xr_setup_ui_message(l.ui, 1, "Linking failed - see data/build/link.log.");
            SDL_free(b.text);
            goto out;
        }
        SDL_free(b.text);
    }
    /* the lifted C and the objects are not needed to play: only the logs and the stamp stay */
    rm_tree(l.work);
    rm_tree(l.obj);
    /* and the unpacked compiler (~300 MB): its zip stays in data/tools/bundle, unpacked again in seconds when the game
     * is built again (a new release, --repair) - ensure_toolchain sees the marker gone */
    {
        static Toolchain t;
        if (read_toolchain(&l, &t) && t.zip[0]) {
            join(p, sizeof p, l.tools, t.dest);
            if (!rm_tree(p))
                lg(&l, "cannot remove %s", p);
        }
    }
    if (!place_file(&l, l.root, file))
        goto out;
    stamp_text(&l, from_sha, stamp_want, sizeof stamp_want);
    join(p, sizeof p, l.build, "stamp.txt");
    write_text(p, stamp_want);
    lg(&l, "built %s", l.out);
    progress_line(1.0, "Built");
    ok = no_start || start_game(&l);
out:
    if (cancelled(&l)) {
        /* the player quit during the installation: nothing half-made stays that a later start could take as the game
           - no stamp was written; the game's program, the lifted C and the objects go; the next start begins again */
        if (building) {
            rm_tree(l.work);
            rm_tree(l.obj);
            if (is_file(l.out) && !SDL_RemovePath(l.out))
                lg(&l, "cannot remove %s", l.out);
        }
        lg(&l, "the installation was stopped by the player; the next start begins it again");
        ok = 0;
    }
    xr_setup_ui_close(l.ui);
    if (l.log)
        SDL_CloseIO(l.log);
    SDL_Quit();
    return ok ? 0 : 1;
}
