/* Emula el lado Java del juego (clases com.gamevil.nexus2.Natives y NexusFont).
 * El juego llama aca a traves de JNI; todo lo no implementado se loguea UNA vez
 * con nombre y firma para saber que falta. */
#include "jni.h"
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>

int g_title_visible = 0;
int g_swapped = 0;
int g_locale = 0;
static int g_glinear = 0;

/* ---------- archivos ---------- */
static const char *strip(const char *p) {
    while (*p == '/') p++;
    if (!strncmp(p, "assets/", 7)) p += 7;
    return p;
}
static int try_path(char *out, size_t n, const char *dir, const char *rel) {
    snprintf(out, n, "%s/%s", dir, rel);
    return access(out, R_OK) == 0;
}
static int find_asset(const char *name, char *out, size_t n) {
    const char *rel = strip(name);
    if (try_path(out, n, g_assetdir, rel)) return 1;
    if (try_path(out, n, g_savedir, rel)) return 1;
    if (try_path(out, n, g_gamedir, rel)) return 1;
    return 0;
}
static void *slurp(const char *path, long *n) {
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    void *buf = malloc(sz > 0 ? sz : 1);
    if (fread(buf, 1, sz, f) != (size_t)sz) { fclose(f); free(buf); return NULL; }
    fclose(f); *n = sz; return buf;
}
static void mkdirs(char *path) {
    for (char *p = path + 1; *p; p++) if (*p == '/') { *p = 0; mkdir(path, 0755); *p = '/'; }
}
static JObj *load_bytes(const char *name, const char *dir) {
    char path[1024]; long n;
    if (dir) snprintf(path, sizeof path, "%s/%s", dir, strip(name));
    else if (!find_asset(name, path, sizeof path)) { LOG("[JAVA] archivo NO encontrado: %s", name); return NULL; }
    void *buf = slurp(path, &n);
    if (!buf) return NULL;
    JObj *a = jni_new_bytes(buf, (int)n); free(buf); return a;
}

/* ---------- timers asincronos ---------- */
typedef struct { int id, kind; long long due; int active; } Timer;
static Timer timers[64];
static long long now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000LL + t.tv_nsec / 1000000; }
void natives_timers_poll(void (*cb)(int id, int kind, int stamp)) {
    long long now = now_ms();
    for (int i = 0; i < 64; i++) if (timers[i].active && now >= timers[i].due) {
        timers[i].active = 0; cb(timers[i].id, timers[i].kind, (int)now);
    }
}
static void timer_set(int id, int delay, int kind) {
    for (int i = 0; i < 64; i++) if (!timers[i].active) {
        timers[i].id = id; timers[i].kind = kind; timers[i].due = now_ms() + (delay > 0 ? delay : 0); timers[i].active = 1; return;
    }
}

/* ---------- handlers ---------- */
typedef void (*HFn)(MethodID *m, JObj *self, jv *a, jv *r);
typedef struct { const char *name; HFn fn; } Handler;

