/* test_matrix15.c - the Matrix Editor's core (0.7.2): number text <-> 15C BCD (vc_bcd_from_text /
 * vc_bcd_text, the copy-then-paste round trip), matrix write / read in a 2 KB image (vc_matrix15_write,
 * vc_image_matrix15, vc_matrix15_room) against the reference dumps, and the firmware itself using the
 * written matrices (det, the free count of g MEM) in 15.0 and in 15.2 (cold-start gap packed).
 * Needs the C8C8 15C firmware for the last part (FW15=..., skipped when absent).
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 José González Divassón */
#include "voyager_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
static const char *env(const char *k, const char *d) { const char *v = getenv(k); return v && *v ? v : d; }
static uint8_t *readfile(const char *p, size_t *n) {
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    uint8_t *b = malloc(0x80000); *n = fread(b, 1, 0x80000, f); fclose(f); return b;
}
static void mtap(vc_calc *c, int k) { vc_press_key(c, k); vc_run_ms(c, 120); vc_release_key(c, k); vc_run_ms(c, 600); }
static const char *text(vc_calc *c) { static char t[64]; vc_display d; vc_get_display_cells(c, &d); vc_display_text(&d, t, sizeof t); return t; }
static int sw(vc_calc *c, int pick) {
    mtap(c, 30); vc_run_ms(c, 500);
    if (strspn(text(c), " ") != strlen(text(c))) { mtap(c, 30); vc_run_ms(c, 500); }
    vc_press_key(c, 32); vc_press_key(c, 25); vc_run_ms(c, 100); vc_press_key(c, 30); vc_run_ms(c, 300);
    vc_release_key(c, 30); vc_run_ms(c, 200); vc_release_key(c, 25); vc_release_key(c, 32); vc_run_ms(c, 800);
    mtap(c, 16); mtap(c, pick); vc_run_ms(c, 2000); mtap(c, 24);
    return vc_model_variant(c);
}
static uint64_t bcd(const char *s) { uint64_t v = 0; int r = vc_bcd_from_text(s, &v); CHECK(r == VC_OK, "parse '%s'", s); return v; }

static void parsing(void) {
    struct { const char *in; uint64_t want; } T[] = {
        {"0", 0}, {"-0", 0}, {"0.000", 0}, {"1", 0x01000000000000ull}, {"1.5", 0x01500000000000ull},
        {"-1.5", 0x91500000000000ull}, {"2E-3", 0x02000000000997ull}, {"2e-3", 0x02000000000997ull},
        {"0.002", 0x02000000000997ull}, {"1e5", 0x01000000000005ull}, {"123456", 0x01234560000005ull},
        {"+7", 0x07000000000000ull}, {"9.999999999e99", 0x09999999999099ull}, {"1e-99", 0x01000000000901ull},
        {"12345678905", 0x01234567891010ull}, {"9.9999999995", 0x01000000000001ull}, {" 3.25 ", 0x03250000000000ull},
        {"1E+2", 0x01000000000002ull}, {"0.5e1", 0x05000000000000ull}, {".25", 0x02500000000999ull}, {"1e-100", 0},
    };
    size_t i;
    for (i = 0; i < sizeof T / sizeof T[0]; i++) {
        uint64_t v = 1; int r = vc_bcd_from_text(T[i].in, &v);
        CHECK(r == VC_OK && v == T[i].want, "'%s' -> %014llx want %014llx (r %d)", T[i].in, (unsigned long long)v, (unsigned long long)T[i].want, r);
    }
    const char *bad[] = {"", "abc", "1,5", "1.2.3", "1e", "1e99x", "1e100", "--1", "e5", "1 2"};
    for (i = 0; i < sizeof bad / sizeof bad[0]; i++) { uint64_t v; CHECK(vc_bcd_from_text(bad[i], &v) == VC_ERR_ARG, "'%s' should be refused", bad[i]); }
    /* round trip: every value the inspector can show reads back to the same register (copy -> paste) */
    srand(7);
    int n, bad_rt = 0;
    for (n = 0; n < 20000; n++) {
        uint64_t v = 0; int k;
        v = rand() % 2 ? 9 : 0;
        v = v << 4 | (uint64_t)(1 + rand() % 9);
        for (k = 0; k < 9; k++) v = v << 4 | (uint64_t)(rand() % 10);
        int e = rand() % 199 - 99, ee = e < 0 ? 1000 + e : e;
        v = v << 12 | (uint64_t)(ee / 100) << 8 | (uint64_t)(ee / 10 % 10) << 4 | (uint64_t)(ee % 10);
        char t[64]; uint64_t back = 0;
        vc_bcd_text(v, t, sizeof t);
        if (vc_bcd_from_text(t, &back) != VC_OK || back != v) { if (bad_rt++ < 5) printf("  round trip %014llx -> '%s' -> %014llx\n", (unsigned long long)v, t, (unsigned long long)back); }
    }
    CHECK(bad_rt == 0, "%d of 20000 values do not round-trip through the text", bad_rt);
}

