#ifndef ARM32_VFP_H
#define ARM32_VFP_H

#include <stdint.h>
#include <string.h>
#include <math.h>

/* Execute Thumb-2 VFP & NEON instructions (EC00..EFEF, F900..F9FF)
 * Returns 1 if handled, 0 if unhandled */
static inline int arm32_exec_thumb2_vfp(arm32_cpu_t *cpu, uint32_t pc, uint16_t insn, uint16_t insn2) {
    /* 1. Element & Structure load/store: VLD1 / VST1 / VLD2 / VST2 / VLD3 / VST3 / VLD4 / VST4 (F900..F9FF) */
    if ((insn & 0xFF00u) == 0xF900u) {
        int load = (insn >> 4) & 1;
        uint32_t rn = insn & 0xFu;
        uint32_t d_bit = (insn >> 6) & 1u;
        uint32_t rd = ((insn2 >> 12) & 0xFu) | (d_bit << 4); /* D register index (0..31) */
        uint32_t itype = (insn2 >> 8) & 0xFu;
        uint32_t rm = insn2 & 0xFu;
        uint32_t base = cpu->r[rn];
        uint32_t eff = base;

        /* Check common VLD1 / VST1 multi-register (itype 7 = 1 reg, 8 = 4 regs, 0xA = 2 regs, 0x6 = 3 regs, etc.) */
        int num_dregs = 1;
        if (itype == 0x2 || itype == 0xA) num_dregs = 2;
        else if (itype == 0x6) num_dregs = 3;
        else if (itype == 0x0 || itype == 0x8) num_dregs = 4;

        if (load) {
            for (int i = 0; i < num_dregs; i++) {
                uint32_t d = (rd + i) & 31u;
                cpu->v[d * 2 + 0] = *(const uint32_t *)(uintptr_t)(eff + 0);
                cpu->v[d * 2 + 1] = *(const uint32_t *)(uintptr_t)(eff + 4);
                eff += 8;
            }
        } else {
            for (int i = 0; i < num_dregs; i++) {
                uint32_t d = (rd + i) & 31u;
                *(uint32_t *)(uintptr_t)(eff + 0) = cpu->v[d * 2 + 0];
                *(uint32_t *)(uintptr_t)(eff + 4) = cpu->v[d * 2 + 1];
                eff += 8;
            }
        }

        /* Writeback */
        if (rm == 0xD) {
            cpu->r[rn] = base + num_dregs * 8;
        } else if (rm != 0xF) {
            cpu->r[rn] = base + cpu->r[rm];
        }
        return 1;
    }

    /* 2. VLDR / VSTR / VLDM / VSTM / VPUSH / VPOP (EC80..ECBF / ED80..EDBF / ED2D / ECBD) */
    if ((insn & 0xFE00u) == 0xEC00u) {
        int l = (insn >> 4) & 1; /* 1 = load, 0 = store */
        uint32_t rn = insn & 0xFu;
        uint32_t cp = (insn2 >> 8) & 0xFu;

        if (cp == 10 || cp == 11) {
            /* Coprocessor 10 & 11 are VFP / NEON */
            int is_double = (cp == 11);
            uint32_t imm8 = insn2 & 0xFFu;
            uint32_t d_bit = (insn >> 6) & 1u;
            uint32_t vd = (insn2 >> 12) & 0xFu;
            uint32_t base = (rn == 15) ? ((pc + 4) & ~3u) : cpu->r[rn];

            int p = (insn >> 8) & 1;
            int u = (insn >> 7) & 1;
            int w = (insn >> 5) & 1;

            if (p && !w) {
                /* VLDR / VSTR: Single register transfer with #imm8 * 4 offset */
                uint32_t offset = imm8 * 4u;
                uint32_t addr = u ? (base + offset) : (base - offset);
                if (is_double) {
                    uint32_t d = (d_bit << 4) | vd;
                    if (l) {
                        cpu->v[d * 2 + 0] = *(const uint32_t *)(uintptr_t)(addr + 0);
                        cpu->v[d * 2 + 1] = *(const uint32_t *)(uintptr_t)(addr + 4);
                    } else {
                        *(uint32_t *)(uintptr_t)(addr + 0) = cpu->v[d * 2 + 0];
                        *(uint32_t *)(uintptr_t)(addr + 4) = cpu->v[d * 2 + 1];
                    }
                } else {
                    uint32_t s = (vd << 1) | d_bit;
                    if (l) {
                        cpu->v[s] = *(const uint32_t *)(uintptr_t)addr;
                    } else {
                        *(uint32_t *)(uintptr_t)addr = cpu->v[s];
                    }
                }
                return 1;
            }

            /* VLDM / VSTM / VPUSH / VPOP: Multi-register transfer */
            if (is_double) {
                uint32_t start_d = (d_bit << 4) | vd;
                uint32_t count = imm8 / 2;
                if (count == 0) count = 1;
                uint32_t bytes = count * 8;
                uint32_t addr = (p && !u) ? (base - bytes) : base;

                for (uint32_t i = 0; i < count; i++) {
                    uint32_t d = (start_d + i) & 31u;
                    if (l) {
                        cpu->v[d * 2 + 0] = *(const uint32_t *)(uintptr_t)(addr + i * 8 + 0);
                        cpu->v[d * 2 + 1] = *(const uint32_t *)(uintptr_t)(addr + i * 8 + 4);
                    } else {
                        *(uint32_t *)(uintptr_t)(addr + i * 8 + 0) = cpu->v[d * 2 + 0];
                        *(uint32_t *)(uintptr_t)(addr + i * 8 + 4) = cpu->v[d * 2 + 1];
                    }
                }
                if (w) cpu->r[rn] = u ? (base + bytes) : (base - bytes);
                return 1;
            } else {
                uint32_t start_s = (vd << 1) | d_bit;
                uint32_t count = imm8;
                if (count == 0) count = 1;
                uint32_t bytes = count * 4;
                uint32_t addr = (p && !u) ? (base - bytes) : base;

                for (uint32_t i = 0; i < count; i++) {
                    uint32_t s = (start_s + i) & 63u;
                    if (l) {
                        cpu->v[s] = *(const uint32_t *)(uintptr_t)(addr + i * 4);
                    } else {
                        *(uint32_t *)(uintptr_t)(addr + i * 4) = cpu->v[s];
                    }
                }
                if (w) cpu->r[rn] = u ? (base + bytes) : (base - bytes);
                return 1;
            }
        }
    }


    /* 3. VMOV between core register and single-precision register (EE00..EE10), or VMRS (EEF1) */
    if (((insn & 0xFFE0u) == 0xEE00u || insn == 0xEEF1u) && ((insn2 & 0x0F7Fu) == 0x0A10u)) {
        uint32_t cp = (insn2 >> 8) & 0xFu;
        if (cp == 10) {
            int to_arm = (insn >> 4) & 1;
            uint32_t vn = insn & 0xFu;
            uint32_t rt = (insn2 >> 12) & 0xFu;
            uint32_t n_bit = (insn2 >> 7) & 1u;
            uint32_t s = ((vn << 1) | n_bit) & 63u;
            if (to_arm) {
                if (rt == 15) {
                    /* VMRS APSR_nzcv, FPSCR */
                    cpu->cpsr = (cpu->cpsr & ~0xF0000000u) | (cpu->fpscr & 0xF0000000u);
                } else {
                    cpu->r[rt] = cpu->v[s];
                }
            } else {
                cpu->v[s] = cpu->r[rt];
            }
            return 1;
        }
    }

    /* 4. VMOV between two core registers and double-precision register / two S-registers (EC40..EC50) */
    if ((insn & 0xFFE0u) == 0xEC40u) {
        uint32_t cp = (insn2 >> 8) & 0xFu;
        if (cp == 11) {
            int to_arm = (insn >> 4) & 1;
            uint32_t rt = (insn2 >> 12) & 0xFu;
            uint32_t rt2 = (insn2 >> 8) & 0xFu;
            uint32_t vm = insn2 & 0xFu;
            uint32_t m_bit = (insn >> 5) & 1u;
            uint32_t d = ((m_bit << 4) | vm) & 31u;
            if (to_arm) {
                cpu->r[rt]  = cpu->v[d * 2 + 0];
                cpu->r[rt2] = cpu->v[d * 2 + 1];
            } else {
                cpu->v[d * 2 + 0] = cpu->r[rt];
                cpu->v[d * 2 + 1] = cpu->r[rt2];
            }
            return 1;
        }
    }

    /* 5. VFP Arithmetic Operations: VADD, VSUB, VMUL, VDIV, VCMP, VCVT, VMLA, VMLS (EE00..EEFF / EF00..EFFF) */
    if ((insn & 0xEF00u) == 0xEE00u) {
        uint32_t cp = (insn2 >> 8) & 0xFu;
        if (cp == 10 || cp == 11) {
            int is_double = (cp == 11);
            uint32_t d_bit = (insn >> 6) & 1u;
            uint32_t vd = (insn2 >> 12) & 0xFu;
            uint32_t vn = insn & 0xFu;
            uint32_t vm = insn2 & 0xFu;
            uint32_t n_bit = (insn2 >> 7) & 1u;
            uint32_t m_bit = (insn2 >> 5) & 1u;
            uint32_t op = (insn >> 4) & 0xFu;

            /* Check VCVT (between float and integer / double) */
            if ((insn & 0x0FB0u) == 0x0EB0u) {
                uint32_t opc2 = insn & 0xFu;
                if (!is_double) {
                    uint32_t sd = ((vd << 1) | d_bit) & 63u;
                    uint32_t sm = ((vm << 1) | m_bit) & 63u;
                    if (opc2 == 8) {
                        /* VCVT.F32.U32 */
                        float fval = (float)cpu->v[sm];
                        memcpy(&cpu->v[sd], &fval, 4);
                    } else if (opc2 == 9) {
                        /* VCVT.F32.S32 */
                        float fval = (float)(int32_t)cpu->v[sm];
                        memcpy(&cpu->v[sd], &fval, 4);
                    } else if (opc2 == 12 || opc2 == 14) {
                        /* VCVT.U32.F32 */
                        float fval; memcpy(&fval, &cpu->v[sm], 4);
                        if (isnan(fval) || fval <= 0.0f) cpu->v[sd] = 0;
                        else if (fval >= 4294967295.0f) cpu->v[sd] = 0xFFFFFFFFu;
                        else cpu->v[sd] = (uint32_t)fval;
                    } else if (opc2 == 13 || opc2 == 15) {
                        /* VCVT.S32.F32 */
                        float fval; memcpy(&fval, &cpu->v[sm], 4);
                        if (isnan(fval)) cpu->v[sd] = 0;
                        else if (fval <= -2147483648.0f) cpu->v[sd] = 0x80000000u;
                        else if (fval >= 2147483647.0f) cpu->v[sd] = 0x7FFFFFFFu;
                        else cpu->v[sd] = (uint32_t)(int32_t)fval;
                    } else if (opc2 == 7) {
                        /* VCVT.F64.F32 */
                        float fval; memcpy(&fval, &cpu->v[sm], 4);
                        double dval = (double)fval;
                        uint32_t dd = ((d_bit << 4) | vd) & 31u;
                        memcpy(&cpu->v[dd * 2], &dval, 8);
                    }
                    return 1;
                } else {
                    uint32_t dd = ((d_bit << 4) | vd) & 31u;
                    uint32_t dm = ((m_bit << 4) | vm) & 31u;
                    double dval; memcpy(&dval, &cpu->v[dm * 2], 8);
                    if (opc2 == 8) {
                        /* VCVT.F64.U32 */
                        uint32_t sm = ((vm << 1) | m_bit) & 63u;
                        double dv = (double)cpu->v[sm];
                        memcpy(&cpu->v[dd * 2], &dv, 8);
                    } else if (opc2 == 9) {
                        /* VCVT.F64.S32 */
                        uint32_t sm = ((vm << 1) | m_bit) & 63u;
                        double dv = (double)(int32_t)cpu->v[sm];
                        memcpy(&cpu->v[dd * 2], &dv, 8);
                    } else if (opc2 == 12 || opc2 == 14) {
                        /* VCVT.U32.F64 */
                        uint32_t sd = ((vd << 1) | d_bit) & 63u;
                        if (isnan(dval) || dval <= 0.0) cpu->v[sd] = 0;
                        else if (dval >= 4294967295.0) cpu->v[sd] = 0xFFFFFFFFu;
                        else cpu->v[sd] = (uint32_t)dval;
                    } else if (opc2 == 13 || opc2 == 15) {
                        /* VCVT.S32.F64 */
                        uint32_t sd = ((vd << 1) | d_bit) & 63u;
                        if (isnan(dval)) cpu->v[sd] = 0;
                        else if (dval <= -2147483648.0) cpu->v[sd] = 0x80000000u;
                        else if (dval >= 2147483647.0) cpu->v[sd] = 0x7FFFFFFFu;
                        else cpu->v[sd] = (uint32_t)(int32_t)dval;
                    } else if (opc2 == 7) {
                        /* VCVT.F32.F64 */
                        float fv = (float)dval;
                        uint32_t sd = ((vd << 1) | d_bit) & 63u;
                        memcpy(&cpu->v[sd], &fv, 4);
                    }
                    return 1;
                }
            }

            if (!is_double) {
                /* Single Precision Float Math */
                uint32_t sd = ((vd << 1) | d_bit) & 63u;
                uint32_t sn = ((vn << 1) | n_bit) & 63u;
                uint32_t sm = ((vm << 1) | m_bit) & 63u;
                float fn, fm, fd;
                memcpy(&fn, &cpu->v[sn], 4);
                memcpy(&fm, &cpu->v[sm], 4);
                memcpy(&fd, &cpu->v[sd], 4);

                if ((insn & 0x0B00u) == 0x0B00u && (insn2 & 0x0040u) == 0x0040u) {
                    /* Check VCMP (insn2 has opc2 == 0b00100) */
                    if ((insn2 & 0x00E0u) == 0x0040u) {
                        /* VCMP / VCMPE */
                        cpu->fpscr &= ~0xF0000000u;
                        if (fn == fm) cpu->fpscr |= (1u << 30) | (1u << 29); /* Z=1, C=1 */
                        else if (fn < fm) cpu->fpscr |= (1u << 31);          /* N=1 */
                        else cpu->fpscr |= (1u << 29);                       /* C=1 */
                        return 1;
                    }
                }

                if (op == 0x0)      fd = fd + (fn * fm); /* VMLA */
                else if (op == 0x1) fd = fd - (fn * fm); /* VMLS */
                else if (op == 0x2) fd = fn * fm;        /* VMUL */
                else if (op == 0x3) fd = fn + fm;        /* VADD */
                else if (op == 0x4) fd = -(fn * fm);     /* VNMUL */
                else if (op == 0x7) fd = fn - fm;        /* VSUB */
                else if (op == 0x8) fd = fn / fm;        /* VDIV */
                else fd = fm;

                memcpy(&cpu->v[sd], &fd, 4);
                return 1;
            } else {
                /* Double Precision Float Math */
                uint32_t dd = ((d_bit << 4) | vd) & 31u;
                uint32_t dn = ((n_bit << 4) | vn) & 31u;
                uint32_t dm = ((m_bit << 4) | vm) & 31u;
                double fn, fm, fd;
                memcpy(&fn, &cpu->v[dn * 2], 8);
                memcpy(&fm, &cpu->v[dm * 2], 8);
                memcpy(&fd, &cpu->v[dd * 2], 8);

                if (op == 0x0)      fd = fd + (fn * fm); /* VMLA */
                else if (op == 0x1) fd = fd - (fn * fm); /* VMLS */
                else if (op == 0x2) fd = fn * fm;        /* VMUL */
                else if (op == 0x3) fd = fn + fm;        /* VADD */
                else if (op == 0x4) fd = -(fn * fm);     /* VNMUL */
                else if (op == 0x7) fd = fn - fm;        /* VSUB */
                else if (op == 0x8) fd = fn / fm;        /* VDIV */
                else fd = fm;

                memcpy(&cpu->v[dd * 2], &fd, 8);
                return 1;
            }
        }
    }

    /* 6. NEON Data Processing (EF00..EFFF / FF00..FFFF) */
    if ((insn & 0xEF00u) == 0xEF00u) {
        uint32_t q = (insn2 >> 6) & 1u;
        uint32_t d_bit = (insn >> 6) & 1u;
        uint32_t vd = (insn2 >> 12) & 0xFu;
        uint32_t rd = ((d_bit << 4) | vd) & 31u;

        /* If VMOV.I32 or VMOV.I8 / VORR.I / VMVN.I immediate */
        if ((insn & 0xEF80u) == 0xEF80u && (insn2 & 0x0090u) == 0x0010u) {
            uint32_t i_bit = (insn >> 12) & 1u;
            uint32_t abc = insn & 7u;
            uint32_t imm4 = insn2 & 0xFu;
            uint32_t imm8 = (i_bit << 7) | (abc << 4) | imm4;
            uint32_t cmode = (insn2 >> 8) & 0xFu;
            int op = (insn2 >> 5) & 1;
            uint32_t val32 = imm8;

            switch (cmode >> 1) {
                case 0: val32 = imm8; break;
                case 1: val32 = imm8 << 8; break;
                case 2: val32 = imm8 << 16; break;
                case 3: val32 = imm8 << 24; break;
                case 4: val32 = (imm8 << 16) | imm8; break;
                case 5: val32 = (imm8 << 24) | (imm8 << 8); break;
                case 7:
                    if ((cmode & 1) == 0) {
                        val32 = (imm8 << 24) | (imm8 << 16) | (imm8 << 8) | imm8;
                    } else {
                        val32 = (imm8 & 0x80u ? 0x80000000u : 0u) |
                                ((imm8 & 0x40u) ? 0x3F000000u : 0x40000000u) |
                                ((imm8 & 0x3Fu) << 19);
                    }
                    break;
                default: val32 = imm8; break;
            }

            if (op) {
                if ((cmode & 1) && (cmode != 0xF)) {
                    val32 = ~val32;
                } else {
                    cpu->v[rd * 2 + 0] |= val32;
                    cpu->v[rd * 2 + 1] |= val32;
                    if (q) {
                        cpu->v[(rd + 1) * 2 + 0] |= val32;
                        cpu->v[(rd + 1) * 2 + 1] |= val32;
                    }
                    return 1;
                }
            }

            cpu->v[rd * 2 + 0] = val32;
            cpu->v[rd * 2 + 1] = val32;
            if (q) {
                cpu->v[(rd + 1) * 2 + 0] = val32;
                cpu->v[(rd + 1) * 2 + 1] = val32;
            }
            return 1;
        }

        /* Generic NEON bitwise ORR / AND / EOR / BIC */
        uint32_t vn = insn & 0xFu;
        uint32_t vm = insn2 & 0xFu;
        uint32_t n_bit = (insn >> 7) & 1u;
        uint32_t m_bit = (insn2 >> 5) & 1u;
        uint32_t rn = ((n_bit << 4) | vn) & 31u;
        uint32_t rm = ((m_bit << 4) | vm) & 31u;
        uint32_t op = (insn >> 4) & 0xFu;

        if (op == 0x1) {
            /* VORR */
            cpu->v[rd * 2 + 0] = cpu->v[rn * 2 + 0] | cpu->v[rm * 2 + 0];
            cpu->v[rd * 2 + 1] = cpu->v[rn * 2 + 1] | cpu->v[rm * 2 + 1];
            if (q) {
                cpu->v[(rd + 1) * 2 + 0] = cpu->v[(rn + 1) * 2 + 0] | cpu->v[(rm + 1) * 2 + 0];
                cpu->v[(rd + 1) * 2 + 1] = cpu->v[(rn + 1) * 2 + 1] | cpu->v[(rm + 1) * 2 + 1];
            }
            return 1;
        } else if (op == 0x0) {
            /* VAND */
            cpu->v[rd * 2 + 0] = cpu->v[rn * 2 + 0] & cpu->v[rm * 2 + 0];
            cpu->v[rd * 2 + 1] = cpu->v[rn * 2 + 1] & cpu->v[rm * 2 + 1];
            if (q) {
                cpu->v[(rd + 1) * 2 + 0] = cpu->v[(rn + 1) * 2 + 0] & cpu->v[(rm + 1) * 2 + 0];
                cpu->v[(rd + 1) * 2 + 1] = cpu->v[(rn + 1) * 2 + 1] & cpu->v[(rm + 1) * 2 + 1];
            }
            return 1;
        } else if (op == 0x8) {
            /* VEOR */
            cpu->v[rd * 2 + 0] = cpu->v[rn * 2 + 0] ^ cpu->v[rm * 2 + 0];
            cpu->v[rd * 2 + 1] = cpu->v[rn * 2 + 1] ^ cpu->v[rm * 2 + 1];
            if (q) {
                cpu->v[(rd + 1) * 2 + 0] = cpu->v[(rn + 1) * 2 + 0] ^ cpu->v[(rm + 1) * 2 + 0];
                cpu->v[(rd + 1) * 2 + 1] = cpu->v[(rn + 1) * 2 + 1] ^ cpu->v[(rm + 1) * 2 + 1];
            }
            return 1;
        } else if (op == 0x2) {
            /* VBIC */
            cpu->v[rd * 2 + 0] = cpu->v[rn * 2 + 0] & ~cpu->v[rm * 2 + 0];
            cpu->v[rd * 2 + 1] = cpu->v[rn * 2 + 1] & ~cpu->v[rm * 2 + 1];
            if (q) {
                cpu->v[(rd + 1) * 2 + 0] = cpu->v[(rn + 1) * 2 + 0] & ~cpu->v[(rm + 1) * 2 + 0];
                cpu->v[(rd + 1) * 2 + 1] = cpu->v[(rn + 1) * 2 + 1] & ~cpu->v[(rm + 1) * 2 + 1];
            }
            return 1;
        }
    }

    return 0;
}

