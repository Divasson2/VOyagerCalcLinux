/* machine.c - the HP-16C CE "calculator": memory map, peripherals, the two engines and
 * the public API (voyager_core.h).
 *
 * The behaviour mirrors our Python reference simulator (voyager-sim-public, GPL-2.0, by
 * the same author). Spots that port the Python model's logic are marked "PORT:".
 * No HP firmware or ROM bytes are contained here: the firmware is supplied at run time
 * and recognised by SHA-256 fingerprints of short code windows.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 José González Divassón
 */
#include "voyager_core.h"
#include "cpu.h"
#include "sha256.h"
#include "internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---------------------------------------------------------------- constants */
#define FLASH_BASE   0x4000u
#define FLASH_SIZE   0x40000u
#define SRAM_BASE    0x20000000u
#define PERIPH_BASE  0x40000000u
#define PERIPH_SIZE  0x100000u
#define SCS_BASE     0xE0000000u
#define SCS_SIZE     0x100000u
#define USERPAGE     0x800000u
#define HIGH_BASE    0xFFFF0000u

/* HP-16C CE firmware layout (addresses checked by fingerprints) */
#define A16_ROMBASE  0xD824u   /* Nut ROM table base handed to the interpreter */
#define A16_POWERON  0x8894u   /* power-on initialisation */
#define A16_HOSTKEY  0x893Au   /* key entry: queue one key code and run the Nut CPU */
#define A16_FETCH    0x7306u   /* Nut instruction fetch point */
#define A16_CLKSW    0x9BB8u   /* clock switch (skipped by the runner) */
#define A16_PAINT    0xBDA2u   /* paints a text frame */
#define A12M_PAINT   0x18D92u  /* paint routine address in our HP-12C CE multi builds */
#define IMAGE_SIZE   0x1C000u  /* the application image 0x4000..0x1FFFF */
#define FLASH_DUMP   0x20000u  /* a full dump of the 128 KB flash: bootloader 0..0x3FFF + the image */
#define RUN_SENT     0x1000u   /* return address marking "routine finished" */
#define RUN_STACK    0x200013E0u
#define DUMP_ADDR    0x20000018u
#define STRUCT_ADDR  0x20000020u
#define ISR_SENT     0x2000F000u
#define LCDCA_DR     0x40080014u
#define GPIO_BASE    0x400E1000u
#define ADC_BASE     0x40038000u

static const uint8_t HDR[8] = {0x00, 0x08, 0, 0, 0, 0, 0, 0};

/* SHA-256 of 16-byte code windows (HP-16C CE layout); identical to the Python reference. */
static const struct { uint32_t addr; const char *sha; } FP16[] = {
    {0x8894, "1e8b1f0bec9459339d10625e8072768e3c46f83958dcc3b3ba775a2983af1e70"},
    {0x893A, "c300cff860f1a8f7a9c2627096a5d0e630baadba2cbc5508bab353a9d58102dd"},
    {0x9BB8, "0ca9c0c14d3760a6018a5790c00af237ecbd2d200e4a7707936b70418247c31c"},
};
static const char *FP_PAINT = "2dde7636aead543cf2646541a2193e06ab2a3843b3703c111674125465623feb";

/* ---------------------------------------------------------------- keys */
/* PORT: key layout and aliases as in voyager/runner16c.py and wholecalc.py */
static const char *KEYS16[40] = {
    "A", "B", "C", "D", "E", "F", "7", "8", "9", "\xc3\xb7",
    "GSB", "GTO", "HEX", "DEC", "OCT", "BIN", "4", "5", "6", "\xc3\x97",
    "R/S", "SST", "R\xe2\x86\x93", "x<>y", "BSP", "ENTER", "1", "2", "3", "\xe2\x88\x92",
    "ON", "f", "g", "STO", "RCL", "ENT2", "0", ".", "CHS", "+"};
static const struct { const char *alias, *name; } ALIAS16[] = {
    {"*", "\xc3\x97"}, {"X", "\xc3\x97"}, {"/", "\xc3\xb7"}, {"-", "\xe2\x88\x92"},
    {"RDN", "R\xe2\x86\x93"}, {"RDOWN", "R\xe2\x86\x93"}, {"XY", "x<>y"}, {"X<>Y", "x<>y"},
    {"<-", "BSP"}, {"BACK", "BSP"}, {"RS", "R/S"}};
static const char *KEYS15[40] = { /* PORT: wholecalc.KEYS_15C */
    "\xe2\x88\x9ax", "e^x", "10^x", "y^x", "1/x", "CHS", "7", "8", "9", "\xc3\xb7",
    "SST", "GTO", "SIN", "COS", "TAN", "EEX", "4", "5", "6", "\xc3\x97",
    "R/S", "GSB", "R\xe2\x86\x93", "x<>y", "\xe2\x86\x90", "ENTER", "1", "2", "3", "\xe2\x88\x92",
    "ON", "f", "g", "STO", "RCL", "ENT2", "0", ".", "\xce\xa3+", "+"};
static const struct { const char *alias, *name; } ALIAS15[] = { /* PORT: wholecalc.ALIAS_15C */
    {"SQRT", "\xe2\x88\x9ax"}, {"EX", "e^x"}, {"E^X", "e^x"}, {"10X", "10^x"}, {"10^X", "10^x"}, {"YX", "y^x"},
    {"Y^X", "y^x"}, {"1/X", "1/x"}, {"<-", "\xe2\x86\x90"}, {"BSP", "\xe2\x86\x90"}, {"BACK", "\xe2\x86\x90"},
    {"S+", "\xce\xa3+"}, {"SIGMA+", "\xce\xa3+"}, {"RDN", "R\xe2\x86\x93"}, {"XY", "x<>y"}, {"X<>Y", "x<>y"},
    {"*", "\xc3\x97"}, {"/", "\xc3\xb7"}, {"-", "\xe2\x88\x92"}, {"RS", "R/S"}};
static const char *KEYS12[40] = { /* HP-12C keyboard, same matrix positions */
    "n", "i", "PV", "PMT", "FV", "CHS", "7", "8", "9", "\xc3\xb7",
    "y^x", "1/x", "%T", "\xce\x94%", "%", "EEX", "4", "5", "6", "\xc3\x97",
    "R/S", "SST", "R\xe2\x86\x93", "x<>y", "CLx", "ENTER", "1", "2", "3", "\xe2\x88\x92",
    "ON", "f", "g", "STO", "RCL", "ENT2", "0", ".", "\xce\xa3+", "+"};
static const struct { const char *alias, *name; } ALIAS12[] = {
    {"YX", "y^x"}, {"Y^X", "y^x"}, {"1/X", "1/x"}, {"%t", "%T"}, {"D%", "\xce\x94%"}, {"S+", "\xce\xa3+"},
    {"SIGMA+", "\xce\xa3+"}, {"RDN", "R\xe2\x86\x93"}, {"XY", "x<>y"}, {"X<>Y", "x<>y"}, {"BSP", "CLx"},
    {"*", "\xc3\x97"}, {"/", "\xc3\xb7"}, {"-", "\xe2\x88\x92"}, {"RS", "R/S"}};
typedef struct { const char **keys; const void *alias; size_t nalias; } keyset;
static keyset keyset_for(int model) {
    keyset k = {NULL, NULL, 0};
    if (model == VC_MODEL_16C) { k.keys = KEYS16; k.alias = ALIAS16; k.nalias = sizeof ALIAS16 / sizeof ALIAS16[0]; }
    if (model == VC_MODEL_15C) { k.keys = KEYS15; k.alias = ALIAS15; k.nalias = sizeof ALIAS15 / sizeof ALIAS15[0]; }
    if (model == VC_MODEL_12C) { k.keys = KEYS12; k.alias = ALIAS12; k.nalias = sizeof ALIAS12 / sizeof ALIAS12[0]; }
    return k;
}
#define KEY_ON 30
#define KEY_F  31
#define KEY_G  32

/* ---------------------------------------------------------------- display tables */
/* PORT: 7-segment map of the Nut display registers, from voyager/runner16c.py */
static const uint8_t SEGMAP[10][7] = { /* a b c d e f g bit numbers, digit position 0..9 */
    {53, 55, 51, 41, 50, 52, 54}, {47, 49, 45, 33, 44, 46, 48}, {29, 31, 27, 39, 26, 28, 30},
    {21, 23, 19, 7, 18, 20, 22},  {11, 13, 9, 15, 8, 10, 12},   {99, 101, 97, 63, 96, 98, 100},
    {89, 91, 87, 65, 86, 88, 90}, {107, 109, 105, 69, 104, 106, 108}, {83, 85, 81, 73, 80, 82, 84},
    {77, 79, 75, 103, 74, 76, 78}};
