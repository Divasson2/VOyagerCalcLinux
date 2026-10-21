/* test_pool15.c - the HP-15C memory pool decoder (vc_inspect_pool15, _imag, _matrix) and the
 * dim15 / flag 8 fixes, against the reference decoder docs/pool15c/pool15.py on the 80 dumps in
 * docs/pool15c/dumps (expected values: tests/pool15_expected.txt, made by gen_pool15_expected.py).
 * Needs the C8C8 15C firmware (not included): FW15=... (default /workspace/Firmware/...C8C8.bin).
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 José González Divassón */
#include "voyager_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("  FAIL "); printf(__VA_ARGS__); printf("\n"); } } while (0)
static const char *env(const char *k, const char *d) { const char *v = getenv(k); return v && *v ? v : d; }
static uint8_t *readfile(const char *p, size_t *n) {
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    uint8_t *b = malloc(0x80000); *n = fread(b, 1, 0x80000, f); fclose(f); return b;
}
static void mtap(vc_calc *c, int k) { vc_press_key(c, k); vc_run_ms(c, 120); vc_release_key(c, k); vc_run_ms(c, 600); }
static const char *text(vc_calc *c) { static char t[64]; vc_display d; vc_get_display_cells(c, &d); vc_display_text(&d, t, sizeof t); return t; }
static int sw(vc_calc *c, int pick) {      /* off, g-ENTER-ON, 4, model key (as test_multi.c) */
    mtap(c, 30); vc_run_ms(c, 500);
    if (strspn(text(c), " ") != strlen(text(c))) { mtap(c, 30); vc_run_ms(c, 500); }
    vc_press_key(c, 32); vc_press_key(c, 25); vc_run_ms(c, 100); vc_press_key(c, 30); vc_run_ms(c, 300);
    vc_release_key(c, 30); vc_run_ms(c, 200); vc_release_key(c, 25); vc_release_key(c, 32); vc_run_ms(c, 800);
    mtap(c, 16); mtap(c, pick); vc_run_ms(c, 2000); mtap(c, 24);
    return vc_model_variant(c);
}
static char *field(char **s) { char *f = *s, *e = strchr(f, '|'); if (e) { *e = 0; *s = e + 1; } else *s = f + strlen(f); return f; }

