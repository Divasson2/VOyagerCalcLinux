/* test_mem16.c - 0.7.7: the two supported 16C memory layouts (stock 203 B at 0x6F0, extended 206 slots at 0x168)
 * on real firmware: detection, the inspector reading each, the structural pre-check, stock <-> 206 conversion,
 * and the retired intermediate builds (B7B7, E3E3, E8E8) reported as unsupported, never as a layout.
 * Firmware files are looked up by environment variable (ST, E6, EA, B7, E3, E8) and skipped when absent.
 * SPDX-License-Identifier: MIT */
#include "voyager_core.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static int fails, checks;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
static const char *env(const char *k, const char *d) { const char *v = getenv(k); return v && *v ? v : d; }

static const char *text(vc_calc *c) { static char t[64]; vc_display d; vc_get_display_cells(c, &d); vc_display_text(&d, t, sizeof t); return t; }
static void tap(vc_calc *c, int k) { vc_press_key(c, k); vc_run_ms(c, 120); vc_release_key(c, k); vc_run_ms(c, 600); }
static void keys(vc_calc *c, const char *s) {
    char w[32];
    while (*s) {
        while (*s == ' ') s++;
        size_t n = strcspn(s, " "); if (!n) break;
        if (n >= sizeof w) n = sizeof w - 1;
        memcpy(w, s, n); w[n] = 0; s += n;
        int k = vc_key_code(16, w);
        if (k >= 0) { tap(c, k); continue; }
        for (char *p = w; *p; p++) { char one[2] = {*p, 0}; k = vc_key_code(16, one); if (k < 0) { printf("  bad key %s\n", one); fails++; } else tap(c, k); }
    }
}
static int sw(vc_calc *c, int pick) {
    tap(c, 30); vc_run_ms(c, 500);
    if (strspn(text(c), " ") != strlen(text(c))) { tap(c, 30); vc_run_ms(c, 500); }
    vc_press_key(c, 32); vc_press_key(c, 25); vc_run_ms(c, 100); vc_press_key(c, 30); vc_run_ms(c, 300);
    vc_release_key(c, 30); vc_run_ms(c, 200); vc_release_key(c, 25); vc_release_key(c, 32); vc_run_ms(c, 800);
    tap(c, 16); tap(c, pick); vc_run_ms(c, 2000); tap(c, 24);
    return vc_model(c);
}
/* open a firmware in the whole engine, in 16C mode; NULL when the file is missing */
static vc_calc *open16(const char *path, int multi) {
    FILE *f = fopen(path, "rb"); if (!f) { printf("  SKIP (no %s)\n", path); return NULL; }
    static uint8_t b[0x80000]; size_t n = fread(b, 1, sizeof b, f); fclose(f);
    vc_calc *c = vc_create();
    if (vc_load_firmware(c, b, n) || vc_start(c, VC_ENGINE_WHOLE, 1)) { printf("  FAIL cannot start %s\n", path); fails++; vc_destroy(c); return NULL; }
    tap(c, 30); vc_run_ms(c, 1500);
    if (multi) { CHECK(sw(c, 5) == 16, "%s: switch to 16C", path); }
    else { tap(c, 30); vc_run_ms(c, 500); tap(c, 30); vc_run_ms(c, 800); }
    return c;
}
/* what the inspector shows: info, X..T, I, registers, program (one text, to compare two calculators) */
static void snap(vc_calc *c, char *out, size_t cap) {
    vc_inspect_info in; vc_value v; static vc_prog_line p[100]; size_t o = 0; int i, n;
    if (vc_inspect(c, &in)) { snprintf(out, cap, "inspect failed"); return; }
    o += (size_t)snprintf(out + o, cap - o, "ws%d b%d c%d fl%x regs%d lines%d line%d ext%d|", in.word_size, in.base, in.complement, in.flags, in.registers, in.lines, in.line, in.ext_memory);
    for (i = VC_REG_X; i <= VC_REG_I; i++) if (!vc_inspect_stack(c, i, &v)) o += (size_t)snprintf(out + o, cap - o, "%s=%s;", v.name, v.text);
    static const int R[] = {0, 1, 2, 3, 5, 10, 15, 16, 40, 100, 200, 400, 611, 700};
    for (i = 0; i < (int)(sizeof R / sizeof R[0]); i++) if (R[i] < in.registers && !vc_inspect_register(c, R[i], &v)) o += (size_t)snprintf(out + o, cap - o, "%s=%s;", v.name, v.text);
    n = vc_inspect_program(c, 1, p, 100);
    for (i = 0; i < n && o + 40 < cap; i++) o += (size_t)snprintf(out + o, cap - o, "%s,", p[i].keys);
}
/* the standard script: stack, registers, I, flags, program */
static void script(vc_calc *c, int ext) {
    keys(c, "HEX 1 0 f STO");                                  /* HEX, WSIZE 16 */
    if (ext) keys(c, "2 6 3 STO SST 7 7 7 7 STO R/S A B STO 0 1 2 3 STO 1 5 STO 5");   /* I = 0x263, R(0x263 = 611) = 7777, R0 = AB ... */
    else keys(c, "A B STO 0 1 2 3 STO 1 5 STO 5");
    keys(c, "g 4 3 4 5 6 ENTER 7 8 9 ENTER 3 +");
    keys(c, "g R/S f R↓ g GTO A 3 + 1 2 × g GSB g R/S");     /* P/R, CLEAR PRGM, LBL A 3 + 1 2 * RTN, P/R */
    vc_run_ms(c, 500);
}
static int save(vc_calc *c, uint8_t m[VC_MEMORY_SIZE]) { return vc_save_memory(c, m); }
static void verdict(vc_calc *c, const uint8_t *m, int want_v, int want_img, const char *what) {
    vc_memcheck k; CHECK(vc_memory_precheck(c, m, &k) == VC_OK, "%s: precheck call", what);
    CHECK(k.verdict == want_v && k.image_layout == want_img, "%s: verdict %d (want %d) image layout %d (want %d) '%s'", what, k.verdict, want_v, k.image_layout, want_img, k.message);
    CHECK((k.verdict == VC_MEMCHK_OK) == (k.message[0] == 0), "%s: message only when not OK", what);
}

