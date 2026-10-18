#include "elf_loader.h"
#include "arm32_bridge.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

/* Standard 32-bit ELF definitions */
#define EI_NIDENT 16
#define ELFMAG "\177ELF"
#define SELFMAG 4

#define ELFCLASS32  1
#define ELFCLASS64  2
#define ELFDATA2LSB 1

#define EM_386      3
#define EM_ARM      40
#define EM_X86_64   62
#define EM_AARCH64  183

#define PT_LOAD    1
#define PT_DYNAMIC 2

#define DT_NULL         0
#define DT_NEEDED       1
#define DT_PLTRELSZ     2
#define DT_PLTGOT       3
#define DT_HASH         4
#define DT_STRTAB       5
#define DT_SYMTAB       6
#define DT_RELA         7
#define DT_RELASZ       8
#define DT_RELAENT      9
#define DT_STRSZ       10
#define DT_SYMENT      11
#define DT_INIT        12
#define DT_FINI        13
#define DT_SONAME      14
#define DT_RPATH       15
#define DT_SYMBOLIC    16
#define DT_REL         17
#define DT_RELSZ       18
#define DT_RELENT      19
#define DT_PLTREL      20
#define DT_DEBUG       21
#define DT_TEXTREL     22
#define DT_JMPREL      23
#define DT_INIT_ARRAY   25
#define DT_FINI_ARRAY   26
#define DT_INIT_ARRAYSZ 27
#define DT_FINI_ARRAYSZ 28

#define SHN_UNDEF 0

#define ELF32_R_SYM(val)  ((val) >> 8)
#define ELF32_R_TYPE(val) ((val) & 0xff)

/* x86 (EM_386) relocation types */
#define R_386_NONE      0
#define R_386_32        1
#define R_386_PC32      2
#define R_386_GOT32     3
#define R_386_PLT32     4
#define R_386_COPY      5
#define R_386_GLOB_DAT  6
#define R_386_JMP_SLOT  7
#define R_386_RELATIVE  8

/* ARM32 (EM_ARM) relocation types */
#define R_ARM_NONE      0
#define R_ARM_ABS32     2
#define R_ARM_REL32     3
#define R_ARM_COPY      20
#define R_ARM_GLOB_DAT  21
#define R_ARM_JUMP_SLOT 22
#define R_ARM_RELATIVE  23

#pragma pack(push, 1)

