/* Compatibilidad bionic (Android) -> glibc. Se aplican "enganchando" la GOT de la libreria. */
#define _GNU_SOURCE
#include "common.h"
#include "hook.h"
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include <sys/syscall.h>

/* bionic usa mutex/cond de 4 bytes; glibc de 24+. Guardamos un puntero en esos 4 bytes. */
static pthread_mutex_t *get_mutex(void **slot) {
    uintptr_t v = (uintptr_t)*slot;
    if (v < 0x10000) {                                    /* inicializador estatico (0, 0x4000 recursivo, 0x8000 errorcheck) */
        pthread_mutex_t *m = malloc(sizeof *m);
        pthread_mutexattr_t at; pthread_mutexattr_init(&at);
        if (v & 0x4000) pthread_mutexattr_settype(&at, PTHREAD_MUTEX_RECURSIVE);
        pthread_mutex_init(m, &at); pthread_mutexattr_destroy(&at);
        *slot = m;
    }
    return (pthread_mutex_t *)*slot;
}
static pthread_cond_t *get_cond(void **slot) {
    uintptr_t v = (uintptr_t)*slot;
    if (v < 0x10000) { pthread_cond_t *c = malloc(sizeof *c); pthread_cond_init(c, NULL); *slot = c; }
    return (pthread_cond_t *)*slot;
}
SOFT static int b_mutex_init(void **slot, const void *attr) { *slot = (void *)0; get_mutex(slot); return 0; }
SOFT static int b_mutex_destroy(void **slot) {
    if ((uintptr_t)*slot >= 0x10000) { pthread_mutex_destroy(*slot); free(*slot); } *slot = NULL; return 0; }
SOFT static int b_mutex_lock(void **slot) { return pthread_mutex_lock(get_mutex(slot)); }
SOFT static int b_mutex_unlock(void **slot) { return pthread_mutex_unlock(get_mutex(slot)); }
SOFT static int b_cond_wait(void **c, void **m) { return pthread_cond_wait(get_cond(c), get_mutex(m)); }
SOFT static int b_cond_broadcast(void **c) { return pthread_cond_broadcast(get_cond(c)); }

/* struct stat de bionic ARM == struct stat64 del kernel */
SOFT static int b_stat(const char *path, void *buf) { return (int)syscall(195 /* __NR_stat64 */, path, buf); }

/* libm softfp: ceil(double) recibe/devuelve en r0:r1 */
SOFT static double b_ceil(double x) {
    if (x != x || x >= 4503599627370496.0 || x <= -4503599627370496.0) return x;
    double r = (double)(long long)x; if (r < x) r += 1.0; return r;
}
SOFT static int *b_errno(void) { return __errno_location(); }
static unsigned long b_stack_guard = 0x5A3C9E71UL;
SOFT static int b_aeabi_atexit(void *obj, void (*dtor)(void *), void *dso) { extern int __cxa_atexit(void (*)(void *), void *, void *); return __cxa_atexit(dtor, obj, dso); }
SOFT static void b_abort(void) { LOG("[COMPAT] el juego llamo a una funcion de excepciones C++ (no soportada)"); abort(); }
SOFT static void *b_find_exidx(void *pc, int *count) { if (count) *count = 0; return NULL; }

/* ---- GLES1 ---- */
#define GLX(n) extern void gles1_##n();
GLX(glActiveTexture) GLX(glBindTexture) GLX(glClear) GLX(glClearColorx) GLX(glColorPointer) GLX(glDisable)
GLX(glDisableClientState) GLX(glDrawArrays) GLX(glEnable) GLX(glEnableClientState) GLX(glGenTextures) GLX(glHint)
GLX(glLoadIdentity) GLX(glMatrixMode) GLX(glNormalPointer) GLX(glOrthof) GLX(glTexCoordPointer) GLX(glTexEnvf)
GLX(glTexImage2D) GLX(glTexParameterx) GLX(glTexSubImage2D) GLX(glVertexPointer) GLX(glViewport)
#define GH(n) { #n, (void *)gles1_##n }

const Hook g_hooks[] = {
    { "pthread_mutex_init", b_mutex_init }, { "pthread_mutex_destroy", b_mutex_destroy },
    { "pthread_mutex_lock", b_mutex_lock }, { "pthread_mutex_unlock", b_mutex_unlock },
    { "pthread_cond_wait", b_cond_wait }, { "pthread_cond_broadcast", b_cond_broadcast },
    { "stat", b_stat }, { "ceil", b_ceil }, { "__errno", b_errno },
    { "__stack_chk_guard", &b_stack_guard }, { "__aeabi_atexit", b_aeabi_atexit },
    { "__cxa_begin_cleanup", b_abort }, { "__cxa_call_unexpected", b_abort }, { "__cxa_type_match", b_abort },
    { "__gnu_Unwind_Find_exidx", b_find_exidx },
    GH(glActiveTexture), GH(glBindTexture), GH(glClear), GH(glClearColorx), GH(glColorPointer), GH(glDisable),
    GH(glDisableClientState), GH(glDrawArrays), GH(glEnable), GH(glEnableClientState), GH(glGenTextures), GH(glHint),
    GH(glLoadIdentity), GH(glMatrixMode), GH(glNormalPointer), GH(glOrthof), GH(glTexCoordPointer), GH(glTexEnvf),
    GH(glTexImage2D), GH(glTexParameterx), GH(glTexSubImage2D), GH(glVertexPointer), GH(glViewport),
    { NULL, NULL }
};
