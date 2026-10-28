#define _GNU_SOURCE
#include "common.h"
#include "jni.h"
#include "hook.h"
#include <SDL2/SDL.h>
#include <dlfcn.h>
#include <signal.h>
#include <ucontext.h>
#include <unistd.h>
#include <sys/stat.h>
#include <libgen.h>

char g_gamedir[512], g_assetdir[512], g_savedir[512];
volatile int g_quit = 0;
int g_width = 640, g_height = 480;
static SDL_Window *win; static SDL_GLContext glctx;
static void *g_lib, *g_libbase;
static int devinfo_a = 1, devinfo_b = 0;

/* ---------- log ---------- */
void logf_(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap); fputc('\n', stderr); fflush(stderr);
}
void log_once(const char *key, const char *fmt, ...) {
    static char seen[512][48]; static int n;
    for (int i = 0; i < n; i++) if (!strncmp(seen[i], key, 47)) return;
    if (n < 512) { strncpy(seen[n], key, 47); seen[n][47] = 0; n++; }
    va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap); fputc('\n', stderr); fflush(stderr);
}

/* ---------- crash handler ---------- */
static void on_crash(int sig, siginfo_t *si, void *ctx) {
#ifdef __arm__
    ucontext_t *uc = ctx;
    unsigned long pc = uc->uc_mcontext.arm_pc, lr = uc->uc_mcontext.arm_lr, sp = uc->uc_mcontext.arm_sp;
    fprintf(stderr, "\n*** CRASH senal %d, addr=%p\n    pc=0x%lx lr=0x%lx sp=0x%lx\n    base_juego=%p  pc_off=0x%lx lr_off=0x%lx\n",
            sig, si->si_addr, pc, lr, sp, g_libbase, pc - (unsigned long)g_libbase, lr - (unsigned long)g_libbase);
    fprintf(stderr, "    r0=%lx r1=%lx r2=%lx r3=%lx r4=%lx r5=%lx\n", uc->uc_mcontext.arm_r0, uc->uc_mcontext.arm_r1,
            uc->uc_mcontext.arm_r2, uc->uc_mcontext.arm_r3, uc->uc_mcontext.arm_r4, uc->uc_mcontext.arm_r5);
#else
    fprintf(stderr, "\n*** CRASH senal %d, addr=%p\n", sig, si->si_addr);
#endif
    fflush(stderr); _exit(139);
}

/* ---------- config / controles ---------- */
typedef struct { SDL_Keycode key; int title_only; int kind; int a, b, c, d; } Bind;   /* kind 0=hal 1=cmd */
static Bind binds[64]; static int nbinds;

static void add_bind(const char *keyname, const char *action) {
    Bind b = { 0 }; const char *p = action;
    SDL_Keycode k = SDL_GetKeyFromName(keyname);
    if (k == SDLK_UNKNOWN) { LOG("[CFG] tecla desconocida: %s", keyname); return; }
    b.key = k;
    while (*p == ' ') p++;
    if (*p == '@') { b.title_only = 1; p++; while (*p == ' ') p++; }
    if (!strncmp(p, "hal ", 4)) { b.kind = 0; b.a = atoi(p + 4); }
    else if (!strncmp(p, "cmd ", 4)) { b.kind = 1; sscanf(p + 4, "%d %d %d %d", &b.a, &b.b, &b.c, &b.d); }
    else { LOG("[CFG] accion no valida: %s", action); return; }
    if (nbinds < 64) binds[nbinds++] = b;
}
static void load_cfg(void) {
    char path[600]; snprintf(path, sizeof path, "%s/zenonia3.cfg", g_gamedir);
    FILE *f = fopen(path, "r");
    if (!f) { LOG("[CFG] no hay zenonia3.cfg, uso valores por defecto"); return; }
    char line[256];
    while (fgets(line, sizeof line, f)) {
        char *h = strchr(line, '#'); if (h) *h = 0;
        char k[64], rest[192];
        if (sscanf(line, "set %63s %d", k, &(int){0}) == 2) {
            int v; sscanf(line, "set %*s %d", &v);
            if (!strcmp(k, "width")) g_width = v; else if (!strcmp(k, "height")) g_height = v;
            else if (!strcmp(k, "locale")) g_locale = v;
            else if (!strcmp(k, "devinfo_a")) devinfo_a = v; else if (!strcmp(k, "devinfo_b")) devinfo_b = v;
        } else if (sscanf(line, "bind %63s = %191[^\n]", k, rest) == 2) add_bind(k, rest);
    }
    fclose(f);
}

/* ---------- puente a la libreria del juego ---------- */
typedef void (*fn_v)(void *, void *);
typedef void (*fn_ii)(void *, void *, int, int);
typedef void (*fn_iiii)(void *, void *, int, int, int, int);
typedef void (*fn_i)(void *, void *, int);
typedef void (*fn_ts)(void *, void *, int, int);
static void *env, *cls;
static fn_v f_initref, f_render, f_resume, f_pause, f_destroy;
static fn_ii f_devinfo, f_bufsize, f_resize, f_timer_ts;
static fn_iiii f_clet; static fn_i f_timer;

static void *sym(const char *n) {
    char full[200]; snprintf(full, sizeof full, "Java_com_gamevil_nexus2_Natives_%s", n);
    void *p = dlsym(g_lib, full);
    if (!p) p = jni_registered_native(n);
    if (!p) LOG("[LOAD] AVISO: no encuentro %s", full);
    return p;
}
void main_swap(void) { SDL_GL_SwapWindow(win); }
void natives_timers_poll(void (*cb)(int, int, int));
static void timer_cb(int id, int kind, int stamp) {
    if (kind == 1 && f_timer_ts) f_timer_ts(env, cls, id, stamp); else if (f_timer) f_timer(env, cls, id);
}
static void send_clet(int a, int b, int c, int d) { if (f_clet) f_clet(env, cls, a, b, c, d); }

