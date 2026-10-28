/* JNI falso: lo justo para que libgameDSO.so (Gamevil nexus2) crea que esta en Android */
#include "jni.h"
#include <stdarg.h>

#define NSLOTS 233
static void *g_table[NSLOTS];
static void **g_envp = g_table;           /* JNIEnv = (void ***) */
static void *g_vm_table[8];
static void **g_vmp = g_vm_table;         /* JavaVM */

void *jni_env_ptr(void) { return &g_envp; }
void *jni_vm_ptr(void) { return &g_vmp; }

static void stub_hit(int n) { log_once("jslot", "[JNI] slot sin implementar: %d (ver tabla JNINativeInterface)", n); }
#include "jni_stubs.inc"

/* ---------- objetos ---------- */
static JObj *new_obj(int kind) {
    JObj *o = calloc(1, sizeof(JObj));
    o->magic = JMAGIC; o->kind = kind;
    return o;
}
static JObj *classes[128]; static int nclasses;

JObj *jni_find_class(const char *name) {
    for (int i = 0; i < nclasses; i++) if (!strcmp(classes[i]->name, name)) return classes[i];
    JObj *c = new_obj(K_CLASS); c->name = strdup(name); c->global = 1;
    if (nclasses < 128) classes[nclasses++] = c;
    return c;
}
JObj *jni_new_string(const char *s) {
    JObj *o = new_obj(K_STRING); o->str = strdup(s ? s : ""); return o;
}
static JObj *new_array(const char *et, int esz, int len) {
    JObj *o = new_obj(K_ARRAY); o->name = (char *)et; o->esz = esz; o->len = len;
    o->data = calloc(len > 0 ? len : 1, esz); return o;
}
JObj *jni_new_bytes(const void *p, int n) {
    JObj *o = new_array("B", 1, n); if (p && n > 0) memcpy(o->data, p, n); return o;
}
JObj *jni_new_floats(const float *p, int n) {
    JObj *o = new_array("F", 4, n); if (p && n > 0) memcpy(o->data, p, n * 4); return o;
}
const char *jni_str(void *o) {
    JObj *j = o; return (j && j->magic == JMAGIC && j->kind == K_STRING) ? j->str : "";
}

/* ---------- natives registrados ---------- */
static struct { char *name; void *fn; } reg[256]; static int nreg;
void *jni_registered_native(const char *name) {
    for (int i = 0; i < nreg; i++) if (!strcmp(reg[i].name, name)) return reg[i].fn;
    return NULL;
}
typedef struct { const char *name, *sig; void *fn; } JNINativeMethod_;
SOFT static int RegisterNatives(void *e, void *cls, JNINativeMethod_ *m, int n) {
    for (int i = 0; i < n && nreg < 256; i++) {
        LOG("[JNI] RegisterNatives %s %s", m[i].name, m[i].sig);
        reg[nreg].name = strdup(m[i].name); reg[nreg].fn = m[i].fn; nreg++;
    }
    return 0;
}

/* ---------- funciones basicas ---------- */
SOFT static int GetVersion(void *e) { return 0x00010006; }
SOFT static void *FindClass(void *e, const char *n) { LOG("[JNI] FindClass %s", n); return jni_find_class(n); }
SOFT static void *GetObjectClass(void *e, JObj *o) { return (o && o->kind == K_OBJECT && o->cls) ? o->cls : jni_find_class("java/lang/Object"); }
SOFT static void *NewGlobalRef(void *e, JObj *o) { if (o && o->magic == JMAGIC) o->global = 1; return o; }
SOFT static void DeleteGlobalRef(void *e, JObj *o) { }
SOFT static void DeleteLocalRef(void *e, JObj *o) {
    if (o && o->magic == JMAGIC && !o->global && (o->kind == K_STRING || o->kind == K_ARRAY)) {
        o->magic = 0; free(o->str); free(o->data); free(o);
    }
}
SOFT static int IsSameObject(void *e, void *a, void *b) { return a == b; }
SOFT static void *NewLocalRef(void *e, void *o) { return o; }
SOFT static int EnsureLocalCapacity(void *e, int n) { return 0; }
SOFT static int PushLocalFrame(void *e, int n) { return 0; }
SOFT static void *PopLocalFrame(void *e, void *r) { return r; }
SOFT static void *ExceptionOccurred(void *e) { return NULL; }
SOFT static void ExceptionVoid(void *e) { }
SOFT static int ExceptionCheck(void *e) { return 0; }
SOFT static int IsInstanceOf(void *e, void *o, void *c) { return 1; }
SOFT static void *AllocObject(void *e, JObj *cls) { JObj *o = new_obj(K_OBJECT); o->cls = cls; return o; }