typedef struct {
    uint8_t  e_ident[EI_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf32_Ehdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} Elf32_Phdr;

typedef struct {
    int32_t  d_tag;
    uint32_t d_val;
} Elf32_Dyn;

typedef struct {
    uint32_t st_name;
    uint32_t st_value;
    uint32_t st_size;
    uint8_t  st_info;
    uint8_t  st_other;
    uint16_t st_shndx;
} Elf32_Sym;

#define SHT_DYNSYM 11

typedef struct {
    uint32_t sh_name;
    uint32_t sh_type;
    uint32_t sh_flags;
    uint32_t sh_addr;
    uint32_t sh_offset;
    uint32_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint32_t sh_addralign;
    uint32_t sh_entsize;
} Elf32_Shdr;

typedef struct {
    uint32_t r_offset;
    uint32_t r_info;
} Elf32_Rel;

#pragma pack(pop)

#define MAX_REGISTERED_MODULES 32
static elf_module_t *g_modules[MAX_REGISTERED_MODULES];
static int g_module_count = 0;

void elf_register_module(elf_module_t *mod) {
    if (!mod) return;
    for (int i = 0; i < g_module_count; i++) {
        if (g_modules[i] == mod) return;
    }
    if (g_module_count < MAX_REGISTERED_MODULES) {
        g_modules[g_module_count++] = mod;
    }
}

int elf_probe_machine(const char *path, char *arch_name, size_t arch_name_sz) {
    if (arch_name && arch_name_sz > 0) arch_name[0] = '\0';
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    Elf32_Ehdr ehdr;
    if (fread(&ehdr, 1, sizeof(ehdr), f) != sizeof(ehdr)) {
        fclose(f);
        return -1;
    }
    fclose(f);
    if (memcmp(ehdr.e_ident, ELFMAG, SELFMAG) != 0) return -2;

    const char *name = "unknown";
    switch (ehdr.e_machine) {
        case EM_386:     name = "x86 (32-bit IA-32)"; break;
        case EM_X86_64:  name = "x86_64 (64-bit AMD64)"; break;
        case EM_ARM:     name = "armeabi-v7a (32-bit ARM)"; break;
        case EM_AARCH64: name = "arm64-v8a (64-bit AArch64)"; break;
        default:         break;
    }
    if (arch_name && arch_name_sz > 0) {
        snprintf(arch_name, arch_name_sz, "%s", name);
    }
    return (int)ehdr.e_machine;
}

static uint32_t elf_hash(const uint8_t *name) {
    uint32_t h = 0, g;
    while (*name) {
        h = (h << 4) + *name++;
        if ((g = (h & 0xf0000000)) != 0)
            h ^= g >> 24;
        h &= 0x0fffffff;
    }
    return h;
}

static const char *basename_of(const char *path) {
    const char *s1 = strrchr(path, '/');
    const char *s2 = strrchr(path, '\\');
    const char *s = (s1 > s2) ? s1 : s2;
    return s ? s + 1 : path;
}

int elf_load(const char *path, elf_module_t *mod) {
    memset(mod, 0, sizeof(*mod));
    snprintf(mod->path, sizeof(mod->path), "%s", path);
    snprintf(mod->name, sizeof(mod->name), "%s", basename_of(path));

    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "[ELF] Failed to open %s\n", path);
        return -1;
    }

    fseek(f, 0, SEEK_END);
    size_t file_size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t *file_data = (uint8_t *)malloc(file_size);
    if (!file_data) {
        fclose(f);
        return -1;
    }
    if (fread(file_data, 1, file_size, f) != file_size) {
        free(file_data);
        fclose(f);
        return -1;
    }
    fclose(f);

    Elf32_Ehdr *ehdr = (Elf32_Ehdr *)file_data;
    if (memcmp(ehdr->e_ident, ELFMAG, SELFMAG) != 0) {
        fprintf(stderr, "[ELF] %s is not a valid ELF binary\n", path);
        free(file_data);
        return -1;
    }
    if (ehdr->e_ident[4] != ELFCLASS32 ||
        ehdr->e_ident[5] != ELFDATA2LSB ||
        (ehdr->e_machine != EM_386 && ehdr->e_machine != EM_ARM)) {
        char arch[64];
        elf_probe_machine(path, arch, sizeof(arch));
        fprintf(stderr, "[ELF] %s has architecture '%s' (expected 32-bit x86 or ARM32 ELF)\n", path, arch);
        free(file_data);
        return -1;
    }

    mod->machine = ehdr->e_machine;

    Elf32_Phdr *phdrs = (Elf32_Phdr *)(file_data + ehdr->e_phoff);
    uint32_t min_vaddr = 0xFFFFFFFF;
    uint32_t max_vaddr = 0;

    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdrs[i].p_type == PT_LOAD) {
            if (phdrs[i].p_vaddr < min_vaddr)
                min_vaddr = phdrs[i].p_vaddr;
            uint32_t end = phdrs[i].p_vaddr + phdrs[i].p_memsz;
            if (end > max_vaddr)
                max_vaddr = end;
        }
    }

    size_t total_size = (max_vaddr - min_vaddr + 0xFFF) & ~0xFFF;
    void *base = VirtualAlloc(NULL, total_size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!base) {
        fprintf(stderr, "[ELF] VirtualAlloc failed (%zu bytes)\n", total_size);
        free(file_data);
        return -1;
    }

    mod->base = base;
    mod->size = total_size;

    /* Load segments */
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdrs[i].p_type == PT_LOAD) {
            uint8_t *dest = (uint8_t *)base + phdrs[i].p_vaddr;
            if (phdrs[i].p_filesz > 0) {
                memcpy(dest, file_data + phdrs[i].p_offset, phdrs[i].p_filesz);
            }
            if (phdrs[i].p_memsz > phdrs[i].p_filesz) {
                memset(dest + phdrs[i].p_filesz, 0, phdrs[i].p_memsz - phdrs[i].p_filesz);
            }
        }
    }

    /* Locate PT_DYNAMIC */
    Elf32_Dyn *dyn = NULL;
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdrs[i].p_type == PT_DYNAMIC) {
            dyn = (Elf32_Dyn *)((uint8_t *)base + phdrs[i].p_vaddr);
            break;
        }
    }

    if (dyn) {
        uint32_t init_array_addr = 0;
        uint32_t init_array_sz = 0;

        for (Elf32_Dyn *d = dyn; d->d_tag != DT_NULL; d++) {
            switch (d->d_tag) {
                case DT_NEEDED:
                    if (mod->num_needed < 32) {
                        mod->needed[mod->num_needed++] = d->d_val;
                    }
                    break;
                case DT_STRTAB:
                    mod->dynstr = (const char *)base + d->d_val;
                    break;
                case DT_SYMTAB:
                    mod->dynsym = (void *)((uint8_t *)base + d->d_val);
                    break;
                case DT_HASH:
                    mod->hash = (uint32_t *)((uint8_t *)base + d->d_val);
                    break;
                case DT_REL:
                    mod->reldyn = (void *)((uint8_t *)base + d->d_val);
                    break;
                case DT_RELSZ:
                    mod->num_reldyn = d->d_val / sizeof(Elf32_Rel);
                    break;
                case DT_JMPREL:
                    mod->relplt = (void *)((uint8_t *)base + d->d_val);
                    break;
                case DT_PLTRELSZ:
                    mod->num_relplt = d->d_val / sizeof(Elf32_Rel);
                    break;
                case DT_INIT:
                    mod->init_func = (void (*)(void))((uint8_t *)base + d->d_val);
                    break;
                case DT_INIT_ARRAY:
                    init_array_addr = d->d_val;
                    break;
                case DT_INIT_ARRAYSZ:
                    init_array_sz = d->d_val;
                    break;
            }
        }

        if (init_array_addr && init_array_sz) {
            mod->init_array = (void (**)(void))((uint8_t *)base + init_array_addr);
            mod->num_init_array = init_array_sz / sizeof(void *);
        }

        if (mod->hash) {
            mod->num_dynsym = mod->hash[1];
        } else if (ehdr->e_shoff && ehdr->e_shnum > 0 &&
                   ehdr->e_shoff + (size_t)ehdr->e_shnum * sizeof(Elf32_Shdr) <= file_size) {
            const Elf32_Shdr *shdrs = (const Elf32_Shdr *)(file_data + ehdr->e_shoff);
            for (int i = 0; i < ehdr->e_shnum; i++) {
                if (shdrs[i].sh_type == SHT_DYNSYM && shdrs[i].sh_size >= sizeof(Elf32_Sym)) {
                    mod->num_dynsym = shdrs[i].sh_size / sizeof(Elf32_Sym);
                    break;
                }
            }
        }
        if (mod->num_dynsym == 0 && mod->dynsym && mod->dynstr &&
            (uintptr_t)mod->dynstr > (uintptr_t)mod->dynsym) {
            size_t diff = (size_t)((uintptr_t)mod->dynstr - (uintptr_t)mod->dynsym);
            if (diff < 16 * 1024 * 1024) {
                mod->num_dynsym = diff / sizeof(Elf32_Sym);
            }
        }
    }

    free(file_data);
    elf_register_module(mod);
    printf("[ELF] Loaded '%s' (%s) at base %p (size: %zu KB, %zu symbols)\n",
           mod->name, mod->machine == EM_ARM ? "ARM32" : "x86",
           mod->base, mod->size / 1024, mod->num_dynsym);
    return 0;
}

