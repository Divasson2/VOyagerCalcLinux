/* test_layout206.c - firmware catalogue and the two supported 16C memory layouts (stock, 206; 0.7.7).
 * Synthetic 2 KB images only: no HP ROM and no captured calculator memory.
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 José González Divassón */
#include "voyager_core.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int fails, checks;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static const uint8_t MAGIC[8] = {0xAF, 0x54, 0x39, 0xFE, 0xD9, 0x67, 0xBC, 0x12};

static void blank(uint8_t *m) {
    memset(m, 0, VC_MEMORY_SIZE);
    m[0] = 0; m[1] = 8;                       /* common prefix: LE word 8, word 0 */
    memcpy(m + 0x7F0, MAGIC, 8);
}

/* lines = (0xFC - a) * 7 + pos, with 1 <= pos <= 7. */
static void set_lines(uint8_t *m, int lines) {
    int blocks = lines == 0 ? 0 : (lines - 1) / 7;
    int pos = lines == 0 ? 0 : (lines - 1) % 7 + 1;
    int a = 0xFC - blocks;
    m[0x7E0] = (uint8_t)a;
    m[0x7E1] = (uint8_t)(pos & 15);
    m[0x7E8] = m[0x7E0];
    m[0x7E9] = m[0x7E1];
}

static int has(unsigned mask, int layout) { return (mask & (1u << layout)) != 0; }

static const vc_firmware_profile *by_code(int ce) {
    int i;
    for (i = 0; i < vc_firmware_profile_count(); i++)
        if (vc_firmware_profile_at(i)->checksum == ce) return vc_firmware_profile_at(i);
    return NULL;
}

