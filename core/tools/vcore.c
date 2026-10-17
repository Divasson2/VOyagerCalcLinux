/* vcore.c - command-line test driver for the native Voyager core.
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 José González Divassón
 *
 *   vcore --firmware F.bin --info
 *   vcore --firmware F.bin [--whole] [--load M.16c] [--fmem] --keys "2147483647 GSB A" --show [--save OUT.16c]
 *   vcore --firmware F.bin [--load M.16c] --keys "..." --inspect --list   (registers, modes, program listing)
 *   vcore --firmware F.bin --protocol SEQS.txt      (one key sequence per line; per-key log for comparisons)
 */
#include "voyager_core.h"
#include "../src/sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

/* key names of the active model first, then any model's (matrix positions are shared) */
static int anykey(vc_calc *c, const char *name) {
    int k = vc_key_code(vc_model(c), name);
    if (k < 0) k = vc_key_code(VC_MODEL_16C, name);
    if (k < 0) k = vc_key_code(VC_MODEL_15C, name);
    if (k < 0) k = vc_key_code(VC_MODEL_12C, name);
    return k;
}
static double now(void) { return (double)clock() / CLOCKS_PER_SEC; }  /* CPU time; portable C */

static uint8_t *readfile(const char *p, size_t *n) {
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long l = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc((size_t)l ? (size_t)l : 1);
    if (b && fread(b, 1, (size_t)l, f) != (size_t)l) { free(b); b = NULL; }
    fclose(f); *n = (size_t)l; return b;
}
static void die(const char *m, vc_calc *c) { fprintf(stderr, "error: %s%s%s\n", m, c ? ": " : "", c ? vc_last_error(c) : ""); exit(1); }

static const char *resname(int r) {
    switch (r) { case VC_KEY_DONE: return "done"; case VC_KEY_LIMIT: return "limit"; case VC_KEY_HELD: return "held";
    case VC_ERR_HOLD: return "hold-error"; case VC_ERR_FAULT: return "fault"; default: return "error"; }
}
static void memhash(vc_calc *c, char out[17]) {
    uint8_t m[VC_MEMORY_SIZE], h[32]; int i;
    vc_save_memory(c, m); vc_sha256(m, sizeof m, h);
    for (i = 0; i < 8; i++) snprintf(out + 2 * i, 3, "%02x", h[i]);
}

/* type a script: digits groups are typed digit by digit (as the Python runner does) */
static int run_keys(vc_calc *c, const char *script, uint64_t max_steps, int trace, int whole) {
    char buf[4096]; snprintf(buf, sizeof buf, "%s", script);
    char *save = NULL, *tok = strtok_r(buf, " \t\r\n", &save);
    int r = VC_KEY_DONE;
    while (tok) {
        char one[2] = {0, 0};
        size_t L = strlen(tok), i, isnum = L > 1;
        for (i = 0; i < L; i++) if (!isdigit((unsigned char)tok[i])) isnum = 0;
        size_t cnt = isnum ? L : 1;
        for (i = 0; i < cnt; i++) {
            const char *name = tok;
            if (isnum) { one[0] = tok[i]; name = one; }
            if (whole && !strncmp(name, "wait:", 5)) { vc_run_ms(c, (uint32_t)atoi(name + 5)); continue; }
            if (whole && strchr(name, '+') && strcmp(name, "+")) { /* chord, e.g. g+ENTER+ON */
                char cb[64], *sv = NULL, *t; snprintf(cb, sizeof cb, "%s", name);
                for (t = strtok_r(cb, "+", &sv); t; t = strtok_r(NULL, "+", &sv)) {
                    int k = anykey(c, t); if (k < 0) { fprintf(stderr, "unknown key %s\n", t); exit(2); }
                    vc_press_key(c, k);
                }
                vc_run_ms(c, 300);
                { int k; for (k = 0; k < 40; k++) vc_release_key(c, k); }
                r = vc_run_ms(c, 1000);
                if (trace) { char d[16]; vc_get_display(c, d, sizeof d); printf("  %10s  %s\n", name, d); }
                if (r < 0) return r;
                continue;
            }
            int code = anykey(c, name);
            if (code < 0) { fprintf(stderr, "unknown key %s\n", name); exit(2); }
            if (whole && code == 30) { vc_press_key(c, 30); vc_run_ms(c, 150); vc_release_key(c, 30); r = vc_run_ms(c, 1500); }
            else r = vc_key(c, code, max_steps);
            if (trace) { char d[16]; vc_get_display(c, d, sizeof d); printf("  %6s  %s\n", name, d); }
            if (r == VC_KEY_HELD) { char d[16]; vc_get_held_display(c, d, sizeof d); printf("  (while f %s was held the display showed: %s)\n", name, d); }
            if (r == VC_KEY_LIMIT) { printf("  stopped: --max-steps %llu Nut instructions used up\n", (unsigned long long)max_steps); return r; }
            if (r < 0) return r;
        }
        tok = strtok_r(NULL, " \t\r\n", &save);
    }
    return r;
}