static const struct { uint8_t segs; char ch; } PATTERNS[] = {
#define S(a,b,c,d,e,f,g) ((a)|(b)<<1|(c)<<2|(d)<<3|(e)<<4|(f)<<5|(g)<<6)
    {S(1,1,1,1,1,1,0), '0'}, {S(0,1,1,0,0,0,0), '1'}, {S(1,1,0,1,1,0,1), '2'}, {S(1,1,1,1,0,0,1), '3'},
    {S(0,1,1,0,0,1,1), '4'}, {S(1,0,1,1,0,1,1), '5'}, {S(1,0,1,1,1,1,1), '6'}, {S(1,1,1,0,0,0,0), '7'},
    {S(1,1,1,1,1,1,1), '8'}, {S(1,1,1,1,0,1,1), '9'}, {S(1,1,1,0,1,1,1), 'A'}, {S(0,0,1,1,1,1,1), 'b'},
    {S(1,0,0,1,1,1,0), 'C'}, {S(0,1,1,1,1,0,1), 'd'}, {S(1,0,0,1,1,1,1), 'E'}, {S(1,0,0,0,1,1,1), 'F'},
    {S(0,0,0,0,0,0,1), '-'}, {S(0,0,0,0,0,0,0), ' '}, {S(1,1,0,0,1,1,1), 'P'}, {S(0,0,0,0,1,0,1), 'r'},
    {S(0,0,1,0,1,1,1), 'h'}, {S(0,0,1,1,1,0,1), 'o'}, {S(0,0,0,1,1,1,1), 't'}, {S(0,0,0,1,1,1,0), 'L'},
    {S(0,0,1,0,1,0,1), 'n'}, {S(0,1,1,1,1,1,0), 'U'}, {S(0,0,1,1,1,0,0), 'u'}, {S(1,1,1,1,0,1,0), '?'}, {S(0,1,1,0,1,1,1), 'H'}, {S(0,1,1,1,0,0,0), 'J'},
    {S(1,0,1,1,1,1,0), 'G'}, {S(0,0,0,1,0,0,0), '_'}, {S(0,1,1,1,0,1,1), 'y'}, {S(0,0,0,0,1,1,0), 'i'},
#undef S
};

/* ---------------------------------------------------------------- peripheral map */
/* address -> value map with the semantics of a Python dict keyed by access address
 * (the reference model stores each written value under the exact address). */
typedef struct { uint32_t *key, *val; uint8_t *used; uint32_t cap, n; } amap;
static uint32_t ahash(uint32_t k) { k ^= k >> 16; k *= 0x7feb352du; k ^= k >> 15; k *= 0x846ca68bu; return k ^ (k >> 16); }
static int amap_init(amap *m, uint32_t cap) {
    m->cap = cap; m->n = 0;
    m->key = calloc(cap, 4); m->val = calloc(cap, 4); m->used = calloc(cap, 1);
    return m->key && m->val && m->used ? 0 : -1;
}
static void amap_free(amap *m) { free(m->key); free(m->val); free(m->used); memset(m, 0, sizeof *m); }
static void amap_clear(amap *m) { if (m->used) memset(m->used, 0, m->cap); m->n = 0; }
static int amap_get(const amap *m, uint32_t k, uint32_t *v) {
    uint32_t i = ahash(k) & (m->cap - 1);
    while (m->used[i]) { if (m->key[i] == k) { *v = m->val[i]; return 1; } i = (i + 1) & (m->cap - 1); }
    return 0;
}
static void amap_put(amap *m, uint32_t k, uint32_t v) {
    if (m->n * 2 >= m->cap) { /* grow */
        amap o = *m; uint32_t i;
        if (amap_init(m, o.cap * 2)) { *m = o; return; }
        for (i = 0; i < o.cap; i++) if (o.used[i]) amap_put(m, o.key[i], o.val[i]);
        amap_free(&o);
    }
    uint32_t i = ahash(k) & (m->cap - 1);
    while (m->used[i]) { if (m->key[i] == k) { m->val[i] = v; return; } i = (i + 1) & (m->cap - 1); }
    m->used[i] = 1; m->key[i] = k; m->val[i] = v; m->n++;
}

/* ---------------------------------------------------------------- the calculator */
typedef struct { uint32_t oder, ovr, gper; } gpio_port;

#define MAX_FRAMES 64
struct vc_calc {
    vc_cpu cpu;
    uint8_t *img; size_t img_len;
    int model, family, variant, ext16, runner_ok;
    uint32_t paint;
    int checksum;            /* CE checksum shown by the calculator (XYXY -> XY), -1 if the image is too short */
    uint32_t stripped;       /* bytes dropped from the front of the file (a full flash dump) */
    int loose_paint;         /* paint routine found by its code shape, not by the exact fingerprint */
    char info[160];
    char err[640];
    int engine;              /* -1 = not started */
    uint8_t *flash, *sram;
    uint8_t *periph, *scs, *high, *userpage;  /* runner: plain memory; whole: high/userpage */
    amap mm;                 /* whole: peripheral + SCS registers */
    /* runner state */
    uint64_t steps, limit;
    int prefix;
    vc_cpu snap_cpu; uint8_t *snap_sram; int have_snap;
    char held[16];
    /* whole state */
    gpio_port gpio[3];
    uint8_t keydown[40];
    int on;
    uint64_t ticks, sleeps, insn;
    uint32_t busy;           /* inspection only: ticks in a row without sleep */
    uint32_t tick_insn;
    int ast_en, adc_en, adc_conv, adc_isr;
    uint32_t adc_ie;         /* ADCIFE interrupt enable (IER sets, IDR clears) */
    uint32_t nvic_en2;       /* NVIC ISER2 / ICER2 (IRQ 64..95): bit 5 = IRQ 69 = ADCIFE */
    uint32_t clk_addr;       /* SRAM address of the firmware's CPU-clock variable (Hz), build specific */
    uint32_t adc_value, adc_vec, tick_vec;
    double battery;
    double ipc;                  /* ARM instructions per CPU clock used for the 10 ms slice (default 0.75) */
    uint32_t consec_a; int consec_n, consec_flip, consec_valid;
    char frames[MAX_FRAMES][21]; uint32_t nframes;
};

static void seterr(vc_calc *c, const char *msg) { snprintf(c->err, sizeof c->err, "%s", msg); }
static uint32_t rd32(vc_calc *c, uint32_t a) { uint32_t v = 0; cpu_read(&c->cpu, a, 4, &v); return v; }

/* ---------------- runner bus: plain memory everywhere */
static int plain_rw(vc_calc *c, uint32_t a, int size, uint32_t *rv, int wr, uint32_t wv) {
    uint8_t *p = NULL;
    if (a - PERIPH_BASE < PERIPH_SIZE && c->periph) p = c->periph + (a - PERIPH_BASE);
    else if (a - SCS_BASE < SCS_SIZE && c->scs) p = c->scs + (a - SCS_BASE);
    else if (c->engine == VC_ENGINE_WHOLE && a - HIGH_BASE < 0x10000u) p = c->high + (a - HIGH_BASE);
    else if (c->engine == VC_ENGINE_WHOLE && a - USERPAGE < 0x1000u) p = c->userpage + (a - USERPAGE);
    if (!p) return 1;
    if (wr) { int i; for (i = 0; i < size; i++) p[i] = (uint8_t)(wv >> (8 * i)); }
    else { uint32_t v = 0; int i; for (i = 0; i < size; i++) v |= (uint32_t)p[i] << (8 * i); *rv = v; }
    return 0;
}
static int runner_read(void *ctx, uint32_t a, int size, uint32_t *v) { return plain_rw(ctx, a, size, v, 0, 0); }
static int runner_write(void *ctx, uint32_t a, int size, uint32_t v) { return plain_rw(ctx, a, size, NULL, 1, v); }

/* ---------------- whole bus: peripherals modelled like the Python reference */
static const uint8_t COLPIN[10][2] = {{2,7},{0,7},{1,5},{1,4},{1,0},{1,2},{1,3},{2,2},{2,1},{2,0}}; /* PORT */
static const uint8_t ROWPIN[4][2] = {{2,3},{2,4},{2,5},{2,6}};                                      /* PORT */