static void catalogue(void) {
    static const int codes[] = {0x0E, 0xBA, 0xB3, 0xB4, 0xB5, 0xB7, 0xC8, 0xE3, 0xE5, 0xE6, 0xE8, 0xE9, 0xEA, 0xC9, 0xEC};
    enum { ROWS = (int)(sizeof codes / sizeof codes[0]) };
    int i, seen[256];
    memset(seen, 0, sizeof seen);
    printf("== catalogue\n");
    CHECK(vc_firmware_profile_count() == ROWS, "count %d", vc_firmware_profile_count());
    CHECK(vc_firmware_profile_at(-1) == NULL && vc_firmware_profile_at(ROWS) == NULL, "bounds");
    for (i = 0; i < ROWS; i++) {
        const vc_firmware_profile *r = vc_firmware_profile_at(i);
        CHECK(r && r->display_name && r->display_name[0] && r->notes[0] && r->sources[0] && r->switch_method[0], "row %d text", i);
        CHECK(!seen[r->checksum & 255], "duplicate CE %02X", r->checksum);
        seen[r->checksum & 255] = 1;
        if (__builtin_popcount(r->modes) > 1) {
            CHECK(r->switch_menu == 4, "%02X menu", r->checksum);
            CHECK((r->modes & VC_FW_12C) == 0 || r->key120 >= 0, "%02X key 12", r->checksum);
            CHECK((r->modes & VC_FW_150) == 0 || r->key150 >= 0, "%02X key 15.0", r->checksum);
            CHECK((r->modes & VC_FW_152) == 0 || r->key152 >= 0, "%02X key 15.2", r->checksum);
            CHECK((r->modes & VC_FW_16C) == 0 || r->key160 >= 0, "%02X key 16", r->checksum);
        } else {
            CHECK(r->switch_menu == 0 && r->key120 < 0 && r->key150 < 0 && r->key152 < 0 && r->key160 < 0, "%02X single", r->checksum);
        }
    }
    for (i = 0; i < ROWS; i++) CHECK(by_code(codes[i]) != NULL, "missing %02X", codes[i]);
    CHECK(by_code(0x0E)->mem16_layout == VC_MEM16_STD, "0E");
    /* 0.7.7: only stock and 206 are supported; every other 16C pool is UNSUPPORTED, never another layout. */
    for (i = 0; i < ROWS; i++) {
        int l = vc_firmware_profile_at(i)->mem16_layout;
        CHECK(l == VC_MEM16_STD || l == VC_MEM16_LAYOUT206 || l == VC_MEM16_UNSUPPORTED, "%02X layout %d", vc_firmware_profile_at(i)->checksum, l);
        CHECK(strstr(vc_firmware_profile_at(i)->notes, "176") == NULL && strstr(vc_firmware_profile_at(i)->notes, "616") == NULL, "%02X notes mention 176/616", vc_firmware_profile_at(i)->checksum);
    }
    CHECK(by_code(0xB3)->mem16_layout == VC_MEM16_UNSUPPORTED, "B3");
    CHECK(by_code(0xB4)->mem16_layout == VC_MEM16_UNSUPPORTED, "B4");
    CHECK(by_code(0xB5)->mem16_layout == VC_MEM16_UNSUPPORTED, "B5");
    CHECK(by_code(0xB7)->mem16_layout == VC_MEM16_UNSUPPORTED, "B7");
    CHECK(by_code(0xC8)->modes == (VC_FW_150 | VC_FW_152 | VC_FW_16C) && by_code(0xC8)->mem16_layout == VC_MEM16_STD, "C8");
    CHECK(by_code(0xE3)->mem16_layout == VC_MEM16_UNSUPPORTED, "E3");
    CHECK(by_code(0xE5)->mem16_layout == VC_MEM16_STD && by_code(0xE6)->mem16_layout == VC_MEM16_STD, "E5/E6");
    CHECK(by_code(0xE8)->mem16_layout == VC_MEM16_UNSUPPORTED && by_code(0xE8)->key160 == 5, "E8");
    CHECK(by_code(0xE9)->mem16_layout == VC_MEM16_UNSUPPORTED && by_code(0xE9)->per_mode_slots && by_code(0xE9)->key160 == 1 && by_code(0xE9)->key152 == 3, "E9");
    CHECK(strstr(by_code(0xB7)->notes, "Retired") != NULL && strstr(by_code(0xE9)->notes, "Retired") != NULL, "retired notes");
    CHECK(by_code(0xEA)->mem16_layout == VC_MEM16_LAYOUT206 && by_code(0xEA)->per_mode_slots && by_code(0xEA)->key152 == 3, "EA");
    CHECK(strcmp(by_code(0xEA)->display_name, "HP-16C Enhanced EAEA") == 0, "EA name %s", by_code(0xEA)->display_name);
    CHECK(strstr(by_code(0xEA)->display_name, "TEST") == NULL, "EAEA is a release, not a test build");
    CHECK(strstr(by_code(0xEA)->notes, "automatically") != NULL && strstr(by_code(0xEA)->notes, "Undo Load") != NULL, "EA notes %s", by_code(0xEA)->notes);
    CHECK(strstr(by_code(0xEA)->notes, "explicit conversion") == NULL, "EA notes still say explicit conversion");
    CHECK(strstr(by_code(0xEA)->sources, "HP16C-CE-enhanced-152-EAEA-guide.pdf") != NULL, "EA guide");
    CHECK(strstr(by_code(0xEA)->sources, "HP16C-EAEA-HARDWARE-NOTES.md") != NULL, "EA hardware notes");
    CHECK(strstr(by_code(0xB3)->display_name, "TEST") != NULL && strstr(by_code(0xB4)->display_name, "TEST") != NULL, "historical TEST names stay");
    CHECK(by_code(0xE9)->per_mode_slots && by_code(0xEA)->per_mode_slots, "slots");
    /* 0.7.7: the pool206 VMS1 wrappers. Model sets come from the ROM images found in the binaries
     * (C9C9: 15.0 + 15.2 + 16C; ECEC: 12C + 15.2 + 16C); menu-4 keys from their guides (top-row
     * positions 2/3, 4/5, 6/7 = matrix keys 1, 3, 5). */
    {
        const vc_firmware_profile *c9 = by_code(0xC9), *ec = by_code(0xEC);
        CHECK(c9->modes == (VC_FW_150 | VC_FW_152 | VC_FW_16C), "C9 modes %u", c9->modes);
        CHECK(c9->mem16_layout == VC_MEM16_LAYOUT206 && c9->per_mode_slots && c9->switch_menu == 4, "C9 layout/slots/menu");
        CHECK(c9->key120 < 0 && c9->key150 == 1 && c9->key152 == 3 && c9->key160 == 5, "C9 keys %d %d %d %d", c9->key120, c9->key150, c9->key152, c9->key160);
        CHECK(strstr(c9->display_name, "C9C9") != NULL && strstr(c9->display_name, "TEST") == NULL, "C9 name %s", c9->display_name);
        CHECK(strstr(c9->notes, "15.0 after reset") != NULL && strstr(c9->notes, "R0–R720") != NULL, "C9 notes %s", c9->notes);
        CHECK(ec->modes == (VC_FW_12C | VC_FW_152 | VC_FW_16C), "EC modes %u", ec->modes);
        CHECK(ec->mem16_layout == VC_MEM16_LAYOUT206 && ec->per_mode_slots && ec->switch_menu == 4, "EC layout/slots/menu");
        CHECK(ec->key120 == 1 && ec->key150 < 0 && ec->key152 == 3 && ec->key160 == 5, "EC keys %d %d %d %d", ec->key120, ec->key150, ec->key152, ec->key160);
        CHECK(strstr(ec->display_name, "ECEC") != NULL && strstr(ec->display_name, "TEST") == NULL, "EC name %s", ec->display_name);
        CHECK(strstr(ec->notes, "12C after reset") != NULL && strstr(ec->notes, "R0–R720") != NULL, "EC notes %s", ec->notes);
    }
    for (i = 0; i < ROWS; i++) {
        int ce = vc_firmware_profile_at(i)->checksum;
        if (ce != 0xE9 && ce != 0xEA && ce != 0xC9 && ce != 0xEC) CHECK(!vc_firmware_profile_at(i)->per_mode_slots, "%02X should not keep mode memory", ce);
    }
    CHECK(strstr(by_code(0xB3)->notes, "alias") != NULL, "B3 note");
}

