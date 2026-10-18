#include "arm32_bridge.h"
#include "watchdog.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARM32_THUNK_BASE  0xF0000000u
#define ARM32_THUNK_MASK  0xFF000000u
#define ARM32_EXIT_LR     0xF0FFFFF0u
#define MAX_ARM32_THUNKS  2048

typedef struct {
    const char *name;
    uintptr_t   host_fn;
} arm32_thunk_t;

static arm32_thunk_t g_thunks[MAX_ARM32_THUNKS];
static int           g_thunk_count = 0;

uintptr_t arm32_register_host_thunk(const char *name, uintptr_t host_fn) {
    for (int i = 0; i < g_thunk_count; i++) {
        if (g_thunks[i].host_fn == host_fn) {
            return (uintptr_t)(ARM32_THUNK_BASE | ((uint32_t)i << 2));
        }
    }
    if (g_thunk_count >= MAX_ARM32_THUNKS) {
        return host_fn;
    }
    int idx = g_thunk_count++;
    g_thunks[idx].name    = name;
    g_thunks[idx].host_fn = host_fn;
    return (uintptr_t)(ARM32_THUNK_BASE | ((uint32_t)idx << 2));
}

int arm32_is_host_thunk(uint32_t addr) {
    return ((addr & ARM32_THUNK_MASK) == ARM32_THUNK_BASE);
}

typedef uint32_t (__attribute__((cdecl)) *host_call8_fn)(
    uint32_t, uint32_t, uint32_t, uint32_t,
    uint32_t, uint32_t, uint32_t, uint32_t);

typedef struct {
    uint32_t r[16];      /* R0..R12, SP(R13), LR(R14), PC(R15) */
    uint32_t cpsr;       /* N(31), Z(30), C(29), V(28), T(5) */
    uint32_t v[64];      /* VFP/NEON register file: S(n)=v[n], D(n)=v[2n..2n+1], Q(n)=D(2n..2n+1) */
    uint32_t fpscr;      /* VFP status: N Z C V in bits 31..28 */
    uint8_t  itstate;    /* Thumb-2 IT block state: firstcond[7:4] mask[3:0] */
} arm32_cpu_t;

#include "arm32_vfp.h"

#define FLAG_N (1u << 31)
#define FLAG_Z (1u << 30)
#define FLAG_C (1u << 29)
#define FLAG_V (1u << 28)
#define FLAG_T (1u << 5)

static void update_nz(arm32_cpu_t *cpu, uint32_t res) {
    cpu->cpsr &= ~(FLAG_N | FLAG_Z);
    if (res & 0x80000000u) cpu->cpsr |= FLAG_N;
    if (res == 0)          cpu->cpsr |= FLAG_Z;
}

static void update_sub_flags(arm32_cpu_t *cpu, uint32_t a, uint32_t b, uint32_t res) {
    cpu->cpsr &= ~(FLAG_N | FLAG_Z | FLAG_C | FLAG_V);
    if (res & 0x80000000u) cpu->cpsr |= FLAG_N;
    if (res == 0)          cpu->cpsr |= FLAG_Z;
    if (a >= b)            cpu->cpsr |= FLAG_C;
    if (((a ^ b) & (a ^ res)) & 0x80000000u) cpu->cpsr |= FLAG_V;
}

static void update_add_flags(arm32_cpu_t *cpu, uint32_t a, uint32_t b, uint32_t res) {
    cpu->cpsr &= ~(FLAG_N | FLAG_Z | FLAG_C | FLAG_V);
    if (res & 0x80000000u) cpu->cpsr |= FLAG_N;
    if (res == 0)          cpu->cpsr |= FLAG_Z;
    if (res < a)           cpu->cpsr |= FLAG_C;
    if ((~(a ^ b) & (a ^ res)) & 0x80000000u) cpu->cpsr |= FLAG_V;
}

static int check_cond(const arm32_cpu_t *cpu, uint32_t cond) {
    int n = (cpu->cpsr & FLAG_N) != 0;
    int z = (cpu->cpsr & FLAG_Z) != 0;
    int c = (cpu->cpsr & FLAG_C) != 0;
    int v = (cpu->cpsr & FLAG_V) != 0;
    switch (cond & 0xF) {
        case 0x0: return z;               /* EQ */
        case 0x1: return !z;              /* NE */
        case 0x2: return c;               /* CS/HS */
        case 0x3: return !c;              /* CC/LO */
        case 0x4: return n;               /* MI */
        case 0x5: return !n;              /* PL */
        case 0x6: return v;               /* VS */
        case 0x7: return !v;              /* VC */
        case 0x8: return c && !z;         /* HI */
        case 0x9: return !c || z;         /* LS */
        case 0xA: return n == v;          /* GE */
        case 0xB: return n != v;          /* LT */
        case 0xC: return !z && (n == v);  /* GT */
        case 0xD: return z || (n != v);   /* LE */
        case 0xE: return 1;               /* AL */
        default:  return 1;
    }
}

static void branch_to(arm32_cpu_t *cpu, uint32_t target) {
    if (target & 1u) {
        cpu->cpsr |= FLAG_T;
        cpu->r[15] = target & ~1u;
    } else {
        cpu->cpsr &= ~FLAG_T;
        cpu->r[15] = target & ~3u;
    }
}

static uint32_t ror32(uint32_t val, uint32_t sh) {
    sh &= 31u;
    return sh ? ((val >> sh) | (val << (32u - sh))) : val;
}

static __thread uint8_t *t_arm_stack = NULL;
static __thread uint32_t t_arm_nested_sp = 0;  /* guest SP while a host shim runs */
static __thread int      t_last_host_idx = -1;
static __thread uint32_t t_last_host_args[4];
static __thread uint32_t t_last_host_lr = 0;

void arm32_describe_context(char *out, size_t out_size) {
    if (!out || !out_size) return;
    out[0] = '\0';
    if (t_last_host_idx < 0 || t_last_host_idx >= g_thunk_count) return;
    char caller[128];
    watchdog_describe_address(t_last_host_lr & ~1u, caller, sizeof(caller));
    snprintf(out, out_size,
             "last host shim '%s'(0x%08X, 0x%08X, 0x%08X, 0x%08X) called from guest %s",
             g_thunks[t_last_host_idx].name ? g_thunks[t_last_host_idx].name : "?",
             t_last_host_args[0], t_last_host_args[1],
             t_last_host_args[2], t_last_host_args[3], caller);
}

/* ---- Guest fault guard --------------------------------------------------
 * A single misbehaving guest routine (e.g. one of hundreds of .init_array
 * constructors touching an unshimmed global) must not kill the whole
 * process. While the interpreter is executing guest code (but NOT while a
 * host shim is running), an access violation unwinds back to the owning
 * arm32_call(), which logs the fault and returns 0. Nested arm32_call()s
 * (host shim -> guest callback) each push their own guard frame. */
typedef struct arm32_guard {
    void               *jb[5];      /* __builtin_setjmp buffer */
    void               *seh_head;   /* FS:[0] at setjmp time */
    struct arm32_guard *prev;
    volatile int        active;     /* 1 while interpreting guest insns */
    volatile uint32_t   cur_pc;
    volatile uint32_t   prev_pc;
    uint32_t            fault_addr;
    uint32_t            fault_pc;
} arm32_guard_t;

static __thread arm32_guard_t *t_guard = NULL;
static volatile LONG g_guard_veh_installed = 0;
static volatile LONG g_guard_fault_count = 0;

static LONG WINAPI arm32_guard_veh(EXCEPTION_POINTERS *ep) {
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION &&
        code != EXCEPTION_DATATYPE_MISALIGNMENT &&
        code != EXCEPTION_ILLEGAL_INSTRUCTION &&
        code != EXCEPTION_INT_DIVIDE_BY_ZERO) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    arm32_guard_t *g = t_guard;
    if (!g || !g->active) return EXCEPTION_CONTINUE_SEARCH;
    g->active     = 0;
    g->fault_pc   = g->cur_pc;
    g->fault_addr = (code == EXCEPTION_ACCESS_VIOLATION)
                  ? (uint32_t)ep->ExceptionRecord->ExceptionInformation[1] : 0;
    /* Restore SEH chain to what it was when the guard was armed */
    NT_TIB *tib = (NT_TIB *)NtCurrentTeb();
    tib->ExceptionList = (struct _EXCEPTION_REGISTRATION_RECORD *)g->seh_head;
    __builtin_longjmp(g->jb, 1);
    return EXCEPTION_CONTINUE_SEARCH; /* unreachable */
}

