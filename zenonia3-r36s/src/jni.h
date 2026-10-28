#ifndef JNI_H_
#define JNI_H_
#include "common.h"

enum { K_CLASS = 1, K_STRING, K_ARRAY, K_OBJECT };
#define JMAGIC 0x4a4e4958

typedef struct JObj {
    int magic, kind, global;
    char *name;           /* clase: nombre; array: tipo de elemento */
    char *str;            /* string utf8 */
    int len, esz;         /* arrays */
    void *data;
    struct JObj *cls;     /* objetos */
} JObj;

typedef struct { char *name, *sig; int is_static; JObj *cls; } MethodID;

JObj *jni_find_class(const char *name);
JObj *jni_new_string(const char *utf8);
JObj *jni_new_bytes(const void *p, int n);
JObj *jni_new_floats(const float *p, int n);
const char *jni_str(void *o);
void *jni_env_ptr(void);
void *jni_vm_ptr(void);
void *jni_registered_native(const char *name);

/* implementado en natives.c: emula la parte Java del juego */
void java_call(MethodID *m, JObj *self, jv *args, jv *ret);
#endif
