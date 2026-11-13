/* test_pool206_fw.c - 0.7.7: pool206 firmware identification on real firmware files.
 * Reads the user's firmware from VC_FIRMWARE_DIR (never in the repository); skips without it.
 * Checks the catalogue match (CE code + exact model set), the 16C layout, and the ROM-constant
 * fallback inside the 3-ROM / Multi wrappers (a copy with one padding byte changed has no
 * catalogue match, so only the ROM detection is left).
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 José González Divassón */
#include "voyager_core.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, checks, seen;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint8_t *read_all(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    static uint8_t buf[0x40000]; *n = fread(buf, 1, sizeof buf, f); fclose(f);
    uint8_t *b = malloc(*n ? *n : 1); if (b) memcpy(b, buf, *n); return b;
}
/* first file in dir whose name contains every needle */
static uint8_t *find_fw(const char *dir, const char *a, const char *b, size_t *n) {
    DIR *dp = opendir(dir); struct dirent *e; uint8_t *r = NULL;
    while (dp && (e = readdir(dp)) && !r) {
        if (!strstr(e->d_name, a) || (b && !strstr(e->d_name, b)) || !strstr(e->d_name, ".bin")) continue;
        char p[2048]; snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
        r = read_all(p, n);
    }
    if (dp) closedir(dp);
    return r;
}

static void one(const char *dir, const char *a, const char *b, int ce, unsigned modes, int expect_layout, int rom_layout) {
    size_t n = 0; uint8_t *d = find_fw(dir, a, b, &n);
    if (!d) { printf("  skip %s%s (not in folder)\n", a, b ? b : ""); return; }
    seen++;
    vc_calc *c = vc_create();
    CHECK(vc_load_firmware(c, d, n) == VC_OK, "%s load: %s", a, vc_last_error(c));
    CHECK(vc_firmware_checksum(c) == ce, "%s CE %02X", a, vc_firmware_checksum(c));
    const vc_firmware_profile *p = vc_firmware_known_profile(c);
    CHECK(p && p->checksum == ce && p->modes == modes, "%s profile %s", a, p ? p->display_name : "(none)");
    CHECK(vc_firmware_mem16(c) == expect_layout, "%s catalogue layout %d", a, vc_firmware_mem16(c));
    int v[4], k = vc_firmware_images(c, v, 4); unsigned m = 0;
    for (int i = 0; i < k; i++) m |= v[i] == 120 ? VC_FW_12C : v[i] == 150 ? VC_FW_150 : v[i] == 152 ? VC_FW_152 : v[i] == 160 ? VC_FW_16C : 0;
    CHECK(m == modes, "%s ROM model set %u", a, m);
    vc_destroy(c);
    /* ROM-only path: change one padding byte so the CE code no longer matches the catalogue. */
    /* +1 would turn E9E9 into EAEA's code, C8C8 into C9C9's: pick a delta with no catalogue row. */
    const uint8_t orig = d[0x1BFEE];
    c = NULL;
    for (int delta = 1; delta < 256; delta++) {
        if (c) vc_destroy(c);
        d[0x1BFEE] = (uint8_t)(orig + delta);
        c = vc_create();
        if (vc_load_firmware(c, d, n) != VC_OK || !vc_firmware_known_profile(c)) break;
    }
    CHECK(vc_firmware_checksum(c) != ce, "%s patched load", a);
    CHECK(vc_firmware_known_profile(c) == NULL, "%s patched copy still matched a profile", a);
    CHECK(vc_firmware_mem16(c) == rom_layout, "%s ROM-detected layout %d, want %d", a, vc_firmware_mem16(c), rom_layout);
    printf("  %s: CE %02X%02X, %s, ROM fallback %d\n", a, ce, ce, p ? p->display_name : "?", vc_firmware_mem16(c));
    vc_destroy(c);
    free(d);
}

int main(void) {
    const char *dir = getenv("VC_FIRMWARE_DIR");
    if (!dir) { printf("test_pool206_fw: VC_FIRMWARE_DIR not set; skipped\n"); return 0; }
    printf("== pool206 wrappers and EAEA\n");
    one(dir, "HP15c-3ROM-pool206", "C9C9", 0xC9, VC_FW_150 | VC_FW_152 | VC_FW_16C, VC_MEM16_LAYOUT206, VC_MEM16_LAYOUT206);
    one(dir, "HP12c-Multi-pool206", "ECEC", 0xEC, VC_FW_12C | VC_FW_152 | VC_FW_16C, VC_MEM16_LAYOUT206, VC_MEM16_LAYOUT206);
    one(dir, "HP16c-enhanced-152-pool206-EAEA", NULL, 0xEA, VC_FW_152 | VC_FW_16C, VC_MEM16_LAYOUT206, VC_MEM16_LAYOUT206);
    printf("== stock wrappers stay stock; retired extended builds are unsupported (catalogue) or undetermined (ROM only)\n");
    /* 0.7.7: no 176-slot layout. A patched copy of a retired extended build has no catalogue row, so the ROM
     * detection must not guess: it reports UNDETERMINED (the app then asks stock or 206). */
    one(dir, "HP12c-multi-152-ext16-feat-E8E8", NULL, 0xE8, VC_FW_12C | VC_FW_152 | VC_FW_16C, VC_MEM16_UNSUPPORTED, VC_MEM16_UNDETERMINED);
    one(dir, "HP12c-multi-152-ext16-E3E3", NULL, 0xE3, VC_FW_12C | VC_FW_152 | VC_FW_16C, VC_MEM16_UNSUPPORTED, VC_MEM16_UNDETERMINED);
    one(dir, "HP12c-multi-152-std16-E6E6", NULL, 0xE6, VC_FW_12C | VC_FW_152 | VC_FW_16C, VC_MEM16_STD, VC_MEM16_STD);
    one(dir, "HP16c-enhanced-152-ext16-E9E9", NULL, 0xE9, VC_FW_152 | VC_FW_16C, VC_MEM16_UNSUPPORTED, VC_MEM16_UNDETERMINED);
    one(dir, "trim4-show-C8C8", NULL, 0xC8, VC_FW_150 | VC_FW_152 | VC_FW_16C, VC_MEM16_STD, VC_MEM16_STD);
    printf("test_pool206_fw: %d firmware files, %d checks, %d failures\n", seen, checks, fails);
    return fails ? 1 : 0;
}
