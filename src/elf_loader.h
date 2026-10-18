#ifndef ELF_LOADER_H
#define ELF_LOADER_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *name;
    uintptr_t func;
} host_symbol_t;

typedef struct {
    char name[128];         /* Short filename of the .so (e.g. libgame.so) */
    char path[512];         /* Full path on disk */
    uint16_t machine;       /* ELF e_machine: 3 = EM_386 (x86), 40 = EM_ARM (ARM32) */
    void *base;             /* Base address where ELF was mapped */
    size_t size;            /* Total virtual memory size */
    void *entry;            /* ELF entry point if any */

    /* Dynamic section pointers */
    const char *dynstr;
    void *dynsym;           /* Elf32_Sym* */
    size_t num_dynsym;
    uint32_t *hash;         /* DT_HASH table */

    /* Relocations */
    void *reldyn;           /* Elf32_Rel* */
    size_t num_reldyn;
    void *relplt;           /* Elf32_Rel* */
    size_t num_relplt;

    /* Needed libraries (DT_NEEDED offsets in dynstr) */
    uint32_t needed[32];
    size_t num_needed;

    /* Init functions */
    void (**init_array)(void);
    size_t num_init_array;
    void (*init_func)(void);
} elf_module_t;

/* Inspect an ELF file's architecture without loading it:
 * returns 3 for x86 (EM_386), 62 for x86_64, 40 for ARM32, 183 for ARM64, <0 on error */
int elf_probe_machine(const char *path, char *arch_name, size_t arch_name_sz);

/* Load a 32-bit ELF shared object (x86 EM_386 or ARM32 EM_ARM) into memory */
int elf_load(const char *path, elf_module_t *mod);

/* Register a loaded module in the global module table so other .so files or dlsym can resolve from it */
void elf_register_module(elf_module_t *mod);

/* Non-destructive dry-run check of symbol resolution for --scan / --inspect */
void elf_analyze_relocations(const elf_module_t *mod, const host_symbol_t *host_syms, size_t sym_count,
                             int *out_total_imports, int *out_resolved_imports, int *out_missing_imports);

/* Relocate and resolve symbols using the provided host symbol table.
 * If allow_missing_stubs != 0, unresolved external symbols are bound to a logged no-op stub. */
int elf_relocate(elf_module_t *mod, const host_symbol_t *syms, size_t sym_count, int allow_missing_stubs);

/* Run .init and .init_array constructors */
void elf_init(elf_module_t *mod);

/* Lookup an exported symbol in the loaded module */
uintptr_t elf_sym(const elf_module_t *mod, const char *name);

/* Lookup an exported symbol across all registered modules */
uintptr_t elf_sym_global(const char *name);

/* Find the first exported symbol whose name contains `pattern` (or starts with `pattern` if prefix_only=1).
 * Writes the full symbol name into out_name (if non-NULL) and returns its address. */
uintptr_t elf_find_sym_match(const elf_module_t *mod, const char *pattern, int prefix_only, char *out_name, size_t out_len);

/* Print a summary of the module's needed libraries and exported entry points (for --inspect / --scan) */
void elf_print_summary(const elf_module_t *mod);

#ifdef __cplusplus
}
#endif

#endif /* ELF_LOADER_H */