uintptr_t elf_sym(const elf_module_t *mod, const char *name) {
    if (!mod || !mod->base || !mod->dynsym || !mod->dynstr || !name)
        return 0;

    const Elf32_Sym *syms = (const Elf32_Sym *)mod->dynsym;

    if (mod->hash) {
        uint32_t h = elf_hash((const uint8_t *)name);
        uint32_t nbucket = mod->hash[0];
        uint32_t nchain = mod->hash[1];
        const uint32_t *bucket = &mod->hash[2];
        const uint32_t *chain = &bucket[nbucket];

        for (uint32_t idx = bucket[h % nbucket]; idx != 0 && idx < nchain; idx = chain[idx]) {
            if (syms[idx].st_shndx != SHN_UNDEF &&
                strcmp(mod->dynstr + syms[idx].st_name, name) == 0) {
                return (uintptr_t)mod->base + syms[idx].st_value;
            }
        }
    }

    for (size_t i = 0; i < mod->num_dynsym; i++) {
        if (syms[i].st_shndx != SHN_UNDEF &&
            strcmp(mod->dynstr + syms[i].st_name, name) == 0) {
            return (uintptr_t)mod->base + syms[i].st_value;
        }
    }

    return 0;
}

uintptr_t elf_sym_global(const char *name) {
    for (int i = 0; i < g_module_count; i++) {
        uintptr_t addr = elf_sym(g_modules[i], name);
        if (addr) return addr;
    }
    return 0;
}