static void arm32_guard_install(void) {
    if (InterlockedCompareExchange(&g_guard_veh_installed, 1, 0) == 0) {
        /* first=0: run AFTER main.c's stack-commit handler */
        AddVectoredExceptionHandler(0, arm32_guard_veh);
    }
}

static uint32_t arm32_interpret(arm32_guard_t *guard, uintptr_t guest_entry, int argc,
                                uint32_t a0, uint32_t a1, uint32_t a2,
                                uint32_t a3, uint32_t a4, uint32_t a5);

uint32_t arm32_call(uintptr_t guest_entry, int argc,
                    uint32_t a0, uint32_t a1, uint32_t a2,
                    uint32_t a3, uint32_t a4, uint32_t a5) {
    if (!guest_entry) return 0;
    arm32_guard_install();

    arm32_guard_t guard;
    memset(&guard, 0, sizeof(guard));
    guard.prev     = t_guard;
    guard.seh_head = ((NT_TIB *)NtCurrentTeb())->ExceptionList;
    t_guard = &guard;

    if (__builtin_setjmp(guard.jb) != 0) {
        /* Recovered from a guest fault */
        arm32_guard_t *g = t_guard;
        t_guard = g->prev;
        LONG n = InterlockedIncrement(&g_guard_fault_count);
        if (n <= 64) {
            char where[128], entry[128], prevd[128];
            watchdog_describe_address(g->fault_pc, where, sizeof(where));
            watchdog_describe_address((uint32_t)guest_entry, entry, sizeof(entry));
            watchdog_describe_address(g->prev_pc & ~1u, prevd, sizeof(prevd));
            printf("[ARM32] Guest fault (bad access 0x%08X) at %s in call to %s (prev insn at %s%s) -> returning 0\n",
                   g->fault_addr, where, entry, prevd, (g->prev_pc & 1u) ? " [Thumb]" : " [ARM]");
            fflush(stdout);
        }
        return 0;
    }

    uint32_t ret = arm32_interpret(&guard, guest_entry, argc, a0, a1, a2, a3, a4, a5);
    t_guard = guard.prev;
    return ret;
}

/* Thumb-2 ITSTATE advance (ARM ARM A2.5.2) */
static uint8_t it_advance(uint8_t it) {
    if ((it & 0x7u) == 0) return 0;
    return (uint8_t)((it & 0xE0u) | ((it << 1) & 0x1Fu));
}

/* 16-bit Thumb instructions that set flags even inside an IT block */
static int thumb16_is_compare(uint16_t insn) {
    if ((insn & 0xF800u) == 0x2800u) return 1;                 /* CMP imm8 */
    if ((insn & 0xFF00u) == 0x4500u) return 1;                 /* CMP hi   */
    if ((insn & 0xFC00u) == 0x4000u) {
        uint32_t op = (insn >> 6) & 0xFu;
        return op == 0x8 || op == 0xA || op == 0xB;            /* TST/CMP/CMN */
    }
    return 0;
}