static void window_and_empty(void) {
    uint8_t m[VC_MEMORY_SIZE];
    vc_mem16_analysis a;
    int pos, i;
    static const int touch[] = {0x6E8, 0x6EF, 0x6F0, 0x76F, 0x770, 0x7D7};
    printf("== chip window and empty images\n");
    blank(m);
    m[0x7D8] = 0xAB; m[0x7E1] = 0xA0;          /* I and I-high; the program pointer stays 0 */
    CHECK(vc_memory_analyze16(m, &a) == VC_OK, "analyze");
    CHECK(a.magic_valid && a.pointers_valid && a.upper_empty && a.lines == 0, "empty tail still empty window");
    CHECK(a.candidates == ((1u << VC_MEM16_STD) | (1u << VC_MEM16_LAYOUT206)), "blank fits exactly the two layouts (mask %X)", a.candidates);
    CHECK(a.primary_candidates == a.candidates && a.program_heads == 0, "no program head on a blank image");
    for (i = 0; i < (int)(sizeof touch / sizeof touch[0]); i++) {
        uint8_t changed[VC_MEMORY_SIZE];
        pos = touch[i];
        memcpy(changed, m, sizeof changed);
        changed[pos] = 1;
        vc_memory_analyze16(changed, &a);
        CHECK(!a.upper_empty, "upper window occupied at %04X", pos);
        CHECK(a.gap_empty == (pos >= 0x6F0), "gap %04X", pos);
        CHECK(a.stock_pool_empty == (pos < 0x6F0), "stock pool %04X", pos);
    }
    m[0x7F0] ^= 1;
    vc_memory_analyze16(m, &a);
    CHECK(!a.magic_valid && a.candidates == 0, "a flipped magic byte is not a content checksum, and it ends the hypotheses");
}

