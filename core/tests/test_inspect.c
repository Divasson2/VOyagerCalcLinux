/* test_inspect.c - the read-only inspection API: registers, modes, program listing, pointer.
 *
 * usage: test_inspect   (paths from the environment, defaults under /workspace; missing files skip)
 *   B7  HP-16C CE Path-C firmware (B7B7)      ST  stock HP-16C CE firmware
 *   MULTI  HP-12C CE multi E3E3               SAMPLES  voyager-sim-public/samples
 *   MRLIST  miller-rabin-A.txt                PHILIST  phi-E-D-2747.txt
 * Checks:
 *   - decoded listings = the published listings (Miller-Rabin 117 steps: keycodes and mnemonics;
 *     phi E+D 84 steps: mnemonics), Path-C and stock firmware;
 *   - registers after "2147483647 GSB A" (n, n-1, d, s ...) on both layouts, f MEM register count;
 *   - SST / BST / GTO move the reported line; R/S in the whole engine reports running, then stopped;
 *   - bases, word sizes, complement modes, FLOAT; R615 at WSIZE 16 (Path-C top register);
 *   - inspection never changes the calculator: same memory, same statistics, same run;
 *   - the multi's 16C (Path-C), 15C (15.2) and 12C modes.
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 José González Divassón */
#include "voyager_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int fails, checks;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static const char *env(const char *k, const char *d) { const char *v = getenv(k); return v && *v ? v : d; }
static uint8_t *readfile(const char *p, size_t *n) {
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    static uint8_t *b; b = malloc(0x80000); *n = fread(b, 1, 0x80000, f); fclose(f); return b;
}
static vc_calc *open_fw(const char *path, int engine) {
    size_t n; uint8_t *b = readfile(path, &n);
    if (!b) { printf("  SKIP (no %s)\n", path); return NULL; }
    vc_calc *c = vc_create(); int r = vc_load_firmware(c, b, n); free(b);
    if (r || vc_start(c, engine, 1)) { printf("  FAIL cannot start %s: %s\n", path, vc_last_error(c)); fails++; vc_destroy(c); return NULL; }
    return c;
}
static int load_mem(vc_calc *c, const char *path) {
    size_t n; uint8_t *b = readfile(path, &n); int ok = b && n == VC_MEMORY_SIZE;
    if (ok) vc_load_memory(c, b); else printf("  SKIP (no %s)\n", path);
    free(b); return ok;
}

/* keys: names separated by spaces; a word that is no key name is typed character by character */
static int whole;
static void tap(vc_calc *c, int k) {
    if (!whole) { vc_key(c, k, 0); return; }
    vc_press_key(c, k); vc_run_ms(c, 80); vc_release_key(c, k); vc_run_ms(c, 250);
}
static void keys(vc_calc *c, const char *s) {
    char w[32]; int m = vc_model(c) ? vc_model(c) : 16;
    while (*s) {
        while (*s == ' ') s++;
        size_t n = strcspn(s, " "); if (!n) break;
        if (n >= sizeof w) n = sizeof w - 1;
        memcpy(w, s, n); w[n] = 0; s += n;
        int k = vc_key_code(m, w);
        if (k >= 0) { tap(c, k); continue; }
        for (char *p = w; *p; p++) { char one[2] = {*p, 0}; k = vc_key_code(m, one); if (k < 0) { printf("  bad key %s\n", one); fails++; } else tap(c, k); }
    }
}
static vc_inspect_info info(vc_calc *c) { vc_inspect_info i; CHECK(vc_inspect(c, &i) == VC_OK, "vc_inspect"); return i; }
static vc_value stk(vc_calc *c, int r) { vc_value v; memset(&v, 0, sizeof v); CHECK(vc_inspect_stack(c, r, &v) == VC_OK, "stack %d", r); return v; }
static vc_value reg(vc_calc *c, int r) { vc_value v; memset(&v, 0, sizeof v); CHECK(vc_inspect_register(c, r, &v) == VC_OK, "register %d", r); return v; }
#define TEXT(v, want) CHECK(!strcmp((v).text, want), "%s = '%s', want '%s'", (v).name, (v).text, want)
#define DEC(v, want) CHECK(!strcmp((v).dec, want), "%s dec = '%s', want '%s'", (v).name, (v).dec, want)

