/* cpu.c - ARMv7-M integer Thumb-2 interpreter (Cortex-M4 without FPU).
 * Written from the ARMv7-M Architecture Reference Manual (DDI 0403).
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 José González Divassón
 */
#include "cpu.h"
#include <string.h>
#include <stdint.h>
#include <stdio.h>

#define SRAM_BASE 0x20000000u

void cpu_init(vc_cpu *cpu) {
    uint8_t *f = cpu->flash, *s = cpu->sram; uint32_t fs = cpu->flash_size, ss = cpu->sram_size;
    vc_bus b = cpu->bus;
    memset(cpu, 0, sizeof *cpu);
    cpu->flash = f; cpu->sram = s; cpu->flash_size = fs; cpu->sram_size = ss; cpu->bus = b;
    cpu->hook_lo = 0xFFFFFFFFu; cpu->hook_hi = 0;
}

void cpu_add_hook(vc_cpu *cpu, uint32_t addr, vc_hook_fn fn, void *user) {
    if (cpu->nhooks >= VC_MAX_HOOKS) return;
    cpu->hook_addr[cpu->nhooks] = addr; cpu->hook_fn[cpu->nhooks] = fn; cpu->hook_user[cpu->nhooks] = user;
    cpu->nhooks++;
    if (addr < cpu->hook_lo) cpu->hook_lo = addr;
    if (addr > cpu->hook_hi) cpu->hook_hi = addr;
}

/* ------------------------------------------------------------------ memory */
#define bf (cpu->bf_)  /* fault seen during the current instruction */