static void ambiguity_and_convert(void) {
    uint8_t img[VC_MEMORY_SIZE], wide[VC_MEMORY_SIZE], back[VC_MEMORY_SIZE], stock[VC_MEMORY_SIZE];
    vc_mem16_analysis a;
    int i;
    printf("== stock/206 ambiguity, conversion\n");
    /* A program cell below the stock pool: only 206 can hold it, and nothing ranks a third layout. */
    blank(img);
    set_lines(img, 7);
    img[0x6E0] = 0x0B;
    for (i = 1; i < 7; i++) img[0x6E0 + i] = 0;
    img[0x6E3] = 0x22;
    CHECK(vc_memory_analyze16(img, &a) == VC_OK, "low analyze");
    CHECK(a.candidates == (1u << VC_MEM16_LAYOUT206) && a.program_heads == 0, "data at +6E0 is a 206 register, never another pool (mask %X heads %X)", a.candidates, a.program_heads);
    CHECK(a.primary_candidates == a.candidates, "no fingerprint");
    CHECK(vc_memory_layout(img) == VC_MEM16_LAYOUT206, "classifier: data below the stock pool is 206, never another layout");

    /* A one-cell program at +7D0 is stock and sparse 206 at once. */
    blank(stock);
    set_lines(stock, 7);
    stock[0x7D0] = 0x0B;
    vc_memory_analyze16(stock, &a);
    CHECK(has(a.program_heads, VC_MEM16_STD) && has(a.program_heads, VC_MEM16_LAYOUT206), "shared program head");
    CHECK(a.primary_candidates == a.candidates && a.candidates == ((1u << VC_MEM16_STD) | (1u << VC_MEM16_LAYOUT206)), "both, unranked");
    CHECK(vc_memory_layout(stock) == VC_MEM16_STD, "classifier: top-only data looks stock");
    memcpy(wide, stock, sizeof wide);
    CHECK(vc_memory_convert_pool(wide, VC_MEM16_STD, VC_MEM16_LAYOUT206) == 1, "stock → 206");
    CHECK(memcmp(wide, stock, VC_MEMORY_SIZE) == 0, "a sparse program at the top is the same bytes in both");

    /* Low data only: 206 only. Do not lift it into the stock pool. */
    blank(img);
    img[0x168] = 0x42;
    vc_memory_analyze16(img, &a);
    CHECK(a.upper_empty, "low data leaves the high window empty");
    CHECK(!has(a.candidates, VC_MEM16_STD) && has(a.candidates, VC_MEM16_LAYOUT206), "only 206 owns +168");
    memcpy(wide, img, sizeof wide);
    CHECK(vc_memory_convert_pool(wide, VC_MEM16_LAYOUT206, VC_MEM16_STD) == 1 && wide[0x6F0] == 0x42 && wide[0x168] == 0, "R0 alone moves into the stock pool");
    img[0x168 + 100 * 8] = 0x43;               /* a register past the 29 stock slots */
    memcpy(wide, img, sizeof wide);
    CHECK(vc_memory_convert_pool(wide, VC_MEM16_LAYOUT206, VC_MEM16_STD) == 0, "slot 100 does not fit stock");
    CHECK(memcmp(wide, img, VC_MEMORY_SIZE) == 0, "refusal leaves it");

    /* Stock program and registers survive a trip through 206. */
    blank(stock);
    set_lines(stock, 3);
    stock[0x7D0] = 0x21; stock[0x7D1] = 0x01; stock[0x7D2] = 0x22;   /* three program bytes */
    stock[0x6F0] = 0x11;                                              /* R0 */
    memcpy(wide, stock, sizeof wide);
    CHECK(vc_memory_convert_pool(wide, VC_MEM16_STD, VC_MEM16_LAYOUT206) == 1, "stock program → 206");
    CHECK(memcmp(wide, stock, 0x168) == 0, "header kept");
    CHECK(memcmp(wide + 0x7D8, stock + 0x7D8, VC_MEMORY_SIZE - 0x7D8) == 0, "FD/FE/FF, magic and tail kept");
    CHECK(memcmp(wide + 0x7F0, MAGIC, 8) == 0, "magic still AF5439FED967BC12");
    CHECK(wide[0x168] == 0x11 && wide[0x6F0] == 0, "R0 moved to the 206 base");
    CHECK(wide[0x7D0] == 0x21 && wide[0x7D2] == 0x22, "program stays at the top");
    memcpy(back, wide, sizeof back);
    CHECK(vc_memory_convert_pool(back, VC_MEM16_LAYOUT206, VC_MEM16_STD) == 1, "206 → stock");
    CHECK(memcmp(back, stock, VC_MEMORY_SIZE) == 0, "stock program round trip");

    /* Retired layout numbers are not conversion sources or targets. */
    for (i = 2; i <= 3; i++) {
        memcpy(wide, stock, sizeof wide);
        CHECK(vc_memory_convert_pool(wide, i, VC_MEM16_LAYOUT206) == 0 && vc_memory_convert_pool(wide, VC_MEM16_LAYOUT206, i) == 0, "retired %d refused", i);
        CHECK(memcmp(wide, stock, VC_MEMORY_SIZE) == 0, "retired %d left the image", i);
    }
    CHECK(vc_memory_convert_pool(wide, VC_MEM16_UNSUPPORTED, VC_MEM16_LAYOUT206) == 0 && vc_memory_convert_pool(wide, VC_MEM16_UNDETERMINED, VC_MEM16_STD) == 0, "unsupported/undetermined refused");
}

