/* cpu.h - small ARMv7-M (Cortex-M4, integer Thumb-2) interpreter.
 * Written from the ARMv7-M Architecture Reference Manual. No FPU.
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 José González Divassón
 */
#ifndef VC_CPU_H
#define VC_CPU_H
#include <stdint.h>
#include <stddef.h>

/* Slow-path bus used for everything that is not plain flash/SRAM. */
typedef struct vc_bus {
    void *ctx;
    /* return 0 on success, nonzero = bus fault (unmapped) */
    int (*read)(void *ctx, uint32_t addr, int size, uint32_t *val);
    int (*write)(void *ctx, uint32_t addr, int size, uint32_t val);
} vc_bus;

/* Stop reasons for cpu_run */
enum {
    CPU_RUN_COUNT = 0,   /* instruction budget used up */
    CPU_RUN_STOPADDR,    /* PC reached stop address (before executing it) */
    CPU_RUN_WFI,         /* a WFI instruction was executed */
    CPU_RUN_HOOK,        /* a hook asked to stop */
    CPU_RUN_FAULT        /* undefined instruction or bus fault; see cpu->fault */
};

typedef struct vc_cpu vc_cpu;
/* Called before the instruction at hook_addr[i] executes.
 * Return 0 = execute normally, 1 = stop before executing (CPU_RUN_HOOK),
 * 2 = hook changed PC; re-fetch without executing. */
typedef int (*vc_hook_fn)(vc_cpu *cpu, uint32_t pc, void *user);

#define VC_MAX_HOOKS 8

struct vc_cpu {
    uint32_t r[16];          /* r[15] = address of the current/next instruction */
    uint32_t n, z, c, v, q;  /* flags as 0/1 */
    uint32_t primask, faultmask, basepri, control, msp, psp;
    uint8_t  it;             /* ITSTATE */
    uint64_t icount;         /* instructions executed (including skipped IT ones) */
    /* fast memory */
    uint8_t *flash; uint32_t flash_size;   /* at address 0 */
    uint8_t *sram;  uint32_t sram_size;    /* at 0x20000000 */
    vc_bus bus;
    /* hooks */
    int nhooks;
    uint32_t hook_addr[VC_MAX_HOOKS];
    vc_hook_fn hook_fn[VC_MAX_HOOKS];
    void *hook_user[VC_MAX_HOOKS];
    uint32_t hook_lo, hook_hi;             /* quick range filter */
    /* fault info */
    char fault[128];
    int bf_;                               /* bus/undefined fault during current instruction */
};

void cpu_init(vc_cpu *cpu);
void cpu_add_hook(vc_cpu *cpu, uint32_t addr, vc_hook_fn fn, void *user);
/* Run until budget instructions were executed, PC == stop (if stop != 0xFFFFFFFF),
 * a WFI is executed, a hook stops, or a fault. */
int cpu_run(vc_cpu *cpu, uint64_t budget, uint32_t stop);

/* memory helpers usable by the machine */
int cpu_read(vc_cpu *cpu, uint32_t a, int size, uint32_t *v);
int cpu_write(vc_cpu *cpu, uint32_t a, int size, uint32_t v);

#endif