static inline uint32_t ld_le(const uint8_t *p, int size) {
    switch (size) {
    case 1: return p[0];
    case 2: return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
    default: return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    }
}
static inline void st_le(uint8_t *p, int size, uint32_t v) {
    p[0] = (uint8_t)v;
    if (size > 1) p[1] = (uint8_t)(v >> 8);
    if (size > 2) { p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
}

int cpu_read(vc_cpu *cpu, uint32_t a, int size, uint32_t *v) {
    if (a < cpu->flash_size && a + (uint32_t)size <= cpu->flash_size) { *v = ld_le(cpu->flash + a, size); return 0; }
    uint32_t o = a - SRAM_BASE;
    if (o < cpu->sram_size && o + (uint32_t)size <= cpu->sram_size) { *v = ld_le(cpu->sram + o, size); return 0; }
    if (cpu->bus.read) return cpu->bus.read(cpu->bus.ctx, a, size, v);
    return 1;
}
int cpu_write(vc_cpu *cpu, uint32_t a, int size, uint32_t v) {
    if (a < cpu->flash_size && a + (uint32_t)size <= cpu->flash_size) { st_le(cpu->flash + a, size, v); return 0; }
    uint32_t o = a - SRAM_BASE;
    if (o < cpu->sram_size && o + (uint32_t)size <= cpu->sram_size) { st_le(cpu->sram + o, size, v); return 0; }
    if (cpu->bus.write) return cpu->bus.write(cpu->bus.ctx, a, size, v);
    return 1;
}

static inline uint32_t RD(vc_cpu *cpu, uint32_t a, int size) {
    uint32_t v = 0;
    if (cpu_read(cpu, a, size, &v)) {
        if (!bf) snprintf(cpu->fault, sizeof cpu->fault, "bus fault: read%d at 0x%08x (pc 0x%08x)", size * 8, a, cpu->r[15]);
        bf = 1;
    }
    return v;
}
static inline void WR(vc_cpu *cpu, uint32_t a, int size, uint32_t v) {
    if (cpu_write(cpu, a, size, v)) {
        if (!bf) snprintf(cpu->fault, sizeof cpu->fault, "bus fault: write%d at 0x%08x (pc 0x%08x)", size * 8, a, cpu->r[15]);
        bf = 1;
    }
}

/* ------------------------------------------------------------------ ALU helpers */
static inline uint32_t add_c(uint32_t a, uint32_t b, uint32_t cin, uint32_t *c, uint32_t *v) {
    uint64_t u = (uint64_t)a + b + cin;
    uint32_t r = (uint32_t)u;
    *c = (uint32_t)(u >> 32);
    *v = ((~(a ^ b) & (a ^ r)) >> 31) & 1;
    return r;
}

/* Shift_C: type 0 LSL, 1 LSR, 2 ASR, 3 ROR, 4 RRX */
static inline uint32_t shift_c(uint32_t x, int type, uint32_t n, uint32_t cin, uint32_t *cout) {
    if (n == 0 && type != 4) { *cout = cin; return x; }
    switch (type) {
    case 0:
        if (n < 32) { *cout = (x >> (32 - n)) & 1; return x << n; }
        *cout = (n == 32) ? (x & 1) : 0; return 0;
    case 1:
        if (n < 32) { *cout = (x >> (n - 1)) & 1; return x >> n; }
        *cout = (n == 32) ? (x >> 31) : 0; return 0;
    case 2:
        if (n < 32) { *cout = (x >> (n - 1)) & 1; return (uint32_t)((int32_t)x >> n); }
        *cout = x >> 31; return (x >> 31) ? 0xFFFFFFFFu : 0;
    case 3: {
        uint32_t m = n & 31;
        uint32_t r = m ? ((x >> m) | (x << (32 - m))) : x;
        *cout = r >> 31; return r;
    }
    default: /* RRX */
        *cout = x & 1; return (x >> 1) | (cin << 31);
    }
}

/* DecodeImmShift */
static inline void decode_imm_shift(int type, uint32_t imm5, int *t, uint32_t *n) {
    switch (type) {
    case 0: *t = 0; *n = imm5; break;
    case 1: *t = 1; *n = imm5 ? imm5 : 32; break;
    case 2: *t = 2; *n = imm5 ? imm5 : 32; break;
    default: if (imm5 == 0) { *t = 4; *n = 1; } else { *t = 3; *n = imm5; } break;
    }
}

static inline uint32_t thumb_expand_imm_c(uint32_t imm12, uint32_t cin, uint32_t *cout) {
    uint32_t imm8 = imm12 & 0xFF;
    if ((imm12 >> 10) == 0) {
        *cout = cin;
        switch ((imm12 >> 8) & 3) {
        case 0: return imm8;
        case 1: return imm8 | (imm8 << 16);
        case 2: return (imm8 << 8) | (imm8 << 24);
        default: return imm8 | (imm8 << 8) | (imm8 << 16) | (imm8 << 24);
        }
    } else {
        uint32_t un = 0x80 | (imm12 & 0x7F);
        uint32_t rot = imm12 >> 7;
        uint32_t r = (un >> rot) | (un << (32 - rot));
        *cout = r >> 31;
        return r;
    }
}

static inline int cond_pass(vc_cpu *c, uint32_t cond) {
    int r;
    switch (cond >> 1) {
    case 0: r = c->z; break;
    case 1: r = c->c; break;
    case 2: r = c->n; break;
    case 3: r = c->v; break;
    case 4: r = c->c && !c->z; break;
    case 5: r = c->n == c->v; break;
    case 6: r = !c->z && (c->n == c->v); break;
    default: r = 1; break;
    }
    if ((cond & 1) && cond != 15) r = !r;
    return r;
}

static inline void set_nz(vc_cpu *c, uint32_t r) { c->n = r >> 31; c->z = (r == 0); }

static inline uint32_t sat_s(int64_t v, int bits, int *sat) {
    int64_t mx = ((int64_t)1 << (bits - 1)) - 1, mn = -((int64_t)1 << (bits - 1));
    if (v > mx) { *sat = 1; return (uint32_t)mx; }
    if (v < mn) { *sat = 1; return (uint32_t)mn; }
    *sat = 0; return (uint32_t)v;
}
static inline uint32_t sat_u(int64_t v, int bits, int *sat) {
    int64_t mx = ((int64_t)1 << bits) - 1;
    if (v > mx) { *sat = 1; return (uint32_t)mx; }
    if (v < 0) { *sat = 1; return 0; }
    *sat = 0; return (uint32_t)v;
}

/* ------------------------------------------------------------------ special registers */
static uint32_t xpsr(vc_cpu *c) {
    return (c->n << 31) | (c->z << 30) | (c->c << 29) | (c->v << 28) | (c->q << 27) | (1u << 24)
         | ((uint32_t)(c->it & 3) << 25) | ((uint32_t)(c->it >> 2) << 10);
}
static uint32_t mrs(vc_cpu *c, uint32_t sysm) {
    switch (sysm) {
    case 0: case 1: case 2: case 3: return xpsr(c) & (sysm & 2 ? 0xF8000000u : 0xF8000000u); /* APSR flags; IPSR=0 (thread) */
    case 5: case 6: case 7: return 0;
    case 8: return (c->control & 2) ? c->msp : c->r[13];
    case 9: return (c->control & 2) ? c->r[13] : c->psp;
    case 16: return c->primask & 1;
    case 17: case 18: return c->basepri & 0xFF;
    case 19: return c->faultmask & 1;
    case 20: return c->control & 3;
    default: return 0;
    }
}
static void msr(vc_cpu *c, uint32_t sysm, uint32_t mask, uint32_t v) {
    switch (sysm) {
    case 0: case 1: case 2: case 3:
        if (mask & 2) { c->n = v >> 31; c->z = (v >> 30) & 1; c->c = (v >> 29) & 1; c->v = (v >> 28) & 1; c->q = (v >> 27) & 1; }
        break;
    case 8: if (c->control & 2) c->msp = v & ~3u; else c->r[13] = v & ~3u; break;
    case 9: if (c->control & 2) c->r[13] = v & ~3u; else c->psp = v & ~3u; break;
    case 16: c->primask = v & 1; break;
    case 17: c->basepri = v & 0xFF; break;
    case 18: if ((v & 0xFF) && ((v & 0xFF) < c->basepri || c->basepri == 0)) c->basepri = v & 0xFF; break;
    case 19: c->faultmask = v & 1; break;
    case 20: {
        uint32_t nc = v & 3;
        if ((nc & 2) != (c->control & 2)) { /* switch stack pointer */
            if (nc & 2) { c->msp = c->r[13]; c->r[13] = c->psp; }
            else { c->psp = c->r[13]; c->r[13] = c->msp; }
        }
        c->control = nc;
        break;
    }
    default: break;
    }
}

/* ------------------------------------------------------------------ the interpreter */
#define UNDEF() do { snprintf(cpu->fault, sizeof cpu->fault, "undefined/unsupported instruction %04x %04x at 0x%08x", hw1, hw2, pc); bf = 1; goto done; } while (0)

int cpu_run(vc_cpu *cpu, uint64_t budget, uint32_t stop) {
    uint32_t *r = cpu->r;
    bf = 0;
    for (;;) {
        uint32_t pc = r[15];
        if (pc == stop) return CPU_RUN_STOPADDR;
        int in_it = (cpu->it & 0xF) != 0;
        int skip = in_it && !cond_pass(cpu, cpu->it >> 4);
        uint32_t hw1 = RD(cpu, pc, 2), hw2 = 0;
        int wide = (hw1 >> 11) >= 0x1D;
        if (wide) hw2 = RD(cpu, pc + 2, 2);
        if (bf) { return CPU_RUN_FAULT; }

        /* Instruction budget, counted exactly like the Unicorn engine our Python reference
         * uses (so time slices end at the same instruction): an instruction skipped inside
         * an IT block is not counted, and a slice never ends inside an IT block. */
        int counted = !skip;

        if (counted && !budget && !in_it) break;
        if (pc >= cpu->hook_lo && pc <= cpu->hook_hi) {
            int act = 0, i;
            for (i = 0; i < cpu->nhooks; i++) if (cpu->hook_addr[i] == pc) { act = cpu->hook_fn[i](cpu, pc, cpu->hook_user[i]); break; }
            if (act == 1) return CPU_RUN_HOOK;
            if (act == 2) continue;
        }
        if (counted) { if (budget) budget--; cpu->icount++; }
        uint32_t next = pc + (wide ? 4 : 2);
        int is_wfi = 0;
        if (skip) goto advance;
#define R(n) ((n) == 15 ? pc + 4 : r[n])
        if (!wide) {
            /* ---------------------------------------------------- 16-bit */
            uint32_t op6 = hw1 >> 10;
            int sf = !in_it;
            if ((hw1 >> 13) == 0) {
                uint32_t op = (hw1 >> 11) & 3, rd = hw1 & 7, rm = (hw1 >> 3) & 7;
                if (op < 3) { /* LSL/LSR/ASR imm */
                    int t; uint32_t n, co; decode_imm_shift((int)op, (hw1 >> 6) & 31, &t, &n);
                    uint32_t res = shift_c(r[rm], t, n, cpu->c, &co);
                    r[rd] = res; if (sf) { set_nz(cpu, res); cpu->c = co; }
                } else {
                    uint32_t rn = (hw1 >> 3) & 7, x = r[rn], y, cc, vv, res;
                    y = (hw1 & 0x400) ? ((hw1 >> 6) & 7) : r[(hw1 >> 6) & 7];
                    if (hw1 & 0x200) res = add_c(x, ~y, 1, &cc, &vv); else res = add_c(x, y, 0, &cc, &vv);
                    r[rd] = res; if (sf) { set_nz(cpu, res); cpu->c = cc; cpu->v = vv; }
                }
            } else if ((hw1 >> 13) == 1) {
                uint32_t op = (hw1 >> 11) & 3, rd = (hw1 >> 8) & 7, imm = hw1 & 0xFF, cc, vv, res;
                switch (op) {
                case 0: r[rd] = imm; if (sf) set_nz(cpu, imm); break;
                case 1: res = add_c(r[rd], ~imm, 1, &cc, &vv); set_nz(cpu, res); cpu->c = cc; cpu->v = vv; break;
                case 2: res = add_c(r[rd], imm, 0, &cc, &vv); r[rd] = res; if (sf) { set_nz(cpu, res); cpu->c = cc; cpu->v = vv; } break;
                default: res = add_c(r[rd], ~imm, 1, &cc, &vv); r[rd] = res; if (sf) { set_nz(cpu, res); cpu->c = cc; cpu->v = vv; } break;
                }
            } else if (op6 == 0x10) { /* data processing */
                uint32_t op = (hw1 >> 6) & 15, rdn = hw1 & 7, rm = (hw1 >> 3) & 7;
                uint32_t a = r[rdn], b = r[rm], res = 0, cc = cpu->c, vv = cpu->v;
                int wr = 1, arith = 0;
                switch (op) {
                case 0: res = a & b; break;
                case 1: res = a ^ b; break;
                case 2: res = shift_c(a, 0, b & 0xFF, cpu->c, &cc); break;
                case 3: res = shift_c(a, 1, b & 0xFF, cpu->c, &cc); break;
                case 4: res = shift_c(a, 2, b & 0xFF, cpu->c, &cc); break;
                case 5: res = add_c(a, b, cpu->c, &cc, &vv); arith = 1; break;
                case 6: res = add_c(a, ~b, cpu->c, &cc, &vv); arith = 1; break;
                case 7: res = shift_c(a, 3, b & 0xFF, cpu->c, &cc); break;
                case 8: res = a & b; wr = 0; set_nz(cpu, res); cpu->c = cc; break;      /* TST */
                case 9: res = add_c(~b, 0, 1, &cc, &vv); arith = 1; r[rdn] = res; wr = 2; break; /* RSB #0: Rd = 0 - Rm */
                case 10: res = add_c(a, ~b, 1, &cc, &vv); wr = 0; set_nz(cpu, res); cpu->c = cc; cpu->v = vv; break; /* CMP */
                case 11: res = add_c(a, b, 0, &cc, &vv); wr = 0; set_nz(cpu, res); cpu->c = cc; cpu->v = vv; break;  /* CMN */
                case 12: res = a | b; break;
                case 13: res = a * b; break;
                case 14: res = a & ~b; break;
                default: res = ~b; break;
                }
                if (wr == 1) r[rdn] = res;
                if (wr && sf) {
                    set_nz(cpu, res);
                    if (op != 13) cpu->c = cc;
                    if (arith) cpu->v = vv;
                }
            } else if (op6 == 0x11) { /* special data / BX */
                uint32_t op = (hw1 >> 8) & 3;
                uint32_t rd = (hw1 & 7) | ((hw1 >> 4) & 8), rm = (hw1 >> 3) & 15;
                if (op == 0) { /* ADD hi */
                    uint32_t res = R(rd) + R(rm);
                    if (rd == 15) next = res & ~1u; else r[rd] = res;
                } else if (op == 1) { /* CMP hi */
                    uint32_t cc, vv, res = add_c(R(rd), ~R(rm), 1, &cc, &vv);
                    set_nz(cpu, res); cpu->c = cc; cpu->v = vv;
                } else if (op == 2) { /* MOV hi */
                    uint32_t v = R(rm);
                    if (rd == 15) next = v & ~1u; else r[rd] = v;
                } else { /* BX / BLX */
                    uint32_t t = R(rm);
                    if (hw1 & 0x80) r[14] = (pc + 2) | 1;
                    if (!(t & 1)) { snprintf(cpu->fault, sizeof cpu->fault, "BX to ARM state 0x%08x at 0x%08x", t, pc); bf = 1; goto done; }
                    next = t & ~1u;
                }
            } else if ((hw1 >> 11) == 9) { /* LDR literal */
                uint32_t a = ((pc + 4) & ~3u) + ((hw1 & 0xFF) << 2);
                r[(hw1 >> 8) & 7] = RD(cpu, a, 4);
            } else if ((hw1 >> 12) == 5) { /* load/store register offset */
                uint32_t op = (hw1 >> 9) & 7, rt = hw1 & 7, a = r[(hw1 >> 3) & 7] + r[(hw1 >> 6) & 7];
                switch (op) {
                case 0: WR(cpu, a, 4, r[rt]); break;
                case 1: WR(cpu, a, 2, r[rt]); break;
                case 2: WR(cpu, a, 1, r[rt]); break;
                case 3: r[rt] = (uint32_t)(int32_t)(int8_t)RD(cpu, a, 1); break;
                case 4: r[rt] = RD(cpu, a, 4); break;
                case 5: r[rt] = RD(cpu, a, 2); break;
                case 6: r[rt] = RD(cpu, a, 1); break;
                default: r[rt] = (uint32_t)(int32_t)(int16_t)RD(cpu, a, 2); break;
                }
            } else if ((hw1 >> 13) == 3) { /* STR/LDR(B) imm5 */
                uint32_t rt = hw1 & 7, rn = (hw1 >> 3) & 7, imm = (hw1 >> 6) & 31;
                int byte = (hw1 >> 12) & 1, load = (hw1 >> 11) & 1;
                uint32_t a = r[rn] + (byte ? imm : imm << 2);
                if (load) r[rt] = RD(cpu, a, byte ? 1 : 4); else WR(cpu, a, byte ? 1 : 4, r[rt]);
            } else if ((hw1 >> 12) == 8) { /* STRH/LDRH imm5 */
                uint32_t rt = hw1 & 7, rn = (hw1 >> 3) & 7, a = r[rn] + (((hw1 >> 6) & 31) << 1);
                if (hw1 & 0x800) r[rt] = RD(cpu, a, 2); else WR(cpu, a, 2, r[rt]);
            } else if ((hw1 >> 12) == 9) { /* STR/LDR SP-relative */
                uint32_t rt = (hw1 >> 8) & 7, a = r[13] + ((hw1 & 0xFF) << 2);
                if (hw1 & 0x800) r[rt] = RD(cpu, a, 4); else WR(cpu, a, 4, r[rt]);
            } else if ((hw1 >> 12) == 10) { /* ADR / ADD SP */
                uint32_t rd = (hw1 >> 8) & 7, imm = (hw1 & 0xFF) << 2;
                r[rd] = (hw1 & 0x800) ? r[13] + imm : ((pc + 4) & ~3u) + imm;
            } else if ((hw1 >> 12) == 11) { /* misc */
                uint32_t op = (hw1 >> 5) & 0x7F;
                if ((op & 0x7C) == 0x00) { r[13] += (hw1 & 0x7F) << 2; }
                else if ((op & 0x7C) == 0x04) { r[13] -= (hw1 & 0x7F) << 2; }
                else if ((hw1 & 0x0500) == 0x0100) { /* CBZ/CBNZ */
                    uint32_t rn = hw1 & 7, imm = (((hw1 >> 9) & 1) << 6) | (((hw1 >> 3) & 31) << 1);
                    int nz = (hw1 >> 11) & 1;
                    if ((r[rn] != 0) == nz) next = pc + 4 + imm;
                } else if ((hw1 & 0xFF00) == 0xB200) { /* extend */
                    uint32_t rd = hw1 & 7, m = r[(hw1 >> 3) & 7];
                    switch ((hw1 >> 6) & 3) {
                    case 0: r[rd] = (uint32_t)(int32_t)(int16_t)m; break;
                    case 1: r[rd] = (uint32_t)(int32_t)(int8_t)m; break;
                    case 2: r[rd] = m & 0xFFFF; break;
                    default: r[rd] = m & 0xFF; break;
                    }
                } else if ((hw1 & 0xFE00) == 0xB400) { /* PUSH */
                    uint32_t list = (hw1 & 0xFF) | ((hw1 & 0x100) ? 0x4000 : 0);
                    int cnt = __builtin_popcount(list), i;
                    uint32_t a = r[13] - 4u * (uint32_t)cnt;
                    r[13] = a;
                    for (i = 0; i < 15; i++) if (list & (1u << i)) { WR(cpu, a, 4, r[i]); a += 4; }
                } else if ((hw1 & 0xFFE8) == 0xB660) { /* CPS */
                    int dis = (hw1 >> 4) & 1;
                    if (hw1 & 2) cpu->primask = (uint32_t)dis;
                    if (hw1 & 1) cpu->faultmask = (uint32_t)dis;
                } else if ((hw1 & 0xFF00) == 0xBA00) { /* REV */
                    uint32_t rd = hw1 & 7, m = r[(hw1 >> 3) & 7];
                    switch ((hw1 >> 6) & 3) {
                    case 0: r[rd] = __builtin_bswap32(m); break;
                    case 1: r[rd] = ((m & 0x00FF00FFu) << 8) | ((m >> 8) & 0x00FF00FFu); break;
                    case 3: r[rd] = (uint32_t)(int32_t)(int16_t)(((m & 0xFF) << 8) | ((m >> 8) & 0xFF)); break;
                    default: UNDEF();
                    }
                } else if ((hw1 & 0xFE00) == 0xBC00) { /* POP */
                    uint32_t list = (hw1 & 0xFF) | ((hw1 & 0x100) ? 0x8000 : 0);
                    uint32_t a = r[13]; int i;
                    for (i = 0; i < 16; i++) if (list & (1u << i)) {
                        uint32_t v = RD(cpu, a, 4); a += 4;
                        if (i == 15) {
                            if (!(v & 1)) { snprintf(cpu->fault, sizeof cpu->fault, "POP pc to ARM state 0x%08x at 0x%08x", v, pc); bf = 1; goto done; }
                            next = v & ~1u;
                        } else r[i] = v;
                    }
                    r[13] = a;
                } else if ((hw1 & 0xFF00) == 0xBE00) { /* BKPT */
                    snprintf(cpu->fault, sizeof cpu->fault, "BKPT at 0x%08x", pc); bf = 1; goto done;
                } else if ((hw1 & 0xFF00) == 0xBF00) { /* IT / hints */
                    if (hw1 & 0xF) { cpu->it = (uint8_t)(hw1 & 0xFF); r[15] = next; continue; /* IT does not advance */ }
                    if ((hw1 & 0xF0) == 0x30) is_wfi = 1; /* WFI */
                    /* NOP, YIELD, WFE, SEV: nothing */
                } else UNDEF();
            } else if ((hw1 >> 12) == 12) { /* STM / LDM */
                uint32_t rn = (hw1 >> 8) & 7, list = hw1 & 0xFF, a = r[rn]; int i;
                if (hw1 & 0x800) {
                    for (i = 0; i < 8; i++) if (list & (1u << i)) { r[i] = RD(cpu, a, 4); a += 4; }
                    if (!(list & (1u << rn))) r[rn] = a;
                } else {
                    for (i = 0; i < 8; i++) if (list & (1u << i)) { WR(cpu, a, 4, r[i]); a += 4; }
                    r[rn] = a;
                }
            } else if ((hw1 >> 12) == 13) { /* B cond / SVC / UDF */
                uint32_t cond = (hw1 >> 8) & 15;
                if (cond == 15) { snprintf(cpu->fault, sizeof cpu->fault, "SVC at 0x%08x", pc); bf = 1; goto done; }
                if (cond == 14) UNDEF();
                if (cond_pass(cpu, cond)) next = pc + 4 + (uint32_t)((int32_t)(int8_t)(hw1 & 0xFF) * 2);
            } else if ((hw1 >> 11) == 0x1C) { /* B */
                int32_t off = (int32_t)((hw1 & 0x7FF) << 21) >> 20;
                next = pc + 4 + (uint32_t)off;
            } else UNDEF();
        } else {
            /* ---------------------------------------------------- 32-bit */
            uint32_t op1 = (hw1 >> 11) & 3, op2 = (hw1 >> 4) & 0x7F;
            if (op1 == 1) {
                if ((op2 & 0x64) == 0x00) { /* load/store multiple */
                    uint32_t op = (hw1 >> 7) & 3, w = (hw1 >> 5) & 1, l = (hw1 >> 4) & 1, rn = hw1 & 15;
                    uint32_t list = hw2, a; int cnt = __builtin_popcount(list), i;
                    if (op == 1) a = r[rn];
                    else if (op == 2) a = r[rn] - 4u * (uint32_t)cnt;
                    else UNDEF();
                    uint32_t base_new = (op == 1) ? r[rn] + 4u * (uint32_t)cnt : r[rn] - 4u * (uint32_t)cnt;
                    if (l) {
                        uint32_t newpc = 0; int haspc = 0;
                        for (i = 0; i < 16; i++) if (list & (1u << i)) {
                            uint32_t v = RD(cpu, a, 4); a += 4;
                            if (i == 15) { newpc = v; haspc = 1; } else r[i] = v;
                        }
                        if (w && !(list & (1u << rn))) r[rn] = base_new;
                        if (haspc) {
                            if (!(newpc & 1)) { snprintf(cpu->fault, sizeof cpu->fault, "LDM pc to ARM state at 0x%08x", pc); bf = 1; goto done; }
                            next = newpc & ~1u;
                        }
                    } else {
                        for (i = 0; i < 15; i++) if (list & (1u << i)) { WR(cpu, a, 4, r[i]); a += 4; }
                        if (w) r[rn] = base_new;
                    }
                } else if ((op2 & 0x64) == 0x04) { /* dual / exclusive / table branch */
                    uint32_t p = (hw1 >> 8) & 1, u = (hw1 >> 7) & 1, w = (hw1 >> 5) & 1, l = (hw1 >> 4) & 1, rn = hw1 & 15;
                    uint32_t rt = hw2 >> 12, rt2 = (hw2 >> 8) & 15;
                    if (p || w) { /* LDRD/STRD */
                        uint32_t imm = (hw2 & 0xFF) << 2;
                        uint32_t base = (rn == 15) ? ((pc + 4) & ~3u) : r[rn];
                        uint32_t off = u ? base + imm : base - imm;
                        uint32_t a = p ? off : base;
                        if (l) { uint32_t v1 = RD(cpu, a, 4), v2 = RD(cpu, a + 4, 4); r[rt] = v1; r[rt2] = v2; }
                        else { WR(cpu, a, 4, r[rt]); WR(cpu, a + 4, 4, r[rt2]); }
                        if (w) r[rn] = off;
                    } else if (!u) { /* STREX / LDREX */
                        uint32_t a = r[rn] + ((hw2 & 0xFF) << 2);
                        if (l) r[rt] = RD(cpu, a, 4);
                        else { WR(cpu, a, 4, r[rt]); r[rt2] = 0; }
                    } else {
                        uint32_t op3 = (hw2 >> 4) & 15;
                        if (l && op3 == 0) { /* TBB */
                            uint32_t base = R(rn), m = r[hw2 & 15];
                            next = pc + 4 + 2 * RD(cpu, base + m, 1);
                        } else if (l && op3 == 1) { /* TBH */
                            uint32_t base = R(rn), m = r[hw2 & 15];
                            next = pc + 4 + 2 * RD(cpu, base + (m << 1), 2);
                        } else if (l && op3 == 4) r[rt] = RD(cpu, r[rn], 1);
                        else if (l && op3 == 5) r[rt] = RD(cpu, r[rn], 2);
                        else if (!l && op3 == 4) { WR(cpu, r[rn], 1, r[rt]); r[hw2 & 15] = 0; }
                        else if (!l && op3 == 5) { WR(cpu, r[rn], 2, r[rt]); r[hw2 & 15] = 0; }
                        else UNDEF();
                    }
                } else if ((op2 & 0x60) == 0x20) { /* data processing (shifted register) */
                    uint32_t op = (hw1 >> 5) & 15, s = (hw1 >> 4) & 1, rn = hw1 & 15;
                    uint32_t rd = (hw2 >> 8) & 15, rm = hw2 & 15;
                    uint32_t imm5 = ((hw2 >> 10) & 0x1C) | ((hw2 >> 6) & 3);
                    int t; uint32_t n, sc;
                    decode_imm_shift((hw2 >> 4) & 3, imm5, &t, &n);
                    uint32_t b = shift_c(r[rm], t, n, cpu->c, &sc);
                    uint32_t a = R(rn), res = 0, cc = sc, vv = cpu->v;
                    int logical = 1, wr = 1;
                    switch (op) {
                    case 0: res = a & b; if (rd == 15 && s) wr = 0; break;
                    case 1: res = a & ~b; break;
                    case 2: res = (rn == 15) ? b : (a | b); break;
                    case 3: res = (rn == 15) ? ~b : (a | ~b); break;
                    case 4: res = a ^ b; if (rd == 15 && s) wr = 0; break;
                    case 6: /* PKHBT / PKHTB */
                        if (hw2 & 0x20) res = (a & 0xFFFF0000u) | (b & 0xFFFF); else res = (b & 0xFFFF0000u) | (a & 0xFFFF);
                        s = 0; break;
                    case 8: res = add_c(a, b, 0, &cc, &vv); logical = 0; if (rd == 15 && s) wr = 0; break;
                    case 10: res = add_c(a, b, cpu->c, &cc, &vv); logical = 0; break;
                    case 11: res = add_c(a, ~b, cpu->c, &cc, &vv); logical = 0; break;
                    case 13: res = add_c(a, ~b, 1, &cc, &vv); logical = 0; if (rd == 15 && s) wr = 0; break;
                    case 14: res = add_c(b, ~a, 1, &cc, &vv); logical = 0; break;
                    default: UNDEF();
                    }
                    if (wr) { if (rd == 15) UNDEF(); r[rd] = res; }
                    if (s) { set_nz(cpu, res); cpu->c = cc; if (!logical) cpu->v = vv; }
                } else UNDEF(); /* coprocessor */
            } else if (op1 == 2) {
                if (!(hw2 & 0x8000)) {
                    if (!(op2 & 0x20)) { /* data processing modified immediate */
                        uint32_t op = (hw1 >> 5) & 15, s = (hw1 >> 4) & 1, rn = hw1 & 15, rd = (hw2 >> 8) & 15;
                        uint32_t imm12 = ((hw1 >> 10) & 1) << 11 | ((hw2 >> 12) & 7) << 8 | (hw2 & 0xFF);
                        uint32_t ic, b = thumb_expand_imm_c(imm12, cpu->c, &ic);
                        uint32_t a = R(rn), res = 0, cc = ic, vv = cpu->v;
                        int logical = 1, wr = 1;
                        switch (op) {
                        case 0: res = a & b; if (rd == 15 && s) wr = 0; break;
                        case 1: res = a & ~b; break;
                        case 2: res = (rn == 15) ? b : (a | b); break;
                        case 3: res = (rn == 15) ? ~b : (a | ~b); break;
                        case 4: res = a ^ b; if (rd == 15 && s) wr = 0; break;
                        case 8: res = add_c(a, b, 0, &cc, &vv); logical = 0; if (rd == 15 && s) wr = 0; break;
                        case 10: res = add_c(a, b, cpu->c, &cc, &vv); logical = 0; break;
                        case 11: res = add_c(a, ~b, cpu->c, &cc, &vv); logical = 0; break;
                        case 13: res = add_c(a, ~b, 1, &cc, &vv); logical = 0; if (rd == 15 && s) wr = 0; break;
                        case 14: res = add_c(b, ~a, 1, &cc, &vv); logical = 0; break;
                        default: UNDEF();
                        }
                        if (wr) { if (rd == 15) UNDEF(); r[rd] = res; }
                        if (s) { set_nz(cpu, res); cpu->c = cc; if (!logical) cpu->v = vv; }
                    } else { /* plain binary immediate */
                        uint32_t op = (hw1 >> 4) & 31, rn = hw1 & 15, rd = (hw2 >> 8) & 15;
                        uint32_t imm12 = ((hw1 >> 10) & 1) << 11 | ((hw2 >> 12) & 7) << 8 | (hw2 & 0xFF);
                        uint32_t lsb = ((hw2 >> 10) & 0x1C) | ((hw2 >> 6) & 3), w5 = hw2 & 31;
                        switch (op) {
                        case 0: r[rd] = (rn == 15 ? ((pc + 4) & ~3u) : r[rn]) + imm12; break;   /* ADDW / ADR */
                        case 10: r[rd] = (rn == 15 ? ((pc + 4) & ~3u) : r[rn]) - imm12; break;  /* SUBW / ADR */
                        case 4: r[rd] = (hw1 & 15) << 12 | imm12; break;                        /* MOVW */
                        case 12: r[rd] = (r[rd] & 0xFFFF) | (((hw1 & 15) << 12 | imm12) << 16); break; /* MOVT */
                        case 16: case 18: { /* SSAT (asr if sh) */
                            int sh = (hw1 >> 5) & 1, satv; uint32_t co; uint32_t amt = lsb;
                            if (op == 18 && amt == 0) UNDEF(); /* SSAT16 not supported */
                            uint32_t x = shift_c(r[rn], sh ? 2 : 0, sh && amt == 0 ? 32 : amt, cpu->c, &co);
                            r[rd] = sat_s((int32_t)x, (int)w5 + 1, &satv); if (satv) cpu->q = 1; break;
                        }
                        case 24: case 26: {
                            int sh = (hw1 >> 5) & 1, satv; uint32_t co; uint32_t amt = lsb;
                            if (op == 26 && amt == 0) UNDEF();
                            uint32_t x = shift_c(r[rn], sh ? 2 : 0, sh && amt == 0 ? 32 : amt, cpu->c, &co);
                            r[rd] = sat_u((int32_t)x, (int)w5, &satv); if (satv) cpu->q = 1; break;
                        }
                        case 20: { /* SBFX */
                            uint32_t wid = w5 + 1; if (lsb + wid > 32) UNDEF();
                            uint32_t x = r[rn] << (32 - lsb - wid);
                            r[rd] = (uint32_t)((int32_t)x >> (32 - wid)); break;
                        }
                        case 28: { /* UBFX */
                            uint32_t wid = w5 + 1; if (lsb + wid > 32) UNDEF();
                            r[rd] = (r[rn] >> lsb) & (wid == 32 ? 0xFFFFFFFFu : ((1u << wid) - 1)); break;
                        }
                        case 22: { /* BFI / BFC */
                            uint32_t msb = w5; if (msb < lsb) UNDEF();
                            uint32_t wid = msb - lsb + 1;
                            uint32_t mask = (wid == 32 ? 0xFFFFFFFFu : ((1u << wid) - 1)) << lsb;
                            uint32_t src = (rn == 15) ? 0 : (r[rn] << lsb);
                            r[rd] = (r[rd] & ~mask) | (src & mask); break;
                        }
                        default: UNDEF();
                        }
                    }
                } else { /* branches and miscellaneous control */
                    uint32_t op1b = (hw2 >> 12) & 7;
                    if ((op1b & 5) == 0) {
                        uint32_t op = (hw1 >> 6) & 15;
                        if ((op & 14) != 14) { /* B cond T3 */
                            uint32_t s = (hw1 >> 10) & 1, j1 = (hw2 >> 13) & 1, j2 = (hw2 >> 11) & 1;
                            uint32_t imm = (s << 20) | (j2 << 19) | (j1 << 18) | ((hw1 & 0x3F) << 12) | ((hw2 & 0x7FF) << 1);
                            int32_t off = (int32_t)(imm << 11) >> 11;
                            if (cond_pass(cpu, op)) next = pc + 4 + (uint32_t)off;
                        } else {
                            uint32_t o = (hw1 >> 4) & 0x7F;
                            if (o == 0x38 || o == 0x39) msr(cpu, hw2 & 0xFF, (hw2 >> 10) & 3, r[hw1 & 15]);
                            else if (o == 0x3A) { if ((hw2 & 0xFF) == 3) is_wfi = 1; }
                            else if (o == 0x3B) { /* DSB/DMB/ISB/CLREX: nothing */ }
                            else if (o == 0x3E || o == 0x3F) r[(hw2 >> 8) & 15] = mrs(cpu, hw2 & 0xFF);
                            else UNDEF();
                        }
                    } else if ((op1b & 5) == 1 || (op1b & 5) == 5) { /* B T4 / BL */
                        uint32_t s = (hw1 >> 10) & 1, j1 = (hw2 >> 13) & 1, j2 = (hw2 >> 11) & 1;
                        uint32_t i1 = !(j1 ^ s), i2 = !(j2 ^ s);
                        uint32_t imm = (s << 24) | (i1 << 23) | (i2 << 22) | ((hw1 & 0x3FF) << 12) | ((hw2 & 0x7FF) << 1);
                        int32_t off = (int32_t)(imm << 7) >> 7;
                        if (op1b & 4) r[14] = next | 1;
                        next = pc + 4 + (uint32_t)off;
                    } else UNDEF();
                }
            } else { /* op1 == 3 */
                if ((op2 & 0x71) == 0x00 || (op2 & 0x67) == 0x01 || (op2 & 0x67) == 0x03 || (op2 & 0x67) == 0x05) {
                    /* load/store single */
                    int load = (hw1 >> 4) & 1, size = 1 << ((hw1 >> 5) & 3), sgn = (hw1 >> 8) & 1;
                    uint32_t rn = hw1 & 15, rt = hw2 >> 12, a, base, off;
                    int wb = 0;
                    if (size == 8) UNDEF();
                    if (rn == 15) {
                        if (!load) UNDEF();
                        base = (pc + 4) & ~3u; off = hw2 & 0xFFF;
                        a = (hw1 & 0x80) ? base + off : base - off;
                    } else if (hw1 & 0x80) { a = r[rn] + (hw2 & 0xFFF); }
                    else if (hw2 & 0x800) {
                        uint32_t p = (hw2 >> 10) & 1, u = (hw2 >> 9) & 1, w = (hw2 >> 8) & 1, imm = hw2 & 0xFF;
                        off = u ? r[rn] + imm : r[rn] - imm;
                        a = p ? off : r[rn]; if (w) wb = 1; base = off;
                        if (!p && !w) UNDEF();
                    } else if ((hw2 & 0xFC0) == 0) {
                        a = r[rn] + (r[hw2 & 15] << ((hw2 >> 4) & 3));
                    } else UNDEF();
                    if (load) {
                        if (rt == 15 && size != 4) { /* PLD/PLI hints */ }
                        else {
                            uint32_t v = RD(cpu, a, size);
                            if (sgn) v = (size == 1) ? (uint32_t)(int32_t)(int8_t)v : (uint32_t)(int32_t)(int16_t)v;
                            if (wb) r[rn] = base;
                            if (rt == 15) {
                                if (!(v & 1)) { snprintf(cpu->fault, sizeof cpu->fault, "LDR pc to ARM state at 0x%08x", pc); bf = 1; goto done; }
                                next = v & ~1u;
                            } else r[rt] = v;
                        }
                    } else {
                        WR(cpu, a, size, r[rt]);
                        if (wb) r[rn] = base;
                    }
                } else if ((op2 & 0x70) == 0x20) { /* data processing (register) */
                    uint32_t o1 = (hw1 >> 4) & 15, o2 = (hw2 >> 4) & 15, rn = hw1 & 15, rd = (hw2 >> 8) & 15, rm = hw2 & 15;
                    if ((o1 & 8) == 0 && o2 == 0) { /* shift by register */
                        uint32_t co, res = shift_c(r[rn], (int)((o1 >> 1) & 3), r[rm] & 0xFF, cpu->c, &co);
                        r[rd] = res; if (o1 & 1) { set_nz(cpu, res); cpu->c = co; }
                    } else if ((o1 & 8) == 0 && (o2 & 8)) { /* extend (and add) */
                        uint32_t rot = ((hw2 >> 4) & 3) * 8, m = r[rm];
                        m = rot ? ((m >> rot) | (m << (32 - rot))) : m;
                        uint32_t v;
                        switch (o1) {
                        case 0: v = (uint32_t)(int32_t)(int16_t)m; break;
                        case 1: v = m & 0xFFFF; break;
                        case 4: v = (uint32_t)(int32_t)(int8_t)m; break;
                        case 5: v = m & 0xFF; break;
                        case 2: v = (((uint32_t)(int32_t)(int8_t)m) & 0xFFFF) | ((uint32_t)(int32_t)(int8_t)(m >> 16) << 16); break; /* SXTB16 */
                        case 3: v = m & 0x00FF00FFu; break; /* UXTB16 */
                        default: UNDEF();
                        }
                        if (rn != 15) {
                            if (o1 == 2 || o1 == 3) v = (((r[rn] & 0xFFFF) + (v & 0xFFFF)) & 0xFFFF) | (((r[rn] >> 16) + (v >> 16)) << 16);
                            else v += r[rn];
                        }
                        r[rd] = v;
                    } else if ((o1 & 0xC) == 8 && (o2 & 0xC) == 8) { /* misc */
                        uint32_t m = r[rm], a = r[rn];
                        int sat;
                        switch (((o1 & 3) << 2) | (o2 & 3)) {
                        case 0: r[rd] = sat_s((int64_t)(int32_t)m + (int32_t)a, 32, &sat); if (sat) cpu->q = 1; break; /* QADD Rd=Rm+Rn */
                        case 2: r[rd] = sat_s((int64_t)(int32_t)m - (int32_t)a, 32, &sat); if (sat) cpu->q = 1; break; /* QSUB */
                        case 4: r[rd] = __builtin_bswap32(m); break;
                        case 5: r[rd] = ((m & 0x00FF00FFu) << 8) | ((m >> 8) & 0x00FF00FFu); break;
                        case 6: { uint32_t x = m, y = 0; int i; for (i = 0; i < 32; i++) { y = (y << 1) | (x & 1); x >>= 1; } r[rd] = y; break; }
                        case 7: r[rd] = (uint32_t)(int32_t)(int16_t)(((m & 0xFF) << 8) | ((m >> 8) & 0xFF)); break;
                        case 12: r[rd] = m ? (uint32_t)__builtin_clz(m) : 32; break;
                        default: UNDEF();
                        }
                    } else UNDEF();
                } else if ((op2 & 0x78) == 0x30) { /* multiply, MAC */
                    uint32_t o1 = (hw1 >> 4) & 7, o2 = (hw2 >> 4) & 3, rn = hw1 & 15, ra = hw2 >> 12, rd = (hw2 >> 8) & 15, rm = hw2 & 15;
                    if (o1 == 0 && o2 == 0) r[rd] = r[rn] * r[rm] + (ra == 15 ? 0 : r[ra]);
                    else if (o1 == 0 && o2 == 1) r[rd] = r[ra] - r[rn] * r[rm];
                    else if (o1 == 1) { /* SMULxy / SMLAxy */
                        int16_t x = (int16_t)((hw2 & 0x20) ? (r[rn] >> 16) : r[rn]);
                        int16_t y = (int16_t)((hw2 & 0x10) ? (r[rm] >> 16) : r[rm]);
                        int64_t p = (int64_t)x * y;
                        if (ra != 15) { p += (int32_t)r[ra]; if (p != (int32_t)p) cpu->q = 1; }
                        r[rd] = (uint32_t)p;
                    } else UNDEF();
                } else if ((op2 & 0x78) == 0x38) { /* long multiply, divide */
                    uint32_t o1 = (hw1 >> 4) & 7, o2 = (hw2 >> 4) & 15, rn = hw1 & 15, rdlo = hw2 >> 12, rdhi = (hw2 >> 8) & 15, rm = hw2 & 15;
                    if (o1 == 0 && o2 == 0) { int64_t p = (int64_t)(int32_t)r[rn] * (int32_t)r[rm]; r[rdlo] = (uint32_t)p; r[rdhi] = (uint32_t)((uint64_t)p >> 32); }
                    else if (o1 == 2 && o2 == 0) { uint64_t p = (uint64_t)r[rn] * r[rm]; r[rdlo] = (uint32_t)p; r[rdhi] = (uint32_t)(p >> 32); }
                    else if (o1 == 4 && o2 == 0) { int64_t acc = (int64_t)(((uint64_t)r[rdhi] << 32) | r[rdlo]); acc += (int64_t)(int32_t)r[rn] * (int32_t)r[rm]; r[rdlo] = (uint32_t)acc; r[rdhi] = (uint32_t)((uint64_t)acc >> 32); }
                    else if (o1 == 6 && o2 == 0) { uint64_t acc = ((uint64_t)r[rdhi] << 32) | r[rdlo]; acc += (uint64_t)r[rn] * r[rm]; r[rdlo] = (uint32_t)acc; r[rdhi] = (uint32_t)(acc >> 32); }
                    else if (o1 == 6 && o2 == 6) { uint64_t acc = (uint64_t)r[rn] * r[rm] + r[rdhi] + r[rdlo]; r[rdlo] = (uint32_t)acc; r[rdhi] = (uint32_t)(acc >> 32); }
                    else if (o1 == 1 && o2 == 15) { int32_t a = (int32_t)r[rn], b = (int32_t)r[rm]; r[rdhi] = b == 0 ? 0 : (a == INT32_MIN && b == -1) ? (uint32_t)a : (uint32_t)(a / b); }
                    else if (o1 == 3 && o2 == 15) { uint32_t a = r[rn], b = r[rm]; r[rdhi] = b ? a / b : 0; }
                    else UNDEF();
                } else UNDEF();
            }
        }
#undef R
    advance:
        if (bf) goto done;
        if (in_it) { if ((cpu->it & 7) == 0) cpu->it = 0; else cpu->it = (uint8_t)((cpu->it & 0xE0) | ((cpu->it << 1) & 0x1F)); }
        r[15] = next;
        if (is_wfi) return CPU_RUN_WFI;
        continue;
    done:
        r[15] = pc;
        return CPU_RUN_FAULT;
    }
    return CPU_RUN_COUNT;
}