static uint32_t gpio_pvr(vc_calc *c, int port) { /* PORT: wholecalc._pvr */
    uint32_t v = 0xFFFFFFFFu; int k;
    for (k = 0; k < 40; k++) if (c->keydown[k]) {
        int r = k / 10, col = k % 10;
        gpio_port *g = &c->gpio[ROWPIN[r][0]]; int rb = ROWPIN[r][1];
        if (((g->oder >> rb) & 1) && !((g->ovr >> rb) & 1))
            if (COLPIN[col][0] == port) v &= ~(1u << COLPIN[col][1]);
    }
    if (port == 1 && c->on) v &= ~(1u << 1);
    return v;
}
static uint32_t ready_loop(vc_calc *c, uint32_t a, uint32_t v) { /* PORT: wholecalc._ready_loop */
    if (c->consec_valid && c->consec_a == a) c->consec_n++;
    else { c->consec_a = a; c->consec_n = 0; c->consec_flip = 0; c->consec_valid = 1; }
    if (c->consec_n > 40) { c->consec_flip ^= 1; c->consec_n = 0; }
    return c->consec_flip ? ~v : v;
}
static uint32_t szmask(int size) { return size == 4 ? 0xFFFFFFFFu : ((1u << (8 * size)) - 1); }

static int whole_read(void *ctx, uint32_t a, int size, uint32_t *rv) {
    vc_calc *c = ctx; uint32_t v = 0;
    if (a - PERIPH_BASE < PERIPH_SIZE) {
        amap_get(&c->mm, a, &v);
        if (a - GPIO_BASE < 0x600u) {
            int port = (int)((a - GPIO_BASE) >> 9); uint32_t o = (a - GPIO_BASE) & 0x1FF;
            if (o == 0x60) v = gpio_pvr(c, port);
            else if (o == 0x40) v = c->gpio[port].oder;
            else if (o == 0x50) v = c->gpio[port].ovr;
        } else if (a == ADC_BASE + 8) {
            v = c->adc_isr ? 1 : ((c->adc_en ? 0x01000000u : 0) | (c->adc_conv ? 1u : 0));
        } else if (a == ADC_BASE + 0x2C) v = c->adc_value;
        else if (a == ADC_BASE + 0x38) v = c->adc_ie;       /* IMR */
        else if (a == 0x400A0008u) v |= 1;
        else v = ready_loop(c, a, v);
        *rv = v & szmask(size); return 0;
    }
    if (a - SCS_BASE < SCS_SIZE) { amap_get(&c->mm, a, &v); *rv = v & szmask(size); return 0; }
    return plain_rw(c, a, size, rv, 0, 0);
}
static int whole_write(void *ctx, uint32_t a, int size, uint32_t v) {
    vc_calc *c = ctx;
    if (a - PERIPH_BASE < PERIPH_SIZE) {
        amap_put(&c->mm, a, v);
        if (a - GPIO_BASE < 0x600u) {
            int port = (int)((a - GPIO_BASE) >> 9); uint32_t o = (a - GPIO_BASE) & 0x1FF;
            gpio_port *g = &c->gpio[port];
            uint32_t *f[3] = {&g->oder, &g->ovr, &g->gper}; const uint32_t base[3] = {0x40, 0x50, 0x00}; int i;
            for (i = 0; i < 3; i++) {
                if (o == base[i]) *f[i] = v;
                else if (o == base[i] + 4) *f[i] |= v;
                else if (o == base[i] + 8) *f[i] &= ~v;
                else if (o == base[i] + 0xC) *f[i] ^= v;
            }
        } else if (a == ADC_BASE) {
            if (v & 0x100) c->adc_en = 1;
            if (v & 0x200) c->adc_en = 0;
            if ((v & 0x8) && c->adc_en) c->adc_conv = 1;
        } else if (a == ADC_BASE + 0xC && (v & 1)) c->adc_conv = 0;
        else if (a == ADC_BASE + 0x30) c->adc_ie |= v;      /* IER */
        else if (a == ADC_BASE + 0x34) c->adc_ie &= ~v;     /* IDR */
        (void)size; return 0;
    }
    if (a - SCS_BASE < SCS_SIZE) {
        amap_put(&c->mm, a, v);
        if (a == 0xE000E104u && (v & 0x80)) c->ast_en = 1;
        if (a == 0xE000E184u && (v & 0x80)) c->ast_en = 0;
        if (a == 0xE000E108u) c->nvic_en2 |= v;             /* ISER2 */
        if (a == 0xE000E188u) c->nvic_en2 &= ~v;            /* ICER2 */
        return 0;
    }
    return plain_rw(c, a, size, NULL, 1, v);
}

/* ---------------------------------------------------------------- hooks */
static int hook_fetch(vc_cpu *cpu, uint32_t pc, void *u) {
    vc_calc *c = u; (void)cpu; (void)pc;
    c->steps++;
    return c->steps > c->limit ? 1 : 0;
}
static int hook_clksw(vc_cpu *cpu, uint32_t pc, void *u) { (void)pc; (void)u; cpu->r[15] = cpu->r[14] & ~1u; return 2; }
static int hook_paint(vc_cpu *cpu, uint32_t pc, void *u) {
    vc_calc *c = u; (void)pc; int i;
    char *f = c->frames[c->nframes % MAX_FRAMES];
    for (i = 0; i < 20; i++) { uint32_t b = 0; cpu_read(cpu, cpu->r[0] + (uint32_t)i, 1, &b); f[i] = (char)b; }
    f[20] = 0; c->nframes++;
    return 0;
}

/* ---------------------------------------------------------------- life cycle */
vc_calc *vc_create(void) {
    vc_calc *c = calloc(1, sizeof *c);
    if (!c) return NULL;
    c->engine = -1;
    c->battery = 3.0;
    c->ipc = VC_IPC_REFERENCE;
    c->flash = calloc(FLASH_SIZE, 1);
    c->sram = calloc(0x10000, 1);
    c->snap_sram = calloc(0x10000, 1);
    c->high = calloc(0x10000, 1);
    c->userpage = malloc(0x1000);
    if (!c->flash || !c->sram || !c->snap_sram || !c->high || !c->userpage || amap_init(&c->mm, 1024)) { vc_destroy(c); return NULL; }
    return c;
}
void vc_destroy(vc_calc *c) {
    if (!c) return;
    free(c->img); free(c->flash); free(c->sram); free(c->snap_sram); free(c->periph); free(c->scs);
    free(c->high); free(c->userpage); amap_free(&c->mm); free(c);
}

static void hexsha(const uint8_t *p, size_t n, char out[65]) {
    uint8_t h[32]; int i; vc_sha256(p, n, h);
    for (i = 0; i < 32; i++) snprintf(out + 2 * i, 3, "%02x", h[i]);
}

/* ---------------------------------------------------------------- firmware loading */
/* The image for flash address 0x4000 starts with an ARM vector table: initial SP in SRAM, reset
 * vector inside the image. */
static int vector_ok(const uint8_t *d, size_t len) {
    if (len < 8) return 0;
    uint32_t sp = (uint32_t)d[0] | d[1] << 8 | d[2] << 16 | (uint32_t)d[3] << 24;
    uint32_t rst = (uint32_t)d[4] | d[5] << 8 | d[6] << 16 | (uint32_t)d[7] << 24;
    return !(sp < SRAM_BASE || sp > SRAM_BASE + 0x10000 || (rst & ~1u) < FLASH_BASE || (rst & ~1u) >= FLASH_BASE + len);
}
/* The CE checksum the calculator shows as XYXY: (sum of the 0x1C000 image bytes - the byte at
 * 0x1BFEF) mod 256. Same rule as VoyagerFlash. -1 if the image is shorter than that. */
static int ce_checksum(const uint8_t *d, size_t len) {
    size_t i; uint32_t sum = 0;
    if (len < IMAGE_SIZE) return -1;
    for (i = 0; i < IMAGE_SIZE; i++) sum += d[i];
    return (int)((sum - d[0x1BFEF]) & 0xFF);
}
/* The LCD paint routine by its code shape: registers, branch targets and literal-pool offsets
 * may differ between builds (HP's own 12C image differs from the 15C CE one in two halfwords),
 * so each halfword is compared under a mask. Exactly one hit, else 0. */
static uint32_t find_paint_loose(const uint8_t *img, size_t len) {
    static const uint16_t V[15] = {0xB538, 0x4604, 0x2500, 0x5C00, 0x4628, 0xF000, 0xF800, 0x1C6D, 0x2D14, 0xDBF8, 0xBD31, 0x4800, 0x7800, 0x28FF, 0xD009};
    static const uint16_t M[15] = {0xFFFF, 0xFFFF, 0xFFFF, 0xFE00, 0xFFFF, 0xF800, 0xF800, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xF800, 0xFFFF, 0xFFFF, 0xFFFF};
    size_t o; int hits = 0; uint32_t at = 0;
    for (o = 0; o + 30 <= len; o += 2) {
        int k;
        for (k = 0; k < 15; k++) {
            uint16_t h = (uint16_t)(img[o + 2 * k] | img[o + 2 * k + 1] << 8);
            if ((h & M[k]) != V[k]) break;
        }
        if (k == 15) { hits++; at = FLASH_BASE + (uint32_t)o; }
    }
    return hits == 1 ? at : 0;
}
/* Does the code load the address 0x20000010 from a literal? CE builds that run several calculators
 * (and the 15C CE) publish the running ROM's address there; HP's original 12C image never does,
 * so the emulator could not tell which calculator it is driving. */