/* ------------------------------------------------------------ listings */
static void norm(const char *in, char *out, size_t cap, int fold) {
    static const char *const R[][2] = { {"\xe2\x88\x92", "-"}, {"\xe2\x89\xb7", "<>"}, {"\xc3\x97", "*"}, {"\xc3\xb7", "/"} };
    size_t o = 0; while (*in == ' ') in++;
    while (*in && o + 3 < cap) {
        size_t k, done = 0;
        for (k = 0; k < sizeof R / sizeof R[0]; k++) { size_t L = strlen(R[k][0]); if (!strncmp(in, R[k][0], L)) { o += (size_t)snprintf(out + o, cap - o, "%s", R[k][1]); in += L; done = 1; break; } }
        if (!done) { char ch = *in++; out[o++] = fold && ch >= 'a' && ch <= 'z' ? (char)(ch - 32) : ch; }
    }
    while (o && (out[o - 1] == ' ' || out[o - 1] == '\n' || out[o - 1] == '\r')) o--;
    out[o] = 0;
}
/* reference lines "NNN  MNEMONIC | KEYS ; CODES  comment" */
static int compare_listing(vc_calc *c, const char *path, int want_lines, int with_codes) {
    FILE *f = fopen(path, "r"); if (!f) { printf("  SKIP (no %s)\n", path); return 0; }
    static vc_prog_line p[1000];
    int n = vc_inspect_program(c, 1, p, 1000), i = 0, bad = 0;
    char line[512], a[64], b[64];
    CHECK(n == want_lines, "%s: %d lines decoded, want %d", path, n, want_lines);
    CHECK(info(c).lines == n, "info.lines %d != %d", info(c).lines, n);
    while (fgets(line, sizeof line, f)) {
        if (!(line[0] >= '0' && line[0] <= '9' && line[3] == ' ' && line[4] == ' ')) continue;
        if (i >= n) { bad++; break; }
        char *bar = strchr(line, '|'), *semi = strchr(line, ';');
        if (!bar) continue;
        *bar = 0; norm(line + 5, a, sizeof a, 0); norm(p[i].mnemonic, b, sizeof b, 0);
        if (strcmp(a, b)) { if (bad++ < 5) printf("  line %03d: mnemonic '%s', listing '%s'\n", i + 1, b, a); }
        if (with_codes && semi) {
            char codes[16]; snprintf(codes, sizeof codes, "%.10s", semi + 2);
            norm(codes, a, sizeof a, 1); norm(p[i].keys, b, sizeof b, 1);
            if (strcmp(a, b)) { if (bad++ < 5) printf("  line %03d: keys '%s', listing '%s'\n", i + 1, b, a); }
        }
        CHECK(p[i].line == i + 1, "line number %d", p[i].line);
        i++;
    }
    fclose(f);
    CHECK(i == n && !bad, "%s: %d differences (%d of %d lines read)", path, bad, i, n);
    printf("  listing %s: %d lines, %s\n", strrchr(path, '/') + 1, n, bad ? "DIFFERENT" : "identical");
    return 1;
}

/* inspection must not change anything: memory and statistics before == after */
static void readonly(vc_calc *c) {
    uint8_t m0[VC_MEMORY_SIZE], m1[VC_MEMORY_SIZE]; vc_stats s0, s1; vc_display d0, d1; int k, r;
    static vc_prog_line p[1000]; vc_value v; vc_inspect_info in;
    vc_save_memory(c, m0); vc_get_stats(c, &s0); vc_get_display_cells(c, &d0);
    for (k = 0; k < 20; k++) {
        vc_inspect(c, &in);
        for (r = 0; r < VC_REG_COUNT; r++) vc_inspect_stack(c, r, &v);
        for (r = 0; r < in.registers + 2; r++) vc_inspect_register(c, r, &v);
        vc_inspect_program(c, 1, p, 1000);
    }
    vc_save_memory(c, m1); vc_get_stats(c, &s1); vc_get_display_cells(c, &d1);
    CHECK(!memcmp(m0, m1, sizeof m0), "inspection changed the memory");
    CHECK(!memcmp(&s0, &s1, sizeof s0), "inspection changed the statistics");
    CHECK(!memcmp(&d0, &d1, sizeof d0), "inspection changed the display");
}