uintptr_t elf_find_sym_match(const elf_module_t *mod, const char *pattern, int prefix_only, char *out_name, size_t out_len) {
    if (!mod || !mod->base || !mod->dynsym || !mod->dynstr || !pattern)
        return 0;

    const Elf32_Sym *syms = (const Elf32_Sym *)mod->dynsym;
    size_t pat_len = strlen(pattern);

    for (size_t i = 0; i < mod->num_dynsym; i++) {
        if (syms[i].st_shndx == SHN_UNDEF || syms[i].st_value == 0) continue;
        const char *sname = mod->dynstr + syms[i].st_name;
        int match = prefix_only ? (strncmp(sname, pattern, pat_len) == 0)
                                : (strstr(sname, pattern) != NULL);
        if (match) {
            if (out_name && out_len > 0) {
                snprintf(out_name, out_len, "%s", sname);
            }
            return (uintptr_t)mod->base + syms[i].st_value;
        }
    }
    return 0;
}

static uintptr_t find_host_sym(const host_symbol_t *syms, size_t count, const char *name, int *out_is_guest_elf) {
    if (out_is_guest_elf) *out_is_guest_elf = 0;
    /* First check other loaded ELF modules */
    uintptr_t ext = elf_sym_global(name);
    if (ext) {
        if (out_is_guest_elf) *out_is_guest_elf = 1;
        return ext;
    }

    /* Exact match in host symbol table */
    for (size_t i = 0; i < count; i++) {
        if (strcmp(syms[i].name, name) == 0)
            return syms[i].func;
    }
    /* Versioned match (e.g. "fopen@LIBC" or "malloc@@LIBC") */
    const char *at = strchr(name, '@');
    if (at) {
        size_t base_len = (size_t)(at - name);
        for (size_t i = 0; i < count; i++) {
            if (strncmp(syms[i].name, name, base_len) == 0 && syms[i].name[base_len] == '\0') {
                return syms[i].func;
            }
        }
    }
    return 0;
}

static int __attribute__((cdecl)) missing_symbol_stub(void) {
    return 0;
}

#define ELF32_ST_BIND(val) ((val) >> 4)
#define ELF32_ST_TYPE(val) ((val) & 0xf)
#define STB_WEAK   2
#define STT_OBJECT 1