static int has_rom_pointer(const uint8_t *img, size_t len) {
    size_t o;
    for (o = 0; o + 4 <= len; o += 2)
        if (img[o] == 0x10 && img[o + 1] == 0 && img[o + 2] == 0 && img[o + 3] == 0x20) return 1;
    return 0;
}
/* Calculator images (Nut ROMs) found by their header; bit 0 = 12C, 1 = 15C, 2 = 16C. */
static int rom_mask(const uint8_t *img, size_t len) {
    size_t o; int m = 0;
    for (o = 0; o + 14 <= len; o += 2) {
        const uint8_t *p = img + o;
        if (p[2] != 0x4E || p[3] != 0 || p[4] != 0x70 || p[5] != 2 || p[6] != 0x38 || p[7] != 2 || p[8] != 0xEE || p[9] != 2) continue;
        int w5 = p[10] | p[11] << 8;
        if (w5 == 0x14F) m |= 1; else if (w5 == 0x10F) m |= 2; else if (w5 == 0x0FF) m |= 4;
    }
    return m;
}
static void rom_names(int m, char *out, size_t n) {
    out[0] = 0;
    if (m & 1) snprintf(out + strlen(out), n - strlen(out), "%s12C", out[0] ? ", " : "");
    if (m & 2) snprintf(out + strlen(out), n - strlen(out), "%s15C", out[0] ? ", " : "");
    if (m & 4) snprintf(out + strlen(out), n - strlen(out), "%s16C", out[0] ? ", " : "");
    if (!out[0]) snprintf(out, n, "no Nut ROM found");
}
/* HP's original firmware layout runs when it holds the 12C ROM only (the one verified: the 2015 HP-12C). */
static int stock_runnable(const vc_calc *c) { return c->family == VC_FAMILY_STOCK && c->img && rom_mask(c->img, c->img_len) == 1; }
static void ck_text(int ck, char out[12]) { if (ck < 0) snprintf(out, 12, "unknown"); else snprintf(out, 12, "%02X%02X", ck & 255, ck & 255); }

int vc_load_firmware(vc_calc *c, const uint8_t *data, size_t len) {
    size_t i; char hs[65];
    if (!c || !data) return VC_ERR_ARG;
    c->stripped = 0; c->checksum = -1; c->loose_paint = 0;
    /* 1. A full flash dump (bootloader 0..0x3FFF + the image at 0x4000): use the part from 0x4000. A file
     *    that is itself a valid image (also one padded with 0xFF after it) is used as it is. */
    if (len >= FLASH_DUMP && len <= FLASH_SIZE && !vector_ok(data, len) && vector_ok(data + FLASH_BASE, len - FLASH_BASE)) {
        data += FLASH_BASE; len -= FLASH_BASE; c->stripped = FLASH_BASE;
    }
    if (len == 0) { seterr(c, "The file is empty."); return VC_ERR_FIRMWARE; }
    if (len < IMAGE_SIZE) {
        snprintf(c->err, sizeof c->err, "This file is %zu bytes. VoyagerCalc needs the firmware image for flash address 0x4000: 114,688 bytes (112 KB), "
                 "or a full dump of the calculator's 128 KB flash (131,072 bytes, read from address 0). Is the file cut short?", len);
        return VC_ERR_FIRMWARE;
    }
    if (len > FLASH_SIZE - FLASH_BASE) {
        snprintf(c->err, sizeof c->err, "This file is %zu bytes, larger than any firmware or flash dump this chip can hold. VoyagerCalc needs "
                 "the firmware image for flash address 0x4000: 114,688 bytes (112 KB), or a full dump of the 128 KB flash.", len);
        return VC_ERR_FIRMWARE;
    }
    if (!vector_ok(data, len)) {
        if (len >= FLASH_DUMP)
            snprintf(c->err, sizeof c->err, "This file is %zu bytes, the size of a full flash dump, but there is no firmware at address 0x4000 "
                     "(no ARM vector table there). Expected: the bootloader in the first 16 KB and the calculator's firmware after it.", len);
        else
            snprintf(c->err, sizeof c->err, "This file is %zu bytes but does not start with an ARM vector table for flash address 0x4000, so it is not "
                     "a firmware image. Expected: the 114,688-byte firmware image, or a full 128 KB flash dump. A dump read from the wrong "
                     "offset, a text or zip file, or another chip's firmware looks like this.", len);
        return VC_ERR_FIRMWARE;
    }
    free(c->img);
    c->img = malloc(len); if (!c->img) return VC_ERR_ARG;
    memcpy(c->img, data, len); c->img_len = len;
    c->checksum = ce_checksum(c->img, len);
    c->runner_ok = 1;
    for (i = 0; i < sizeof FP16 / sizeof FP16[0]; i++) {
        uint32_t off = FP16[i].addr - FLASH_BASE;
        hexsha(c->img + off, 16, hs);
        if (strcmp(hs, FP16[i].sha)) c->runner_ok = 0;
    }
    int hits = 0; uint32_t at = 0;
    for (i = 0; i + 16 < len; i += 2) {
        hexsha(c->img + i, 16, hs);
        if (!strcmp(hs, FP_PAINT)) { hits++; at = FLASH_BASE + (uint32_t)i; }
    }
    c->paint = hits == 1 ? at : 0;
    c->model = VC_MODEL_NONE; c->family = VC_MODEL_NONE;
    int loose = 0;
    if (c->paint) {
        if (c->runner_ok && c->paint == A16_PAINT) c->family = VC_MODEL_16C;
        else if (c->paint == A12M_PAINT) c->family = VC_FAMILY_MULTI;          /* our HP-12C CE multi builds */
        else if (!c->runner_ok && c->paint > 0x14000) c->family = VC_MODEL_15C;
    } else if ((c->paint = find_paint_loose(c->img, len)) != 0) {
        /* Not one of the known builds, but the same display routine: recognised by its shape. */
        loose = c->loose_paint = 1;
        if (c->runner_ok) c->family = VC_MODEL_16C;
        else if (has_rom_pointer(c->img, len)) c->family = VC_MODEL_15C;      /* the CE engine follows the model by the ROM pointer */
        else c->family = VC_FAMILY_STOCK;
    }
    c->model = c->family == VC_FAMILY_MULTI ? VC_MODEL_12C : c->family == VC_FAMILY_STOCK ? VC_MODEL_12C : c->family;
    c->ext16 = c->family == VC_MODEL_16C && (c->img[0xE000] | c->img[0xE001] | c->img[0xE002] | c->img[0xE003]);
    char ck[12], names[40]; ck_text(c->checksum, ck);
    int rm = rom_mask(c->img, len); rom_names(rm, names, sizeof names);
    if (c->family == VC_FAMILY_STOCK) {
        if (stock_runnable(c))
            snprintf(c->info, sizeof c->info, "HP's original HP-12C firmware; limited: no inspector, memory files or model switch");
        else
            snprintf(c->info, sizeof c->info, "HP original-layout firmware, checksum %s, contains the %s ROM (not verified, not run)", ck, names);
    } else {
        const char *base = c->family == VC_MODEL_16C ? "HP-16C CE firmware" : c->family == VC_MODEL_15C ? (loose && rm == 1 ? "HP-12C CE firmware" : loose && rm == 4 ? "HP-16C CE firmware" : "HP-15C CE firmware") :
                           c->family == VC_FAMILY_MULTI ? "HP-12C CE multi firmware (12C / 15C / 16C)" : "unknown firmware";
        int m16 = c->family == VC_FAMILY_MULTI ? vc_firmware_mem16(c) : VC_MEM16_NONE;
        const char *e16 = c->ext16 ? " (extended memory)" :
                          m16 == VC_MEM16_EXT ? " (16C: extended memory)" : m16 == VC_MEM16_EXT_E3E3 ? " (16C: extended memory, early E3E3 layout)" :
                          m16 == VC_MEM16_STD ? " (16C: standard memory)" : "";
        snprintf(c->info, sizeof c->info, "%s%s%s", base, e16, loose && c->family != VC_MODEL_NONE ? " (recognised by its structure, not a tested build)" : "");
    }
    c->engine = -1;
    if (c->family == VC_FAMILY_STOCK && !stock_runnable(c)) {
        snprintf(c->err, sizeof c->err, "This is HP calculator firmware in the original HP layout (checksum %s, contains the %s ROM). VoyagerCalc runs the original "
                 "HP-12C firmware, but this one has not been verified, so it is not started. Please choose a Voyager firmware or the original HP-12C image instead. "
                 "Nothing was changed on your calculator.", ck, names);
        return VC_ERR_UNSUPPORTED;
    }
    if (c->family == VC_MODEL_NONE) {
        if (rm)
            snprintf(c->err, sizeof c->err, "This is a valid firmware image (checksum %s, contains the %s ROM) but VoyagerCalc does not know its display routine, "
                     "so it cannot run it. The tested builds are the HP-15C CE, HP-16C CE and Voyager 12C multi firmware.", ck, names);
        else
            snprintf(c->err, sizeof c->err, "This file has a valid vector table (checksum %s) but no HP calculator ROM was found in it, so it is not an HP-12C, "
                     "HP-15C or HP-16C CE firmware that VoyagerCalc can run.", ck);
        return VC_ERR_FIRMWARE;
    }
    return VC_OK;
}
int vc_firmware_checksum(const vc_calc *c) { return c ? c->checksum : -1; }
uint32_t vc_firmware_stripped(const vc_calc *c) { return c ? c->stripped : 0; }
int vc_firmware_family(const vc_calc *c) { return c ? c->family : 0; }
uint32_t vc_debug_read32(vc_calc *c, uint32_t addr) { return c && c->engine >= 0 ? rd32(c, addr) : 0; }
/* Which Nut ROM the firmware runs now. Multi-ROM builds (our 12C multi, and the 15C 3-ROM
 * builds) keep the selected ROM's flash address at 0x20000010; the address differs per build,
 * so the ROM is recognised by its content: Nut word 5 is 0x14F in the 12C ROM, 0x10F in the
 * 15C ROMs and 0x0FF in the 16C ROM; a 15C ROM with the long-memory markers
 * (words 0x356 and 0xBE9 = 0x040) is 15.2, otherwise 15.0. */