static void mr_registers(vc_calc *c, int regs) {
    vc_value v;
    vc_inspect_info in = info(c);
    CHECK(in.model == 16 && in.base == 10 && in.word_size == 64 && in.complement == 0, "modes %d %d %d %d", in.model, in.base, in.word_size, in.complement);
    CHECK(in.registers == regs, "registers %d, want %d", in.registers, regs);
    TEXT(stk(c, VC_REG_X), "1"); TEXT(stk(c, VC_REG_LASTX), "2147483647"); TEXT(stk(c, VC_REG_I), "38");
    TEXT(reg(c, 0), "2147483647"); TEXT(reg(c, 1), "2147483646"); TEXT(reg(c, 2), "1073741823"); TEXT(reg(c, 3), "1");
    CHECK(!strcmp(reg(c, 0).name, "R0"), "name %s", reg(c, 0).name);
    if (regs > 17) CHECK(!strcmp(reg(c, 17).name, "R.1"), "name %s", reg(c, 17).name);
    CHECK(vc_inspect_register(c, regs, &v) == VC_ERR_ARG, "R%d should not exist", regs);
}

const char *text(vc_calc *c);
int main(void) {
    const char *B7 = env("B7", "/workspace/Firmware/Voyager16-ext-path-c-trim4-show-B7B7.bin");
    const char *ST = env("ST", "/workspace/Firmware/_baseline_hp16c-firmware.bin");
    const char *MULTI = env("MULTI", "/workspace/Firmware/HP12c-multi-152-ext16-E3E3.bin");
    const char *SAMPLES = env("SAMPLES", "/workspace/voyager-sim-public/samples");
    const char *MRLIST = env("MRLIST", "/workspace/blog-prime/programs/miller-rabin-A.txt");
    const char *PHILIST = env("PHILIST", "/workspace/blog-phi/programs/phi-E-D-2747.txt");
    char p[512]; vc_calc *c; vc_inspect_info in; vc_value v;

    printf("== calculator images in the firmware (vc_firmware_images)\n");
    {
        const char *fws[3] = {B7, MULTI, env("F15", "/workspace/Firmware/hp15c-3rom-mute120-fsm5-rel80-opt5cycle-debounce-menu-stopwatch-48fix-keys-batt-trim4-show-C8C8.bin")};
        static const int want[3][4] = {{160}, {120, 152, 160}, {150, 152, 160}};
        static const int wantn[3] = {1, 3, 3};
        for (int f = 0; f < 3; f++) {
            size_t n; uint8_t *b = readfile(fws[f], &n);
            if (!b) { printf("  SKIP (no %s)\n", fws[f]); continue; }
            vc_calc *x = vc_create(); vc_load_firmware(x, b, n); free(b);
            int v[8] = {0}, k = vc_firmware_images(x, v, 8);
            CHECK(k == wantn[f] && !memcmp(v, want[f], sizeof(int) * (size_t)wantn[f]), "%s: %d images %d %d %d", fws[f], k, v[0], v[1], v[2]);
            printf("  %s: %d image(s):", strrchr(fws[f], '/') + 1, k); for (int j = 0; j < k; j++) printf(" %d", v[j]); printf("\n");
            vc_destroy(x);
        }
    }
    printf("== HP-16C Path-C (B7B7): Miller-Rabin listing and registers\n");
    if ((c = open_fw(B7, VC_ENGINE_RUNNER))) {
        snprintf(p, sizeof p, "%s/miller-rabin-ext-ready.16c", SAMPLES);
        if (load_mem(c, p)) {
            in = info(c); CHECK(in.ext_memory && in.registers == 139 && in.line == 0, "ext %d regs %d line %d", in.ext_memory, in.registers, in.line);
            compare_listing(c, MRLIST, 117, 1);
            readonly(c);
            keys(c, "2147483647 GSB A");
            mr_registers(c, 139); readonly(c);
            /* pointer: GTO label, SST/BST in program mode, SST in run mode */
            keys(c, "GTO F"); CHECK(info(c).line == 114, "GTO F: line %d", info(c).line);
            keys(c, "g R/S"); in = info(c); CHECK(in.prgm_mode && in.line == 114, "P/R: prgm %d line %d", in.prgm_mode, in.line);
            keys(c, "SST SST"); CHECK(info(c).line == 116, "SST SST: line %d", info(c).line);
            keys(c, "SST SST"); CHECK(info(c).line == 0, "SST past the end wraps to 000: line %d", info(c).line);
            keys(c, "g SST"); CHECK(info(c).line == 117, "BST from 000: line %d", info(c).line);
            keys(c, "g SST g SST g SST"); CHECK(info(c).line == 114, "BST x3: line %d", info(c).line);
            keys(c, "GTO . 0 1 1"); CHECK(info(c).line == 11, "GTO . 011: line %d", info(c).line);
            keys(c, "g R/S"); CHECK(!info(c).prgm_mode, "run mode");
            keys(c, "GTO A SST"); CHECK(info(c).line == 2, "run-mode SST of LBL A: line %d", info(c).line);
            keys(c, "SST SST"); CHECK(info(c).line == 4, "SST x2: line %d", info(c).line);
            TEXT(stk(c, VC_REG_X), "64");
            keys(c, "SST"); CHECK(info(c).line == 5, "SST: line %d", info(c).line);
            readonly(c);
        }
        vc_destroy(c);
    }
    if ((c = open_fw(B7, VC_ENGINE_RUNNER))) {
        snprintf(p, sizeof p, "%s/phi-2747-ext-ready.16c", SAMPLES);
        if (load_mem(c, p)) { compare_listing(c, PHILIST, 84, 0); CHECK(info(c).registers == 164, "phi registers %d", info(c).registers); }
        vc_destroy(c);
    }

    printf("== HP-16C stock layout\n");
    if ((c = open_fw(ST, VC_ENGINE_RUNNER))) {
        snprintf(p, sizeof p, "%s/miller-rabin-standard-ready.16c", SAMPLES);
        if (load_mem(c, p)) {
            CHECK(!info(c).ext_memory, "stock firmware reported extended memory");
            compare_listing(c, MRLIST, 117, 1);
            keys(c, "2147483647 GSB A"); mr_registers(c, 10); readonly(c);
        }
        vc_destroy(c);
    }

    printf("== HP-16C bases, word sizes, complements, FLOAT, R615\n");
    if ((c = open_fw(B7, VC_ENGINE_RUNNER))) {
        keys(c, "DEC 1 6 f STO f 2 1 0 0 CHS");
        in = info(c); CHECK(in.word_size == 16 && in.complement == 2 && in.base == 10 && in.registers == 616, "ws %d comp %d base %d regs %d", in.word_size, in.complement, in.base, in.registers);
        v = stk(c, VC_REG_X); TEXT(v, "-100"); CHECK(v.kind == VC_VAL_INT && v.word == 0xFF9C && v.negative && v.number == -100, "X word %llx", (unsigned long long)v.word);
        keys(c, "HEX"); v = stk(c, VC_REG_X); TEXT(v, "FF9C"); DEC(v, "-100"); CHECK(info(c).base == 16, "base");
        keys(c, "OCT"); TEXT(stk(c, VC_REG_X), "177634");
        keys(c, "BIN"); TEXT(stk(c, VC_REG_X), "1111111110011100");
        keys(c, "f 1"); v = stk(c, VC_REG_X); DEC(v, "-99"); CHECK(info(c).complement == 1, "1's");
        keys(c, "f 3"); v = stk(c, VC_REG_X); DEC(v, "65436"); CHECK(info(c).complement == 0 && !v.negative, "UNSGN");
        keys(c, "HEX 2 6 7 STO SST 7 7 7 7 STO R/S 1 2 STO 0");
        TEXT(stk(c, VC_REG_I), "267"); v = reg(c, 615); TEXT(v, "7777"); CHECK(!strcmp(v.name, "R615"), "name %s", v.name);
        TEXT(reg(c, 0), "12"); CHECK(vc_inspect_register(c, 616, &v) == VC_ERR_ARG, "R616 exists at WSIZE 16");
        keys(c, "F F F F 1 +"); CHECK(info(c).flags & 0x10, "carry flag (4) after FFFF + 1: flags %x", info(c).flags);
        keys(c, "g 4 3"); CHECK(info(c).flags & 0x08, "SF 3: flags %x", info(c).flags);
        keys(c, "DEC 3 3 f STO"); in = info(c); CHECK(in.word_size == 33 && in.registers == 2464 / 9, "WSIZE 33: %d %d", in.word_size, in.registers);
        keys(c, "6 4 f STO 1 ENTER 2 +"); CHECK(info(c).word_size == 64, "WSIZE 64"); TEXT(stk(c, VC_REG_X), "3");
        keys(c, "f RCL 4 2 ENTER 3 /");
        in = info(c); CHECK(in.base == 0 && in.word_size == 56, "FLOAT: base %d ws %d", in.base, in.word_size);
        v = stk(c, VC_REG_X); TEXT(v, "0.6666666667"); CHECK(v.kind == VC_VAL_FLOAT, "float kind");
        keys(c, "1 EEX"); (void)0;
        readonly(c);
        vc_destroy(c);
    }

    printf("== HP-16C whole engine: R/S running state and live pointer, inspection does not disturb timing\n");
    {
        vc_calc *a = open_fw(B7, VC_ENGINE_WHOLE), *b = open_fw(B7, VC_ENGINE_WHOLE);
        snprintf(p, sizeof p, "%s/miller-rabin-ext-ready.16c", SAMPLES);
        if (a && b) {
            vc_calc *both[2] = {a, b}; int k, i, saw_run = 0, lines_seen = 0, last = -1;
            whole = 1;
            for (k = 0; k < 2; k++) {
                vc_press_key(both[k], 30); vc_run_ms(both[k], 150); vc_release_key(both[k], 30); vc_run_ms(both[k], 1500);
                keys(both[k], "BSP"); load_mem(both[k], p); keys(both[k], "1 0 0 0 7 GSB");
                if (!k) CHECK(!info(a).running, "running before the program");
                keys(both[k], "A");
            }
            for (i = 0; i < 400; i++) {
                vc_run_ms(a, 10); vc_run_ms(b, 10);
                in = info(a); (void)stk(a, VC_REG_X); (void)reg(a, 0);
                if (in.running) { saw_run++; if (in.line != last) { lines_seen++; last = in.line; } }
            }
            CHECK(saw_run > 20 && lines_seen > 10, "running seen %d ticks, %d different lines", saw_run, lines_seen);
            for (i = 0; i < 12000 && info(a).running; i++) { vc_run_ms(a, 10); vc_run_ms(b, 10); }
            vc_run_ms(a, 500); vc_run_ms(b, 500);
            CHECK(!info(a).running, "still running after 2 minutes");
            printf("  10007 GSB A ran about %d ms\n", (400 + i) * 10);
            TEXT(stk(a, VC_REG_X), "1"); TEXT(reg(a, 0), "10007");
            uint8_t ma[VC_MEMORY_SIZE], mb[VC_MEMORY_SIZE]; vc_stats sa, sb;
            vc_save_memory(a, ma); vc_save_memory(b, mb); vc_get_stats(a, &sa); vc_get_stats(b, &sb);
            CHECK(!memcmp(ma, mb, sizeof ma) && sa.arm_instructions == sb.arm_instructions && sa.sim_ms == sb.sim_ms && sa.sleeps == sb.sleeps,
                  "inspected run differs from the plain run (arm %llu vs %llu)", (unsigned long long)sa.arm_instructions, (unsigned long long)sb.arm_instructions);
            printf("  running for %d of 400 ticks, %d pointer moves seen; inspected run == plain run (%llu ARM instructions)\n", saw_run, lines_seen, (unsigned long long)sa.arm_instructions);
            whole = 0;
        }
        if (a) vc_destroy(a);
        if (b) vc_destroy(b);
    }

    printf("== HP-15C CE (15.0 firmware)\n");
    if ((c = open_fw(env("F15", "/workspace/Firmware/hp15c-3rom-mute120-fsm5-rel80-opt5cycle-debounce-menu-stopwatch-48fix-keys-batt-trim4-show-C8C8.bin"), VC_ENGINE_WHOLE))) {
        extern void test15_boot(vc_calc *c);
        whole = 1; test15_boot(c); whole = 0;
        vc_destroy(c);
    }
    printf("== HP-12C CE multi (%s): 16C, 15C and 12C modes\n", strrchr(MULTI, '/') ? strrchr(MULTI, '/') + 1 : MULTI);
    if ((c = open_fw(MULTI, VC_ENGINE_WHOLE))) {
        extern int multi_tests(vc_calc *c);
        whole = 1; multi_tests(c); whole = 0;
        vc_destroy(c);
    }

    printf("%d checks, %d failed\n%s\n", checks, fails, fails ? "INSPECT: FAIL" : "INSPECT: OK");
    return fails != 0;
}

