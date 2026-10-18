#ifndef HOOK_UTIL_H
#define HOOK_UTIL_H

#include "elf_loader.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Redirects every GOT entry (PLT jump slots and GLOB_DAT) that the loaded
 * module uses to reach `symbol` to `replacement`. Returns the original target,
 * or NULL if the module never imports it through the GOT. */
void *hook_got(elf_module_t *mod, const char *symbol, void *replacement);

/* Replaces virtual function `slot` (0-based, after the offset-to-top and
 * RTTI words) in the vtable named `vtable_symbol`. Returns the original. */
void *hook_vtable(elf_module_t *mod, const char *vtable_symbol, int slot, void *replacement);

/* Patches the prologue of an exported function `symbol` with a 5-byte relative JMP
 * to `replacement`. Returns 1 on success, 0 if symbol not found. */
int hook_prologue_jmp(elf_module_t *mod, const char *symbol, void *replacement);

/* Address of `offset` inside the module. */
void *module_addr(const elf_module_t *mod, uintptr_t offset);

#ifdef __cplusplus
}
#endif

#endif /* HOOK_UTIL_H */