static int protocol(vc_calc *c, const char *path, const uint8_t *mem, uint64_t max_steps, int whole) {
    FILE *f = fopen(path, "r"); if (!f) die("cannot open protocol file", NULL);
    char line[8192]; int seq = 0;
    while (fgets(line, sizeof line, f)) {
        char *p = line; while (*p == ' ') p++;
        if (*p == '#' || *p == '\n' || !*p) continue;
        if (whole) {
            if (vc_start(c, VC_ENGINE_WHOLE, 0)) die("start", c);
            vc_press_key(c, 30); vc_run_ms(c, 150); vc_release_key(c, 30); vc_run_ms(c, 1500);
            vc_key(c, 24, 0);
        } else if (vc_start(c, VC_ENGINE_RUNNER, 1)) die("start", c);
        if (mem) vc_load_memory(c, mem);
        char *save = NULL, *tok = strtok_r(p, " \t\r\n", &save); int k = 0;
        while (tok) {
            int code = anykey(c, tok);
            if (code < 0) { fprintf(stderr, "unknown key %s\n", tok); exit(2); }
            int r;
            if (whole && code == 30) { vc_press_key(c, 30); vc_run_ms(c, 150); vc_release_key(c, 30); r = vc_run_ms(c, 1500); }
            else r = vc_key(c, code, max_steps);
            char d[16], h[17]; vc_get_display(c, d, sizeof d); memhash(c, h);
            if (whole) {
                char fr[48]; vc_stats st; vc_last_frame(c, fr, sizeof fr); vc_get_stats(c, &st);
                printf("S%d K%d %s |%s| %s frames=%u last=|%s| ms=%llu sleeps=%llu\n", seq, k, tok, d, h, vc_frame_count(c), fr,
                       (unsigned long long)st.sim_ms, (unsigned long long)st.sleeps);
            } else
            printf("S%d K%d %s %s |%s| %s\n", seq, k, tok, resname(r), d, h);
            k++;
            if (r < 0 || r == VC_KEY_LIMIT) break;
            tok = strtok_r(NULL, " \t\r\n", &save);
        }
        seq++;
    }
    fclose(f); return 0;
}

/* --inspect: modes, stack and every storage register; words in hex (floats: raw BCD), then text */
static void print_inspect(vc_calc *c) {
    vc_inspect_info in; vc_value v; int i;
    if (vc_inspect(c, &in)) { printf("inspect: not available\n"); return; }
    printf("inspect: model %d variant %d base %d ws %d comp %d flags 0x%02x prgm %d running %d line %d lines %d regs %d ext %d\n",
           in.model, in.variant, in.base, in.word_size, in.complement, in.flags, in.prgm_mode, in.running, in.line, in.lines, in.registers, in.ext_memory);
    for (i = 0; i < VC_REG_COUNT; i++)
        if (!vc_inspect_stack(c, i, &v)) printf("%-6s %016llx  %s  (%s)\n", v.name, (unsigned long long)(v.kind == VC_VAL_INT ? v.word : v.raw), v.text, v.dec);
    for (i = 0; i < in.registers; i++)
        if (!vc_inspect_register(c, i, &v)) printf("%-6s %016llx  %s  (%s)\n", v.name, (unsigned long long)(v.kind == VC_VAL_INT ? v.word : v.raw), v.text, v.dec);
}
static void print_list(vc_calc *c) {
    vc_inspect_info in; static vc_prog_line p[1000]; int i, n;
    if (vc_inspect(c, &in) || !in.has_program) { printf("program: not available\n"); return; }
    n = vc_inspect_program(c, 1, p, 1000);
    printf("program: %d lines, at line %03d\n", n, in.line);
    for (i = 0; i < n; i++) printf("%03d- %-10s %s%s%s\n", p[i].line, p[i].keys, p[i].prefix ? (char[]){p[i].prefix, ' ', 0} : "", "", p[i].mnemonic);
}