/* ------------------------------------------------------------ multi */
const char *text(vc_calc *c) { static char t[64]; vc_display d; vc_get_display_cells(c, &d); vc_display_text(&d, t, sizeof t); return t; }
static void mtap(vc_calc *c, int k) { vc_press_key(c, k); vc_run_ms(c, 120); vc_release_key(c, k); vc_run_ms(c, 600); }
static int sw(vc_calc *c, int pick) {      /* same as test_multi.c: off, g-ENTER-ON, 4, model key */
    mtap(c, 30); vc_run_ms(c, 500);
    if (strspn(text(c), " ") != strlen(text(c))) { mtap(c, 30); vc_run_ms(c, 500); }
    vc_press_key(c, 32); vc_press_key(c, 25); vc_run_ms(c, 100); vc_press_key(c, 30); vc_run_ms(c, 300);
    vc_release_key(c, 30); vc_run_ms(c, 200); vc_release_key(c, 25); vc_release_key(c, 32); vc_run_ms(c, 800);
    mtap(c, 16); mtap(c, pick); vc_run_ms(c, 2000); mtap(c, 24);
    return vc_model(c);
}
static void test15(vc_calc *c, int variant, int regs) {
    vc_inspect_info in; vc_value v; static vc_prog_line p[100]; int n; (void)v;
    keys(c, "1 2 3 STO 0 4 5 6 STO 1 7 8 9 STO 2 1 . 2 5 STO TAN 9 STO . 9 3 CHS EEX CHS 7 STO 5 2 ENTER 3 + 1 ENTER 3 ÷");
    keys(c, "g RCL"); printf("  15C MEM: |%s|\n", text(c)); vc_run_ms(c, 3000);
    in = info(c); CHECK(in.model == 15 && in.variant == variant && in.registers == regs, "15C: model %d variant %d regs %d", in.model, in.variant, in.registers);
    TEXT(stk(c, VC_REG_X), "0.3333333333"); TEXT(stk(c, VC_REG_LASTX), "3"); TEXT(stk(c, VC_REG_Y), "5"); TEXT(stk(c, VC_REG_I), "1.25");
    TEXT(reg(c, 0), "123"); TEXT(reg(c, 1), "456"); TEXT(reg(c, 2), "789"); TEXT(reg(c, 19), "9"); CHECK(!strcmp(reg(c, 19).name, "R.9"), "15C name %s", reg(c, 19).name); TEXT(reg(c, 5), "-3e-7");
    CHECK(fabs(reg(c, 5).number + 3e-7) < 1e-20, "number %g", reg(c, 5).number);
    keys(c, "g R/S f BSP f SST SQRT 2 × g GSB g R/S");     /* P/R, CLEAR PRGM, LBL A 2 × RTN, P/R */
    in = info(c); n = vc_inspect_program(c, 1, p, 100);
    CHECK(in.has_program && n == 4 && in.lines == 4, "15C program: %d lines (info %d)", n, in.lines);
    if (n == 4) {
        CHECK(!strcmp(p[0].keys, "42,21,11") && !strcmp(p[0].mnemonic, "LBL A"), "15C 001 %s %s", p[0].keys, p[0].mnemonic);
        CHECK(!strcmp(p[1].keys, "2"), "15C 002 %s", p[1].keys);
        CHECK(!strcmp(p[2].keys, "20"), "15C 003 %s", p[2].keys);
        CHECK(!strcmp(p[3].keys, "43 32") && !strcmp(p[3].mnemonic, "RTN"), "15C 004 %s %s", p[3].keys, p[3].mnemonic);
    }
    keys(c, "5 GSB SQRT"); vc_run_ms(c, 1000); TEXT(stk(c, VC_REG_X), "10");
    keys(c, "GTO SQRT"); CHECK(info(c).line == 1, "15C GTO A: line %d", info(c).line);
    keys(c, "SST SST"); CHECK(info(c).line == 3, "15C SST x2: line %d", info(c).line);
    readonly(c);
}
void test15_boot(vc_calc *c) { mtap(c, 30); vc_run_ms(c, 1500); mtap(c, 24); test15(c, 150, 20); printf("  15C checked\n"); }
int multi_tests(vc_calc *c) {
    vc_inspect_info in; vc_value v; static vc_prog_line p[100]; int n;
    mtap(c, 30); vc_run_ms(c, 1500);
    /* 12C */
    keys(c, "CLx 1 2 n 3 i 4 5 PV 6 7 PMT 8 9 FV 1 1 STO 0 2 2 STO 6 3 3 STO 7 4 4 STO . 9 2 ENTER 3 +");
    in = info(c); CHECK(in.model == 12 && in.registers == 20, "12C: model %d regs %d", in.model, in.registers);
    TEXT(stk(c, VC_REG_X), "5"); TEXT(stk(c, VC_REG_Y), "44"); TEXT(stk(c, VC_REG_LASTX), "3");
    TEXT(stk(c, VC_REG_FIN_N), "12"); TEXT(stk(c, VC_REG_FIN_I), "3"); TEXT(stk(c, VC_REG_FIN_PV), "45"); TEXT(stk(c, VC_REG_FIN_PMT), "67"); TEXT(stk(c, VC_REG_FIN_FV), "89");
    TEXT(reg(c, 0), "11"); TEXT(reg(c, 6), "22"); TEXT(reg(c, 7), "33"); v = reg(c, 19); TEXT(v, "44"); CHECK(!strcmp(v.name, "R.9"), "12C name %s", v.name);
    CHECK(vc_inspect_stack(c, VC_REG_I, &v) == VC_ERR_ARG, "12C has no I");
    readonly(c);
    CHECK(sw(c, 3) == 15, "switch to 15C");
    test15(c, 152, 95);
    printf("  12C and 15C checked\n");
    /* 16C mode (Path-C) */
    CHECK(sw(c, 5) == 16, "switch to 16C");
    keys(c, "HEX 1 0 f STO 2 6 7 STO SST 5 5 5 STO R/S A B STO 0");
    in = info(c); CHECK(in.model == 16 && in.ext_memory && in.word_size == 16 && in.registers == 616, "multi 16C: ext %d ws %d regs %d", in.ext_memory, in.word_size, in.registers);
    TEXT(reg(c, 615), "555"); TEXT(reg(c, 0), "AB"); TEXT(stk(c, VC_REG_I), "267");
    keys(c, "g R/S f R↓ g GTO A 3 + g GSB g R/S");   /* P/R, CLEAR PRGM, LBL A 3 + RTN, P/R */
    n = vc_inspect_program(c, 1, p, 100);
    CHECK(n == 4 && !strcmp(p[0].keys, "43,22, A") && !strcmp(p[2].keys, "40") && !strcmp(p[3].mnemonic, "RTN"), "multi 16C program: %d lines '%s' '%s'", n, n ? p[0].keys : "", n > 3 ? p[3].mnemonic : "");
    keys(c, "GTO A SST"); CHECK(info(c).line == 2, "multi 16C SST: line %d", info(c).line);
    readonly(c);
    printf("  multi 16C checked\n");
    return 0;
}