static MethodID *mk_method(JObj *cls, const char *n, const char *s, int st) {
    MethodID *m = calloc(1, sizeof *m);
    m->name = strdup(n); m->sig = strdup(s); m->is_static = st; m->cls = cls;
    return m;
}
SOFT static void *GetMethodID(void *e, JObj *c, const char *n, const char *s) {
    LOG("[JNI] GetMethodID %s.%s %s", c ? c->name : "?", n, s); return mk_method(c, n, s, 0);
}
SOFT static void *GetStaticMethodID(void *e, JObj *c, const char *n, const char *s) {
    LOG("[JNI] GetStaticMethodID %s.%s %s", c ? c->name : "?", n, s); return mk_method(c, n, s, 1);
}
SOFT static void *GetFieldID(void *e, JObj *c, const char *n, const char *s) {
    LOG("[JNI] GetFieldID %s.%s %s", c ? c->name : "?", n, s); return mk_method(c, n, s, 0);
}
SOFT static int GetFieldI(void *e, void *o, void *f) { log_once("fld", "[JNI] Get*Field llamado"); return 0; }

/* ---------- llamadas a metodos Java ---------- */
static void norm(const char c, jv *v) {
    switch (c) {
    case 'Z': v->i &= 0xff; break;
    case 'B': v->i = (signed char)v->i; break;
    case 'C': v->i &= 0xffff; break;
    case 'S': v->i = (short)v->i; break;
    }
}
static void collect_va(MethodID *m, va_list ap, jv *a) {
    const char *p = m->sig; int n = 0;
    if (*p == '(') p++;
    while (*p && *p != ')' && n < 16) {
        jv v; memset(&v, 0, sizeof v);
        switch (*p) {
        case 'F': v.f = (float)va_arg(ap, double); p++; break;
        case 'D': v.d = va_arg(ap, double); p++; break;
        case 'J': v.j = va_arg(ap, long long); p++; break;
        case 'L': v.l = va_arg(ap, void *); while (*p && *p != ';') p++; p++; break;
        case '[': v.l = va_arg(ap, void *); while (*p == '[') p++; if (*p == 'L') { while (*p && *p != ';') p++; } p++; break;
        default: v.i = va_arg(ap, int); norm(*p, &v); p++; break;
        }
        a[n++] = v;
    }
}
static void collect_a(MethodID *m, jv *src, jv *a) {
    const char *p = m->sig; int n = 0;
    if (*p == '(') p++;
    while (*p && *p != ')' && n < 16) {
        a[n] = src[n]; norm(*p, &a[n]); n++;
        if (*p == 'L') { while (*p && *p != ';') p++; p++; }
        else if (*p == '[') { while (*p == '[') p++; if (*p == 'L') { while (*p && *p != ';') p++; } p++; }
        else p++;
    }
}
static void do_call(MethodID *m, void *self, jv *a, jv *r) {
    memset(r, 0, sizeof *r);
    if (!m) { log_once("nullm", "[JNI] llamada con methodID NULL"); return; }
    java_call(m, (JObj *)self, a, r);
}

#define DEFCALL(NAME, CT, FLD) \
SOFT static CT Call##NAME##Method(void *e, void *o, MethodID *m, ...) { \
    va_list ap; jv a[16], r; va_start(ap, m); if (m) collect_va(m, ap, a); va_end(ap); do_call(m, o, a, &r); return (CT)r.FLD; } \
SOFT static CT Call##NAME##MethodV(void *e, void *o, MethodID *m, va_list ap) { \
    jv a[16], r; if (m) collect_va(m, ap, a); do_call(m, o, a, &r); return (CT)r.FLD; } \
SOFT static CT Call##NAME##MethodA(void *e, void *o, MethodID *m, jv *args) { \
    jv a[16], r; if (m) collect_a(m, args, a); do_call(m, o, a, &r); return (CT)r.FLD; }

DEFCALL(Object, void *, l)
DEFCALL(Boolean, int, i)
DEFCALL(Byte, int, i)
DEFCALL(Char, int, i)
DEFCALL(Short, int, i)
DEFCALL(Int, int, i)
DEFCALL(Long, long long, j)
DEFCALL(Float, float, f)
DEFCALL(Double, double, d)

SOFT static void CallVoidMethod(void *e, void *o, MethodID *m, ...) {
    va_list ap; jv a[16], r; va_start(ap, m); if (m) collect_va(m, ap, a); va_end(ap); do_call(m, o, a, &r); }
SOFT static void CallVoidMethodV(void *e, void *o, MethodID *m, va_list ap) {
    jv a[16], r; if (m) collect_va(m, ap, a); do_call(m, o, a, &r); }