int main(int argc, char **argv) {
    const char *fwp = NULL, *load = NULL, *savep = NULL, *proto = NULL;
    const char *keys[64]; int nkeys = 0, inspect = 0, list = 0, lcd = 0, info = 0, whole = 0, fmem = 0, show = 0, trace = 0, count = 0, bench = 0, frames = 0;
    uint64_t max_steps = 0; int i; double battery = 3.0;
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
#define ARG() (i + 1 < argc ? argv[++i] : (die("missing value", NULL), ""))
        if (!strcmp(a, "--firmware") || !strcmp(a, "-f")) fwp = ARG();
        else if (!strcmp(a, "--info")) info = 1;
        else if (!strcmp(a, "--whole")) whole = 1;
        else if (!strcmp(a, "--load")) load = ARG();
        else if (!strcmp(a, "--save")) savep = ARG();
        else if (!strcmp(a, "--keys") || !strcmp(a, "-k")) { if (nkeys < 64) keys[nkeys++] = ARG(); }
        else if (!strcmp(a, "--fmem")) fmem = 1;
        else if (!strcmp(a, "--show")) show = 1;
        else if (!strcmp(a, "--trace")) trace = 1;
        else if (!strcmp(a, "--count")) count = 1;
        else if (!strcmp(a, "--bench")) bench = 1;
        else if (!strcmp(a, "--frames")) frames = 1;
        else if (!strcmp(a, "--lcd")) lcd = 1;
        else if (!strcmp(a, "--inspect")) inspect = 1;
        else if (!strcmp(a, "--list")) list = 1;
        else if (!strcmp(a, "--battery")) battery = atof(ARG());
        else if (!strcmp(a, "--max-steps")) max_steps = strtoull(ARG(), NULL, 10);
        else if (!strcmp(a, "--protocol")) proto = ARG();
        else { fprintf(stderr, "unknown option %s\n", a); return 2; }
    }
    if (!fwp) { fprintf(stderr, "usage: vcore --firmware FILE.bin [--info] [--whole] [--load M.16c] [--fmem] [--keys \"...\"] [--show] [--trace] [--save OUT.16c] [--max-steps N] [--count] [--bench] [--inspect] [--list] [--protocol SEQS.txt]\n"); return 2; }
    size_t n; uint8_t *fw = readfile(fwp, &n); if (!fw) die("cannot read firmware file", NULL);
    vc_calc *c = vc_create(); if (!c) die("out of memory", NULL);
    int r = vc_load_firmware(c, fw, n); free(fw);
    vc_set_battery(c, battery);
    printf("firmware: %s  (%s)\n", fwp, vc_firmware_info(c));
    if (vc_firmware_checksum(c) >= 0) printf("checksum: %02X%02X%s\n", vc_firmware_checksum(c), vc_firmware_checksum(c), vc_firmware_stripped(c) ? "   (full flash dump: first 0x4000 bytes dropped)" : "");
    if (r) die("firmware", c);
    if (info) { printf("program runner: %s   whole-calculator model: yes\n", vc_firmware_family(c) == VC_FAMILY_STOCK ? "no" : "yes"); return 0; }
    uint8_t *mem = NULL;
    if (load) { size_t m; mem = readfile(load, &m); if (!mem || m != VC_MEMORY_SIZE) die("a .16c memory file must be exactly 2048 bytes", NULL); }
    if (proto) { protocol(c, proto, mem, max_steps, whole); vc_destroy(c); free(mem); return 0; }
    double t0 = now();
    if (whole) {
        if (vc_start(c, VC_ENGINE_WHOLE, 0)) die("start", c);
        /* like voyagersim --whole: turn on, then BSP to clear 'Pr Error' */
        vc_press_key(c, 30); vc_run_ms(c, 150); vc_release_key(c, 30); vc_run_ms(c, 1500);
        vc_key(c, 24, 0);
        if (mem) { vc_load_memory(c, mem); printf("loaded %s\n", load); }
    } else {
        if (vc_start(c, VC_ENGINE_RUNNER, 1)) die("start", c);
        if (mem) { vc_load_memory(c, mem); printf("loaded %s\n", load); }
        if (fmem) {
            r = vc_key(c, 31, 0); if (r >= 0) r = vc_key(c, 36, 0);
            char d[16]; if (r == VC_KEY_HELD) vc_get_held_display(c, d, sizeof d); else vc_get_display(c, d, sizeof d);
            char *s = d; while (*s == ' ') s++;
            size_t L = strlen(s); while (L && s[L - 1] == ' ') s[--L] = 0;
            printf("f MEM: %s\n", s);
        }
    }
    double t1 = now();
    vc_stats st0; vc_get_stats(c, &st0);
    for (i = 0; i < nkeys; i++) {
        r = run_keys(c, keys[i], max_steps, trace, whole);
        if (r < 0) die("key", c);
    }
    double t2 = now();
    vc_stats st; vc_get_stats(c, &st);
    if (show) {
        char d[16]; vc_get_display(c, d, sizeof d);
        if (whole) { char fr[48]; vc_last_frame(c, fr, sizeof fr); printf("display: %s%s%s%s\n", d, fr[0] ? "   (last painted frame: " : "", fr, fr[0] ? ")" : ""); }
        else printf("display: %s\n", d);
        uint32_t an = vc_get_annunciators(c);
        if (an) printf("annunciators:%s%s%s%s%s%s\n", an & VC_ANN_F ? " f" : "", an & VC_ANN_G ? " g" : "", an & VC_ANN_C ? " C" : "",
                       an & VC_ANN_G_OV ? " G" : "", an & VC_ANN_PRGM ? " PRGM" : "", an & VC_ANN_BATT ? " BATT" : "");
    }
    if (inspect) print_inspect(c);
    if (list) print_list(c);
    if (lcd) { uint8_t sg[VC_LCD_BYTES]; int j; vc_get_lcd_segments(c, sg); printf("lcd:"); for (j = 0; j < VC_LCD_BYTES; j++) printf("%s%02x", j % 8 ? "" : " ", sg[j]); printf("\n"); }
    if (frames) { char fr[48]; vc_last_frame(c, fr, sizeof fr); printf("frames painted: %u, last: %s\n", vc_frame_count(c), fr); }
    if (count) { char h[17]; memhash(c, h); printf("Nut instructions: %llu   ARM instructions: %llu   memory hash %s\n", (unsigned long long)st.nut_instructions, (unsigned long long)st.arm_instructions, h); }
    if (bench) {
        double dt = t2 - t1; uint64_t arm = st.arm_instructions - st0.arm_instructions, nut = st.nut_instructions - st0.nut_instructions;
        printf("bench: start %.3f s, keys %.3f s, %llu ARM instr (%.1f M/s), %llu Nut instr (%.2f M/s)", t1 - t0, dt,
               (unsigned long long)arm, dt > 0 ? arm / dt / 1e6 : 0, (unsigned long long)nut, dt > 0 ? nut / dt / 1e6 : 0);
        if (whole) printf(", simulated %llu ms, %llu sleeps", (unsigned long long)st.sim_ms, (unsigned long long)st.sleeps);
        printf("\n");
    }
    if (savep) {
        FILE *f = fopen(savep, "rb"); if (f) { fclose(f); fprintf(stderr, "%s already exists; nothing was overwritten\n", savep); return 1; }
        uint8_t m[VC_MEMORY_SIZE]; vc_save_memory(c, m);
        f = fopen(savep, "wb"); if (!f || fwrite(m, 1, sizeof m, f) != sizeof m) die("cannot write", NULL);
        fclose(f); printf("saved %s\n", savep);
    }
    vc_destroy(c); free(mem);
    return 0;
}
