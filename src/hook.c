/* Parchea la GOT / relocaciones de una .so ya cargada con dlopen para redirigir simbolos. */
#define _GNU_SOURCE
#include "common.h"
#include "hook.h"
#include <dlfcn.h>
#include <link.h>
#include <elf.h>
#include <sys/mman.h>
#include <unistd.h>

static void *fixup(ElfW(Addr) base, ElfW(Addr) p) { return (void *)(p < base ? base + p : p); }

static void patch(uint32_t *where, uint32_t value) {
    uintptr_t page = (uintptr_t)where & ~(uintptr_t)4095;
    mprotect((void *)page, 8192, PROT_READ | PROT_WRITE | PROT_EXEC);
    *where = value;
}

static int patch_table(ElfW(Addr) base, const Elf32_Rel *rel, size_t n, const Elf32_Sym *sym, const char *str, const Hook *hooks) {
    int count = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned type = ELF32_R_TYPE(rel[i].r_info), si = ELF32_R_SYM(rel[i].r_info);
        if (!si || (type != R_ARM_JUMP_SLOT && type != R_ARM_GLOB_DAT && type != R_ARM_ABS32)) continue;
        const char *name = str + sym[si].st_name;
        for (const Hook *h = hooks; h->name; h++) if (!strcmp(h->name, name)) {
            patch((uint32_t *)(base + rel[i].r_offset), (uint32_t)(uintptr_t)h->addr);
            count++; break;
        }
    }
    return count;
}

int hook_apply(void *handle, const Hook *hooks, void **base_out) {
    struct link_map *lm = NULL;
    if (dlinfo(handle, RTLD_DI_LINKMAP, &lm) != 0 || !lm) return -1;
    ElfW(Addr) base = lm->l_addr; if (base_out) *base_out = (void *)base;
    const Elf32_Sym *sym = NULL; const char *str = NULL; const Elf32_Rel *jmprel = NULL, *rel = NULL;
    size_t jmpsz = 0, relsz = 0;
    for (const ElfW(Dyn) *d = lm->l_ld; d->d_tag != DT_NULL; d++) {
        switch (d->d_tag) {
        case DT_SYMTAB: sym = fixup(base, d->d_un.d_ptr); break;
        case DT_STRTAB: str = fixup(base, d->d_un.d_ptr); break;
        case DT_JMPREL: jmprel = fixup(base, d->d_un.d_ptr); break;
        case DT_PLTRELSZ: jmpsz = d->d_un.d_val; break;
        case DT_REL: rel = fixup(base, d->d_un.d_ptr); break;
        case DT_RELSZ: relsz = d->d_un.d_val; break;
        }
    }
    if (!sym || !str) return -1;
    int n = 0;
    if (jmprel) n += patch_table(base, jmprel, jmpsz / sizeof(Elf32_Rel), sym, str, hooks);
    if (rel) n += patch_table(base, rel, relsz / sizeof(Elf32_Rel), sym, str, hooks);
    LOG("[HOOK] %d relocaciones redirigidas (base=%p)", n, (void *)base);
    return n;
}
