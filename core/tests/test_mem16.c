/* test_mem16.c - the three 16C memory layouts (standard 203 B at 0x6F0, extended 1232 B at 0x168 = B7B7 / E8E8,
 * early E3E3 at 0x170): firmware detection, the inspector reading each, the structural pre-check, the
 * E3E3 -> new converter, and B7B7 <-> E8E8 images moving both ways.
 * Firmware files are looked up by environment variable (B7, ST, E3, E6, E8) and skipped when absent.
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
    static const int R[] = {0, 1, 2, 3, 5, 10, 15, 16, 40, 100, 200, 400, 611};
    for (i = 0; i < (int)(sizeof R / sizeof R[0]); i++) if (R[i] < in.registers && !vc_inspect_register(c, R[i], &v)) o += (size_t)snprintf(out + o, cap - o, "%s=%s;", v.name, v.text);
    n = vc_inspect_program(c, 1, p, 100);
    for (i = 0; i < n && o + 40 < cap; i++) o += (size_t)snprintf(out + o, cap - o, "%s,", p[i].keys);
}
/* the standard script: stack, registers, I, flags, program */
static void script(vc_calc *c, int ext) {
    keys(c, "HEX 1 0 f STO");                                  /* HEX, WSIZE 16 */
    if (ext) keys(c, "2 6 3 STO SST 7 7 7 7 STO R/S A B STO 0 1 2 3 STO 1 5 STO 5");   /* I = 0x263, R611 = 7777 (the program takes the top 4 registers), R0 = AB ... */
    else keys(c, "A B STO 0 1 2 3 STO 1 5 STO 5");
    keys(c, "g 4 3 4 5 6 ENTER 7 8 9 ENTER 3 +");
    keys(c, "g R/S f R↓ g GTO A 3 + 1 2 × g GSB g R/S");     /* P/R, CLEAR PRGM, LBL A 3 + 1 2 * RTN, P/R */
    vc_run_ms(c, 500);
}
static int save(vc_calc *c, uint8_t m[VC_MEMORY_SIZE]) { return vc_save_memory(c, m); }
static int dump_equal_regs(vc_calc *a, vc_calc *b, const char *what) {
    char sa[1500], sb[1500]; snap(a, sa, sizeof sa); snap(b, sb, sizeof sb);
    int ok = !strcmp(sa, sb); CHECK(ok, "%s:\n    A: %s\n    B: %s", what, sa, sb); return ok;
}
static void verdict(vc_calc *c, const uint8_t *m, int want_v, int want_img, const char *what) {
    vc_memcheck k; CHECK(vc_memory_precheck(c, m, &k) == VC_OK, "%s: precheck call", what);
    CHECK(k.verdict == want_v && k.image_layout == want_img, "%s: verdict %d (want %d) image layout %d (want %d) '%s'", what, k.verdict, want_v, k.image_layout, want_img, k.message);
    CHECK((k.verdict == VC_MEMCHK_OK) == (k.message[0] == 0), "%s: message only when not OK", what);
}

