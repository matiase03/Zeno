#ifndef HOOK_H
#define HOOK_H
typedef struct { const char *name; void *addr; } Hook;
int hook_apply(void *handle, const Hook *hooks, void **base_out);
extern const Hook g_hooks[];
#endif