static void refusals(void) {
    uint8_t img[VC_MEMORY_SIZE], bad[VC_MEMORY_SIZE];
    int i;
    printf("== refusals stay atomic\n");
    blank(img);
    set_lines(img, 90);                        /* 13 program slots */
    img[0x168 + (29 - 13) * 8] = 1;            /* would be lost when shrinking 206 → stock */
    img[0x168 + (206 - 13) * 8] = 0x0B;
    memcpy(bad, img, sizeof bad);
    CHECK(vc_memory_convert_pool(bad, VC_MEM16_LAYOUT206, VC_MEM16_STD) == 0, "lossy shrink refused");
    CHECK(memcmp(bad, img, VC_MEMORY_SIZE) == 0, "refusal does not write");
    memcpy(bad, img, sizeof bad);
    bad[0x4E] |= 0x80;
    CHECK(vc_memory_convert_pool(bad, VC_MEM16_LAYOUT206, VC_MEM16_STD) == 0, "active capture refused");
    CHECK(bad[0x4E] & 0x80, "active image unchanged");
    memcpy(bad, img, sizeof bad);
    bad[0x7E1] = 8;
    CHECK(vc_memory_convert_pool(bad, VC_MEM16_LAYOUT206, VC_MEM16_STD) == 0, "bad pointer refused");
    memset(bad, 0, sizeof bad);
    CHECK(vc_memory_convert_pool(bad, VC_MEM16_STD, VC_MEM16_LAYOUT206) == 0, "no magic refused");
    blank(img);
    set_lines(img, 7);
    img[0x6E0] = 0x0B;
    CHECK(vc_memory_convert_pool(img, VC_MEM16_STD, VC_MEM16_LAYOUT206) == 0, "a cell below the stock pool is not a stock image");
    CHECK(img[0x6E0] == 0x0B, "failed stock read left the cell");
    CHECK(vc_memory_convert_pool(img, VC_MEM16_LAYOUT206, VC_MEM16_LAYOUT206) == 0, "same layout is not a conversion");
    CHECK(vc_memory_analyze16(NULL, NULL) == VC_ERR_ARG, "analyze args");
    CHECK(vc_set_mem16_layout(NULL, VC_MEM16_STD) == VC_ERR_ARG, "override args");
    {
        vc_calc *c = vc_create();
        CHECK(vc_set_mem16_layout(c, VC_MEM16_STD) == VC_OK && vc_set_mem16_layout(c, VC_MEM16_LAYOUT206) == VC_OK && vc_set_mem16_layout(c, VC_MEM16_NONE) == VC_OK, "stock / 206 / automatic accepted");
        for (i = 2; i <= 3; i++) CHECK(vc_set_mem16_layout(c, i) == VC_ERR_ARG, "retired layout %d is not a choice", i);
        CHECK(vc_set_mem16_layout(c, VC_MEM16_UNSUPPORTED) == VC_ERR_ARG && vc_set_mem16_layout(c, VC_MEM16_UNDETERMINED) == VC_ERR_ARG && vc_set_mem16_layout(c, VC_MEM16_ANY) == VC_ERR_ARG, "no other choice");
        CHECK(vc_unthrottled_48mhz(c) == 0, "48 MHz unthrottle off by default");
        CHECK(vc_set_unthrottled_48mhz(c, VC_UNTHROTTLED_48MHZ_US) == VC_OK && vc_unthrottled_48mhz(c) == VC_UNTHROTTLED_48MHZ_US, "48 MHz unthrottle set");
        CHECK(vc_set_unthrottled_48mhz(c, 2000000u) == VC_ERR_ARG && vc_set_unthrottled_48mhz(NULL, 1) == VC_ERR_ARG, "48 MHz unthrottle range");
        vc_destroy(c);
    }
}

static void zero_opcode(void) {
    uint8_t m[VC_MEMORY_SIZE];
    vc_mem16_analysis a;
    int i;
    printf("== a legal all-zero opcode is not a layout tag\n");
    blank(m);
    set_lines(m, 14);
    for (i = 0; i < 7; i++) m[0x7D0 + i] = 0;  /* first cell is LBL 0 */
    m[0x7C8] = 0x22;                           /* a later program byte still counts */
    vc_memory_analyze16(m, &a);
    CHECK(has(a.program_heads, VC_MEM16_STD) && has(a.program_heads, VC_MEM16_LAYOUT206), "later program byte is enough");
    CHECK(a.primary_candidates == a.candidates, "never ranked");
    m[0x7C8] = 0;
    vc_memory_analyze16(m, &a);
    CHECK(a.program_heads == 0, "all-zero program sets no head");
    CHECK(has(a.candidates, VC_MEM16_STD) && has(a.candidates, VC_MEM16_LAYOUT206), "both pools can hold an all-zero program");
}

int main(void) {
    catalogue();
    window_and_empty();
    ambiguity_and_convert();
    refusals();
    zero_opcode();
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