static int rom_word(vc_calc *c, uint32_t rb, uint32_t i) {
    uint32_t o = rb - FLASH_BASE + 2 * i;
    if (rb < FLASH_BASE || o + 1 >= c->img_len) return -1;
    return c->img[o] | c->img[o + 1] << 8;
}
static void update_active_model(vc_calc *c) {
    if (c->engine != VC_ENGINE_WHOLE) return;
    if (c->family == VC_FAMILY_STOCK) { c->model = VC_MODEL_12C; c->variant = 120; return; }   /* one ROM, no model word to follow */
    uint32_t rb = rd32(c, 0x20000010);
    int w = rom_word(c, rb, 5);
    if (w == 0x14F) { c->model = VC_MODEL_12C; c->variant = 120; }
    else if (w == 0x0FF) { c->model = VC_MODEL_16C; c->variant = 160; }
    else if (w == 0x10F) { c->model = VC_MODEL_15C; c->variant = rom_word(c, rb, 0x356) == 0x40 && rom_word(c, rb, 0xBE9) == 0x40 ? 152 : 150; }
}
/* Every Nut ROM image starts with the same words 1..4 (0x04E 0x270 0x238 0x2EE); word 5 says
 * which calculator it is (see update_active_model). Scan the image for them. */
static int variant_at(vc_calc *c, uint32_t rb) {
    int w = rom_word(c, rb, 5);
    if (w == 0x14F) return 120;
    if (w == 0x0FF) return 160;
    if (w == 0x10F) return rom_word(c, rb, 0x356) == 0x40 && rom_word(c, rb, 0xBE9) == 0x40 ? 152 : 150;
    return 0;
}
int vc_firmware_images(const vc_calc *c, int *variants, int max) {
    static const int ORDER[4] = {120, 150, 152, 160};
    int found[4] = {0}, n = 0, k;
    uint32_t o;
    if (!c || (!variants && max > 0) || max < 0) return VC_ERR_ARG;
    if (!c->img || c->family == VC_MODEL_NONE) return VC_ERR_STATE;
    for (o = 0; o + 12 <= c->img_len; o += 2) {
        const uint8_t *p = c->img + o;
        if (p[2] != 0x4E || p[3] != 0 || p[4] != 0x70 || p[5] != 2 || p[6] != 0x38 || p[7] != 2 || p[8] != 0xEE || p[9] != 2) continue;
        int v = variant_at((vc_calc *)c, FLASH_BASE + o);
        for (k = 0; k < 4; k++) if (ORDER[k] == v) found[k] = 1;
    }
    for (k = 0; k < 4; k++) if (found[k]) { if (n < max) variants[n] = ORDER[k]; n++; }
    return n;
}
int vc_model_variant(const vc_calc *c) { if (!c) return 0; update_active_model((vc_calc *)c); return c->variant ? c->variant : c->model * 10; }
int vc_model(const vc_calc *c) { if (!c) return 0; update_active_model((vc_calc *)c); return c->model; }
const char *vc_firmware_info(const vc_calc *c) { return c ? c->info : ""; }
unsigned vc_capabilities(const vc_calc *c) {
    if (!c || c->family == VC_MODEL_NONE) return 0;
    if (c->family == VC_FAMILY_STOCK) return VC_CAP_KEYS_DISPLAY;
    return VC_CAP_KEYS_DISPLAY | VC_CAP_INSPECT | VC_CAP_MEMORY | VC_CAP_MODEL_SWITCH;
}
int vc_engine(const vc_calc *c) { return c ? c->engine : -1; }
const char *vc_last_error(const vc_calc *c) { return c ? c->err : "no calculator"; }

/* ---------------------------------------------------------------- runner engine (PORT: runner16c.Calc16C) */
static int runner_call(vc_calc *c, uint32_t fn, uint32_t r0, uint64_t max_steps) {
    vc_cpu *cpu = &c->cpu;
    const uint64_t chunk = 20000000;
    c->limit = max_steps ? c->steps + max_steps : (uint64_t)-1 / 2;
    cpu->r[13] = RUN_STACK; cpu->r[14] = RUN_SENT | 1; cpu->r[0] = r0; cpu->r[15] = fn & ~1u;
    for (;;) {
        uint64_t n0 = c->steps;
        int st = cpu_run(cpu, chunk, RUN_SENT);
        if (st == CPU_RUN_FAULT) { snprintf(c->err, sizeof c->err, "%s", cpu->fault); return VC_ERR_FAULT; }
        if (st == CPU_RUN_STOPADDR) return VC_KEY_DONE;
        if (c->steps > c->limit) return VC_KEY_LIMIT;
        if (c->steps == n0) return VC_KEY_HELD;  /* "hold": no Nut progress */
    }
}

static void runner_save(vc_calc *c) { c->snap_cpu = c->cpu; memcpy(c->snap_sram, c->sram, 0x8000); c->have_snap = 1; }
static void runner_restore(vc_calc *c) {
    vc_cpu keep = c->cpu;
    c->cpu = c->snap_cpu;
    /* keep pointers/hooks/counters of the live CPU */
    c->cpu.flash = keep.flash; c->cpu.sram = keep.sram; c->cpu.bus = keep.bus; c->cpu.icount = keep.icount;
    memcpy(c->sram, c->snap_sram, 0x8000);
}

static void setup_memory(vc_calc *c, int whole) {
    vc_cpu *cpu = &c->cpu;
    memset(c->flash, 0, FLASH_SIZE);
    size_t n = c->img_len;
    if (!whole && n > 0x20000 - FLASH_BASE) n = 0x20000 - FLASH_BASE;  /* runner maps flash up to 0x20000 */
    memcpy(c->flash + FLASH_BASE, c->img, n);
    memset(c->sram, 0, 0x10000);
    memset(c->high, 0, 0x10000);
    memset(c->userpage, 0xFF, 0x1000);
    amap_clear(&c->mm);
    cpu->flash = c->flash; cpu->flash_size = whole ? FLASH_SIZE : 0x20000;
    cpu->sram = c->sram; cpu->sram_size = whole ? 0x10000 : 0x8000;
    cpu->bus.ctx = c;
    cpu->bus.read = whole ? whole_read : runner_read;
    cpu->bus.write = whole ? whole_write : runner_write;
    cpu_init(cpu);
}