void elf_analyze_relocations(const elf_module_t *mod, const host_symbol_t *host_syms, size_t sym_count,
                             int *out_total_imports, int *out_resolved_imports, int *out_missing_imports) {
    int total = 0, resolved = 0, missing = 0;
    if (mod && mod->base && mod->dynsym && mod->dynstr) {
        const Elf32_Sym *syms = (const Elf32_Sym *)mod->dynsym;
        size_t total_relocs = mod->num_reldyn + mod->num_relplt;
        for (size_t i = 0; i < total_relocs; i++) {
            const Elf32_Rel *rel = (i < mod->num_reldyn) ?
                &((const Elf32_Rel *)mod->reldyn)[i] :
                &((const Elf32_Rel *)mod->relplt)[i - mod->num_reldyn];
            uint32_t r_sym = ELF32_R_SYM(rel->r_info);
            if (r_sym != 0 && syms[r_sym].st_shndx == SHN_UNDEF) {
                const char *sym_name = mod->dynstr + syms[r_sym].st_name;
                if (!sym_name[0]) continue;
                total++;
                if (find_host_sym(host_syms, sym_count, sym_name, NULL) != 0 ||
                    ELF32_ST_BIND(syms[r_sym].st_info) == STB_WEAK) {
                    resolved++;
                } else {
                    missing++;
                    if (missing <= 12) {
                        printf("    [Unresolved Import] %s\n", sym_name);
                    }
                }
            }
        }
        if (missing > 12) {
            printf("    ... and %d more unresolved imports (can be auto-stubbed with --allow-missing-syms)\n",
                   missing - 12);
        }
    }
    if (out_total_imports)    *out_total_imports    = total;
    if (out_resolved_imports) *out_resolved_imports = resolved;
    if (out_missing_imports)  *out_missing_imports  = missing;
}

int elf_relocate(elf_module_t *mod, const host_symbol_t *host_syms, size_t sym_count, int allow_missing_stubs) {
    uint8_t *base = (uint8_t *)mod->base;
    Elf32_Sym *syms = (Elf32_Sym *)mod->dynsym;

    size_t total_relocs = mod->num_reldyn + mod->num_relplt;
    int unresolved = 0;
    int is_arm = (mod->machine == EM_ARM);

    for (size_t i = 0; i < total_relocs; i++) {
        Elf32_Rel *rel = (i < mod->num_reldyn) ?
            &((Elf32_Rel *)mod->reldyn)[i] :
            &((Elf32_Rel *)mod->relplt)[i - mod->num_reldyn];

        uint32_t r_type = ELF32_R_TYPE(rel->r_info);
        uint32_t r_sym  = ELF32_R_SYM(rel->r_info);
        uint32_t *target = (uint32_t *)(base + rel->r_offset);

        if ((!is_arm && r_type == R_386_RELATIVE) || (is_arm && r_type == R_ARM_RELATIVE)) {
            *target += (uint32_t)(uintptr_t)base;
            continue;
        }

        uintptr_t sym_val = 0;
        const char *sym_name = NULL;

        if (r_sym != 0) {
            sym_name = mod->dynstr + syms[r_sym].st_name;
            if (syms[r_sym].st_shndx != SHN_UNDEF) {
                sym_val = (uintptr_t)base + syms[r_sym].st_value;
            } else {
                int is_guest_elf = 0;
                sym_val = find_host_sym(host_syms, sym_count, sym_name, &is_guest_elf);
                if (!sym_val) {
                    if (ELF32_ST_BIND(syms[r_sym].st_info) == STB_WEAK) {
                        /* ELF spec: undefined weak symbols resolve to 0 (NULL) */
                        sym_val = 0;
                    } else if (allow_missing_stubs) {
                        sym_val = (uintptr_t)missing_symbol_stub;
                    } else {
                        fprintf(stderr, "[ELF] Unresolved symbol: %s\n", sym_name);
                        unresolved++;
                    }
                }
                if (is_arm && sym_val && !is_guest_elf &&
                    ELF32_ST_TYPE(syms[r_sym].st_info) != STT_OBJECT) {
                    sym_val = arm32_register_host_thunk(sym_name, sym_val);
                }
            }
        }

        if (!is_arm) {
            switch (r_type) {
                case R_386_32:
                    *target += (uint32_t)sym_val;
                    break;
                case R_386_GLOB_DAT:
                case R_386_JMP_SLOT:
                    *target = (uint32_t)sym_val;
                    break;
                case R_386_PC32:
                    *target += (uint32_t)sym_val - (uint32_t)(uintptr_t)target;
                    break;
                case R_386_NONE:
                    break;
                default:
                    break;
            }
        } else {
            switch (r_type) {
                case R_ARM_ABS32:
                    *target += (uint32_t)sym_val;
                    break;
                case R_ARM_GLOB_DAT:
                case R_ARM_JUMP_SLOT:
                    *target = (uint32_t)sym_val;
                    break;
                case R_ARM_REL32:
                    *target += (uint32_t)sym_val - (uint32_t)(uintptr_t)target;
                    break;
                case R_ARM_NONE:
                    break;
                default:
                    break;
            }
        }
    }

    if (unresolved > 0) {
        fprintf(stderr, "[ELF] Total unresolved symbols: %d (pass --allow-missing-syms to auto-stub)\n", unresolved);
        return -1;
    }

    printf("[ELF] Relocations completed (%zu processed).\n", total_relocs);
    return 0;
}

