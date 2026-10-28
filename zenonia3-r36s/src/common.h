#ifndef COMMON_H
#define COMMON_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>

/* La libgameDSO.so de Android es ARM "softfp" (floats en registros enteros),
 * nuestro ejecutable es "hardfp". Toda funcion que el juego llame y que reciba
 * o devuelva float/double DEBE ir marcada SOFT. */
#if defined(__arm__)
#define SOFT __attribute__((pcs("aapcs")))
#else
#define SOFT
#endif

void logf_(const char *fmt, ...);
void log_once(const char *key, const char *fmt, ...);
#define LOG(...) logf_(__VA_ARGS__)

extern char g_gamedir[512], g_assetdir[512], g_savedir[512];
extern volatile int g_quit;
extern int g_title_visible;
extern int g_swapped;
extern int g_width, g_height;
extern int g_locale;

typedef union { int i; long long j; float f; double d; void *l; } jv;

#endif