static uint8_t *dump(const char *name) {
    char p[1024]; size_t n; snprintf(p, sizeof p, "%s/%s", env("DUMPS", "../docs/pool15c/dumps"), name);
    uint8_t *b = readfile(p, &n);
    if (!b || n != VC_MEMORY_SIZE) { printf("  SKIP (no %s)\n", p); free(b); return NULL; }
    return b;
}

static void images(void) {
    vc_pool15 p0, p;
    uint8_t *b = dump("v150_00_baseline.bin");
    if (!b) return;
    CHECK(vc_image_pool15(b, 150, &p0) == VC_OK && p0.free == 78, "baseline free %d", p0.free);
    uint64_t v[6]; const char *t[6] = {"1", "2", "3", "4", "5.5", "-6e-7"}; int i;
    for (i = 0; i < 6; i++) v[i] = bcd(t[i]);
    CHECK(vc_matrix15_write(b, 150, 1, 2, 3, v) == VC_OK, "write B 2x3");
    vc_image_pool15(b, 150, &p);
    CHECK(p.rows[1] == 2 && p.cols[1] == 3 && p.free == 72 && p.matrices == 6, "B 2x3: rows %d cols %d free %d", p.rows[1], p.cols[1], p.free);
    CHECK(p.start[2] == p.start[1] + 6 && p.start[4] == p.start[1] + 6 && p.pool == p.start[1] + 6, "C..E and the pool moved up by 6");
    vc_value x; char want[6][16] = {"1", "2", "3", "4", "5.5", "-6e-7"};
    for (i = 0; i < 6; i++) { CHECK(vc_image_matrix15(b, 150, 1, 1 + i / 3, 1 + i % 3, &x) == VC_OK && !strcmp(x.text, want[i]), "B(%d,%d) '%s'", 1 + i / 3, 1 + i % 3, x.text); }
    uint64_t a[4] = {bcd("10"), bcd("20"), bcd("30"), bcd("40")};
    CHECK(vc_matrix15_write(b, 150, 0, 2, 2, a) == VC_OK, "write A 2x2 below B");
    vc_image_pool15(b, 150, &p);
    CHECK(p.start[1] == p.start[0] + 4 && p.free == 68, "B moved up by 4 (free %d)", p.free);
    CHECK(vc_image_matrix15(b, 150, 1, 2, 3, &x) == VC_OK && !strcmp(x.text, "-6e-7"), "B kept its values after A grew: '%s'", x.text);
    CHECK(vc_image_matrix15(b, 150, 0, 2, 1, &x) == VC_OK && !strcmp(x.text, "30"), "A(2,1)");
    /* too big: 68 free + A's own 4 = 72 */
    int room = 0;
    CHECK(vc_matrix15_room(b, 150, 0, 8, 9, &room) == VC_OK && room == 72, "A 8x9 fits (room %d)", room);
    CHECK(vc_matrix15_room(b, 150, 0, 8, 10, &room) == VC_ERR_ARG, "A 8x10 does not fit");
    CHECK(vc_matrix15_write(b, 150, 0, 9, 9, NULL) == VC_ERR_ARG, "write refused when it does not fit");
    CHECK(vc_matrix15_room(b, 150, 0, 100, 1, NULL) == VC_ERR_ARG, "rows > 99");
    /* shrink A to 1x1 (keeps its first element), then free it: B moves back, free returns */
    CHECK(vc_matrix15_write(b, 150, 0, 1, 1, NULL) == VC_OK, "A 1x1");
    CHECK(vc_image_matrix15(b, 150, 0, 1, 1, &x) == VC_OK && !strcmp(x.text, "10"), "A kept (1,1)");
    CHECK(vc_matrix15_write(b, 150, 0, 0, 5, NULL) == VC_OK, "A 0x5 = freed");
    vc_image_pool15(b, 150, &p);
    CHECK(p.rows[0] == 0 && p.cols[0] == 0 && p.free == 72 && p.start[1] == p.start[0], "A freed (free %d)", p.free);
    CHECK(vc_image_matrix15(b, 150, 1, 1, 1, &x) == VC_OK && !strcmp(x.text, "1"), "B(1,1) after A freed '%s'", x.text);
    free(b);
    /* a program sits at the top and must not move */
    b = dump("v150_80_prog_only.bin");
    if (b) {
        uint8_t before[VC_MEMORY_SIZE]; memcpy(before, b, sizeof before);
        vc_image_pool15(b, 150, &p0);
        int r = vc_matrix15_room(b, 150, 2, 1, 1, &room);
        CHECK(r == VC_OK && room == p0.prog_low - p0.pool, "room below the program %d", room);
        CHECK(vc_matrix15_write(b, 150, 2, 1, room, NULL) == VC_OK, "C fills the free space");
        CHECK(!memcmp(b + 0x70 + 8 * (p0.prog_low - 0x10), before + 0x70 + 8 * (p0.prog_low - 0x10), 8u * (uint32_t)(0x100 - p0.prog_low)), "program bytes unchanged");
        vc_image_pool15(b, 150, &p);
        CHECK(p.free == 0 && p.pool == p0.prog_low, "free 0, pool at the program (free %d)", p.free);
        CHECK(vc_matrix15_write(b, 150, 3, 1, 1, NULL) == VC_ERR_ARG, "nothing more fits");
        free(b);
    }
    /* 15.2 cold start: the gap below R2 is packed first, as the firmware does */
    b = dump("v152_00_baseline.bin");
    if (b) {
        vc_image_pool15(b, 152, &p0);
        CHECK(p0.gap > 0, "15.2 baseline has the cold-start gap (%d)", p0.gap);
        CHECK(vc_matrix15_write(b, 152, 0, 1, 2, a) == VC_OK, "15.2 A 1x2");
        vc_image_pool15(b, 152, &p);
        CHECK(p.gap == 0 && p.data == p.base && p.free == p0.free - 2, "packed: gap %d data %02x free %d (was %d)", p.gap, p.data, p.free, p0.free);
        free(b);
    }
}