static void on_key(SDL_Keycode k, int down) {
    extern int g_title_visible;
    for (int i = 0; i < nbinds; i++) {
        if (binds[i].key != k) continue;
        if (binds[i].title_only && !g_title_visible) continue;
        if (binds[i].kind == 0) send_clet(down ? 2 : 3, binds[i].a, 0, 0);
        else if (down) send_clet(binds[i].a, binds[i].b, binds[i].c, binds[i].d);
        return;
    }
}

int gles_init(void);
int main(int argc, char **argv) {
    char self[600]; ssize_t n = readlink("/proc/self/exe", self, sizeof self - 1);
    if (n > 0) { self[n] = 0; snprintf(g_gamedir, sizeof g_gamedir, "%s", dirname(self)); }
    else getcwd(g_gamedir, sizeof g_gamedir);
    if (getenv("GAMEDIR")) snprintf(g_gamedir, sizeof g_gamedir, "%s", getenv("GAMEDIR"));
    snprintf(g_assetdir, sizeof g_assetdir, "%s/assets", g_gamedir);
    snprintf(g_savedir, sizeof g_savedir, "%s/save", g_gamedir);
    mkdir(g_savedir, 0755);
    LOG("[MAIN] gamedir=%s", g_gamedir);

    struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_sigaction = on_crash; sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, NULL); sigaction(SIGBUS, &sa, NULL); sigaction(SIGILL, &sa, NULL); sigaction(SIGABRT, &sa, NULL);

    load_cfg();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) { LOG("[SDL] init fallo: %s", SDL_GetError()); return 1; }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2); SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8); SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8); SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
    win = SDL_CreateWindow("Zenonia 3", 0, 0, g_width, g_height, SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN | SDL_WINDOW_SHOWN);
    if (!win) { LOG("[SDL] ventana fallo: %s", SDL_GetError()); return 1; }
    glctx = SDL_GL_CreateContext(win);
    if (!glctx) { LOG("[SDL] contexto GLES2 fallo: %s", SDL_GetError()); return 1; }
    SDL_GL_SetSwapInterval(1);
    SDL_ShowCursor(SDL_DISABLE);
    SDL_GL_GetDrawableSize(win, &g_width, &g_height);
    LOG("[SDL] ventana %dx%d, renderer: %s", g_width, g_height, (const char *)SDL_GL_GetProcAddress("glGetString") ? "ok" : "?");
    if (gles_init() != 0) { LOG("[GLES] init fallo"); return 1; }

    char libpath[700]; snprintf(libpath, sizeof libpath, "%s/lib/armeabi/libgameDSO.so", g_gamedir);
    g_lib = dlopen(libpath, RTLD_NOW | RTLD_GLOBAL);
    if (!g_lib) { LOG("[LOAD] dlopen fallo: %s", dlerror()); return 2; }
    hook_apply(g_lib, g_hooks, &g_libbase);

    int (*onload)(void *, void *) = dlsym(g_lib, "JNI_OnLoad");
    if (onload) { LOG("[LOAD] JNI_OnLoad -> 0x%x", onload(jni_vm_ptr(), NULL)); }
    env = jni_env_ptr(); cls = jni_find_class("com/gamevil/nexus2/Natives");
    f_initref = sym("InitializeJNIGlobalRef"); f_render = sym("NativeRender"); f_resume = sym("NativeResumeClet");
    f_pause = sym("NativePauseClet"); f_destroy = sym("NativeDestroyClet");
    f_devinfo = sym("NativeInitDeviceInfo"); f_bufsize = sym("NativeInitWithBufferSize"); f_resize = sym("NativeResize");
    f_clet = sym("handleCletEvent"); f_timer = sym("NativeAsyncTimerCallBack"); f_timer_ts = sym("NativeAsyncTimerCallBackTimeStemp");
    if (!f_render || !f_bufsize) { LOG("[LOAD] faltan funciones esenciales"); return 3; }

    LOG("[INIT] InitializeJNIGlobalRef"); if (f_initref) f_initref(env, cls);
    LOG("[INIT] NativeInitWithBufferSize(%d,%d)", g_width, g_height); f_bufsize(env, cls, g_width, g_height);
    LOG("[INIT] NativeInitDeviceInfo(%d,%d)", devinfo_a, devinfo_b); if (f_devinfo) f_devinfo(env, cls, devinfo_a, devinfo_b);
    LOG("[INIT] NativeResize(%d,%d)", g_width, g_height); if (f_resize) f_resize(env, cls, g_width, g_height);
    LOG("[INIT] NativeResumeClet"); if (f_resume) f_resume(env, cls);

    long frame = 0;
    while (!g_quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) g_quit = 1;
            else if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
                if (e.key.repeat) continue;
                if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) { g_quit = 1; break; }
                on_key(e.key.keysym.sym, e.type == SDL_KEYDOWN);
            }
        }
        natives_timers_poll(timer_cb);
        g_swapped = 0;
        f_render(env, cls);
        if (!g_swapped) SDL_GL_SwapWindow(win);
        if (++frame == 1) LOG("[MAIN] primer frame renderizado OK");
        if (frame % 600 == 0) LOG("[MAIN] frame %ld", frame);
    }
    LOG("[MAIN] saliendo");
    if (f_pause) f_pause(env, cls);
    if (f_destroy) f_destroy(env, cls);
    SDL_Quit();
    return 0;
}