static uint32_t arm32_interpret(arm32_guard_t *guard, uintptr_t guest_entry, int argc,
                                uint32_t a0, uint32_t a1, uint32_t a2,
                                uint32_t a3, uint32_t a4, uint32_t a5) {
    if (!t_arm_stack) {
        t_arm_stack = (uint8_t *)VirtualAlloc(NULL, 2 * 1024 * 1024,
                                              MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    }

    arm32_cpu_t cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.r[0]  = a0;
    cpu.r[1]  = a1;
    cpu.r[2]  = a2;
    cpu.r[3]  = a3;
    /* Nested calls (host shim -> guest callback) share this thread's ARM
     * stack, so start below the outer guest's live SP rather than at the top. */
    uint32_t stack_top = (uint32_t)(uintptr_t)(t_arm_stack + (2 * 1024 * 1024) - 64);
    if (t_arm_nested_sp) stack_top = (t_arm_nested_sp - 256u) & ~7u;
    cpu.r[13] = stack_top;
    cpu.r[14] = ARM32_EXIT_LR;

    if (argc > 4) *(uint32_t *)(uintptr_t)(cpu.r[13] + 0) = a4;
    if (argc > 5) *(uint32_t *)(uintptr_t)(cpu.r[13] + 4) = a5;

    branch_to(&cpu, (uint32_t)guest_entry);
    guard->active = 1;

    uint64_t steps = 0;
    while (cpu.r[15] != (ARM32_EXIT_LR & ~3u) && steps < 50000000ULL) {
        steps++;
        uint32_t pc = cpu.r[15];
        guard->prev_pc = guard->cur_pc;
        guard->cur_pc = pc | ((cpu.cpsr & FLAG_T) ? 1u : 0u);

        /* Check if PC entered a Windows host trap thunk */
        if (arm32_is_host_thunk(pc)) {
            uint32_t idx = (pc & ~ARM32_THUNK_MASK) >> 2;
            if (idx < (uint32_t)g_thunk_count && g_thunks[idx].host_fn) {
                host_call8_fn fn = (host_call8_fn)g_thunks[idx].host_fn;
                uint32_t *sp = (uint32_t *)(uintptr_t)cpu.r[13];
                guard->active = 0;   /* host code faults are not ours to swallow */
                t_last_host_idx = (int)idx;
                t_last_host_args[0] = cpu.r[0]; t_last_host_args[1] = cpu.r[1];
                t_last_host_args[2] = cpu.r[2]; t_last_host_args[3] = cpu.r[3];
                t_last_host_lr = cpu.r[14];
                uint32_t saved_nested = t_arm_nested_sp;
                t_arm_nested_sp = cpu.r[13];
                cpu.r[0] = fn(cpu.r[0], cpu.r[1], cpu.r[2], cpu.r[3],
                              sp[0], sp[1], sp[2], sp[3]);
                t_arm_nested_sp = saved_nested;
                t_guard = guard;     /* nested arm32_call restores, but be safe */
                guard->active = 1;
            } else {
                cpu.r[0] = 0;
            }
            branch_to(&cpu, cpu.r[14]);
            cpu.itstate = 0;
            continue;
        }

        if (cpu.cpsr & FLAG_T) {
            /* ---------------- THUMB / THUMB-2 MODE ---------------- */
            uint16_t insn = *(const uint16_t *)(uintptr_t)pc;
            cpu.r[15] = pc + 2;

            /* IT block: decide whether this instruction executes */
            int in_it = 0;
            if (cpu.itstate) {
                uint32_t it_cond = (cpu.itstate >> 4) & 0xFu;
                cpu.itstate = it_advance(cpu.itstate);
                in_it = 1;
                if (!check_cond(&cpu, it_cond)) {
                    if ((insn & 0xE000u) == 0xE000u && (insn & 0x1800u) != 0)
                        cpu.r[15] = pc + 4;
                    continue;
                }
            }

            /* 32-bit Thumb-2 instruction check */
            if ((insn & 0xE000u) == 0xE000u && (insn & 0x1800u) != 0) {
                uint16_t insn2 = *(const uint16_t *)(uintptr_t)(pc + 2);
                cpu.r[15] = pc + 4;

                /* BL / BLX (immediate) */
                if ((insn & 0xF800u) == 0xF000u && (insn2 & 0xC000u) == 0xC000u) {
                    uint32_t s  = (insn >> 10) & 1u;
                    uint32_t j1 = (insn2 >> 13) & 1u;
                    uint32_t j2 = (insn2 >> 11) & 1u;
                    uint32_t i1 = !(j1 ^ s);
                    uint32_t i2 = !(j2 ^ s);
                    int32_t imm = (int32_t)((s ? 0xFF000000u : 0u) |
                                  (i1 << 23) | (i2 << 22) |
                                  ((uint32_t)(insn & 0x3FFu) << 12) |
                                  ((uint32_t)(insn2 & 0x7FFu) << 1));
                    cpu.r[14] = (pc + 4) | 1u;
                    if ((insn2 & 0x1000u) == 0) {
                        /* BLX (switch to ARM) */
                        branch_to(&cpu, ((pc + 4) & ~3u) + imm);
                    } else {
                        /* BL (stay in Thumb) */
                        branch_to(&cpu, (pc + 4 + imm) | 1u);
                    }
                    continue;
                }
                /* B.W unconditional (F000..F7FF + 9xxx/Bxxx) */
                if ((insn & 0xF800u) == 0xF000u && (insn2 & 0xD000u) == 0x9000u) {
                    uint32_t s  = (insn >> 10) & 1;
                    uint32_t j1 = (insn2 >> 13) & 1;
                    uint32_t j2 = (insn2 >> 11) & 1;
                    uint32_t i1 = !(j1 ^ s);
                    uint32_t i2 = !(j2 ^ s);
                    int32_t imm = (int32_t)((s ? 0xFF000000u : 0u) |
                                  (i1 << 23) | (i2 << 22) |
                                  ((uint32_t)(insn & 0x3FFu) << 12) |
                                  ((uint32_t)(insn2 & 0x7FFu) << 1));
                    cpu.r[15] = pc + 4 + imm;
                    continue;
                }
                /* B<cond>.W conditional (F000..F7FF + 8xxx/Axxx, cond != 111x) */
                if ((insn & 0xF800u) == 0xF000u && (insn2 & 0xD000u) == 0x8000u &&
                    ((insn >> 6) & 0xEu) != 0xEu) {
                    uint32_t cond = (insn >> 6) & 0xFu;
                    if (check_cond(&cpu, cond)) {
                        uint32_t s  = (insn >> 10) & 1;
                        uint32_t j1 = (insn2 >> 13) & 1;
                        uint32_t j2 = (insn2 >> 11) & 1;
                        int32_t imm = (int32_t)((s ? 0xFFF00000u : 0u) |
                                      (j2 << 19) | (j1 << 18) |
                                      ((uint32_t)(insn & 0x3Fu) << 12) |
                                      ((uint32_t)(insn2 & 0x7FFu) << 1));
                        cpu.r[15] = pc + 4 + imm;
                    }
                    continue;
                }
                /* MOVW / MOVT (F24x/F64x / F2Cx/F6Cx) */
                if ((insn & 0xFB50u) == 0xF240u && (insn2 & 0x8000u) == 0) {
                    uint32_t imm16 = ((uint32_t)(insn & 0xFu) << 12) |
                                     (((uint32_t)(insn >> 10) & 1u) << 11) |
                                     (((uint32_t)(insn2 >> 12) & 7u) << 8) |
                                     (insn2 & 0xFFu);
                    uint32_t rd = (insn2 >> 8) & 0xFu;
                    if (insn & 0x0080u) {
                        cpu.r[rd] = (cpu.r[rd] & 0x0000FFFFu) | (imm16 << 16);
                    } else {
                        cpu.r[rd] = imm16;
                    }
                    continue;
                }
                /* ADDW / SUBW / ADR.W (F20x/F60x / F2Ax/F6Ax) */
                if ((insn & 0xFB50u) == 0xF200u && (insn2 & 0x8000u) == 0) {
                    uint32_t imm12 = (((uint32_t)(insn >> 10) & 1u) << 11) |
                                     (((uint32_t)(insn2 >> 12) & 7u) << 8) |
                                     (insn2 & 0xFFu);
                    uint32_t rn = insn & 0xFu;
                    uint32_t rd = (insn2 >> 8) & 0xFu;
                    uint32_t base_v = (rn == 15) ? ((pc + 4) & ~3u) : cpu.r[rn];
                    if (insn & 0x00A0u) cpu.r[rd] = base_v - imm12;
                    else                cpu.r[rd] = base_v + imm12;
                    continue;
                }
                /* Thumb-2 Modified Immediate Data Processing (F000..F1FF / F400..F5FF, bit15 of insn2 == 0) */
                if ((insn & 0xFA00u) == 0xF000u && (insn2 & 0x8000u) == 0) {
                    uint32_t op  = (insn >> 5) & 0xFu;
                    int      s   = (insn >> 4) & 1;
                    uint32_t rn  = insn & 0xFu;
                    uint32_t rd  = (insn2 >> 8) & 0xFu;
                    uint32_t i_b = (insn >> 10) & 1u;
                    uint32_t imm3 = (insn2 >> 12) & 7u;
                    uint32_t imm8 = insn2 & 0xFFu;
                    uint32_t imm12 = (i_b << 11) | (imm3 << 8) | imm8;
                    uint32_t mod_imm = 0;
                    if ((imm12 >> 10) == 0) {
                        switch ((imm12 >> 8) & 3u) {
                            case 0: mod_imm = imm8; break;
                            case 1: mod_imm = (imm8 << 16) | imm8; break;
                            case 2: mod_imm = (imm8 << 24) | (imm8 << 8); break;
                            case 3: mod_imm = (imm8 << 24) | (imm8 << 16) | (imm8 << 8) | imm8; break;
                        }
                    } else {
                        uint32_t unrot = 0x80u | (imm8 & 0x7Fu);
                        mod_imm = ror32(unrot, (imm12 >> 7) & 0x1Fu);
                    }
                    uint32_t op1 = cpu.r[rn];
                    uint32_t res = 0;
                    int write_rd = (rd != 15);
                    switch (op) {
                        case 0x0: res = op1 & mod_imm; if (s) update_nz(&cpu, res); break;
                        case 0x1: res = op1 & ~mod_imm; if (s) update_nz(&cpu, res); break;
                        case 0x2: res = (rn == 15) ? mod_imm : (op1 | mod_imm); if (s) update_nz(&cpu, res); break;
                        case 0x3: res = (rn == 15) ? ~mod_imm : (op1 | ~mod_imm); if (s) update_nz(&cpu, res); break;
                        case 0x4: res = op1 ^ mod_imm; if (s) update_nz(&cpu, res); break;
                        case 0x8: res = op1 + mod_imm; if (s) update_add_flags(&cpu, op1, mod_imm, res); break;
                        case 0xA: res = op1 + mod_imm + ((cpu.cpsr >> 29) & 1u); if (s) update_add_flags(&cpu, op1, mod_imm, res); break;
                        case 0xB: res = op1 - mod_imm - !((cpu.cpsr >> 29) & 1u); if (s) update_sub_flags(&cpu, op1, mod_imm, res); break;
                        case 0xD: res = op1 - mod_imm; if (s) update_sub_flags(&cpu, op1, mod_imm, res); break;
                        case 0xE: res = mod_imm - op1; if (s) update_sub_flags(&cpu, mod_imm, op1, res); break;
                        default:  res = mod_imm; break;
                    }
                    if (write_rd) cpu.r[rd] = res;
                    continue;
                }
                /* PUSH.W {reglist, lr} (E92D xxxx) */
                if (insn == 0xE92Du) {
                    uint32_t mask = insn2;
                    for (int r = 14; r >= 0; r--) {
                        if (mask & (1u << r)) {
                            cpu.r[13] -= 4;
                            *(uint32_t *)(uintptr_t)cpu.r[13] = cpu.r[r];
                        }
                    }
                    continue;
                }
                /* POP.W {reglist, pc} (E8BD xxxx) */
                if (insn == 0xE8BDu) {
                    uint32_t mask = insn2;
                    for (int r = 0; r <= 15; r++) {
                        if (mask & (1u << r)) {
                            uint32_t val = *(uint32_t *)(uintptr_t)cpu.r[13];
                            cpu.r[13] += 4;
                            if (r == 15) branch_to(&cpu, val);
                            else cpu.r[r] = val;
                        }
                    }
                    continue;
                }
                /* TBB / TBH and byte/half/dual exclusives (E8D0 / E8C0) */
                if ((insn & 0xFFE0u) == 0xE8C0u) {
                    int l = (insn >> 4) & 1;
                    uint32_t rn = insn & 0xFu;
                    uint32_t op = (insn2 >> 4) & 0xFu;
                    uint32_t base_v = (rn == 15) ? (pc + 4) : cpu.r[rn];
                    uint32_t rt  = (insn2 >> 12) & 0xFu;
                    uint32_t rt2 = (insn2 >> 8) & 0xFu;
                    uint32_t rd  = insn2 & 0xFu;
                    if (l && (op == 0 || op == 1)) {
                        uint32_t rm = insn2 & 0xFu;
                        uint32_t off = (op == 0)
                            ? *(const uint8_t *)(uintptr_t)(base_v + cpu.r[rm])
                            : *(const uint16_t *)(uintptr_t)(base_v + (cpu.r[rm] << 1));
                        cpu.r[15] = pc + 4 + off * 2u;
                        continue;
                    }
                    if (l) {
                        if (op == 4)      cpu.r[rt] = *(const uint8_t *)(uintptr_t)base_v;
                        else if (op == 5) cpu.r[rt] = *(const uint16_t *)(uintptr_t)base_v;
                        else if (op == 7) {
                            cpu.r[rt]  = *(const uint32_t *)(uintptr_t)base_v;
                            cpu.r[rt2] = *(const uint32_t *)(uintptr_t)(base_v + 4);
                        }
                    } else {
                        if (op == 4)      *(uint8_t *)(uintptr_t)base_v  = (uint8_t)cpu.r[rt];
                        else if (op == 5) *(uint16_t *)(uintptr_t)base_v = (uint16_t)cpu.r[rt];
                        else if (op == 7) {
                            *(uint32_t *)(uintptr_t)base_v       = cpu.r[rt];
                            *(uint32_t *)(uintptr_t)(base_v + 4) = cpu.r[rt2];
                        }
                        if (op == 4 || op == 5 || op == 7) cpu.r[rd] = 0; /* success */
                    }
                    continue;
                }
                /* LDREX (E85x) / STREX (E84x) — single-threaded interpreter: always succeed */
                if ((insn & 0xFFE0u) == 0xE840u) {
                    uint32_t rn = insn & 0xFu;
                    uint32_t rt = (insn2 >> 12) & 0xFu;
                    uint32_t addr = cpu.r[rn] + (insn2 & 0xFFu) * 4u;
                    if (insn & 0x10u) {
                        cpu.r[rt] = *(const uint32_t *)(uintptr_t)addr;
                    } else {
                        uint32_t rd = (insn2 >> 8) & 0xFu;
                        *(uint32_t *)(uintptr_t)addr = cpu.r[rt];
                        cpu.r[rd] = 0;
                    }
                    continue;
                }
                /* LDRD / STRD (E840..E9FF with bit 6 == 1) */
                if ((insn & 0xFE40u) == 0xE840u) {
                    int p = (insn >> 8) & 1;
                    int u = (insn >> 7) & 1;
                    int w = (insn >> 5) & 1;
                    int l = (insn >> 4) & 1;
                    uint32_t rn  = insn & 0xFu;
                    uint32_t rt  = (insn2 >> 12) & 0xFu;
                    uint32_t rt2 = (insn2 >> 8) & 0xFu;
                    uint32_t off = (insn2 & 0xFFu) * 4u;
                    uint32_t base_v = (rn == 15) ? ((pc + 4) & ~3u) : cpu.r[rn];
                    uint32_t addr = p ? (u ? base_v + off : base_v - off) : base_v;
                    if (l) {
                        cpu.r[rt]  = *(const uint32_t *)(uintptr_t)addr;
                        cpu.r[rt2] = *(const uint32_t *)(uintptr_t)(addr + 4);
                    } else {
                        *(uint32_t *)(uintptr_t)addr       = cpu.r[rt];
                        *(uint32_t *)(uintptr_t)(addr + 4) = cpu.r[rt2];
                    }
                    if (!p) addr = u ? base_v + off : base_v - off;
                    if (w && rn != 15) cpu.r[rn] = addr;
                    continue;
                }
                /* LDM.W / STM.W (E880..E93F) */
                if ((insn & 0xFE40u) == 0xE800u) {
                    int u = (insn >> 7) & 1;
                    int w = (insn >> 5) & 1;
                    int l = (insn >> 4) & 1;
                    uint32_t rn = insn & 0xFu;
                    uint32_t mask = insn2;
                    int count = 0;
                    for (int r = 0; r < 16; r++) if (mask & (1u << r)) count++;
                    uint32_t base_v = cpu.r[rn];
                    uint32_t addr = u ? base_v : (base_v - (uint32_t)count * 4u);
                    for (int r = 0; r < 16; r++) {
                        if (mask & (1u << r)) {
                            if (l) {
                                uint32_t val = *(const uint32_t *)(uintptr_t)addr;
                                if (r == 15) branch_to(&cpu, val);
                                else cpu.r[r] = val;
                            } else {
                                *(uint32_t *)(uintptr_t)addr = cpu.r[r];
                            }
                            addr += 4;
                        }
                    }
                    if (w) cpu.r[rn] = u ? (base_v + (uint32_t)count * 4u) : (base_v - (uint32_t)count * 4u);
                    continue;
                }
                /* Single Data Transfer Thumb-2: LDR.W / STR.W / LDRB.W / STRB.W / LDRH.W / STRH.W (F800..F8FF) */
                if ((insn & 0xFF00u) == 0xF800u) {
                    int sign_ext = (insn >> 8) & 1;
                    int imm12_mode = (insn >> 7) & 1;
                    uint32_t size = (insn >> 5) & 3u; /* 0=byte, 1=half, 2=word */
                    int load = (insn >> 4) & 1;
                    uint32_t rn = insn & 0xFu;
                    uint32_t rt = (insn2 >> 12) & 0xFu;
                    uint32_t base_v = (rn == 15) ? ((pc + 4) & ~3u) : cpu.r[rn];
                    uint32_t eff = base_v;
                    int writeback = 0;
                    uint32_t wb_val = base_v;

                    if (rn == 15) {
                        uint32_t off = insn2 & 0xFFFu;
                        eff = imm12_mode ? (base_v + off) : (base_v - off);
                    } else if (imm12_mode) {
                        eff = base_v + (insn2 & 0xFFFu);
                    } else if (insn2 & 0x0800u) {
                        /* Pre/post-indexed or negative 8-bit immediate */
                        int p = (insn2 >> 10) & 1;
                        int u = (insn2 >> 9) & 1;
                        int w = (insn2 >> 8) & 1;
                        uint32_t off = insn2 & 0xFFu;
                        wb_val = u ? (base_v + off) : (base_v - off);
                        eff = p ? wb_val : base_v;
                        writeback = w || !p;
                    } else {
                        /* Shifted register offset */
                        uint32_t rm = insn2 & 0xFu;
                        uint32_t sh = (insn2 >> 4) & 3u;
                        eff = base_v + (cpu.r[rm] << sh);
                    }

                    if (load) {
                        uint32_t val = 0;
                        if (size == 0) {
                            val = sign_ext ? (uint32_t)(int32_t)*(const int8_t *)(uintptr_t)eff
                                           : *(const uint8_t *)(uintptr_t)eff;
                        } else if (size == 1) {
                            val = sign_ext ? (uint32_t)(int32_t)*(const int16_t *)(uintptr_t)eff
                                           : *(const uint16_t *)(uintptr_t)eff;
                        } else {
                            val = *(const uint32_t *)(uintptr_t)eff;
                        }
                        if (writeback && rn != 15) cpu.r[rn] = wb_val;
                        if (rt == 15) branch_to(&cpu, val);
                        else if (rt != 15) cpu.r[rt] = val;
                    } else {
                        uint32_t val = cpu.r[rt];
                        if (size == 0)      *(uint8_t *)(uintptr_t)eff  = (uint8_t)val;
                        else if (size == 1) *(uint16_t *)(uintptr_t)eff = (uint16_t)val;
                        else                *(uint32_t *)(uintptr_t)eff = val;
                        if (writeback && rn != 15) cpu.r[rn] = wb_val;
                    }
                    continue;
                }
                /* Shifted Register Data Processing & Multiply/Divide Thumb-2 (EA00..FBFF) */
                if ((insn & 0xFE00u) == 0xEA00u || (insn & 0xFF00u) == 0xFA00u || (insn & 0xFF00u) == 0xFB00u) {
                    if ((insn & 0xFE00u) == 0xEA00u) {
                        uint32_t op = (insn >> 5) & 0xFu;
                        int s = (insn >> 4) & 1;
                        uint32_t rn = insn & 0xFu;
                        uint32_t rd = (insn2 >> 8) & 0xFu;
                        uint32_t rm = insn2 & 0xFu;
                        uint32_t sh_type = (insn2 >> 4) & 3u;
                        uint32_t sh_amt  = (((insn2 >> 12) & 7u) << 2) | ((insn2 >> 6) & 3u);
                        uint32_t mval = cpu.r[rm];
                        uint32_t op2 = mval;
                        if (sh_amt > 0) {
                            if (sh_type == 0)      op2 = mval << sh_amt;
                            else if (sh_type == 1) op2 = mval >> sh_amt;
                            else if (sh_type == 2) op2 = (uint32_t)((int32_t)mval >> sh_amt);
                            else                   op2 = ror32(mval, sh_amt);
                        }
                        uint32_t op1 = (rn == 15) ? 0 : cpu.r[rn];
                        uint32_t res = 0;
                        switch (op) {
                            case 0x0: res = op1 & op2; break;
                            case 0x1: res = op1 & ~op2; break;
                            case 0x2: res = (rn == 15) ? op2 : (op1 | op2); break;
                            case 0x3: res = (rn == 15) ? ~op2 : (op1 | ~op2); break;
                            case 0x4: res = op1 ^ op2; break;
                            case 0x8: res = op1 + op2; if (s) update_add_flags(&cpu, op1, op2, res); break;
                            case 0xD: res = op1 - op2; if (s) update_sub_flags(&cpu, op1, op2, res); break;
                            case 0xE: res = op2 - op1; if (s) update_sub_flags(&cpu, op2, op1, res); break;
                            default:  res = op2; break;
                        }
                        if (rd != 15) cpu.r[rd] = res;
                    } else if ((insn & 0xFF00u) == 0xFA00u) {
                        uint32_t op1 = (insn >> 4) & 0xFu;
                        uint32_t rn  = insn & 0xFu;
                        uint32_t rd  = (insn2 >> 8) & 0xFu;
                        uint32_t rm  = insn2 & 0xFu;

                        /* CLZ Rd, Rm (0xFAB0 0xF080) */
                        if ((insn & 0xFFF0u) == 0xFAB0u && (insn2 & 0xF0F0u) == 0xF080u) {
                            cpu.r[rd] = cpu.r[rm] ? (uint32_t)__builtin_clz(cpu.r[rm]) : 32u;
                        }
                        /* REV / REV16 / REVSH */
                        else if ((insn & 0xFFF0u) == 0xFA90u) {
                            uint32_t op2 = (insn2 >> 4) & 0xFu;
                            if (op2 == 8)      cpu.r[rd] = __builtin_bswap32(cpu.r[rm]);
                            else if (op2 == 9) {
                                uint32_t v = cpu.r[rm];
                                cpu.r[rd] = ((v & 0xFFu) << 8) | ((v >> 8) & 0xFFu) |
                                            ((v & 0xFF0000u) << 8) | ((v >> 8) & 0xFF0000u);
                            } else if (op2 == 11) {
                                uint32_t v = cpu.r[rm];
                                cpu.r[rd] = (uint32_t)(int32_t)(int16_t)(((v & 0xFFu) << 8) | ((v >> 8) & 0xFFu));
                            }
                        }
                        /* Register Shift: LSL / LSR / ASR / ROR Rd, Rn, Rm (FA00..FA7F with insn2 bit 7 == 0) */
                        else if ((insn & 0xFF80u) == 0xFA00u && (insn2 & 0x0080u) == 0) {
                            uint32_t shift_op = (insn >> 5) & 3u;
                            uint32_t shift = cpu.r[rm] & 0xFFu;
                            uint32_t val = cpu.r[rn];
                            uint32_t res = 0;
                            if (shift_op == 0) {
                                res = (shift < 32) ? (val << shift) : 0;
                            } else if (shift_op == 1) {
                                res = (shift < 32) ? (val >> shift) : 0;
                            } else if (shift_op == 2) {
                                res = (shift < 32) ? (uint32_t)((int32_t)val >> shift)
                                                   : ((int32_t)val < 0 ? 0xFFFFFFFFu : 0);
                            } else {
                                res = ror32(val, shift & 31u);
                            }
                            cpu.r[rd] = res;
                        }
                        /* SXTH / SXTB / UXTH / UXTB with optional rotation and add (FA00..FA5F with insn2 bit 7 == 1) */
                        else if ((insn & 0xFF80u) == 0xFA00u && (insn2 & 0x0080u) != 0) {
                            uint32_t rot = (insn2 >> 4) & 3u;
                            uint32_t v = ror32(cpu.r[rm], rot * 8);
                            uint32_t res = 0;
                            if (op1 == 0x0)      res = (uint32_t)(int32_t)(int16_t)v;  /* SXTAH / SXTH */
                            else if (op1 == 0x1) res = v & 0xFFFFu;                     /* UXTAH / UXTH */
                            else if (op1 == 0x4) res = (uint32_t)(int32_t)(int8_t)v;   /* SXTAB / SXTB */
                            else if (op1 == 0x5) res = v & 0xFFu;                      /* UXTAB / UXTB */
                            if (rn != 15) res += cpu.r[rn];
                            cpu.r[rd] = res;
                        }
                    } else if ((insn & 0xFF00u) == 0xFB00u) {
                        uint32_t rn = insn & 0xFu;
                        uint32_t rd = (insn2 >> 8) & 0xFu;
                        uint32_t rm = insn2 & 0xFu;
                        uint32_t ra = (insn2 >> 12) & 0xFu;

                        /* MUL / MLA / MLS (FB00..FB10) */
                        if ((insn & 0xFFF0u) == 0xFB00u) {
                            int is_mls = (insn2 >> 4) & 1;
                            if (is_mls) {
                                cpu.r[rd] = cpu.r[ra] - (cpu.r[rn] * cpu.r[rm]);
                            } else if (ra == 15) {
                                cpu.r[rd] = cpu.r[rn] * cpu.r[rm];
                            } else {
                                cpu.r[rd] = cpu.r[rn] * cpu.r[rm] + cpu.r[ra];
                            }
                        }
                        /* SDIV (FB90) */
                        else if ((insn & 0xFFF0u) == 0xFB90u) {
                            cpu.r[rd] = cpu.r[rm] ? (uint32_t)((int32_t)cpu.r[rn] / (int32_t)cpu.r[rm]) : 0;
                        }
                        /* UDIV (FBB0) */
                        else if ((insn & 0xFFF0u) == 0xFBB0u) {
                            cpu.r[rd] = cpu.r[rm] ? (cpu.r[rn] / cpu.r[rm]) : 0;
                        }
                        /* SMULL / SMLAL (FB80) & UMULL / UMLAL (FBA0) */
                        else if ((insn & 0xFFF0u) == 0xFB80u) {
                            int64_t prod = (int64_t)(int32_t)cpu.r[rn] * (int64_t)(int32_t)cpu.r[rm];
                            uint32_t rd_lo = ra;
                            uint32_t rd_hi = rd;
                            cpu.r[rd_lo] = (uint32_t)prod;
                            cpu.r[rd_hi] = (uint32_t)(prod >> 32);
                        }
                        else if ((insn & 0xFFF0u) == 0xFBA0u) {
                            uint64_t prod = (uint64_t)cpu.r[rn] * (uint64_t)cpu.r[rm];
                            uint32_t rd_lo = ra;
                            uint32_t rd_hi = rd;
                            cpu.r[rd_lo] = (uint32_t)prod;
                            cpu.r[rd_hi] = (uint32_t)(prod >> 32);
                        }
                    }
                    continue;
                }
                /* VFP / NEON instructions (EC00..EFFF, F900..F9FF) */
                if ((insn & 0xFC00u) == 0xEC00u || (insn & 0xFF00u) == 0xF900u) {
                    if (arm32_exec_thumb2_vfp(&cpu, pc, insn, insn2)) {
                        continue;
                    }
                    /* Fallback: Keep SP balanced on VPUSH (ED2D) and VPOP (ECBD) */
                    if ((insn & 0xFFBFu) == 0xED2Du) {
                        cpu.r[13] -= (insn2 & 0xFFu) * 4u;
                    } else if ((insn & 0xFFBFu) == 0xECBDu) {
                        cpu.r[13] += (insn2 & 0xFFu) * 4u;
                    }
                    continue;
                }
                /* Other Thumb-2 system/barrier/hint instructions: treat as NOP to continue execution */
                continue;
            }

            /* 16-bit Thumb instructions */
            uint32_t pre_flags = cpu.cpsr;
            if ((insn & 0xF800u) == 0x1800u) {
                /* ADD/SUB reg/imm3 */
                int sub = (insn >> 9) & 1;
                int imm = (insn >> 10) & 1;
                uint32_t rm_imm = (insn >> 6) & 7;
                uint32_t rn = (insn >> 3) & 7;
                uint32_t rd = insn & 7;
                uint32_t op2 = imm ? rm_imm : cpu.r[rm_imm];
                uint32_t res = sub ? (cpu.r[rn] - op2) : (cpu.r[rn] + op2);
                if (sub) update_sub_flags(&cpu, cpu.r[rn], op2, res);
                else     update_add_flags(&cpu, cpu.r[rn], op2, res);
                cpu.r[rd] = res;
            } else if ((insn & 0xE000u) == 0x0000u) {
                /* LSL / LSR / ASR immediate */
                uint32_t op  = (insn >> 11) & 3;
                uint32_t imm = (insn >> 6) & 0x1F;
                uint32_t rm  = (insn >> 3) & 7;
                uint32_t rd  = insn & 7;
                uint32_t val = cpu.r[rm];
                if (op == 0) val = imm ? (val << imm) : val;
                else if (op == 1) val = imm ? (val >> imm) : 0;
                else if (op == 2) val = imm ? (uint32_t)((int32_t)val >> imm) : (uint32_t)((int32_t)val >> 31);
                cpu.r[rd] = val;
                update_nz(&cpu, val);
            } else if ((insn & 0xE000u) == 0x2000u) {
                /* MOV / CMP / ADD / SUB 8-bit immediate */
                uint32_t op  = (insn >> 11) & 3;
                uint32_t rdn = (insn >> 8) & 7;
                uint32_t imm = insn & 0xFF;
                if (op == 0) { cpu.r[rdn] = imm; update_nz(&cpu, imm); }
                else if (op == 1) { update_sub_flags(&cpu, cpu.r[rdn], imm, cpu.r[rdn] - imm); }
                else if (op == 2) { uint32_t r = cpu.r[rdn] + imm; update_add_flags(&cpu, cpu.r[rdn], imm, r); cpu.r[rdn] = r; }
                else if (op == 3) { uint32_t r = cpu.r[rdn] - imm; update_sub_flags(&cpu, cpu.r[rdn], imm, r); cpu.r[rdn] = r; }
            } else if ((insn & 0xFC00u) == 0x4000u) {
                /* 16-bit ALU operations */
                uint32_t op  = (insn >> 6) & 0xFu;
                uint32_t rm  = (insn >> 3) & 7u;
                uint32_t rdn = insn & 7u;
                uint32_t a = cpu.r[rdn], b = cpu.r[rm], r = a;
                switch (op) {
                    case 0x0: r = a & b; cpu.r[rdn] = r; update_nz(&cpu, r); break;
                    case 0x1: r = a ^ b; cpu.r[rdn] = r; update_nz(&cpu, r); break;
                    case 0x2: r = (b < 32) ? (a << b) : 0; cpu.r[rdn] = r; update_nz(&cpu, r); break;
                    case 0x3: r = (b < 32) ? (a >> b) : 0; cpu.r[rdn] = r; update_nz(&cpu, r); break;
                    case 0x4: r = (b < 32) ? (uint32_t)((int32_t)a >> b) : (uint32_t)((int32_t)a >> 31); cpu.r[rdn] = r; update_nz(&cpu, r); break;
                    case 0x5: r = a + b + ((cpu.cpsr >> 29) & 1u); update_add_flags(&cpu, a, b, r); cpu.r[rdn] = r; break;
                    case 0x6: r = a - b - !((cpu.cpsr >> 29) & 1u); update_sub_flags(&cpu, a, b, r); cpu.r[rdn] = r; break;
                    case 0x7: r = ror32(a, b & 0x1Fu); cpu.r[rdn] = r; update_nz(&cpu, r); break;
                    case 0x8: update_nz(&cpu, a & b); break;
                    case 0x9: r = 0u - b; update_sub_flags(&cpu, 0u, b, r); cpu.r[rdn] = r; break;
                    case 0xA: update_sub_flags(&cpu, a, b, a - b); break;
                    case 0xB: update_add_flags(&cpu, a, b, a + b); break;
                    case 0xC: r = a | b; cpu.r[rdn] = r; update_nz(&cpu, r); break;
                    case 0xD: r = a * b; cpu.r[rdn] = r; update_nz(&cpu, r); break;
                    case 0xE: r = a & ~b; cpu.r[rdn] = r; update_nz(&cpu, r); break;
                    case 0xF: r = ~b; cpu.r[rdn] = r; update_nz(&cpu, r); break;
                }
            } else if ((insn & 0xFC00u) == 0x4400u) {
                /* Special data / branch & exchange (ADD/CMP/MOV high regs, BX, BLX) */
                uint32_t op = (insn >> 8) & 3;
                uint32_t rm = (insn >> 3) & 0xF;
                uint32_t rdn = ((insn & 0x80u) >> 4) | (insn & 7u);
                uint32_t mval = (rm == 15) ? (pc + 4) : cpu.r[rm];
                if (op == 0) {
                    if (rdn == 15) {
                        /* In Thumb state, ADD PC, Rm stays in Thumb mode! Keep bit 0 set. */
                        branch_to(&cpu, (cpu.r[15] + mval) | 1u);
                    } else cpu.r[rdn] += mval;
                } else if (op == 1) {
                    update_sub_flags(&cpu, cpu.r[rdn], mval, cpu.r[rdn] - mval);
                } else if (op == 2) {
                    if (rdn == 15) branch_to(&cpu, mval);
                    else cpu.r[rdn] = mval;
                } else if (op == 3) {
                    if (insn & 0x80u) cpu.r[14] = (pc + 2) | 1u; /* BLX rm */
                    branch_to(&cpu, mval);
                }
            } else if ((insn & 0xF800u) == 0x4800u) {
                /* LDR Rt, [PC, #imm8*4] */
                uint32_t rt  = (insn >> 8) & 7;
                uint32_t off = (insn & 0xFFu) * 4u;
                uint32_t addr = ((pc + 4) & ~3u) + off;
                cpu.r[rt] = *(const uint32_t *)(uintptr_t)addr;
            } else if ((insn & 0xF000u) == 0x5000u) {
                /* STR/LDR register offset [Rn, Rm] */
                uint32_t op = (insn >> 9) & 7u;
                uint32_t rm = (insn >> 6) & 7u;
                uint32_t rn = (insn >> 3) & 7u;
                uint32_t rt = insn & 7u;
                uint32_t addr = cpu.r[rn] + cpu.r[rm];
                switch (op) {
                    case 0: *(uint32_t *)(uintptr_t)addr = cpu.r[rt]; break;
                    case 1: *(uint16_t *)(uintptr_t)addr = (uint16_t)cpu.r[rt]; break;
                    case 2: *(uint8_t *)(uintptr_t)addr  = (uint8_t)cpu.r[rt]; break;
                    case 3: cpu.r[rt] = (uint32_t)(int32_t)*(const int8_t *)(uintptr_t)addr; break;
                    case 4: cpu.r[rt] = *(const uint32_t *)(uintptr_t)addr; break;
                    case 5: cpu.r[rt] = *(const uint16_t *)(uintptr_t)addr; break;
                    case 6: cpu.r[rt] = *(const uint8_t *)(uintptr_t)addr; break;
                    case 7: cpu.r[rt] = (uint32_t)(int32_t)*(const int16_t *)(uintptr_t)addr; break;
                }
            } else if ((insn & 0xF000u) == 0x6000u) {
                /* STR / LDR [Rn, #imm5*4] */
                int load = (insn >> 11) & 1;
                uint32_t off = ((insn >> 6) & 0x1Fu) * 4u;
                uint32_t rn  = (insn >> 3) & 7;
                uint32_t rt  = insn & 7;
                uint32_t addr = cpu.r[rn] + off;
                if (load) cpu.r[rt] = *(const uint32_t *)(uintptr_t)addr;
                else {
                    *(uint32_t *)(uintptr_t)addr = cpu.r[rt];
                }

            } else if ((insn & 0xF000u) == 0x7000u) {
                /* STRB / LDRB [Rn, #imm5] */
                int load = (insn >> 11) & 1;
                uint32_t off = (insn >> 6) & 0x1Fu;
                uint32_t rn  = (insn >> 3) & 7;
                uint32_t rt  = insn & 7;
                uint32_t addr = cpu.r[rn] + off;
                if (load) cpu.r[rt] = *(const uint8_t *)(uintptr_t)addr;
                else      *(uint8_t *)(uintptr_t)addr = (uint8_t)cpu.r[rt];
            } else if ((insn & 0xF000u) == 0x8000u) {
                /* STRH / LDRH [Rn, #imm5*2] */
                int load = (insn >> 11) & 1;
                uint32_t off = ((insn >> 6) & 0x1Fu) * 2u;
                uint32_t rn  = (insn >> 3) & 7;
                uint32_t rt  = insn & 7;
                uint32_t addr = cpu.r[rn] + off;
                if (load) cpu.r[rt] = *(const uint16_t *)(uintptr_t)addr;
                else      *(uint16_t *)(uintptr_t)addr = (uint16_t)cpu.r[rt];
            } else if ((insn & 0xF000u) == 0x9000u) {
                /* STR / LDR [SP, #imm8*4] */
                int load = (insn >> 11) & 1;
                uint32_t rt  = (insn >> 8) & 7;
                uint32_t off = (insn & 0xFFu) * 4u;
                uint32_t addr = cpu.r[13] + off;
                if (load) cpu.r[rt] = *(const uint32_t *)(uintptr_t)addr;
                else      *(uint32_t *)(uintptr_t)addr = cpu.r[rt];
            } else if ((insn & 0xF800u) == 0xA000u) {
                /* ADR Rd, label */
                uint32_t rd  = (insn >> 8) & 7;
                uint32_t off = (insn & 0xFFu) * 4u;
                cpu.r[rd] = ((pc + 4) & ~3u) + off;
            } else if ((insn & 0xF800u) == 0xA800u) {
                /* ADD Rd, SP, #imm8*4 */
                uint32_t rd  = (insn >> 8) & 7;
                uint32_t off = (insn & 0xFFu) * 4u;
                cpu.r[rd] = cpu.r[13] + off;
            } else if ((insn & 0xFF00u) == 0xB000u) {
                /* ADD/SUB SP, #imm7*4 */
                uint32_t off = (insn & 0x7Fu) * 4u;
                if (insn & 0x80u) cpu.r[13] -= off;
                else              cpu.r[13] += off;
            } else if ((insn & 0xF500u) == 0xB100u) {
                /* CBZ / CBNZ Rn, label */
                int nonzero = (insn >> 11) & 1;
                uint32_t rn = insn & 7u;
                uint32_t imm = (((insn >> 9) & 1u) << 6) | (((insn >> 3) & 0x1Fu) << 1);
                if ((nonzero && cpu.r[rn] != 0) || (!nonzero && cpu.r[rn] == 0)) {
                    cpu.r[15] = pc + 4 + imm;
                }
            } else if ((insn & 0xFF00u) == 0xB200u) {
                /* SXTH / SXTB / UXTH / UXTB */
                uint32_t op = (insn >> 6) & 3u;
                uint32_t rm = (insn >> 3) & 7u;
                uint32_t rd = insn & 7u;
                uint32_t v  = cpu.r[rm];
                if (op == 0)      cpu.r[rd] = (uint32_t)(int32_t)(int16_t)v;
                else if (op == 1) cpu.r[rd] = (uint32_t)(int32_t)(int8_t)v;
                else if (op == 2) cpu.r[rd] = v & 0xFFFFu;
                else              cpu.r[rd] = v & 0xFFu;
            } else if ((insn & 0xFE00u) == 0xB400u) {
                /* PUSH {reglist, lr} */
                if (insn & 0x100u) {
                    cpu.r[13] -= 4;
                    *(uint32_t *)(uintptr_t)cpu.r[13] = cpu.r[14];
                }
                for (int r = 7; r >= 0; r--) {
                    if (insn & (1u << r)) {
                        cpu.r[13] -= 4;
                        *(uint32_t *)(uintptr_t)cpu.r[13] = cpu.r[r];
                    }
                }
            } else if ((insn & 0xFE00u) == 0xBC00u) {
                /* POP {reglist, pc} */
                for (int r = 0; r <= 7; r++) {
                    if (insn & (1u << r)) {
                        cpu.r[r] = *(uint32_t *)(uintptr_t)cpu.r[13];
                        cpu.r[13] += 4;
                    }
                }
                if (insn & 0x100u) {
                    uint32_t new_pc = *(uint32_t *)(uintptr_t)cpu.r[13];
                    cpu.r[13] += 4;
                    branch_to(&cpu, new_pc);
                }
            } else if ((insn & 0xF000u) == 0xC000u) {
                /* STMIA / LDMIA Rn!, {reglist} */
                int load = (insn >> 11) & 1;
                uint32_t rn = (insn >> 8) & 7u;
                uint32_t addr = cpu.r[rn];
                for (int r = 0; r <= 7; r++) {
                    if (insn & (1u << r)) {
                        if (load) cpu.r[r] = *(const uint32_t *)(uintptr_t)addr;
                        else      *(uint32_t *)(uintptr_t)addr = cpu.r[r];
                        addr += 4;
                    }
                }
                if (!load || !(insn & (1u << rn))) cpu.r[rn] = addr;
            } else if ((insn & 0xF000u) == 0xD000u && (insn & 0x0E00u) != 0x0E00u) {
                /* B<cond> imm8 */
                uint32_t cond = (insn >> 8) & 0xF;
                if (check_cond(&cpu, cond)) {
                    int32_t off = (int32_t)(int8_t)(insn & 0xFFu) * 2;
                    cpu.r[15] = pc + 4 + off;
                }
            } else if ((insn & 0xF800u) == 0xE000u) {
                /* B imm11 */
                int32_t off = (int32_t)((insn & 0x7FFu) << 21) >> 20;
                cpu.r[15] = pc + 4 + off;
            } else if ((insn & 0xFF00u) == 0xBF00u) {
                /* IT (mask != 0) or NOP/YIELD/WFE/WFI/SEV hints (mask == 0) */
                if (insn & 0x000Fu) cpu.itstate = (uint8_t)(insn & 0xFFu);
            } else {
                continue;
            }
            if (in_it && !thumb16_is_compare(insn)) {
                cpu.cpsr = (cpu.cpsr & ~(FLAG_N | FLAG_Z | FLAG_C | FLAG_V)) |
                           (pre_flags & (FLAG_N | FLAG_Z | FLAG_C | FLAG_V));
            }
        } else {
            /* ---------------- 32-BIT ARM MODE ---------------- */
            uint32_t insn = *(const uint32_t *)(uintptr_t)pc;
            cpu.r[15] = pc + 4;

            uint32_t cond = insn >> 28;
            if (cond != 0xFu && !check_cond(&cpu, cond)) {
                continue;
            }

            /* VFP / NEON in ARM mode (coprocessors 10 & 11) */
            if ((insn & 0x0E000000u) == 0x0C000000u || (insn & 0x0E000000u) == 0x0E000000u) {
                if (arm32_exec_arm_vfp(&cpu, pc, insn)) {
                    continue;
                }
            }

            /* BX / BLX Rm */
            if ((insn & 0x0FFFFFF0u) == 0x012FFF10u || (insn & 0x0FFFFFF0u) == 0x012FFF30u) {
                uint32_t rm = insn & 0xFu;
                uint32_t target = cpu.r[rm];
                if ((insn & 0xF0u) == 0x30u) cpu.r[14] = pc + 4;
                branch_to(&cpu, target);
                continue;
            }

            /* CLZ Rd, Rm (0x016F0F10) */
            if ((insn & 0x0FFF0FF0u) == 0x016F0F10u) {
                uint32_t rd = (insn >> 12) & 0xFu;
                uint32_t rm = insn & 0xFu;
                uint32_t val = cpu.r[rm];
                uint32_t lz = 32;
                if (val != 0) {
                    lz = __builtin_clz(val);
                }
                cpu.r[rd] = lz;
                continue;
            }

            /* MOVW / MOVT (ARM mode: 0x03000000 / 0x03400000) */
            if ((insn & 0x0FB00000u) == 0x03000000u) {
                uint32_t imm16 = ((insn & 0x000F0000u) >> 4) | (insn & 0x00000FFFu);
                uint32_t rd = (insn >> 12) & 0xFu;
                int is_top = (insn >> 22) & 1;
                if (is_top) {
                    cpu.r[rd] = (cpu.r[rd] & 0x0000FFFFu) | (imm16 << 16);
                } else {
                    cpu.r[rd] = imm16;
                }
                continue;
            }

            /* Multiply and Multiply-Accumulate: MUL, MLA (0x00000090) */
            if ((insn & 0x0FC000F0u) == 0x00000090u) {
                int a = (insn >> 21) & 1; /* Accumulate */
                int s = (insn >> 20) & 1;
                uint32_t rd = (insn >> 16) & 0xFu;
                uint32_t rn = (insn >> 12) & 0xFu;
                uint32_t rs = (insn >> 8) & 0xFu;
                uint32_t rm = insn & 0xFu;
                uint32_t res = cpu.r[rm] * cpu.r[rs];
                if (a) res += cpu.r[rn];
                cpu.r[rd] = res;
                if (s) update_nz(&cpu, res);
                continue;
            }

            /* Extra load/store (LDRH, STRH, LDRD, STRD, LDRSB, LDRSH): (insn & 0x0E000090) == 0x00000090 */
            if ((insn & 0x0E000090u) == 0x00000090u && (insn & 0x00000060u) != 0) {
                int p = (insn >> 24) & 1;
                int u = (insn >> 23) & 1;
                int i_imm = (insn >> 22) & 1;
                int w = (insn >> 21) & 1;
                int l = (insn >> 20) & 1;
                uint32_t rn = (insn >> 16) & 0xFu;
                uint32_t rd = (insn >> 12) & 0xFu;
                uint32_t op = (insn >> 5) & 3u; /* 1 = H, 2 = D/SB, 3 = D/SH */
                uint32_t off = i_imm ? (((insn >> 8) & 0xFu) << 4) | (insn & 0xFu)
                                     : cpu.r[insn & 0xFu];
                uint32_t base_addr = (rn == 15) ? (pc + 8) : cpu.r[rn];
                uint32_t eff = p ? (u ? base_addr + off : base_addr - off) : base_addr;

                if (op == 1) {
                    /* Halfword (STRH / LDRH) */
                    if (l) {
                        cpu.r[rd] = *(const uint16_t *)(uintptr_t)eff;
                    } else {
                        *(uint16_t *)(uintptr_t)eff = (uint16_t)cpu.r[rd];
                    }
                } else if (op == 2) {
                    /* LDRD / STRD or LDRSB */
                    if (l) {
                        cpu.r[rd] = (uint32_t)(int32_t)*(const int8_t *)(uintptr_t)eff;
                    } else {
                        /* STRD */
                        *(uint32_t *)(uintptr_t)eff = cpu.r[rd];
                        *(uint32_t *)(uintptr_t)(eff + 4) = cpu.r[rd + 1];
                    }
                } else if (op == 3) {
                    /* LDRD (load) or LDRSH */
                    if (l) {
                        if ((insn & 0x00100000u) == 0) {
                            /* LDRD */
                            cpu.r[rd] = *(const uint32_t *)(uintptr_t)eff;
                            cpu.r[rd + 1] = *(const uint32_t *)(uintptr_t)(eff + 4);
                        } else {
                            /* LDRSH */
                            cpu.r[rd] = (uint32_t)(int32_t)*(const int16_t *)(uintptr_t)eff;
                        }
                    }
                }

                if (!p) eff = u ? base_addr + off : base_addr - off;
                if (w || !p) cpu.r[rn] = eff;
                continue;
            }

            /* B / BL imm24 */
            if ((insn & 0x0E000000u) == 0x0A000000u) {
                int32_t off = ((int32_t)(insn << 8)) >> 6;
                if (insn & (1u << 24)) cpu.r[14] = pc + 4;
                if (cond == 0xFu) {
                    /* BLX (switch to Thumb) */
                    off += (insn & (1u << 24)) ? 2 : 0;
                    cpu.r[14] = pc + 4;
                    branch_to(&cpu, (pc + 8 + off) | 1u);
                } else {
                    cpu.r[15] = pc + 8 + off;
                }
                continue;
            }

            /* Block Data Transfer (LDM / STM / PUSH / POP) */
            if ((insn & 0x0E000000u) == 0x08000000u) {
                int p = (insn >> 24) & 1;
                int u = (insn >> 23) & 1;
                int w = (insn >> 21) & 1;
                int l = (insn >> 20) & 1;
                uint32_t rn = (insn >> 16) & 0xFu;
                uint32_t mask = insn & 0xFFFFu;
                int count = 0;
                for (int r = 0; r < 16; r++) if (mask & (1u << r)) count++;
                uint32_t base_addr = cpu.r[rn];
                uint32_t addr = u ? (base_addr + (p ? 4u : 0u))
                                  : (base_addr - (uint32_t)count * 4u + (p ? 0u : 4u));
                for (int r = 0; r < 16; r++) {
                    if (mask & (1u << r)) {
                        if (l) {
                            uint32_t val = *(const uint32_t *)(uintptr_t)addr;
                            if (r == 15) branch_to(&cpu, val);
                            else cpu.r[r] = val;
                        } else {
                            *(uint32_t *)(uintptr_t)addr = (r == 15) ? (pc + 8) : cpu.r[r];
                        }
                        addr += 4;
                    }
                }
                if (w) {
                    cpu.r[rn] = u ? (base_addr + (uint32_t)count * 4u)
                                  : (base_addr - (uint32_t)count * 4u);
                }
                continue;
            }

            /* Single Data Transfer (LDR / STR) */
            if ((insn & 0x0C000000u) == 0x04000000u) {
                int i = (insn >> 25) & 1;
                int p = (insn >> 24) & 1;
                int u = (insn >> 23) & 1;
                int b = (insn >> 22) & 1;
                int w = (insn >> 21) & 1;
                int l = (insn >> 20) & 1;
                uint32_t rn = (insn >> 16) & 0xFu;
                uint32_t rd = (insn >> 12) & 0xFu;
                uint32_t base_addr = (rn == 15) ? (pc + 8) : cpu.r[rn];
                uint32_t off = i ? cpu.r[insn & 0xFu] : (insn & 0xFFFu);
                uint32_t eff = p ? (u ? base_addr + off : base_addr - off) : base_addr;
                if (l) {
                    uint32_t val = b ? *(const uint8_t *)(uintptr_t)eff : *(const uint32_t *)(uintptr_t)eff;
                    if (rd == 15) branch_to(&cpu, val);
                    else cpu.r[rd] = val;
                } else {
                    uint32_t val = (rd == 15) ? (pc + 8) : cpu.r[rd];
                    if (b) *(uint8_t *)(uintptr_t)eff = (uint8_t)val;
                    else   *(uint32_t *)(uintptr_t)eff = val;
                }
                if (!p) eff = u ? base_addr + off : base_addr - off;
                if (w || !p) cpu.r[rn] = eff;
                continue;
            }

            /* Data Processing (MOV, ADD, SUB, CMP, AND, ORR, EOR, BIC, MVN) */
            if ((insn & 0x0C000000u) == 0x00000000u) {
                int i = (insn >> 25) & 1;
                uint32_t opcode = (insn >> 21) & 0xFu;
                int s = (insn >> 20) & 1;
                uint32_t rn = (insn >> 16) & 0xFu;
                uint32_t rd = (insn >> 12) & 0xFu;
                uint32_t op1 = (rn == 15) ? (pc + 8) : cpu.r[rn];
                uint32_t op2 = 0;
                if (i) {
                    uint32_t imm8 = insn & 0xFFu;
                    uint32_t rot  = ((insn >> 8) & 0xFu) * 2u;
                    op2 = ror32(imm8, rot);
                } else {
                    uint32_t rm = insn & 0xFu;
                    uint32_t mval = (rm == 15) ? (pc + 8) : cpu.r[rm];
                    uint32_t sh_type = (insn >> 5) & 3u;
                    uint32_t sh_amt  = ((insn >> 4) & 1u) ? (cpu.r[(insn >> 8) & 0xFu] & 0xFFu)
                                                          : ((insn >> 7) & 0x1Fu);
                    if (sh_type == 0)      op2 = (sh_amt < 32) ? (mval << sh_amt) : 0;
                    else if (sh_type == 1) op2 = (sh_amt < 32) ? (mval >> sh_amt) : 0;
                    else if (sh_type == 2) op2 = (sh_amt < 32) ? (uint32_t)((int32_t)mval >> sh_amt) : (uint32_t)((int32_t)mval >> 31);
                    else                   op2 = ror32(mval, sh_amt);
                }

                uint32_t res = 0;
                int write_rd = 1;
                switch (opcode) {
                    case 0x0: res = op1 & op2; if (s) update_nz(&cpu, res); break; /* AND */
                    case 0x1: res = op1 ^ op2; if (s) update_nz(&cpu, res); break; /* EOR */
                    case 0x2: res = op1 - op2; if (s) update_sub_flags(&cpu, op1, op2, res); break; /* SUB */
                    case 0x3: res = op2 - op1; if (s) update_sub_flags(&cpu, op2, op1, res); break; /* RSB */
                    case 0x4: res = op1 + op2; if (s) update_add_flags(&cpu, op1, op2, res); break; /* ADD */
                    case 0x8: res = op1 & op2; update_nz(&cpu, res); write_rd = 0; break; /* TST */
                    case 0x9: res = op1 ^ op2; update_nz(&cpu, res); write_rd = 0; break; /* TEQ */
                    case 0xA: res = op1 - op2; update_sub_flags(&cpu, op1, op2, res); write_rd = 0; break; /* CMP */
                    case 0xB: res = op1 + op2; update_add_flags(&cpu, op1, op2, res); write_rd = 0; break; /* CMN */
                    case 0xC: res = op1 | op2; if (s) update_nz(&cpu, res); break; /* ORR */
                    case 0xD: res = op2;       if (s) update_nz(&cpu, res); break; /* MOV */
                    case 0xE: res = op1 & ~op2; if (s) update_nz(&cpu, res); break; /* BIC */
                    case 0xF: res = ~op2;      if (s) update_nz(&cpu, res); break; /* MVN */
                    default:  res = op2; break;
                }
                if (write_rd) {
                    if (rd == 15) branch_to(&cpu, res);
                    else cpu.r[rd] = res;
                }
                continue;
            }

            char where[128];
            watchdog_describe_address(pc, where, sizeof(where));
            printf("[ARM32] Unhandled ARM insn %08X at %s\n", insn, where);
            break;
        }
    }

    guard->active = 0;
    return cpu.r[0];
}