void elf_init(elf_module_t *mod) {
    int is_arm = (mod->machine == EM_ARM);
    if (mod->init_func) {
        printf("[ELF] Calling DT_INIT @ %p...\n", (void *)mod->init_func);
        if (is_arm) arm32_call((uintptr_t)mod->init_func, 0, 0, 0, 0, 0, 0, 0);
        else        mod->init_func();
    }

    if (mod->init_array && mod->num_init_array > 0) {
        printf("[ELF] Calling %zu .init_array static constructors...\n", mod->num_init_array);
        for (size_t i = 0; i < mod->num_init_array; i++) {
            if (mod->init_array[i] && (intptr_t)mod->init_array[i] != -1) {
                printf("[ELF] [%s] Constructor %zu/%zu @ %p\n", mod->name, i + 1, mod->num_init_array, (void *)mod->init_array[i]);
                fflush(stdout);
                if (is_arm) {
                    arm32_call((uintptr_t)mod->init_array[i], 0, 0, 0, 0, 0, 0, 0);
                } else {
                    mod->init_array[i]();
                }
            }
        }

    }
    printf("[ELF] Constructors finished.\n");
}

void elf_print_summary(const elf_module_t *mod) {
    if (!mod || !mod->base) return;
    printf("  Module: %s (%s, mapped size: %zu KB)\n",
           mod->name, mod->machine == EM_ARM ? "ARM32" : "x86", mod->size / 1024);
    if (mod->dynstr && mod->num_needed > 0) {
        printf("  DT_NEEDED libraries (%zu):\n", mod->num_needed);
        for (size_t i = 0; i < mod->num_needed; i++) {
            printf("    - %s\n", mod->dynstr + mod->needed[i]);
        }
    }
    if (mod->dynsym && mod->dynstr) {
        const Elf32_Sym *syms = (const Elf32_Sym *)mod->dynsym;
        printf("  Exported Lifecycle / JNI Entry Points:\n");
        int count = 0;
        for (size_t i = 0; i < mod->num_dynsym; i++) {
            if (syms[i].st_shndx == SHN_UNDEF || syms[i].st_value == 0) continue;
            const char *sname = mod->dynstr + syms[i].st_name;
            if (strcmp(sname, "android_main") == 0 ||
                strcmp(sname, "ANativeActivity_onCreate") == 0 ||
                strcmp(sname, "SDL_main") == 0 ||
                strcmp(sname, "JNI_OnLoad") == 0 ||
                strncmp(sname, "Java_", 5) == 0) {
                printf("    [0x%08x] %s\n", syms[i].st_value, sname);
                count++;
            }
        }
        if (count == 0) {
            printf("    (no standard android_main / SDL_main / Java_* symbols exported)\n");
        }
    }
}