int main(void) {
    const char *FW = env("FW15", "/workspace/Firmware/hp15c-3rom-mute120-fsm5-rel80-opt5cycle-debounce-menu-stopwatch-48fix-keys-batt-trim4-show-C8C8.bin");
    const char *DUMPS = env("DUMPS", "../docs/pool15c/dumps"), *EXP = env("EXP", "tests/pool15_expected.txt");
    size_t n; uint8_t *fw = readfile(FW, &n);
    if (!fw) { printf("SKIP (no %s)\n", FW); return 0; }
    FILE *ef = fopen(EXP, "r"); if (!ef) { printf("SKIP (no %s)\n", EXP); return 0; }
    vc_calc *c[2]; int want[2] = {150, 152}, k;
    for (k = 0; k < 2; k++) {
        c[k] = vc_create(); vc_load_firmware(c[k], fw, n); vc_start(c[k], VC_ENGINE_WHOLE, 1);
        mtap(c[k], 30); vc_run_ms(c[k], 1500); mtap(c[k], 24);      /* ON, <- clears Pr Error */
        int v = vc_model_variant(c[k]);
        if (v != want[k]) v = sw(c[k], k == 0 ? 0 : 3);           /* menu: sqrt-x = 15.0, y^x = 15.2 (v15.py) */
        printf("== mode %d (running %d)\n", want[k], v);
        CHECK(v == want[k], "could not switch to %d", want[k]);
    }
    char line[2048]; int rows = 0;
    while (fgets(line, sizeof line, ef)) {
        line[strcspn(line, "\n")] = 0; if (!*line) continue;
        char *s = line, *name = field(&s); int var = atoi(field(&s));
        char *data = field(&s), *starts = field(&s), *pool = field(&s), *dims = field(&s), *result = field(&s);
        int cx = atoi(field(&s)), scratch = atoi(field(&s)), gap = atoi(field(&s)), dim_i = atoi(field(&s));
        int mats = atoi(field(&s)), pregs = atoi(field(&s)), pbytes = atoi(field(&s)), fr = atoi(field(&s));
        char *gmem = field(&s); unsigned flags = (unsigned)strtoul(field(&s), NULL, 16); char *imag = field(&s), *mx = field(&s);
        char path[1024]; snprintf(path, sizeof path, "%s/%s", DUMPS, name);
        size_t m; uint8_t *img = readfile(path, &m);
        if (!img || m != VC_MEMORY_SIZE) { printf("  SKIP %s\n", name); free(img); continue; }
        vc_calc *x = c[var == 152]; vc_load_memory(x, img); free(img); rows++;
        vc_pool15 p; CHECK(vc_inspect_pool15(x, &p) == VC_OK, "%s pool", name);
        char b[256];
        snprintf(b, sizeof b, "%02x", p.data); CHECK(!strcmp(b, data), "%s data %s want %s", name, b, data);
        snprintf(b, sizeof b, "%02x %02x %02x %02x %02x", p.start[0], p.start[1], p.start[2], p.start[3], p.start[4]);
        CHECK(!strcmp(b, starts), "%s starts %s want %s", name, b, starts);
        snprintf(b, sizeof b, "%x", p.pool); CHECK(!strcmp(b, pool), "%s pool %s want %s", name, b, pool);
        snprintf(b, sizeof b, "%dx%d %dx%d %dx%d %dx%d %dx%d", p.rows[0], p.cols[0], p.rows[1], p.cols[1], p.rows[2], p.cols[2], p.rows[3], p.cols[3], p.rows[4], p.cols[4]);
        CHECK(!strcmp(b, dims), "%s dims %s want %s", name, b, dims);
        CHECK('A' + p.result == result[0], "%s RESULT %c want %s", name, 'A' + p.result, result);
        CHECK(p.complex == cx && p.scratch == scratch && p.gap == gap, "%s complex %d scratch %d gap %d want %d %d %d", name, p.complex, p.scratch, p.gap, cx, scratch, gap);
        CHECK(p.dim_i == dim_i && p.matrices == mats && p.prog_regs == pregs && p.prog_bytes == pbytes && p.free == fr,
              "%s dim %d mats %d prog %d/%d free %d", name, p.dim_i, p.matrices, p.prog_regs, p.prog_bytes, p.free);
        CHECK(!strcmp(p.gmem, gmem), "%s g MEM %s want %s", name, p.gmem, gmem);
        vc_inspect_info in; vc_inspect(x, &in);
        CHECK(in.flags == flags, "%s flags %x want %x (flag 8 from 0x4C bit 3)", name, in.flags, flags);
        CHECK(in.registers == dim_i + 1, "%s registers %d want %d (dim15 ends at matrix A)", name, in.registers, dim_i + 1);
        vc_value v;
        CHECK(vc_inspect_register(x, dim_i + 1, &v) != VC_OK, "%s R%d beyond DIM (i) readable", name, dim_i + 1);
        if (cx) {
            int r; char *q = imag;
            for (r = VC_REG_LASTX; r >= VC_REG_X; r--) {        /* expected order: LASTx T Z Y X */
                uint64_t w = strtoull(q, &q, 16);
                CHECK(vc_inspect_imag(x, r, &v) == VC_OK && v.raw == w, "%s imag %d %014" PRIx64 " want %014" PRIx64, name, r, v.raw, w);
            }
        } else CHECK(vc_inspect_imag(x, VC_REG_X, &v) == VC_ERR_STATE, "%s imag without flag 8", name);
        if (strcmp(mx, "-")) {
            char *q = mx;
            while (*q) {
                int mi = q[0] - 'A'; uint64_t a = strtoull(q + 2, &q, 16), z = strtoull(q + 1, &q, 16);
                CHECK(vc_inspect_matrix(x, mi, 1, 1, &v) == VC_OK && v.raw == a, "%s %c(1,1)", name, 'A' + mi);
                CHECK(vc_inspect_matrix(x, mi, p.rows[mi], p.cols[mi], &v) == VC_OK && v.raw == z, "%s %c(last)", name, 'A' + mi);
                CHECK(vc_inspect_matrix(x, mi, p.rows[mi] + 1, 1, &v) == VC_ERR_ARG, "%s %c beyond rows", name, 'A' + mi);
                while (*q == ' ') q++;
            }
        }
    }
    fclose(ef);
    printf("pool15: %d dumps, %d checks, %d failed\n", rows, checks, fails);
    return fails ? 1 : 0;
}