SOFT static void CallVoidMethodA(void *e, void *o, MethodID *m, jv *args) {
    jv a[16], r; if (m) collect_a(m, args, a); do_call(m, o, a, &r); }

SOFT static void *NewObjectX(void *e, JObj *cls, MethodID *m, jv *a) {
    JObj *o = new_obj(K_OBJECT); o->cls = cls;
    if (m) { jv r; do_call(m, o, a, &r); }
    return o;
}
SOFT static void *NewObject(void *e, JObj *cls, MethodID *m, ...) {
    va_list ap; jv a[16]; va_start(ap, m); if (m) collect_va(m, ap, a); va_end(ap); return NewObjectX(e, cls, m, a); }
SOFT static void *NewObjectV(void *e, JObj *cls, MethodID *m, va_list ap) {
    jv a[16]; if (m) collect_va(m, ap, a); return NewObjectX(e, cls, m, a); }
SOFT static void *NewObjectA(void *e, JObj *cls, MethodID *m, jv *args) {
    jv a[16]; if (m) collect_a(m, args, a); return NewObjectX(e, cls, m, a); }

/* ---------- strings ---------- */
SOFT static void *NewStringUTF(void *e, const char *s) { return jni_new_string(s); }
SOFT static int GetStringUTFLength(void *e, JObj *s) { return (int)strlen(jni_str(s)); }
SOFT static const char *GetStringUTFChars(void *e, JObj *s, unsigned char *isCopy) {
    if (isCopy) *isCopy = 1; return strdup(jni_str(s)); }
SOFT static void ReleaseStringUTFChars(void *e, JObj *s, const char *c) { free((void *)c); }
SOFT static void *NewString(void *e, const uint16_t *u, int len) {
    char *buf = malloc(len * 3 + 1); int n = 0;
    for (int i = 0; i < len; i++) {
        uint32_t c = u[i];
        if (c < 0x80) buf[n++] = c;
        else if (c < 0x800) { buf[n++] = 0xC0 | (c >> 6); buf[n++] = 0x80 | (c & 63); }
        else { buf[n++] = 0xE0 | (c >> 12); buf[n++] = 0x80 | ((c >> 6) & 63); buf[n++] = 0x80 | (c & 63); }
    }
    buf[n] = 0; JObj *o = jni_new_string(buf); free(buf); return o;
}
static uint16_t *to_utf16(const char *s, int *outn) {
    int cap = (int)strlen(s) + 1; uint16_t *u = malloc(cap * 2); int n = 0;
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        uint32_t c = *p++;
        if (c >= 0xE0) { c = ((c & 15) << 12) | ((p[0] & 63) << 6) | (p[1] & 63); p += 2; }
        else if (c >= 0xC0) { c = ((c & 31) << 6) | (p[0] & 63); p += 1; }
        u[n++] = (uint16_t)c;
    }
    *outn = n; return u;
}
SOFT static int GetStringLength(void *e, JObj *s) { int n; free(to_utf16(jni_str(s), &n)); return n; }
SOFT static const uint16_t *GetStringChars(void *e, JObj *s, unsigned char *isCopy) {
    int n; if (isCopy) *isCopy = 1; return to_utf16(jni_str(s), &n); }
SOFT static void ReleaseStringChars(void *e, JObj *s, const uint16_t *c) { free((void *)c); }

/* ---------- arrays ---------- */
SOFT static int GetArrayLength(void *e, JObj *a) { return a ? a->len : 0; }
#define DEFNEW(NAME, ET, ESZ) SOFT static void *New##NAME##Array(void *e, int n) { return new_array(ET, ESZ, n); }
DEFNEW(Boolean, "Z", 1) DEFNEW(Byte, "B", 1) DEFNEW(Char, "C", 2) DEFNEW(Short, "S", 2)
DEFNEW(Int, "I", 4) DEFNEW(Long, "J", 8) DEFNEW(Float, "F", 4) DEFNEW(Double, "D", 8)
SOFT static void *NewObjectArray(void *e, int n, void *c, void *init) {
    JObj *a = new_array("L", 4, n); void **d = a->data; for (int i = 0; i < n; i++) d[i] = init; return a; }
SOFT static void *GetObjectArrayElement(void *e, JObj *a, int i) { return (a && i >= 0 && i < a->len) ? ((void **)a->data)[i] : NULL; }
SOFT static void SetObjectArrayElement(void *e, JObj *a, int i, void *v) { if (a && i >= 0 && i < a->len) ((void **)a->data)[i] = v; }
SOFT static void *GetElems(void *e, JObj *a, unsigned char *isCopy) { if (isCopy) *isCopy = 0; return a ? a->data : NULL; }
SOFT static void RelElems(void *e, JObj *a, void *el, int mode) { }
SOFT static void GetRegion(void *e, JObj *a, int start, int len, void *buf) {
    if (a && buf && start >= 0 && start + len <= a->len) memcpy(buf, (char *)a->data + start * a->esz, len * a->esz); }