/* Execute 32-bit ARM-mode VFP/NEON instructions (coprocessor 10 & 11)
 * Returns 1 if handled, 0 if unhandled */
static inline int arm32_exec_arm_vfp(arm32_cpu_t *cpu, uint32_t pc, uint32_t insn) {
    uint32_t cp = (insn >> 8) & 0xFu;
    if (cp != 10 && cp != 11) return 0;

    int is_double = (cp == 11);
    uint32_t rn = (insn >> 16) & 0xFu;
    uint32_t vd = (insn >> 12) & 0xFu;
    uint32_t d_bit = (insn >> 22) & 1u;

    /* 1. VLDR / VSTR / VLDM / VSTM / VPUSH / VPOP (0x0C000000..0x0DFFFFFF) */
    if ((insn & 0x0E000000u) == 0x0C000000u) {
        int p = (insn >> 24) & 1;
        int u = (insn >> 23) & 1;
        int w = (insn >> 21) & 1;
        int l = (insn >> 20) & 1;
        uint32_t imm8 = insn & 0xFFu;
        uint32_t base = (rn == 15) ? ((pc + 8) & ~3u) : cpu->r[rn];

        if (p && !w) {
            /* VLDR / VSTR: Single register transfer with #imm8 * 4 offset */
            uint32_t offset = imm8 * 4u;
            uint32_t addr = u ? (base + offset) : (base - offset);
            if (is_double) {
                uint32_t d = (d_bit << 4) | vd;
                if (l) {
                    cpu->v[d * 2 + 0] = *(const uint32_t *)(uintptr_t)(addr + 0);
                    cpu->v[d * 2 + 1] = *(const uint32_t *)(uintptr_t)(addr + 4);
                } else {
                    *(uint32_t *)(uintptr_t)(addr + 0) = cpu->v[d * 2 + 0];
                    *(uint32_t *)(uintptr_t)(addr + 4) = cpu->v[d * 2 + 1];
                }
            } else {
                uint32_t s = (vd << 1) | d_bit;
                if (l) {
                    cpu->v[s] = *(const uint32_t *)(uintptr_t)addr;
                } else {
                    *(uint32_t *)(uintptr_t)addr = cpu->v[s];
                }
            }
            return 1;
        }

        /* VLDM / VSTM / VPUSH / VPOP: Multi-register transfer */
        if (is_double) {
            uint32_t start_d = (d_bit << 4) | vd;
            uint32_t count = imm8 / 2;
            if (count == 0) count = 1;
            uint32_t bytes = count * 8;
            uint32_t addr = (p && !u) ? (base - bytes) : base;

            for (uint32_t i = 0; i < count; i++) {
                uint32_t d = (start_d + i) & 31u;
                if (l) {
                    cpu->v[d * 2 + 0] = *(const uint32_t *)(uintptr_t)(addr + i * 8 + 0);
                    cpu->v[d * 2 + 1] = *(const uint32_t *)(uintptr_t)(addr + i * 8 + 4);
                } else {
                    *(uint32_t *)(uintptr_t)(addr + i * 8 + 0) = cpu->v[d * 2 + 0];
                    *(uint32_t *)(uintptr_t)(addr + i * 8 + 4) = cpu->v[d * 2 + 1];
                }
            }
            if (w) cpu->r[rn] = u ? (base + bytes) : (base - bytes);
            return 1;
        } else {
            uint32_t start_s = (vd << 1) | d_bit;
            uint32_t count = imm8;
            if (count == 0) count = 1;
            uint32_t bytes = count * 4;
            uint32_t addr = (p && !u) ? (base - bytes) : base;

            for (uint32_t i = 0; i < count; i++) {
                uint32_t s = (start_s + i) & 63u;
                if (l) {
                    cpu->v[s] = *(const uint32_t *)(uintptr_t)(addr + i * 4);
                } else {
                    *(uint32_t *)(uintptr_t)(addr + i * 4) = cpu->v[s];
                }
            }
            if (w) cpu->r[rn] = u ? (base + bytes) : (base - bytes);
            return 1;
        }
    }

    /* 2. VMOV core <-> VFP (0x0E000010..0x0E100010) */
    if ((insn & 0x0F000010u) == 0x0E000010u) {
        int to_arm = (insn >> 20) & 1;
        uint32_t rt = (insn >> 12) & 0xFu;
        uint32_t n_bit = (insn >> 7) & 1u;
        uint32_t s = ((rn << 1) | n_bit) & 63u;
        if (to_arm) {
            if (rt == 15) {
                cpu->cpsr = (cpu->cpsr & ~0xF0000000u) | (cpu->fpscr & 0xF0000000u);
            } else {
                cpu->r[rt] = cpu->v[s];
            }
        } else {
            cpu->v[s] = cpu->r[rt];
        }
        return 1;
    }

    /* 3. VFP Float Math (0x0E000000..0x0EFFFFFF) */
    if ((insn & 0x0F000000u) == 0x0E000000u) {
        uint32_t vm = insn & 0xFu;
        uint32_t n_bit = (insn >> 7) & 1u;
        uint32_t m_bit = (insn >> 5) & 1u;
        uint32_t op = (insn >> 20) & 0xFu;

        if (!is_double) {
            uint32_t sd = ((vd << 1) | d_bit) & 63u;
            uint32_t sn = ((rn << 1) | n_bit) & 63u;
            uint32_t sm = ((vm << 1) | m_bit) & 63u;
            float fn, fm, fd;
            memcpy(&fn, &cpu->v[sn], 4);
            memcpy(&fm, &cpu->v[sm], 4);
            memcpy(&fd, &cpu->v[sd], 4);

            if (op == 0x0)      fd = fd + (fn * fm);
            else if (op == 0x1) fd = fd - (fn * fm);
            else if (op == 0x2) fd = fn * fm;
            else if (op == 0x3) fd = fn + fm;
            else if (op == 0x4) fd = -(fn * fm);
            else if (op == 0x7) fd = fn - fm;
            else if (op == 0x8) fd = fn / fm;
            else fd = fm;

            memcpy(&cpu->v[sd], &fd, 4);
            return 1;
        } else {
            uint32_t dd = ((d_bit << 4) | vd) & 31u;
            uint32_t dn = ((n_bit << 4) | rn) & 31u;
            uint32_t dm = ((m_bit << 4) | vm) & 31u;
            double fn, fm, fd;
            memcpy(&fn, &cpu->v[dn * 2], 8);
            memcpy(&fm, &cpu->v[dm * 2], 8);
            memcpy(&fd, &cpu->v[dd * 2], 8);

            if (op == 0x0)      fd = fd + (fn * fm);
            else if (op == 0x1) fd = fd - (fn * fm);
            else if (op == 0x2) fd = fn * fm;
            else if (op == 0x3) fd = fn + fm;
            else if (op == 0x4) fd = -(fn * fm);
            else if (op == 0x7) fd = fn - fm;
            else if (op == 0x8) fd = fn / fm;
            else fd = fm;

            memcpy(&cpu->v[dd * 2], &fd, 8);
            return 1;
        }
    }

    return 0;
}

#endif /* ARM32_VFP_H */