static void h_loadFile(MethodID *m, JObj *s, jv *a, jv *r) { r->l = load_bytes(jni_str(a[0].l), NULL); }
static void h_loadStorage(MethodID *m, JObj *s, jv *a, jv *r) { r->l = load_bytes(jni_str(a[0].l), g_savedir); }
static void h_exist(MethodID *m, JObj *s, jv *a, jv *r) { char p[1024]; r->i = find_asset(jni_str(a[0].l), p, sizeof p); }
static void h_saveFile(MethodID *m, JObj *s, jv *a, jv *r) {
    char path[1024]; JObj *d = a[1].l;
    snprintf(path, sizeof path, "%s/%s", g_savedir, strip(jni_str(a[0].l)));
    mkdirs(path);
    FILE *f = fopen(path, "wb");
    if (f && d && d->data) { fwrite(d->data, 1, d->len, f); fclose(f); r->i = 1; LOG("[JAVA] saveFile %s (%d bytes)", path, d->len); }
    else { r->i = 0; LOG("[JAVA] saveFile FALLO %s", path); }
}
static void h_deleteFile(MethodID *m, JObj *s, jv *a, jv *r) {
    char path[1024]; snprintf(path, sizeof path, "%s/%s", g_savedir, strip(jni_str(a[0].l))); r->i = unlink(path) == 0;
}
static void h_absPath(MethodID *m, JObj *s, jv *a, jv *r) { char p[600]; snprintf(p, sizeof p, "%s/", g_savedir); r->l = jni_new_string(p); }
static void h_ret0(MethodID *m, JObj *s, jv *a, jv *r) { }
static void h_ret1(MethodID *m, JObj *s, jv *a, jv *r) { r->i = 1; }
static void h_big(MethodID *m, JObj *s, jv *a, jv *r) { r->j = 256LL * 1024 * 1024; }
static void h_emptyStr(MethodID *m, JObj *s, jv *a, jv *r) { r->l = jni_new_string(""); }
static void h_version(MethodID *m, JObj *s, jv *a, jv *r) { r->l = jni_new_string("1.0.0"); }
static void h_bytes_android(MethodID *m, JObj *s, jv *a, jv *r) { r->l = jni_new_bytes("R36S", 4); }
static void h_bytes_empty(MethodID *m, JObj *s, jv *a, jv *r) { r->l = jni_new_bytes("", 0); }
static void h_inet(MethodID *m, JObj *s, jv *a, jv *r) { r->l = jni_new_bytes("\0\0\0\0", 4); }
static void h_locale(MethodID *m, JObj *s, jv *a, jv *r) { r->i = g_locale; }
static void h_glinear(MethodID *m, JObj *s, jv *a, jv *r) { r->i = g_glinear; }
static void h_saveglinear(MethodID *m, JObj *s, jv *a, jv *r) { g_glinear = a[0].i; }
static void h_swap(MethodID *m, JObj *s, jv *a, jv *r) { extern void main_swap(void); main_swap(); g_swapped = 1; }
static void h_finish(MethodID *m, JObj *s, jv *a, jv *r) { LOG("[JAVA] finishApp -> salir"); g_quit = 1; }
static void h_timer2(MethodID *m, JObj *s, jv *a, jv *r) { LOG("[JAVA] OnAsyncTimerSet id=%d delay=%d", a[0].i, a[1].i); timer_set(a[0].i, a[1].i, 0); }
static void h_timer3(MethodID *m, JObj *s, jv *a, jv *r) { LOG("[JAVA] OnAsyncTimerSet id=%d delay=%d x=%d", a[0].i, a[1].i, a[2].i); timer_set(a[0].i, a[1].i, 1); }
static void h_showTitle(MethodID *m, JObj *s, jv *a, jv *r) { g_title_visible = 1; LOG("[JAVA] showTitleComponent (menu de titulo visible)"); }
static void h_hideTitle(MethodID *m, JObj *s, jv *a, jv *r) { g_title_visible = 0; LOG("[JAVA] hideTitleComponent"); }
static void h_sound(MethodID *m, JObj *s, jv *a, jv *r) { log_once("snd", "[JAVA] OnSoundPlay(%d,%d,%d) (audio no implementado aun)", a[0].i, a[1].i, a[2].i); }
static void h_log_s(MethodID *m, JObj *s, jv *a, jv *r) { LOG("[JAVA] %s(\"%s\")", m->name, jni_str(a[0].l)); }
static void h_log_i(MethodID *m, JObj *s, jv *a, jv *r) { log_once(m->name, "[JAVA] %s(%d)", m->name, a[0].i); }

/* NexusFont: de momento solo medidas aproximadas; el texto NO se dibuja todavia */
static void h_f_init(MethodID *m, JObj *s, jv *a, jv *r) { LOG("[FONT] GFA_Init(%d,%d,%d,%d,%d,%d)", a[0].i, a[1].i, a[2].i, a[3].i, a[4].i, a[5].i); r->i = 1; }
static void h_f_h(MethodID *m, JObj *s, jv *a, jv *r) { r->i = 16; }
static void h_f_asc(MethodID *m, JObj *s, jv *a, jv *r) { r->i = 13; }
static void h_f_desc(MethodID *m, JObj *s, jv *a, jv *r) { r->i = 3; }
static void h_f_textw(MethodID *m, JObj *s, jv *a, jv *r) { r->f = (float)strlen(jni_str(a[0].l)) * 8.0f; }
static void h_f_floats(MethodID *m, JObj *s, jv *a, jv *r) { float z[8] = {0}; r->l = jni_new_floats(z, 8); }
static void h_f_log(MethodID *m, JObj *s, jv *a, jv *r) { log_once(m->name, "[FONT] %s %s (sin implementar)", m->name, m->sig); }