SOFT static void SetRegion(void *e, JObj *a, int start, int len, const void *buf) {
    if (a && buf && start >= 0 && start + len <= a->len) memcpy((char *)a->data + start * a->esz, buf, len * a->esz); }

SOFT static int GetJavaVM(void *e, void **vm) { *vm = jni_vm_ptr(); return 0; }

/* ---------- JavaVM ---------- */
SOFT static int vm_Destroy(void *vm) { return 0; }
SOFT static int vm_Attach(void *vm, void **penv, void *args) { *penv = jni_env_ptr(); return 0; }
SOFT static int vm_Detach(void *vm) { return 0; }
SOFT static int vm_GetEnv(void *vm, void **penv, int ver) { *penv = jni_env_ptr(); return 0; }

__attribute__((constructor)) static void jni_init_tables(void) {
    memcpy(g_table, jni_stubs, sizeof g_table);
    g_vm_table[3] = vm_Destroy; g_vm_table[4] = vm_Attach; g_vm_table[5] = vm_Detach;
    g_vm_table[6] = vm_GetEnv; g_vm_table[7] = vm_Attach;
    g_table[4] = GetVersion; g_table[6] = FindClass;
    g_table[15] = ExceptionOccurred; g_table[16] = ExceptionVoid; g_table[17] = ExceptionVoid;
    g_table[19] = PushLocalFrame; g_table[20] = PopLocalFrame;
    g_table[21] = NewGlobalRef; g_table[22] = DeleteGlobalRef; g_table[23] = DeleteLocalRef;
    g_table[24] = IsSameObject; g_table[25] = NewLocalRef; g_table[26] = EnsureLocalCapacity;
    g_table[27] = AllocObject; g_table[28] = NewObject; g_table[29] = NewObjectV; g_table[30] = NewObjectA;
    g_table[31] = GetObjectClass; g_table[32] = IsInstanceOf; g_table[33] = GetMethodID;
    /* llamadas de instancia 34..63 y estaticas 114..143 comparten implementacion */
#define SETCALL(base, NAME) g_table[base] = Call##NAME##Method; g_table[base+1] = Call##NAME##MethodV; g_table[base+2] = Call##NAME##MethodA;
    SETCALL(34, Object) SETCALL(37, Boolean) SETCALL(40, Byte) SETCALL(43, Char) SETCALL(46, Short)
    SETCALL(49, Int) SETCALL(52, Long) SETCALL(55, Float) SETCALL(58, Double) SETCALL(61, Void)
    SETCALL(114, Object) SETCALL(117, Boolean) SETCALL(120, Byte) SETCALL(123, Char) SETCALL(126, Short)
    SETCALL(129, Int) SETCALL(132, Long) SETCALL(135, Float) SETCALL(138, Double) SETCALL(141, Void)
    g_table[94] = GetFieldID; g_table[113] = GetStaticMethodID; g_table[144] = GetFieldID;
    for (int i = 95; i <= 103; i++) g_table[i] = GetFieldI;
    for (int i = 145; i <= 153; i++) g_table[i] = GetFieldI;
    g_table[163] = NewString; g_table[164] = GetStringLength; g_table[165] = GetStringChars; g_table[166] = ReleaseStringChars;
    g_table[167] = NewStringUTF; g_table[168] = GetStringUTFLength; g_table[169] = GetStringUTFChars; g_table[170] = ReleaseStringUTFChars;
    g_table[171] = GetArrayLength; g_table[172] = NewObjectArray; g_table[173] = GetObjectArrayElement; g_table[174] = SetObjectArrayElement;
    g_table[175] = NewBooleanArray; g_table[176] = NewByteArray; g_table[177] = NewCharArray; g_table[178] = NewShortArray;
    g_table[179] = NewIntArray; g_table[180] = NewLongArray; g_table[181] = NewFloatArray; g_table[182] = NewDoubleArray;
    for (int i = 183; i <= 190; i++) g_table[i] = GetElems;
    for (int i = 191; i <= 198; i++) g_table[i] = RelElems;
    for (int i = 199; i <= 206; i++) g_table[i] = GetRegion;
    for (int i = 207; i <= 214; i++) g_table[i] = SetRegion;
    g_table[215] = RegisterNatives; g_table[219] = GetJavaVM; g_table[228] = ExceptionCheck;
    g_table[222] = GetElems; g_table[223] = RelElems;
}