int main(void) {
    const char *B7 = env("B7", "/workspace/Firmware/Voyager16-ext-path-c-trim4-show-B7B7.bin");
    const char *ST = env("ST", "/workspace/Firmware/_baseline_hp16c-firmware.bin");
    const char *E3 = env("E3", "/workspace/Firmware/HP12c-multi-152-ext16-E3E3.bin");
    const char *E6 = env("E6", "/workspace/Firmware/HP12c-multi-152-std16-E6E6.bin");
    const char *E8 = env("E8", "/workspace/Firmware/HP12c-multi-152-ext16-feat-E8E8.bin");
    vc_calc *b7 = NULL, *st = NULL, *e3 = NULL, *e6 = NULL, *e8 = NULL;
    static uint8_t mb7[VC_MEMORY_SIZE], mst[VC_MEMORY_SIZE], me3[VC_MEMORY_SIZE], me6[VC_MEMORY_SIZE], me8[VC_MEMORY_SIZE];
    char s1[1500], s2[1500];
    vc_inspect_info in;

    printf("== layout detection and the script on each firmware\n");
    if ((b7 = open16(B7, 0))) { CHECK(vc_mem16_layout(b7) == VC_MEM16_EXT && vc_firmware_mem16(b7) == VC_MEM16_EXT, "B7B7 layout %d", vc_mem16_layout(b7)); script(b7, 1); save(b7, mb7); vc_inspect(b7, &in); CHECK(in.mem_layout == VC_MEM16_EXT && in.pool_offset == 0x168 && in.registers == 612, "B7B7 info %d %x %d", in.mem_layout, in.pool_offset, in.registers); CHECK(vc_memory_layout(mb7) == VC_MEM16_EXT, "B7B7 image layout %d", vc_memory_layout(mb7)); printf("  B7B7: %s\n", vc_firmware_info(b7)); }
    if ((st = open16(ST, 0))) { CHECK(vc_mem16_layout(st) == VC_MEM16_STD, "stock layout %d", vc_mem16_layout(st)); script(st, 0); save(st, mst); vc_inspect(st, &in); CHECK(in.mem_layout == VC_MEM16_STD && in.pool_offset == 0x6F0 && !in.ext_memory, "stock info %d %x", in.mem_layout, in.pool_offset); CHECK(vc_memory_layout(mst) == VC_MEM16_STD, "stock image layout %d", vc_memory_layout(mst)); }
    if ((e3 = open16(E3, 1))) { CHECK(vc_mem16_layout(e3) == VC_MEM16_EXT_E3E3 && vc_firmware_mem16(e3) == VC_MEM16_EXT_E3E3, "E3E3 layout %d", vc_mem16_layout(e3)); script(e3, 1); save(e3, me3); vc_inspect(e3, &in); CHECK(in.mem_layout == VC_MEM16_EXT_E3E3 && in.pool_offset == 0x170 && in.registers == 612, "E3E3 info %d %x %d", in.mem_layout, in.pool_offset, in.registers); CHECK(vc_memory_layout(me3) == VC_MEM16_EXT_E3E3, "E3E3 image layout %d", vc_memory_layout(me3)); printf("  E3E3: %s\n", vc_firmware_info(e3)); }
    if ((e6 = open16(E6, 1))) { CHECK(vc_mem16_layout(e6) == VC_MEM16_STD && vc_firmware_mem16(e6) == VC_MEM16_STD, "E6E6 layout %d", vc_mem16_layout(e6)); script(e6, 0); save(e6, me6); vc_inspect(e6, &in); CHECK(in.mem_layout == VC_MEM16_STD && in.pool_offset == 0x6F0 && !in.ext_memory, "E6E6 info %d %x", in.mem_layout, in.pool_offset); CHECK(vc_memory_layout(me6) == VC_MEM16_STD, "E6E6 image layout %d", vc_memory_layout(me6)); printf("  E6E6: %s\n", vc_firmware_info(e6)); }
    if ((e8 = open16(E8, 1))) { CHECK(vc_mem16_layout(e8) == VC_MEM16_EXT && vc_firmware_mem16(e8) == VC_MEM16_EXT, "E8E8 layout %d", vc_mem16_layout(e8)); script(e8, 1); save(e8, me8); vc_inspect(e8, &in); CHECK(in.mem_layout == VC_MEM16_EXT && in.pool_offset == 0x168 && in.registers == 612, "E8E8 info %d %x %d", in.mem_layout, in.pool_offset, in.registers); CHECK(vc_memory_layout(me8) == VC_MEM16_EXT, "E8E8 image layout %d", vc_memory_layout(me8)); printf("  E8E8: %s\n", vc_firmware_info(e8)); }
    if (e8) { snap(e8, s1, sizeof s1); printf("  E8E8 sees: %.230s\n", s1); CHECK(strstr(s1, "R611=7777") && strstr(s1, "R0=AB") && strstr(s1, "I=263"), "E8E8 values"); }
    if (st) { snap(st, s1, sizeof s1); CHECK(strstr(s1, "R0=AB") && strstr(s1, "R5=5") , "stock values: %s", s1); }
    if (e6) { snap(e6, s1, sizeof s1); CHECK(strstr(s1, "R0=AB") && strstr(s1, "R5=5"), "E6E6 values: %s", s1); }

    printf("== B7B7 <-> E8E8: the image moves both ways; the dumps are byte-identical in the data areas\n");
    if (b7 && e8) {
        
        int d = 0, i; for (i = 0x100; i < 0x7F0; i++) d += mb7[i] != me8[i];
        CHECK(d == 0, "same script: %d bytes differ in 0x100..0x7EF", d);
        printf("  same script on B7B7 and E8E8: %d bytes differ in 0x100..0x7EF\n", d);
        /* B7B7 image into E8E8 and back */
        vc_calc *x = open16(E8, 1), *y = open16(B7, 0);
        if (x && y) {
            tap(x, 30); vc_run_ms(x, 500); tap(y, 30); vc_run_ms(y, 500);
            CHECK(vc_load_memory(x, mb7) == VC_OK, "load B7B7 image into E8E8");
            tap(x, 30); vc_run_ms(x, 1500);
            CHECK(vc_load_memory(y, me8) == VC_OK, "load E8E8 image into B7B7");
            tap(y, 30); vc_run_ms(y, 1500);
            dump_equal_regs(x, b7, "E8E8 loaded with the B7B7 image shows what B7B7 shows");
            dump_equal_regs(y, e8, "B7B7 loaded with the E8E8 image shows what E8E8 shows");
            snap(x, s1, sizeof s1); CHECK(strstr(s1, "R611=7777") && strstr(s1, "R0=AB"), "E8E8 loaded: %s", s1);
            /* same keystrokes after the load: identical outcome */
            keys(x, "g R/S 1 0 GTO A"); keys(y, "g R/S 1 0 GTO A");
            keys(x, "R/S"); keys(y, "R/S"); vc_run_ms(x, 1500); vc_run_ms(y, 1500);
            dump_equal_regs(x, y, "after the same keystrokes");
            CHECK(!strcmp(text(x), text(y)), "display '%s' vs '%s'", text(x), text(y));
        }
        if (x) vc_destroy(x);
        if (y) vc_destroy(y);
    }

    printf("== pre-check verdicts (firmware x image)\n");
    if (e8 && b7 && e3 && e6 && st) {
        verdict(e8, mb7, VC_MEMCHK_OK, VC_MEM16_EXT, "E8E8 <- B7B7 image");
        verdict(b7, me8, VC_MEMCHK_OK, VC_MEM16_EXT, "B7B7 <- E8E8 image");
        verdict(e8, me3, VC_MEMCHK_CONVERT, VC_MEM16_EXT_E3E3, "E8E8 <- E3E3 image");
        verdict(b7, me3, VC_MEMCHK_CONVERT, VC_MEM16_EXT_E3E3, "B7B7 <- E3E3 image");
        verdict(e3, me3, VC_MEMCHK_OK, VC_MEM16_EXT_E3E3, "E3E3 <- E3E3 image");
        verdict(e3, me8, VC_MEMCHK_WARN, VC_MEM16_EXT, "E3E3 <- E8E8 image");
        verdict(e8, me6, VC_MEMCHK_WARN, VC_MEM16_STD, "E8E8 <- E6E6 (standard) image");
        verdict(b7, mst, VC_MEMCHK_WARN, VC_MEM16_STD, "B7B7 <- stock image");
        verdict(e6, me8, VC_MEMCHK_WARN, VC_MEM16_EXT, "E6E6 <- E8E8 (extended) image");
        verdict(st, mb7, VC_MEMCHK_WARN, VC_MEM16_EXT, "stock <- B7B7 image");
        verdict(e6, mst, VC_MEMCHK_OK, VC_MEM16_STD, "E6E6 <- stock image");
        verdict(st, me6, VC_MEMCHK_OK, VC_MEM16_STD, "stock <- E6E6 image");
        /* a cleared memory fits everything; a 15C-like image (data in both areas) is flagged */
        static uint8_t z[VC_MEMORY_SIZE], both[VC_MEMORY_SIZE];
        memcpy(z, me8, sizeof z); memset(z + 0x168, 0, 0x7D8 - 0x168);
        verdict(e8, z, VC_MEMCHK_OK, VC_MEM16_ANY, "cleared image"); verdict(e6, z, VC_MEMCHK_OK, VC_MEM16_ANY, "cleared image (E6E6)");
        memcpy(both, me8, sizeof both); both[0x6F8] = 1; both[0x300] = 1;
        verdict(e8, both, VC_MEMCHK_WARN, VC_MEM16_NONE, "data in both areas");
        printf("  matrix checked\n");
        vc_memcheck k; vc_memory_precheck(e6, me8, &k); printf("  e.g. E6E6 <- E8E8 image: %s\n", k.message);
        vc_memory_precheck(e8, me3, &k); printf("  e.g. E8E8 <- E3E3 image: %s\n", k.message);
        /* the same wrong load really does read wrong (this is what the warning is for) */
        tap(e6, 30); vc_run_ms(e6, 500); vc_load_memory(e6, me8); tap(e6, 30); vc_run_ms(e6, 1500);
        snap(e6, s1, sizeof s1); CHECK(!strstr(s1, "R611=7777"), "extended image read as standard should not show R611");
    }
    printf("== E3E3 image -> new layout\n");
    if (e3 && e8) {
        uint8_t cv[VC_MEMORY_SIZE], un[VC_MEMORY_SIZE]; memcpy(cv, me3, sizeof cv); memcpy(un, mb7, sizeof un);
        CHECK(vc_memory_convert_e3e3(un) == 0 && !memcmp(un, mb7, sizeof un), "a new-layout image is left alone");
        CHECK(vc_memory_convert_e3e3(cv) == 1, "convert");
        CHECK(vc_memory_layout(cv) == VC_MEM16_EXT, "converted image layout %d", vc_memory_layout(cv));
        CHECK(vc_memory_convert_e3e3(cv) == 0, "converting twice does nothing");
        /* loaded into E8E8 the converted image reads like the original does on E3E3 */
        vc_calc *x = open16(E8, 1);
        if (x) {
            tap(x, 30); vc_run_ms(x, 500); CHECK(vc_load_memory(x, cv) == VC_OK, "load converted"); tap(x, 30); vc_run_ms(x, 1500);
            dump_equal_regs(x, e3, "converted E3E3 image on E8E8 == original on E3E3");
            snap(x, s1, sizeof s1); CHECK(strstr(s1, "R611=7777") && strstr(s1, "R0=AB") && strstr(s1, "I=263"), "converted values: %s", s1);
            /* without converting, the image reads wrong on E8E8 */
            tap(x, 30); vc_run_ms(x, 500); vc_load_memory(x, me3); tap(x, 30); vc_run_ms(x, 1500);
            snap(x, s2, sizeof s2); CHECK(strcmp(s2, s1) != 0, "an unconverted E3E3 image should read differently on E8E8");
            vc_destroy(x);
        }
    }
    if (b7) vc_destroy(b7);
    if (st) vc_destroy(st);
    if (e3) vc_destroy(e3);
    if (e6) vc_destroy(e6);
    if (e8) vc_destroy(e8);
    printf("%d checks, %d failed\n%s\n", checks, fails, fails ? "MEM16: FAIL" : "MEM16: OK");
    return fails != 0;
}