/* the firmware uses what was written: det, and its own g MEM count */
static double xval(vc_calc *c) { vc_value v; vc_inspect_stack(c, VC_REG_X, &v); return v.number; }
static void keys(vc_calc *c, const int *k, int n) { int i; for (i = 0; i < n; i++) mtap(c, k[i]); vc_run_ms(c, 1500); }
static void firmware(void) {
    size_t n; uint8_t *fw = readfile(env("FW15", "/workspace/Firmware/hp15c-3rom-mute120-fsm5-rel80-opt5cycle-debounce-menu-stopwatch-48fix-keys-batt-trim4-show-C8C8.bin"), &n);
    if (!fw) { printf("  SKIP firmware part (no FW15)\n"); return; }
    int mode;
    for (mode = 0; mode < 2; mode++) {
        int var = mode ? 152 : 150;
        vc_calc *c = vc_create(); vc_load_firmware(c, fw, n); vc_start(c, VC_ENGINE_WHOLE, 1);
        mtap(c, 30); vc_run_ms(c, 1500); mtap(c, 24);
        if (vc_model_variant(c) != var) sw(c, mode ? 3 : 0);
        CHECK(vc_model_variant(c) == var, "running %d", var);
        uint8_t mem[VC_MEMORY_SIZE]; vc_save_memory(c, mem);
        uint64_t a[4] = {bcd("1"), bcd("2"), bcd("3"), bcd("4")}, b9[9];
        const char *bt[9] = {"2", "0", "1", "1,5", "3", "0", "0", "1", "4"};
        int i; for (i = 0; i < 9; i++) { char t[16]; snprintf(t, sizeof t, "%s", bt[i]); char *cm = strchr(t, ','); if (cm) *cm = '.'; b9[i] = bcd(t); }
        CHECK(vc_matrix15_write(mem, var, 0, 2, 2, a) == VC_OK, "%d: A", var);
        CHECK(vc_matrix15_write(mem, var, 1, 3, 3, b9) == VC_OK, "%d: B", var);
        vc_pool15 mine; vc_image_pool15(mem, var, &mine);
        CHECK(vc_load_memory(c, mem) == VC_OK, "%d: load", var);
        mtap(c, 30); vc_run_ms(c, 800); if (strspn(text(c), " ") == strlen(text(c))) { mtap(c, 30); vc_run_ms(c, 800); }
        { vc_pool15 q; vc_inspect_pool15(c, &q); if (getenv("DBG")) printf("  after load: free %d data %x st %x %x %x pool %x cx %d gap %d scr %d prog %x\n", q.free, q.data, q.start[0], q.start[1], q.start[2], q.pool, q.complex, q.gap, q.scratch, q.prog_low); }
        {   /* the firmware's own g MEM line, shown while MEM is held */
            char want[32], got[64]; int j, o = 0;
            mtap(c, 32); vc_press_key(c, 34); vc_run_ms(c, 400);
            const char *t = text(c); for (j = 0; t[j] && o < 63; j++) if (t[j] != ' ') got[o++] = t[j]; got[o] = 0;
            vc_release_key(c, 34); vc_run_ms(c, 800);
            o = 0; for (j = 0; mine.gmem[j]; j++) if (mine.gmem[j] != ' ') want[o++] = mine.gmem[j]; want[o] = 0;
            CHECK(!strcmp(got, want), "%d: firmware g MEM '%s', written pool says '%s'", var, got, mine.gmem);
        }
        static const int detA[] = {34, 5, 0, 31, 5, 8};        /* RCL MATRIX A, f MATRIX 9 */
        static const int detB[] = {34, 5, 1, 31, 5, 8};
        keys(c, detA, 6);
        CHECK(fabs(xval(c) + 2) < 1e-9, "%d: det A = %g (display '%s'), want -2", var, xval(c), text(c));
        { vc_pool15 q; vc_inspect_pool15(c, &q); if (getenv("DBG")) printf("  after detA: free %d data %x st %x %x %x pool %x cx %d gap %d scr %d prog %x\n", q.free, q.data, q.start[0], q.start[1], q.start[2], q.pool, q.complex, q.gap, q.scratch, q.prog_low); }
        keys(c, detB, 6);
        CHECK(fabs(xval(c) - 25.5) < 1e-9, "%d: det B = %g (display '%s'), want 25.5", var, xval(c), text(c));
        vc_pool15 fp; vc_inspect_pool15(c, &fp);
        if (getenv("DBG")) printf("  after detB: free %d data %x st %x %x %x %x %x pool %x cx %d gap %d scr %d prog %x res %d dims %dx%d\n", fp.free, fp.data, fp.start[0], fp.start[1], fp.start[2], fp.start[3], fp.start[4], fp.pool, fp.complex, fp.gap, fp.scratch, fp.prog_low, fp.result, fp.rows[0], fp.cols[0]);
        /* det leaves the LU decomposition in the RESULT matrix (A): A is 3x3 now, 5 registers more */
        CHECK(fp.free == mine.free - 5 && fp.rows[0] == 3 && fp.rows[1] == 3 && fp.cols[1] == 3, "%d: after det B free %d, written %d", var, fp.free, mine.free);
        /* the firmware's own DIM on top of ours: DIM C 1x1 then det of A again */
        static const int dimC[] = {26, 25, 26, 31, 12, 2};      /* 1 ENTER 1 f DIM C */
        keys(c, dimC, 6);
        vc_inspect_pool15(c, &fp);
        CHECK(fp.rows[2] == 1 && fp.free == mine.free - 6, "%d: firmware DIM C after ours (free %d)", var, fp.free);
        keys(c, detB, 6);
        CHECK(fabs(xval(c) - 25.5) < 1e-9, "%d: det B after DIM C = %g", var, xval(c));
        vc_destroy(c);
    }
    free(fw);
}

int main(void) {
    printf("== number text <-> BCD\n"); parsing();
    printf("== matrices in images\n"); images();
    printf("== the firmware uses them\n"); firmware();
    printf("%d checks, %d failed\n", checks, fails);
    if (!fails) printf("MATRIX15: OK\n");
    return fails != 0;
}