int main(void) {
    const char *ST = env("ST", "/workspace/Firmware/_baseline_hp16c-firmware.bin");
    const char *E6 = env("E6", "/workspace/Firmware/HP12c-multi-152-std16-E6E6.bin");
    const char *EA = env("EA", "/workspace/Firmware/HP16c-enhanced-152-pool206-EAEA-TEST.bin");
    const char *B7 = env("B7", "/workspace/Firmware/Voyager16-ext-path-c-trim4-show-B7B7.bin");
    const char *E3 = env("E3", "/workspace/Firmware/HP12c-multi-152-ext16-E3E3.bin");
    const char *E8 = env("E8", "/workspace/Firmware/HP12c-multi-152-ext16-feat-E8E8.bin");
    vc_calc *st = NULL, *e6 = NULL, *ea = NULL;
    static uint8_t mst[VC_MEMORY_SIZE], me6[VC_MEMORY_SIZE], mea[VC_MEMORY_SIZE];
    char s1[1500];
    vc_inspect_info in;

    printf("== layout detection and the script on each supported firmware\n");
    if ((st = open16(ST, 0))) { CHECK(vc_mem16_layout(st) == VC_MEM16_STD, "stock layout %d", vc_mem16_layout(st)); script(st, 0); save(st, mst); vc_inspect(st, &in); CHECK(in.mem_layout == VC_MEM16_STD && in.pool_offset == 0x6F0 && !in.ext_memory, "stock info %d %x", in.mem_layout, in.pool_offset); CHECK(vc_memory_layout(mst) == VC_MEM16_STD, "stock image layout %d", vc_memory_layout(mst)); }
    if ((e6 = open16(E6, 1))) { CHECK(vc_mem16_layout(e6) == VC_MEM16_STD && vc_firmware_mem16(e6) == VC_MEM16_STD, "E6E6 layout %d", vc_mem16_layout(e6)); script(e6, 0); save(e6, me6); vc_inspect(e6, &in); CHECK(in.mem_layout == VC_MEM16_STD && in.pool_offset == 0x6F0 && !in.ext_memory, "E6E6 info %d %x", in.mem_layout, in.pool_offset); CHECK(vc_memory_layout(me6) == VC_MEM16_STD, "E6E6 image layout %d", vc_memory_layout(me6)); printf("  E6E6: %s\n", vc_firmware_info(e6)); }
    if ((ea = open16(EA, 0))) {
        CHECK(vc_mem16_layout(ea) == VC_MEM16_LAYOUT206 && vc_firmware_mem16(ea) == VC_MEM16_LAYOUT206, "EAEA layout %d", vc_mem16_layout(ea));
        if (vc_model(ea) != 16) CHECK(sw(ea, 1) == 16, "EAEA: switch to 16C");
        script(ea, 1); save(ea, mea); vc_inspect(ea, &in);
        CHECK(in.mem_layout == VC_MEM16_LAYOUT206 && in.pool_offset == 0x168 && in.registers > 611, "EAEA info %d %x %d", in.mem_layout, in.pool_offset, in.registers);
        CHECK(vc_memory_layout(mea) == VC_MEM16_LAYOUT206, "EAEA image layout %d", vc_memory_layout(mea));
        snap(ea, s1, sizeof s1); CHECK(strstr(s1, "R611=7777") && strstr(s1, "R0=AB") && strstr(s1, "I=263"), "EAEA values: %.300s", s1);
        printf("  EAEA: %s\n", vc_firmware_info(ea));
    }
    if (st) { snap(st, s1, sizeof s1); CHECK(strstr(s1, "R0=AB") && strstr(s1, "R5=5") , "stock values: %s", s1); }
    if (e6) { snap(e6, s1, sizeof s1); CHECK(strstr(s1, "R0=AB") && strstr(s1, "R5=5"), "E6E6 values: %s", s1); }

    printf("== pre-check verdicts (firmware x image)\n");
    if (st && e6 && ea) {
        verdict(e6, mst, VC_MEMCHK_OK, VC_MEM16_STD, "E6E6 <- stock image");
        verdict(st, me6, VC_MEMCHK_OK, VC_MEM16_STD, "stock <- E6E6 image");
        verdict(ea, mea, VC_MEMCHK_OK, VC_MEM16_LAYOUT206, "EAEA <- EAEA image");
        verdict(ea, mst, VC_MEMCHK_WARN, VC_MEM16_STD, "EAEA <- stock image (stock or sparse 206)");
        verdict(st, mea, VC_MEMCHK_WARN, VC_MEM16_LAYOUT206, "stock <- EAEA image");
        static uint8_t z[VC_MEMORY_SIZE];
        memcpy(z, mea, sizeof z); memset(z + 0x168, 0, 0x7D8 - 0x168);
        verdict(ea, z, VC_MEMCHK_OK, VC_MEM16_ANY, "cleared image"); verdict(e6, z, VC_MEMCHK_OK, VC_MEM16_ANY, "cleared image (E6E6)");
        vc_memcheck k; vc_memory_precheck(ea, mst, &k); printf("  e.g. EAEA <- stock image: %s\n", k.message);
    }
    printf("== stock image -> 206 on EAEA\n");
    if (st && ea) {
        uint8_t cv[VC_MEMORY_SIZE]; memcpy(cv, mst, sizeof cv);
        CHECK(vc_memory_convert_pool(cv, VC_MEM16_STD, VC_MEM16_LAYOUT206) == 1, "convert stock -> 206");
        tap(ea, 30); vc_run_ms(ea, 500); CHECK(vc_load_memory(ea, cv) == VC_OK, "load converted"); tap(ea, 30); vc_run_ms(ea, 1500);
        snap(ea, s1, sizeof s1); CHECK(strstr(s1, "R0=AB") && strstr(s1, "R5=5"), "converted stock on EAEA: %.300s", s1);
    }
    printf("== retired intermediate builds: unsupported, never another layout\n");
    {
        const char *ret[3] = {B7, E3, E8}; int multi[3] = {0, 1, 1}, i;
        for (i = 0; i < 3; i++) {
            vc_calc *x = open16(ret[i], multi[i]);
            if (!x) continue;
            vc_memcheck k;
            CHECK(vc_mem16_layout(x) == VC_MEM16_UNSUPPORTED && vc_firmware_mem16(x) == VC_MEM16_UNSUPPORTED, "%s layout %d", ret[i], vc_mem16_layout(x));
            CHECK(vc_inspect(x, &in) == VC_ERR_UNSUPPORTED, "%s: inspector off", ret[i]);
            CHECK(!(vc_capabilities(x) & VC_CAP_INSPECT) == !multi[i], "%s: inspect capability only for the other modes of a multi build", ret[i]);
            CHECK(vc_set_mem16_layout(x, VC_MEM16_LAYOUT206) == VC_OK && vc_mem16_layout(x) == VC_MEM16_UNSUPPORTED, "%s: a choice does not make it readable", ret[i]);
            vc_set_mem16_layout(x, VC_MEM16_NONE);
            if (ea) { CHECK(vc_memory_precheck(x, mea, &k) == VC_OK && k.verdict == VC_MEMCHK_UNSUPPORTED && k.message[0], "%s precheck %d", ret[i], k.verdict); }
            printf("  %s: %s\n", ret[i], vc_firmware_info(x));
            vc_destroy(x);
        }
    }
    if (st) vc_destroy(st);
    if (e6) vc_destroy(e6);
    if (ea) vc_destroy(ea);
    printf("%d checks, %d failed\n%s\n", checks, fails, fails ? "MEM16: FAIL" : "MEM16: OK");
    return fails != 0;
}