int vc_start(vc_calc *c, int engine, int cold) {
    if (!c) return VC_ERR_ARG;
    if (c->family == VC_MODEL_NONE) { seterr(c, "no supported firmware loaded"); return VC_ERR_STATE; }
    if (c->family == VC_FAMILY_STOCK && !stock_runnable(c)) { seterr(c, "this HP firmware is recognised but only the original HP-12C has been verified to run"); return VC_ERR_STATE; }
    if (engine == VC_ENGINE_RUNNER && c->family != VC_MODEL_16C) { seterr(c, "the program runner needs an HP-16C CE firmware; use the whole-calculator engine"); return VC_ERR_STATE; }
    c->model = c->family == VC_FAMILY_MULTI || c->family == VC_FAMILY_STOCK ? VC_MODEL_12C : c->family; c->variant = c->family == VC_FAMILY_STOCK ? 120 : 0;
    c->steps = 0; c->prefix = 0; c->have_snap = 0; c->held[0] = 0; c->nframes = 0;
    c->ticks = c->sleeps = c->insn = 0;
    memset(c->keydown, 0, sizeof c->keydown); c->on = 0;
    memset(c->gpio, 0, sizeof c->gpio);
    c->ast_en = c->adc_en = c->adc_conv = c->adc_isr = 0; c->consec_valid = 0;
    c->adc_ie = c->nvic_en2 = 0;
    c->clk_addr = c->family == VC_FAMILY_STOCK ? 0x20000008u : 0x2000085Cu;   /* same variable block, different RAM base per build */
    if (engine == VC_ENGINE_RUNNER) {
        free(c->periph); free(c->scs);
        c->periph = calloc(PERIPH_SIZE, 1); c->scs = calloc(SCS_SIZE, 1);
        if (!c->periph || !c->scs) return VC_ERR_ARG;
        setup_memory(c, 0);
        vc_cpu *cpu = &c->cpu;   /* runner maps only flash, SRAM, peripherals and SCS */
        cpu_add_hook(cpu, A16_CLKSW, hook_clksw, c);
        cpu_add_hook(cpu, A16_FETCH, hook_fetch, c);
        cpu_write(cpu, 0x20000010, 4, A16_ROMBASE);
        cpu_write(cpu, 0x20000014, 4, STRUCT_ADDR);
        memcpy(c->sram + (DUMP_ADDR - SRAM_BASE), HDR, 8);
        c->engine = VC_ENGINE_RUNNER;
        int r = runner_call(c, A16_POWERON, 0, 0);
        if (r < 0) return r;
        if (cold) {
            r = vc_key(c, KEY_ON, 0); if (r >= 0) r = vc_key(c, 24 /* BSP */, 0);
            if (r < 0) return r;
        }
        return VC_OK;
    }
    if (engine == VC_ENGINE_WHOLE) {
        free(c->periph); free(c->scs); c->periph = NULL; c->scs = NULL;
        setup_memory(c, 1);
        vc_cpu *cpu = &c->cpu;
        if (c->paint) cpu_add_hook(cpu, c->paint, hook_paint, c);
        c->limit = (uint64_t)-1 / 2;
        if (c->family == VC_MODEL_16C) cpu_add_hook(cpu, A16_FETCH, hook_fetch, c);   /* only counts Nut instructions */
        c->tick_insn = (uint32_t)(4000000.0 / 100 * c->ipc);   /* 4 MHz until the firmware sets its clock */
        c->adc_value = (uint32_t)(c->battery * 1024 + 0.5);   /* PORT: 12-bit reading, 3072 = 3.0 V */
        memcpy(&c->adc_vec, c->img + (16 + 69) * 4, 4);
        memcpy(&c->tick_vec, c->img + 0xDC, 4);
        uint32_t sp, rst; memcpy(&sp, c->img, 4); memcpy(&rst, c->img + 4, 4);
        cpu->r[13] = sp; cpu->r[15] = rst & ~1u;
        c->engine = VC_ENGINE_WHOLE;
        return vc_run_ms(c, 300); /* boot: power-on code runs, calculator stays off */
    }
    return VC_ERR_ARG;
}

/* ---------------------------------------------------------------- whole engine (PORT: wholecalc.WholeCalc) */
static int whole_isr(vc_calc *c, uint32_t fn) {
    vc_cpu *cpu = &c->cpu;
    uint32_t saved[16]; uint32_t n = cpu->n, z = cpu->z, cc = cpu->c, v = cpu->v, q = cpu->q; uint8_t it = cpu->it;
    memcpy(saved, cpu->r, sizeof saved);
    cpu->r[13] = (cpu->r[13] - 0x100) & ~7u;
    cpu->r[14] = ISR_SENT | 1;
    cpu->r[15] = fn & ~1u;
    cpu->it = 0;
    int st = cpu_run(cpu, 5000000, ISR_SENT);
    uint32_t pc = saved[15];
    memcpy(cpu->r, saved, sizeof saved);
    cpu->r[15] = pc; cpu->n = n; cpu->z = z; cpu->c = cc; cpu->v = v; cpu->q = q; cpu->it = it;
    if (st == CPU_RUN_FAULT) { snprintf(c->err, sizeof c->err, "in interrupt: %s", cpu->fault); return VC_ERR_FAULT; }
    return VC_OK;
}

int vc_run_ms(vc_calc *c, uint32_t ms) {
    if (!c || c->engine != VC_ENGINE_WHOLE) return VC_ERR_STATE;
    vc_cpu *cpu = &c->cpu;
    uint32_t i, steps = ms / 10 ? ms / 10 : 1;
    for (i = 0; i < steps; i++) {
        uint64_t before = cpu->icount;
        int st = cpu_run(cpu, c->tick_insn, 0xFFFFFFFFu);
        if (st == CPU_RUN_FAULT) { snprintf(c->err, sizeof c->err, "%s", cpu->fault); return VC_ERR_FAULT; }
        /* PORT: the reference counts a sleep only for a 16-bit WFI in flash
         * (the reference looks at the halfword before PC, whatever ended the time slice) */
        int slept = 0;
        {
            uint32_t o = cpu->r[15] - 2 - FLASH_BASE;
            slept = o + 2 <= c->img_len && c->img[o] == 0x30 && c->img[o + 1] == 0xBF;
        }
        if (slept) c->sleeps++;
        c->busy = slept ? 0 : c->busy + 1;
        c->insn += cpu->icount - before;
        if (c->ast_en && (cpu->primask == 0 || slept)) {
            /* PORT: _on_tick */
            /* ADC end-of-conversion interrupt, by hardware state (not by a firmware RAM address): a conversion was started,
             * SEOC is enabled in the ADCIFE interrupt mask and IRQ 69 is enabled in the NVIC. */
            if (c->adc_en && c->adc_conv && (c->adc_ie & 1) && (c->nvic_en2 & 0x20)) {
                c->adc_isr = 1;
                int r = whole_isr(c, c->adc_vec); c->adc_isr = 0;
                if (r) return r;
            }
            uint32_t f = rd32(c, c->clk_addr);
            if (f == 4000000 || f == 12000000 || f == 48000000) c->tick_insn = (uint32_t)((double)f / 100 * c->ipc);
            int r = whole_isr(c, c->tick_vec);
            if (r) return r;
            c->ticks++;
        }
    }
    return VC_OK;
}

int vc_set_clock_ipc(vc_calc *c, double ipc) {
    if (!c || !(ipc == 0 || (ipc >= 0.1 && ipc <= 2.0))) return VC_ERR_ARG;
    c->ipc = ipc == 0 ? VC_IPC_REFERENCE : ipc;
    if (c->engine == VC_ENGINE_WHOLE) {
        uint32_t f = rd32(c, c->clk_addr);
        c->tick_insn = (uint32_t)((f == 12000000 || f == 48000000 ? (double)f : 4000000.0) / 100 * c->ipc);
    }
    return VC_OK;
}
double vc_clock_ipc(const vc_calc *c) { return c ? c->ipc : 0; }

int vc_set_battery(vc_calc *c, double volts) {
    if (!c || volts < 0 || volts > 4) return VC_ERR_ARG;
    c->battery = volts; return VC_OK;
}

int vc_press_key(vc_calc *c, int code) {
    if (!c || code < 0 || code >= 40) return VC_ERR_ARG;
    if (c->engine != VC_ENGINE_WHOLE) return VC_ERR_STATE;
    if (code == KEY_ON) c->on = 1; else c->keydown[code] = 1;
    return VC_OK;
}
int vc_release_key(vc_calc *c, int code) {
    if (!c || code < 0 || code >= 40) return VC_ERR_ARG;
    if (c->engine != VC_ENGINE_WHOLE) return VC_ERR_STATE;
    if (code == KEY_ON) c->on = 0; else c->keydown[code] = 0;
    return VC_OK;
}

/* ---------------------------------------------------------------- keys */
int vc_key(vc_calc *c, int code, uint64_t max_steps) {
    if (!c || code < 0 || code >= 40) return VC_ERR_ARG;
    if (c->engine == VC_ENGINE_WHOLE) {
        int r = vc_press_key(c, code); if (r) return r;
        r = vc_run_ms(c, 100); if (r) return r;
        vc_release_key(c, code);
        return vc_run_ms(c, 250);
    }
    if (c->engine != VC_ENGINE_RUNNER) return VC_ERR_STATE;
    /* PORT: Calc16C.press (one token) */
    if (!c->prefix) runner_save(c);
    int r = runner_call(c, A16_HOSTKEY, (uint32_t)code, max_steps);
    if (r == VC_KEY_HELD) {
        vc_get_display(c, c->held, sizeof c->held);
        if (!c->prefix) {
            snprintf(c->err, sizeof c->err, "key %s: the firmware is waiting for a timer or a key release (for example PSE in a program); the runner cannot model that", vc_key_name(16, code));
            return VC_ERR_HOLD;
        }
        runner_restore(c); c->prefix = 0;
        return VC_KEY_HELD;
    }
    if (r == VC_KEY_LIMIT || r < 0) return r;
    c->prefix = (code == KEY_F || code == KEY_G);
    return VC_KEY_DONE;
}

