/* test_multi.c - HP-12C CE multi firmware: boot as 12C, switch to 15C and 16C with the
 * calculator's own service menu (g-ENTER-ON, 4, model key), and check vc_model follows.
 * SPDX-License-Identifier: MIT */
#include "voyager_core.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static vc_calc *c;
static const char *text(void);
static void tap(int k) { vc_press_key(c, k); vc_run_ms(c, 120); vc_release_key(c, k); vc_run_ms(c, 600); if (getenv("DBG")) printf("   tap %d -> |%s|\n", k, text()); }
static const char *text(void) { static char t[64]; vc_display d; vc_get_display_cells(c, &d); vc_display_text(&d, t, sizeof t); return t; }
static int sw(int pick) {   /* off, g-ENTER-ON, 4, pick, clear Pr Error */
    tap(30); vc_run_ms(c, 500);
    if (strspn(text(), " ") != strlen(text())) { tap(30); vc_run_ms(c, 500); }   /* first ON only ended a message */
    vc_press_key(c, 32); vc_press_key(c, 25); vc_run_ms(c, 100); vc_press_key(c, 30); vc_run_ms(c, 300);
    vc_release_key(c, 30); vc_run_ms(c, 200); vc_release_key(c, 25); vc_release_key(c, 32); vc_run_ms(c, 800);
    if (getenv("DBG")) printf("  menu |%s|\n", text());
    tap(16); if (getenv("DBG")) printf("  4 |%s|\n", text());
    tap(pick); vc_run_ms(c, 2000); tap(24);
    return vc_model(c);
}
int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: test_multi HP12c-multi.bin\n"); return 2; }
    FILE *f = fopen(argv[1], "rb"); if (!f) return 2;
    static uint8_t b[0x40000]; size_t n = fread(b, 1, sizeof b, f); fclose(f);
    int fails = 0;
    c = vc_create(); if (vc_load_firmware(c, b, n)) { fprintf(stderr, "%s\n", vc_last_error(c)); return 2; }
    printf("family %d: %s\n", vc_firmware_family(c), vc_firmware_info(c));
    vc_start(c, VC_ENGINE_WHOLE, 1); tap(30); vc_run_ms(c, 1500);
    printf("boot: model %d\n", vc_model(c)); fails += vc_model(c) != 12;
    int m = sw(3); tap(32); tap(34); printf("15C mode (variant %d): model %d, g MEM |%s|\n", vc_model_variant(c), m, text()); fails += m != 15 || vc_model_variant(c) != 152; vc_run_ms(c, 3000);
    m = sw(5); tap(31); tap(36); printf("16C mode: model %d, f MEM |%s|\n", m, text()); fails += m != 16; vc_run_ms(c, 3000);
    m = sw(1); printf("back: model %d\n", m); fails += m != 12;
    vc_destroy(c);
    printf("%s\n", fails ? "MULTI: FAIL" : "MULTI: OK");
    return fails != 0;
}
