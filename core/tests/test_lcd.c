/* test_lcd.c - the display drawn from the LCD memory must show the same characters as the
 * Nut display registers, in the whole-calculator engine, for random key sequences.
 * SPDX-License-Identifier: MIT */
#include "voyager_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const char *K[] = {"0","1","2","3","4","5","6","7","8","9","A","B","C","D","E","F","ENTER","+","-","*","/","CHS","BSP","x<>y","RDN","HEX","DEC","OCT","BIN",".","f","g","STO","RCL"};
int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: test_lcd FIRMWARE.bin [sequences]\n"); return 2; }
    FILE *f = fopen(argv[1], "rb"); if (!f) return 2;
    static uint8_t b[0x40000]; size_t n = fread(b, 1, sizeof b, f); fclose(f);
    int nseq = argc > 2 ? atoi(argv[2]) : 60, bad = 0, total = 0;
    vc_calc *c = vc_create(); if (vc_load_firmware(c, b, n)) { fprintf(stderr, "%s\n", vc_last_error(c)); return 2; }
    const char *pre[] = {"", "f RCL 2", "f RCL 4", "f RCL .", "DEC", "BIN", "OCT"};
    srand(7);
    for (int s = 0; s < nseq; s++) {
        vc_start(c, VC_ENGINE_WHOLE, 0); vc_press_key(c, 30); vc_run_ms(c, 150); vc_release_key(c, 30); vc_run_ms(c, 1500); vc_key(c, 24, 0);
        char pb[32]; snprintf(pb, sizeof pb, "%s", pre[s % 7]); char *sv, *t;
        for (t = strtok_r(pb, " ", &sv); t; t = strtok_r(NULL, " ", &sv)) vc_key(c, vc_key_code(16, t), 0);
        for (int k = 0; k < 15; k++) {
            vc_key(c, vc_key_code(16, K[rand() % 34]), 0);
            char nut[16], lcd[40], strip[40]; vc_display d; size_t j = 0;
            vc_get_display(c, nut, sizeof nut); vc_get_display_cells(c, &d); vc_display_text(&d, lcd, sizeof lcd);
            for (size_t i = 0; lcd[i]; i++) if (lcd[i] != ',' && lcd[i] != '.' && !(i == 0 && d.minus)) strip[j++] = lcd[i];
            strip[j] = 0; total++;
            if (strcmp(strip, nut)) { if (bad++ < 5) printf("MISMATCH nut |%s| lcd |%s|\n", nut, lcd); }
        }
    }
    printf("LCD display check: %d of %d states identical to the Nut display registers\n", total - bad, total);
    vc_destroy(c);
    return bad != 0;
}
