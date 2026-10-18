#include "hook_util.h"
#include <windows.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    uint32_t r_offset;
    uint32_t r_info;
} HookRel;

typedef struct {
    uint32_t st_name;
    uint32_t st_value;
    uint32_t st_size;
    uint8_t  st_info;
    uint8_t  st_other;
    uint16_t st_shndx;
} HookSym;

#define HOOK_R_386_GLOB_DAT 6
#define HOOK_R_386_JMP_SLOT 7

static void *patch_relocs(elf_module_t *mod, const HookRel *rels, size_t count,
                          const char *symbol, void *replacement, void *original) {
    const HookSym *syms = (const HookSym *)mod->dynsym;
    for (size_t i = 0; i < count; i++) {
        uint32_t type = rels[i].r_info & 0xff;
        uint32_t sym = rels[i].r_info >> 8;
        if ((type != HOOK_R_386_JMP_SLOT && type != HOOK_R_386_GLOB_DAT) || sym == 0) continue;
        if (strcmp(mod->dynstr + syms[sym].st_name, symbol) != 0) continue;
        void **slot = (void **)((uint8_t *)mod->base + rels[i].r_offset);
        if (!original) original = *slot;
        *slot = replacement;
    }
    return original;
}

void *hook_got(elf_module_t *mod, const char *symbol, void *replacement) {
    void *original = NULL;
    original = patch_relocs(mod, (const HookRel *)mod->relplt, mod->num_relplt, symbol, replacement, original);
    original = patch_relocs(mod, (const HookRel *)mod->reldyn, mod->num_reldyn, symbol, replacement, original);
    return original;
}

void *hook_vtable(elf_module_t *mod, const char *vtable_symbol, int slot, void *replacement) {
    uintptr_t vt = elf_sym(mod, vtable_symbol);
    if (!vt) return NULL;
    void **entry = (void **)(vt + 8 + (uintptr_t)slot * 4);
    void *original = *entry;
    *entry = replacement;
    return original;
}

int hook_prologue_jmp(elf_module_t *mod, const char *symbol, void *replacement) {
    uint8_t *fn = (uint8_t *)elf_sym(mod, symbol);
    if (!fn) return 0;
    DWORD old;
    if (!VirtualProtect(fn, 5, PAGE_EXECUTE_READWRITE, &old)) return 0;
    int32_t rel = (int32_t)((uintptr_t)replacement - ((uintptr_t)fn + 5));
    fn[0] = 0xE9; /* jmp rel32 */
    memcpy(fn + 1, &rel, 4);
    VirtualProtect(fn, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), fn, 5);
    return 1;
}

void *module_addr(const elf_module_t *mod, uintptr_t offset) {
    return (uint8_t *)mod->base + offset;
}
