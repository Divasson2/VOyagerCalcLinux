/* test_firmware.c - firmware loading: sizes, full flash dumps, checksum, error messages, recognition by
 * code shape. Synthetic images only (built here from instruction encodings and ROM header words);
 * no HP firmware is needed or included. With VC_FIRMWARE_DIR=<folder of .bin> it also checks that every
 * real firmware there is classified the same inside a full flash dump (bootloader + file).
 * SPDX-License-Identifier: MIT */
#include "voyager_core.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define IMG 0x1C000u
static int fails, checks;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* Synthetic stock-like test program (written for this test; assembled with clang --target=thumbv7m, no HP code).
 * Flash 0x6000 = reset, 0x6100 = AST handler (IRQ 39), 0x6180 = ADC handler (IRQ 69), 0x61F0 = config words
 * {IER value, NVIC ISER2 value, clock value (0 = none), clock address, RAM block base}.
 * Block: +1 byte "ADC pending", +4 last ADC value, +0x10 AST ticks, +0x14 completed conversions, +0x18 last tick seen.
 *
   .syntax unified
   .thumb
   .text
   .thumb_func
   reset:
     ldr r0,=0xE000E104
     movs r1,#0x80
     str r1,[r0]
     ldr r5,=0x61F0
     ldr r0,=0x40038000
     movs r1,#1
     lsls r1,r1,#8
     str r1,[r0]
     ldr r1,[r5,#0]
     str r1,[r0,#0x30]
     ldr r0,=0xE000E108
     ldr r1,[r5,#4]
     str r1,[r0]
     ldr r1,[r5,#8]
     cmp r1,#0
     beq 1f
     ldr r0,[r5,#12]
     str r1,[r0]
   1:
     ldr r4,[r5,#16]
   main:
     wfi
     ldr r1,[r4,#0x10]
     ldr r2,[r4,#0x18]
     cmp r1,r2
     beq main
     str r1,[r4,#0x18]
     movs r1,#1
     strb r1,[r4,#1]
     ldr r0,=0x40038000
     movs r1,#8
     str r1,[r0]
   wait:
     ldrb r1,[r4,#1]
     cmp r1,#0
     bne wait
     ldr r1,[r4,#0x14]
     adds r1,#1
     str r1,[r4,#0x14]
     b main
   .ltorg
   .org 0x100
   .thumb_func
   ast_handler:
     ldr r0,=0x61F0
     ldr r0,[r0,#16]
     ldr r1,[r0,#0x10]
     adds r1,#1
     str r1,[r0,#0x10]
     bx lr
   .ltorg
   .org 0x180
   .thumb_func
   adc_handler:
     ldr r0,=0x40038000
     ldr r1,[r0,#0x2C]
     ldr r2,=0x61F0
     ldr r2,[r2,#16]
     str r1,[r2,#4]
     movs r1,#1
     str r1,[r0,#0xC]
     movs r1,#0
     strb r1,[r2,#1]
     bx lr
   .ltorg
   .org 0x1F0
   cfg: .word 0,0,0,0,0
 */
static const uint8_t SYN_MAIN[88] = {
    0x12,0x48,0x80,0x21,0x01,0x60,0x46,0xF2,0xF0,0x15,0x11,0x48,0x01,0x21,0x09,0x02,0x01,0x60,0x29,0x68,0x01,0x63,
    0x0F,0x48,0x69,0x68,0x01,0x60,0xA9,0x68,0x00,0x29,0x01,0xD0,0xE8,0x68,0x01,0x60,0x2C,0x69,0x30,0xBF,0x21,0x69,
    0xA2,0x69,0x91,0x42,0xFA,0xD0,0xA1,0x61,0x01,0x21,0x61,0x70,0x05,0x48,0x08,0x21,0x01,0x60,0x61,0x78,0x00,0x29,
    0xFC,0xD1,0x61,0x69,0x01,0x31,0x61,0x61,0xED,0xE7,0x04,0xE1,0x00,0xE0,0x00,0x80,0x03,0x40,0x08,0xE1,0x00,0xE0 };
static const uint8_t SYN_AST[16] = {
    0x46,0xF2,0xF0,0x10,0x00,0x69,0x01,0x69,0x01,0x31,0x01,0x61,0x70,0x47,0x00,0x00 };
static const uint8_t SYN_ADC[28] = {
    0x05,0x48,0xC1,0x6A,0x46,0xF2,0xF0,0x12,0x12,0x69,0x51,0x60,0x01,0x21,0xC1,0x60,0x00,0x21,0x51,0x70,0x70,0x47,
    0x00,0x00,0x00,0x80,0x03,0x40 };

static void put16(uint8_t *p, unsigned v) { p[0] = v & 255; p[1] = v >> 8; }
static void put32(uint8_t *p, uint32_t v) { put16(p, v & 0xFFFF); put16(p + 2, v >> 16); }
/* A valid-looking image: vector table, optional ROM header (word 5 = w5, 0 = none), optional paint shape,
 * optional ROM pointer literal. The unused bytes are 0xFF like erased flash. */
static uint8_t *make(int w5, int paint_variant, int pointer, int target_checksum) {
    uint8_t *d = malloc(IMG); memset(d, 0xFF, IMG);
    put32(d, 0x200013C0); put32(d + 4, 0x000110EF);
    for (int i = 2; i < 48; i++) put32(d + 4 * i, 0x00011021);
    if (w5) {
        uint8_t *r = d + 0x480C - 0x4000;                    /* word 0, then 0x04E 0x270 0x238 0x2EE, word 5 */
        put16(r, 0x107); put16(r + 2, 0x04E); put16(r + 4, 0x270); put16(r + 6, 0x238); put16(r + 8, 0x2EE); put16(r + 10, (unsigned)w5);
    }
    if (paint_variant) {
        uint8_t *p = d + (paint_variant == 1 ? 0x1CDBA : 0xE1CA) - 0x4000;   /* variant 1 = the exact known routine at the 15C CE address */
        static const unsigned base[15] = {0xB538, 0x4604, 0x2500, 0x5D61, 0x4628, 0xF7FF, 0xFDC2, 0x1C6D, 0x2D14, 0xDBF8, 0xBD31, 0x488D, 0x7800, 0x28FF, 0xD009};
        for (int k = 0; k < 15; k++) put16(p + 2 * k, base[k]);
        if (paint_variant == 2) { put16(p + 6, 0x5D29); put16(p + 10, 0xF7F1); put16(p + 12, 0xFE55); put16(p + 22, 0x4875); }  /* other registers, branch, literal */
        if (paint_variant == 3) put16(p + 4, 0x2501);        /* NOT the shape (movs r5,#1) */
    }
    if (pointer) put32(d + 0xD7C8 - 0x4000, 0x20000010);
    if (target_checksum >= 0) {                              /* checksum = (sum - byte@0x1BFEF) & 255: adjust a filler byte */
        d[0x1BFEF] = 0x5A;
        uint32_t sum = 0; for (unsigned i = 0; i < IMG; i++) sum += d[i];
        int cur = (int)((sum - d[0x1BFEF]) & 255);
        d[0x1B000] = (uint8_t)(d[0x1B000] + target_checksum - cur);
    }
    return d;
}
static int expect_sum(const uint8_t *d) { uint32_t s = 0; for (unsigned i = 0; i < IMG; i++) s += d[i]; return (int)((s - d[0x1BFEF]) & 255); }
static uint8_t *dump(const uint8_t *img, size_t n, uint8_t bootfill, size_t total) {   /* full flash dump */
    uint8_t *f = malloc(total); memset(f, 0xFF, total);
    memset(f, bootfill, 0x4000);
    put32(f, 0x20000800); put32(f + 4, 0x000001C1);          /* the bootloader's own vector table: reset below 0x4000 */
    memcpy(f + 0x4000, img, n); return f;
}
static int load(const uint8_t *d, size_t n, vc_calc **out) { vc_calc *c = vc_create(); *out = c; return vc_load_firmware(c, d, n); }
static int has(const char *s, const char *sub) { return strstr(s, sub) != NULL; }

int main(void) {
    {   /* CPU speed knob: the default keeps the reference's 0.75 (parity); range checked */
        vc_calc *k = vc_create();
        CHECK(vc_clock_ipc(k) == VC_IPC_REFERENCE, "default ipc %g", vc_clock_ipc(k));
        CHECK(vc_set_clock_ipc(k, 0.05) == VC_ERR_ARG && vc_set_clock_ipc(k, 3) == VC_ERR_ARG, "ipc range");
        CHECK(vc_set_clock_ipc(k, VC_IPC_REAL) == VC_OK && vc_clock_ipc(k) == VC_IPC_REAL, "ipc set");
        CHECK(vc_set_clock_ipc(k, 0) == VC_OK && vc_clock_ipc(k) == VC_IPC_REFERENCE, "ipc 0 = default");
        vc_destroy(k);
    }
    vc_calc *c; int r;
    /* ---- exact image, loose code shape + ROM pointer: recognised as a CE firmware, 12C ROM */
    uint8_t *a = make(0x14F, 2, 1, 0xEF);
    r = load(a, IMG, &c);
    CHECK(r == VC_OK, "shape + pointer: %d %s", r, vc_last_error(c));
    CHECK(vc_firmware_family(c) == VC_MODEL_15C, "family %d", vc_firmware_family(c));
    CHECK(vc_firmware_checksum(c) == 0xEF && expect_sum(a) == 0xEF, "checksum %d", vc_firmware_checksum(c));
    CHECK(has(vc_firmware_info(c), "HP-12C CE firmware") && has(vc_firmware_info(c), "structure"), "info: %s", vc_firmware_info(c));
    CHECK(vc_firmware_stripped(c) == 0, "stripped");
    vc_destroy(c);
    /* the exact known routine at its 15C CE address: the old (fingerprint) route still works */
    uint8_t *a1 = make(0x10F, 1, 1, 0x31);
    r = load(a1, IMG, &c); CHECK(r == VC_OK && vc_firmware_family(c) == VC_MODEL_15C, "variant 1: %d", r); vc_destroy(c);
    /* a shape that is not the paint routine is not recognised, and the message says what was found */
    uint8_t *n3 = make(0x14F, 3, 1, 0x44);
    r = load(n3, IMG, &c);
    CHECK(r == VC_ERR_FIRMWARE && vc_firmware_family(c) == VC_MODEL_NONE, "not the shape: %d", r);
    CHECK(has(vc_last_error(c), "valid firmware image") && has(vc_last_error(c), "4444") && has(vc_last_error(c), "12C"), "msg: %s", vc_last_error(c));
    vc_destroy(c);

    /* ---- HP's original layout (no ROM pointer), 12C ROM: recognised and runnable, with limits */
    uint8_t *s = make(0x14F, 2, 0, 0xD1);
    r = load(s, IMG, &c);
    CHECK(r == VC_OK, "stock 12C loads: %d %s", r, vc_last_error(c));
    CHECK(vc_firmware_family(c) == VC_FAMILY_STOCK, "stock family %d", vc_firmware_family(c));
    CHECK(has(vc_firmware_info(c), "original HP-12C") && has(vc_firmware_info(c), "no inspector"), "info %s", vc_firmware_info(c));
    CHECK(vc_firmware_checksum(c) == 0xD1, "checksum");
    CHECK(vc_capabilities(c) == VC_CAP_KEYS_DISPLAY, "stock capabilities %u", vc_capabilities(c));
    CHECK(vc_model(c) == VC_MODEL_12C && vc_model_variant(c) == 120, "stock model %d/%d", vc_model(c), vc_model_variant(c));
    { int v[4]; CHECK(vc_firmware_images(c, v, 4) == 1 && v[0] == 120, "stock images"); }
    CHECK(vc_start(c, VC_ENGINE_RUNNER, 0) == VC_ERR_STATE, "stock has no program runner");
    /* (this synthetic image has no code, so it faults when it runs; what matters is that start is not refused) */
    r = vc_start(c, VC_ENGINE_WHOLE, 0);
    CHECK(r == VC_ERR_FAULT && !has(vc_last_error(c), "not been verified") && !has(vc_last_error(c), "recognised but"), "a stock 12C image is not refused: %d %s", r, vc_last_error(c));
    CHECK(vc_model(c) == VC_MODEL_12C, "stock model after start");
    { uint8_t mem[VC_MEMORY_SIZE] = {0}; vc_inspect_info ii; vc_value vv;
      CHECK(vc_save_memory(c, mem) == VC_ERR_UNSUPPORTED && has(vc_last_error(c), "Continuous Memory"), "stock save: %s", vc_last_error(c));
      CHECK(vc_load_memory(c, mem) == VC_ERR_UNSUPPORTED, "stock load memory");
      CHECK(vc_inspect(c, &ii) < 0 && vc_inspect_stack(c, VC_REG_X, &vv) < 0 && vc_inspect_register(c, 0, &vv) < 0, "stock: nothing to inspect"); }
    vc_destroy(c);
    uint8_t *s2 = make(0x14F, 2, 0, 0x77);               /* other checksum: same family, also a 12C ROM */
    r = load(s2, IMG, &c);
    CHECK(r == VC_OK && vc_firmware_family(c) == VC_FAMILY_STOCK && vc_firmware_checksum(c) == 0x77, "other 12C stock: %d", r);
    vc_destroy(c);
    uint8_t *s3 = make(0x10F, 2, 0, 0x33);               /* original layout, 15C ROM: recognised, not verified, not run */
    r = load(s3, IMG, &c);
    CHECK(r == VC_ERR_UNSUPPORTED && vc_firmware_family(c) == VC_FAMILY_STOCK, "stock 15C: %d", r);
    CHECK(has(vc_last_error(c), "not been verified") && has(vc_last_error(c), "15C") && has(vc_last_error(c), "3333") && !has(vc_last_error(c), "never reports"), "msg: %s", vc_last_error(c));
    CHECK(vc_capabilities(c) == VC_CAP_KEYS_DISPLAY && vc_start(c, VC_ENGINE_WHOLE, 0) == VC_ERR_STATE, "unverified stock must not start");
    vc_destroy(c);
    CHECK(vc_capabilities(NULL) == 0, "no calculator");

    /* ---- CE builds keep every capability */
    r = load(a, IMG, &c);
    CHECK(vc_capabilities(c) == (VC_CAP_KEYS_DISPLAY | VC_CAP_INSPECT | VC_CAP_MEMORY | VC_CAP_MODEL_SWITCH), "CE capabilities %u", vc_capabilities(c));
    vc_destroy(c);

    /* ---- the ADC end-of-conversion interrupt follows the hardware state, not a RAM address (the 0.6.5 stock-12C bug).
     * A tiny synthetic Thumb program (source in the comment above SYN_MAIN): enables the AST tick, ADCIFE and its IER,
     * NVIC IRQ 69, then for every tick sets a "pending" byte, starts a conversion (CR.STRIG) and waits for the ADC
     * handler to clear the byte. Its RAM block base is a parameter, so the same code runs in the stock and the CE layout. */
    for (int k = 0; k < 8; k++) {
        static const struct { int stock; uint32_t block; uint32_t ier, nvic, clk, clkaddr; int expect_run; const char *what; } T[8] = {
            {1, 0x20000000, 1, 0x20, 0,        0,          1, "stock layout (flag at 0x20000001)"},
            {1, 0x20000100, 1, 0x20, 0,        0,          1, "stock, block elsewhere"},
            {0, 0x20000854, 1, 0x20, 0,        0,          1, "CE layout (flag at 0x20000855)"},
            {0, 0x20000000, 1, 0x20, 0,        0,          1, "CE family, block at the stock address"},
            {1, 0x20000100, 0, 0x20, 0,        0,          0, "ADC interrupt not enabled in IER: never delivered"},
            {1, 0x20000100, 1, 0,    0,        0,          0, "IRQ 69 not enabled in the NVIC: never delivered"},
            {1, 0x20000100, 1, 0x20, 12000000, 0x20000008, 1, "stock clock variable at 0x20000008 sets the tick length"},
            {0, 0x20000854, 1, 0x20, 12000000, 0x2000085C, 1, "CE clock variable at 0x2000085C sets the tick length"},
        };
        uint8_t *im = T[k].stock ? make(0x14F, 2, 0, 0xD1) : make(0x14F, 2, 1, 0xEF);
        uint8_t *code = im + (0x6000 - 0x4000);
        memcpy(code, SYN_MAIN, sizeof SYN_MAIN); memcpy(code + 0x100, SYN_AST, sizeof SYN_AST); memcpy(code + 0x180, SYN_ADC, sizeof SYN_ADC);
        put32(code + 0x1F0, T[k].ier); put32(code + 0x1F4, T[k].nvic); put32(code + 0x1F8, T[k].clk);
        put32(code + 0x1FC, T[k].clkaddr); put32(code + 0x200, T[k].block);
        put32(im + 4, 0x6001); put32(im + 4 * (16 + 39), 0x6101); put32(im + 4 * (16 + 69), 0x6181);
        r = load(im, IMG, &c);
        CHECK(r == VC_OK && vc_firmware_family(c) == (T[k].stock ? VC_FAMILY_STOCK : VC_MODEL_15C), "syn %d load %d", k, r);
        CHECK(vc_start(c, VC_ENGINE_WHOLE, 0) == VC_OK, "syn %d start: %s", k, vc_last_error(c));
        CHECK(vc_run_ms(c, 1000) == VC_OK, "syn %d run", k);
        uint32_t ticks = vc_debug_read32(c, T[k].block + 0x10), done = vc_debug_read32(c, T[k].block + 0x14);
        CHECK(ticks > 50, "syn %d: AST ticks %u", k, ticks);
        if (T[k].expect_run) CHECK(done >= 20, "syn %d (%s): conversions completed %u", k, T[k].what, done);
        else CHECK(done == 0, "syn %d (%s): completed %u, expected none", k, T[k].what, done);
        if (T[k].clk) {
            vc_stats st; vc_get_stats(c, &st);
            CHECK(st.arm_instructions / (st.sim_ms / 10) > 40000, "syn %d (%s): %llu instructions per tick", k, T[k].what, (unsigned long long)(st.arm_instructions / (st.sim_ms / 10)));
        } else {
            vc_stats st; vc_get_stats(c, &st);
            CHECK(st.arm_instructions / (st.sim_ms / 10) < 35000, "syn %d: default tick length, %llu per tick", k, (unsigned long long)(st.arm_instructions / (st.sim_ms / 10)));
        }
        vc_destroy(c); free(im);
    }

    /* ---- full flash dumps: bootloader dropped, same result as the bare image */
    for (int k = 0; k < 3; k++) {
        size_t total = k == 2 ? 0x40000 : 0x20000;
        uint8_t *f = dump(a, IMG, k == 1 ? 0x00 : 0xFF, total);
        r = load(f, total, &c);
        CHECK(r == VC_OK && vc_firmware_stripped(c) == 0x4000 && vc_firmware_checksum(c) == 0xEF && vc_firmware_family(c) == VC_MODEL_15C,
              "dump k=%d: %d stripped %u ck %d %s", k, r, vc_firmware_stripped(c), vc_firmware_checksum(c), vc_last_error(c));
        vc_destroy(c); free(f);
    }
    { uint8_t *f = dump(s, IMG, 0xFF, 0x20000); r = load(f, 0x20000, &c);
      CHECK(r == VC_OK && vc_firmware_family(c) == VC_FAMILY_STOCK && vc_firmware_stripped(c) == 0x4000 && vc_firmware_checksum(c) == 0xD1, "stock inside a dump: %d %s", r, vc_last_error(c)); vc_destroy(c); free(f); }
    /* a bare image padded to a full flash size is used as it is */
    { uint8_t *p = malloc(0x20000); memset(p, 0xFF, 0x20000); memcpy(p, a, IMG);
      r = load(p, 0x20000, &c); CHECK(r == VC_OK && vc_firmware_stripped(c) == 0, "padded: %d", r); vc_destroy(c); free(p); }

    /* ---- plain-language errors */
    r = load(a, 0, &c); CHECK(r == VC_ERR_FIRMWARE && has(vc_last_error(c), "empty"), "empty: %s", vc_last_error(c)); vc_destroy(c);
    r = load(a, 2048, &c); CHECK(r == VC_ERR_FIRMWARE && has(vc_last_error(c), "2048 bytes") && has(vc_last_error(c), "114,688"), "2048: %s", vc_last_error(c)); vc_destroy(c);
    r = load(a, IMG - 1, &c); CHECK(r == VC_ERR_FIRMWARE && has(vc_last_error(c), "114687 bytes") && has(vc_last_error(c), "cut short"), "short: %s", vc_last_error(c)); vc_destroy(c);
    { uint8_t *big = calloc(0x50000, 1); r = load(big, 0x50000, &c);
      CHECK(r == VC_ERR_FIRMWARE && has(vc_last_error(c), "larger than"), "big: %s", vc_last_error(c)); vc_destroy(c); free(big); }
    { uint8_t *g = calloc(IMG, 1); memset(g, 'x', IMG); r = load(g, IMG, &c);
      CHECK(r == VC_ERR_FIRMWARE && has(vc_last_error(c), "ARM vector table") && has(vc_last_error(c), "114,688-byte"), "text: %s", vc_last_error(c)); vc_destroy(c);
      r = load(g, 0x20000 > IMG ? 0x20000 : IMG, &c); (void)r; vc_destroy(c);
      uint8_t *f = calloc(0x20000, 1); memset(f, 0xFF, 0x20000); r = load(f, 0x20000, &c);
      CHECK(r == VC_ERR_FIRMWARE && has(vc_last_error(c), "full flash dump") && has(vc_last_error(c), "0x4000"), "dump without firmware: %s", vc_last_error(c)); vc_destroy(c); free(f); free(g); }
    { uint8_t *nr = make(0, 0, 0, 0x12); r = load(nr, IMG, &c);
      CHECK(r == VC_ERR_FIRMWARE && has(vc_last_error(c), "no HP calculator ROM") && has(vc_last_error(c), "1212"), "no ROM: %s", vc_last_error(c)); vc_destroy(c); free(nr); }
    for (int i = 0; i < 4; i++) { /* every message fits the buffer and ends cleanly */
        size_t sizes[4] = {5, 0x30000, 0x20000, IMG};
        uint8_t *b = calloc(0x40000, 1); r = load(b, sizes[i], &c); CHECK(r != VC_OK && strlen(vc_last_error(c)) > 40 && strlen(vc_last_error(c)) < 639, "len"); vc_destroy(c); free(b);
    }

    /* ---- optional: real firmware folder; each file must classify the same inside a full flash dump */
    const char *dir = getenv("VC_FIRMWARE_DIR");
    if (dir) {
        DIR *dp = opendir(dir); struct dirent *e; int n = 0;
        while (dp && (e = readdir(dp))) {
            size_t L = strlen(e->d_name);
            if (L < 5 || strcmp(e->d_name + L - 4, ".bin")) continue;
            char path[1024]; snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
            FILE *fp = fopen(path, "rb"); if (!fp) continue;
            static uint8_t b[0x40000]; size_t m = fread(b, 1, sizeof b, fp); fclose(fp);
            if (m != IMG) continue;
            vc_calc *c1, *c2; int r1 = load(b, m, &c1);
            uint8_t *f = dump(b, m, 0xFF, 0x20000); int r2 = load(f, 0x20000, &c2);
            CHECK(r1 == r2 && vc_firmware_family(c1) == vc_firmware_family(c2) && vc_firmware_checksum(c1) == vc_firmware_checksum(c2) &&
                  (r1 != VC_OK || vc_firmware_stripped(c2) == 0x4000), "%s: bare %d dump %d", e->d_name, r1, r2);
            CHECK(vc_firmware_checksum(c1) == expect_sum(b), "%s checksum", e->d_name);
            if (r1 == VC_OK) { n++; }
            vc_destroy(c1); vc_destroy(c2); free(f);
        }
        if (dp) closedir(dp);
        printf("real firmware folder: %d accepted files checked bare and inside a dump\n", n);
    }
    free(a); free(a1); free(n3); free(s); free(s2); free(s3);
    printf("test_firmware: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