int vc_key_code(int model, const char *name) {
    int i; size_t k;
    keyset ks = keyset_for(model);
    const struct { const char *alias, *name; } *al = ks.alias;
    if (!name || !ks.keys) return -1;
    for (i = 0; i < 40; i++) if (!strcmp(name, ks.keys[i])) return i;
    char up[32]; for (k = 0; k < sizeof up - 1 && name[k]; k++) up[k] = (char)((name[k] >= 'a' && name[k] <= 'z') ? name[k] - 32 : name[k]); up[k] = 0;
    for (k = 0; k < ks.nalias; k++)
        if (!strcmp(name, al[k].alias) || !strcmp(up, al[k].alias)) return vc_key_code(model, al[k].name);
    for (i = 0; i < 40; i++) if (!strcmp(up, ks.keys[i])) return i;
    return -1;
}
const char *vc_key_name(int model, int code) {
    keyset ks = keyset_for(model);
    if (!ks.keys || code < 0 || code >= 40) return "?";
    return ks.keys[code];
}

/* ---------------------------------------------------------------- memory */
int vc_load_memory(vc_calc *c, const uint8_t mem[VC_MEMORY_SIZE]) {
    if (!c || !mem) return VC_ERR_ARG;
    if (c->family == VC_FAMILY_STOCK) { seterr(c, "HP's original firmware does not keep a Continuous Memory image in the layout this app reads, so memory files cannot be loaded into it"); return VC_ERR_UNSUPPORTED; }
    if (c->engine < 0) return VC_ERR_STATE;
    memcpy(c->sram + (DUMP_ADDR - SRAM_BASE), mem, VC_MEMORY_SIZE);
    return VC_OK;
}
int vc_save_memory(vc_calc *c, uint8_t out[VC_MEMORY_SIZE]) {
    if (!c || !out) return VC_ERR_ARG;
    if (c->family == VC_FAMILY_STOCK) { seterr(c, "HP's original firmware does not keep a Continuous Memory image in the layout this app reads, so its memory cannot be saved to a file"); return VC_ERR_UNSUPPORTED; }
    if (c->engine < 0) return VC_ERR_STATE;
    memcpy(out, c->sram + (DUMP_ADDR - SRAM_BASE), VC_MEMORY_SIZE);
    return VC_OK;
}

/* ---------------------------------------------------------------- display */
static uint64_t nut_ram(vc_calc *c, uint32_t addr) { /* PORT: runner16c.nut_ram */
    uint32_t cpu_struct = c->engine == VC_ENGINE_WHOLE ? rd32(c, 0x20000014) : STRUCT_ADDR;
    uint32_t a = addr & 0xFF, base;
    if (c->ext16 && a >= 0x4D && a <= 0xFC) a = (a - 0x2D) & 0xFF;
    uint32_t hi = a >> 4, lo = a & 15;
    if (hi >= 2 && hi <= 0xC) base = 0x20000180u + (hi - 2) * 128;
    else base = cpu_struct + ((uint32_t)c->img[0x1BF00 + hi] << 7) + 0x68;
    uint64_t v = (uint64_t)rd32(c, base + lo * 8) | (uint64_t)rd32(c, base + lo * 8 + 4) << 32;
    return v & ((1ull << 56) - 1);
}

int vc_get_nut_display(vc_calc *c, uint64_t *r9, uint64_t *r10) {
    if (!c || c->engine < 0 || c->family != VC_MODEL_16C) return VC_ERR_STATE;
    if (r9) *r9 = nut_ram(c, 9);
    if (r10) *r10 = nut_ram(c, 10);
    return VC_OK;
}

int vc_get_display(vc_calc *c, char *buf, size_t n) { /* PORT: runner16c.decode_lcd */
    if (!c || !buf || n < 11) return VC_ERR_ARG;
    if (c->engine < 0) return VC_ERR_STATE;
    if (c->family != VC_MODEL_16C) { /* other firmware: text from the LCD */
        vc_display d; int r = vc_get_display_cells(c, &d); if (r) return r;
        return vc_display_text(&d, buf, n);
    }
    uint64_t r9 = nut_ram(c, 9), r10 = nut_ram(c, 10);
    int p, s, j = 0;
    for (p = 9; p >= 0; p--) {
        uint8_t segs = 0;
        for (s = 0; s < 7; s++) {
            int b = SEGMAP[p][s];
            int bit = b >= 56 ? (int)((r9 >> (b - 56)) & 1) : (int)((r10 >> b) & 1);
            if (bit) segs |= (uint8_t)(1 << s);
        }
        char ch = '?'; size_t k;
        for (k = 0; k < sizeof PATTERNS / sizeof PATTERNS[0]; k++) if (PATTERNS[k].segs == segs) { ch = PATTERNS[k].ch; break; }
        buf[j++] = ch;
    }
    buf[j] = 0;
    return VC_OK;
}
int vc_get_held_display(vc_calc *c, char *buf, size_t n) {
    if (!c || !buf || n < 11) return VC_ERR_ARG;
    snprintf(buf, n, "%s", c->held); return VC_OK;
}

int vc_get_lcd_segments(vc_calc *c, uint8_t out[VC_LCD_BYTES]) {
    int i;
    if (!c || !out) return VC_ERR_ARG;
    if (c->engine < 0) return VC_ERR_STATE;
    for (i = 0; i < 8; i++) {
        uint32_t v = 0, a = LCDCA_DR + 4u * (uint32_t)i;
        if (c->engine == VC_ENGINE_WHOLE) amap_get(&c->mm, a, &v); else cpu_read(&c->cpu, a, 4, &v);
        out[4*i] = (uint8_t)v; out[4*i+1] = (uint8_t)(v >> 8); out[4*i+2] = (uint8_t)(v >> 16); out[4*i+3] = (uint8_t)(v >> 24);
    }
    return VC_OK;
}

/* ---------------------------------------------------------------- display cells
 * Map from each display element to its bit in the LCD controller memory (and, for the
 * program runner, to the Nut display register bit the firmware copies it from). Worked out
 * on 2026-10-10 by comparing the two in about 3,300 display states in the simulator (every
 * LCD bit seen matched exactly one Nut bit). Index 0 = leftmost digit. Segments a..g.
 * 255 = never seen lit (no comma after the last three digits on the 16C). */
static const uint8_t LCD_SEG[10][7] = {{194, 131, 66, 65, 129, 193, 130}, {195, 197, 68, 67, 132, 196, 133}, {199, 136, 71, 70, 134, 198, 135}, {200, 202, 73, 72, 137, 201, 138}, {204, 141, 76, 75, 139, 203, 140}, {205, 207, 78, 77, 142, 206, 143}, {209, 146, 81, 80, 144, 208, 145}, {210, 212, 83, 82, 147, 211, 148}, {214, 151, 86, 85, 149, 213, 150}, {215, 217, 88, 87, 152, 216, 153}};
static const uint8_t LCD_DOT[10] = {3, 69, 8, 74, 13, 79, 18, 84, 23, 89};
static const uint8_t LCD_COMMA[10] = {2, 5, 7, 10, 12, 15, 17, 20, 22, 25};   /* the last three are lit only by the all-segments self test (ON + ×) */
/* nut bits for the runner (same order) */
static const uint8_t NUT_SEG[10][7] = {{77, 79, 75, 103, 74, 76, 78}, {83, 85, 81, 73, 80, 82, 84}, {107, 109, 105, 69, 104, 106, 108}, {89, 91, 87, 65, 86, 88, 90}, {99, 101, 97, 63, 96, 98, 100}, {11, 13, 9, 15, 8, 10, 12}, {21, 23, 19, 7, 18, 20, 22}, {29, 31, 27, 39, 26, 28, 30}, {47, 49, 45, 33, 44, 46, 48}, {53, 55, 51, 41, 50, 52, 54}};
static const uint8_t NUT_DOT[10] = {95, 71, 111, 93, 67, 17, 25, 37, 35, 43};
static const uint8_t NUT_COMMA[10] = {94, 70, 110, 92, 66, 16, 24, 255, 255, 255};
#define LCD_MINUS 1
#define NUT_MINUS 102

uint32_t vc_get_annunciators(vc_calc *c);
static int lcd_bit(const uint8_t *seg, int b) { return b < 256 && (seg[b >> 3] >> (b & 7)) & 1; }