static const Handler handlers[] = {
    {"loadFile", h_loadFile}, {"readAssete", h_loadFile}, {"loadFileFromStorage", h_loadStorage},
    {"isAssetExist", h_exist}, {"isFileExist", h_exist}, {"isDownloadFileExist", h_exist},
    {"saveFile", h_saveFile}, {"deleteFile", h_deleteFile}, {"getAbsolueFilePath", h_absPath},
    {"availableSize", h_big}, {"getFreeMemory", h_big}, {"getTotalMemory", h_big}, {"getMaxMemory", h_big}, {"getUsedMemory", h_ret0},
    {"getVersion", h_version}, {"getPlayerName", h_emptyStr},
    {"getPhoneModel", h_bytes_android}, {"getOsVersion", h_bytes_android}, {"getDeviceType", h_bytes_android},
    {"getDeviceID", h_bytes_android}, {"getAndroidID", h_bytes_android}, {"getMacAddress", h_bytes_android},
    {"getPhoneNumber", h_bytes_empty}, {"getOldPhoneNumber", h_bytes_empty}, {"getSimSerialNumber", h_bytes_empty},
    {"getGamevilLiveID", h_bytes_empty}, {"getGamevilLivePW", h_bytes_empty}, {"getPlayerNameByte", h_bytes_empty},
    {"getPlayerNumberByte", h_bytes_empty}, {"getInetHostAddress", h_inet},
    {"getLocaleID", h_locale}, {"getGLOptionLinear", h_glinear}, {"saveGLOptionLinear", h_saveglinear},
    {"isNetAvailable", h_ret0}, {"netConnect", h_ret0}, {"isGamevilLiveLogined", h_ret0}, {"getTapjoyReady", h_ret0},
    {"GWSwapBuffers", h_swap}, {"finishApp", h_finish},
    {"OnAsyncTimerSet", NULL /* segun firma */},
    {"showTitleComponent", h_showTitle}, {"hideTitleComponent", h_hideTitle},
    {"OnSoundPlay", h_sound}, {"OnStopSound", h_ret0}, {"stopAndroidSound", h_ret0}, {"vibrateAndroid", h_ret0}, {"OnVibrate", h_ret0},
    {"OnMessage", h_log_s}, {"openUrl", h_log_s}, {"NexusSetText", h_log_s}, {"NetErrorDialogException", h_log_s},
    {"OnEvent", h_log_i}, {"OnUIStatusChange", h_log_i}, {"changeUIStatus", h_log_i}, {"SetSpeed", h_log_i},
    {"NetErrorDialog", h_log_i},
    /* NexusFont */
    {"GFA_Init", h_f_init}, {"GFA_IsInitialized", h_ret1}, {"GFA_CreateFont", h_ret1},
    {"GFA_CharHeight", h_f_h}, {"GFA_CharWidth", h_f_h}, {"GFA_GetAscent", h_f_asc}, {"GFA_GetDescent", h_f_desc},
    {"GFA_GetTextWidth", h_f_textw}, {"GFA_MeasureText", h_f_floats}, {"GFA_DrawText", h_f_floats}, {"GFA_DrawFont", h_f_floats},
    {NULL, NULL}
};

void java_call(MethodID *m, JObj *self, jv *a, jv *r) {
    const char *n = m->name;
    if (!strcmp(n, "OnAsyncTimerSet")) { (strstr(m->sig, "(III") ? h_timer3 : h_timer2)(m, self, a, r); return; }
    if (!strcmp(n, "<init>") || !strcmp(n, "<clinit>")) return;
    for (const Handler *h = handlers; h->name; h++)
        if (h->fn && !strcmp(h->name, n)) { h->fn(m, self, a, r); return; }
    if (!strncmp(n, "GFA_", 4)) { h_f_log(m, self, a, r); return; }
    /* ocultar/mostrar componentes de UI y similares: solo log */
    log_once(n, "[JAVA] SIN IMPLEMENTAR: %s.%s %s", m->cls ? m->cls->name : "?", n, m->sig);
}