int vc_get_display_cells(vc_calc *c, vc_display *d) {
    uint8_t seg[VC_LCD_BYTES]; int p, s;
    if (!c || !d) return VC_ERR_ARG;
    if (c->engine < 0) return VC_ERR_STATE;
    memset(d, 0, sizeof *d);
    if (c->engine == VC_ENGINE_RUNNER) {
        uint64_t r9 = nut_ram(c, 9), r10 = nut_ram(c, 10);
#define NB(b) ((b) == 255 ? 0 : (b) >= 56 ? (int)((r9 >> ((b) - 56)) & 1) : (int)((r10 >> (b)) & 1))
        for (p = 0; p < 10; p++) {
            for (s = 0; s < 7; s++) if (NB(NUT_SEG[p][s])) d->seg[p] |= (uint8_t)(1 << s);
            d->dot[p] = (uint8_t)NB(NUT_DOT[p]); d->comma[p] = (uint8_t)NB(NUT_COMMA[p]);
        }
        d->minus = (uint8_t)NB(NUT_MINUS);
#undef NB
        /* annunciators: use the LCD memory the firmware keeps up to date for them */
    }
    vc_get_lcd_segments(c, seg);
    if (c->engine == VC_ENGINE_WHOLE) {
        for (p = 0; p < 10; p++) {
            for (s = 0; s < 7; s++) if (lcd_bit(seg, LCD_SEG[p][s])) d->seg[p] |= (uint8_t)(1 << s);
            d->dot[p] = (uint8_t)lcd_bit(seg, LCD_DOT[p]); d->comma[p] = (uint8_t)lcd_bit(seg, LCD_COMMA[p]);
        }
        d->minus = (uint8_t)lcd_bit(seg, LCD_MINUS);
    }
    /* every LCD bit 0..63 that is not a digit, point, comma or the minus sign */
    uint64_t lo = 0; int i;
    for (i = 0; i < 64; i++) if (lcd_bit(seg, i)) lo |= 1ull << i;
    lo &= ~(1ull << LCD_MINUS);
    for (p = 0; p < 10; p++) { if (LCD_DOT[p] < 64) lo &= ~(1ull << LCD_DOT[p]); if (LCD_COMMA[p] < 64) lo &= ~(1ull << LCD_COMMA[p]); }
    d->ann_bits = lo;
    d->annunciators = vc_get_annunciators(c);
    return VC_OK;
}

int vc_display_text(const vc_display *d, char *buf, size_t n) {
    size_t j = 0; int p;
    if (!d || !buf || n < 2) return VC_ERR_ARG;
    if (d->minus && j + 1 < n) buf[j++] = '-';
    for (p = 0; p < 10 && j + 1 < n; p++) {
        char ch = '?'; size_t k;
        for (k = 0; k < sizeof PATTERNS / sizeof PATTERNS[0]; k++) if (PATTERNS[k].segs == d->seg[p]) { ch = PATTERNS[k].ch; break; }
        buf[j++] = ch;
        if (d->comma[p] && j + 1 < n) buf[j++] = ',';
        else if (d->dot[p] && j + 1 < n) buf[j++] = '.';
    }
    buf[j] = 0;
    return VC_OK;
}

/* Annunciator segment bits in the LCD controller memory (bit index into the 32 bytes).
 * Found by experiment with the simulator; see README. */
static const struct { int bit; uint32_t flag; } ANN16[] = {
    {6, VC_ANN_F}, {9, VC_ANN_G}, {21, VC_ANN_C}, {14, VC_ANN_G_OV}, {24, VC_ANN_PRGM},
    /* low battery: not mapped yet (the simulated battery reading did not light a segment) */
};
uint32_t vc_get_annunciators(vc_calc *c) {
    uint8_t seg[32]; uint32_t f = 0; size_t i;
    if (vc_get_lcd_segments(c, seg)) return 0;
    for (i = 0; i < sizeof ANN16 / sizeof ANN16[0]; i++) if (seg[ANN16[i].bit >> 3] >> (ANN16[i].bit & 7) & 1) f |= ANN16[i].flag;
    return f;
}

int vc_last_frame(vc_calc *c, char *buf, size_t n) { /* PORT: wholecalc.cells */
    if (!c || !buf || n < 2) return VC_ERR_ARG;
    buf[0] = 0;
    if (!c->nframes) return VC_OK;
    const char *rec = c->frames[(c->nframes - 1) % MAX_FRAMES];
    size_t j = 0; int i;
    for (i = 0; i < 20 && j + 2 < n; i += 2) {
        char g = rec[i], sp = rec[i + 1];
        buf[j++] = g;
        if (sp == '.' || sp == ',') buf[j++] = sp;
    }
    while (j > 0 && buf[j - 1] == ' ') j--;
    buf[j] = 0;
    return VC_OK;
}
uint32_t vc_frame_count(const vc_calc *c) { return c ? c->nframes : 0; }

void vc_get_stats(const vc_calc *c, vc_stats *o) {
    if (!c || !o) return;
    o->arm_instructions = c->cpu.icount;
    o->nut_instructions = c->steps;
    o->sim_ms = c->ticks * 10;
    o->sleeps = c->sleeps;
}

/* ---------------------------------------------------------------- for inspect.c (read-only) */
const uint8_t *vci_dump(const vc_calc *c) { return c && c->engine >= 0 && c->sram && c->family != VC_FAMILY_STOCK ? c->sram + (DUMP_ADDR - SRAM_BASE) : NULL; }
uint32_t vci_rom_base(const vc_calc *c) {
    if (!c || c->engine < 0) return 0;
    if (c->engine != VC_ENGINE_WHOLE) return A16_ROMBASE;
    const uint8_t *p = c->sram + 0x10;
    return (uint32_t)p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}
int vci_rom_word(const vc_calc *c, uint32_t rb, uint32_t i) { return rom_word((vc_calc *)c, rb, i); }
int vci_flash_u16(const vc_calc *c, uint32_t addr) {
    if (!c || !c->img || addr < FLASH_BASE || addr - FLASH_BASE + 1 >= c->img_len) return -1;
    return c->img[addr - FLASH_BASE] | c->img[addr - FLASH_BASE + 1] << 8;
}
int vci_ext16(const vc_calc *c) { return c ? c->ext16 : 0; }
/* The 16C memory layout of a 16C Nut ROM at flash address rb in a multi / 3-ROM image (see VC_MEM16_*).
 * The Path-C ROM (Nut word 0x1D4 = 0x15C) means extended memory. A multi tells E3E3 from E8E8 by the
 * eight register-address sites of the wrapper: E3E3 left them as `add.w rd, r2, rn, lsl #7` (first
 * halfword 0xEB02); E8E8 and later branch to caves that give the B7B7 offsets (flash 0x1318A is the first). */
static int mem16_of_rom(const vc_calc *c, uint32_t rb) {
    if (vci_rom_word(c, rb, 0x1D4) != 0x15C) return VC_MEM16_STD;
    return vci_flash_u16(c, 0x1318A) == 0xEB02 ? VC_MEM16_EXT_E3E3 : VC_MEM16_EXT;
}
int vc_mem16_layout(const vc_calc *c) {
    if (!c || vc_model(c) != VC_MODEL_16C) return VC_MEM16_NONE;
    if (c->family == VC_MODEL_16C) return c->ext16 ? VC_MEM16_EXT : VC_MEM16_STD;
    return mem16_of_rom(c, vci_rom_base(c));
}
/* Same, for the firmware file: the layout its 16C memory has, whatever model is active now. */
int vc_firmware_mem16(const vc_calc *c) {
    uint32_t o;
    if (!c || !c->img || c->family == VC_MODEL_NONE || c->family == VC_FAMILY_STOCK) return VC_MEM16_NONE;
    if (c->family == VC_MODEL_16C) return c->ext16 ? VC_MEM16_EXT : VC_MEM16_STD;
    for (o = 0; o + 12 <= c->img_len; o += 2) {
        const uint8_t *p = c->img + o;
        if (p[2] != 0x4E || p[3] != 0 || p[4] != 0x70 || p[5] != 2 || p[6] != 0x38 || p[7] != 2 || p[8] != 0xEE || p[9] != 2) continue;
        if (variant_at((vc_calc *)c, FLASH_BASE + o) == 160) return mem16_of_rom(c, FLASH_BASE + o);
    }
    return VC_MEM16_NONE;
}
uint32_t vci_busy_ticks(const vc_calc *c) { return c && c->engine == VC_ENGINE_WHOLE ? c->busy : 0; }
int vci_key_held(const vc_calc *c) {
    int i; if (!c) return 0;
    if (c->on) return 1;
    for (i = 0; i < 40; i++) if (c->keydown[i]) return 1;
    return 0;
}
