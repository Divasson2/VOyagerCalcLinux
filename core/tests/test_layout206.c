/* test_layout206.c - firmware catalogue and the three automatic 16C memory layouts.
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
    static const int codes[] = {0x0E, 0xBA, 0xB3, 0xB4, 0xB5, 0xB7, 0xC8, 0xE3, 0xE5, 0xE6, 0xE8, 0xE9, 0xEA};
    int i, seen[256];
    memset(seen, 0, sizeof seen);
    printf("== catalogue\n");
    CHECK(vc_firmware_profile_count() == 13, "count %d", vc_firmware_profile_count());
    CHECK(vc_firmware_profile_at(-1) == NULL && vc_firmware_profile_at(13) == NULL, "bounds");
    for (i = 0; i < 13; i++) {
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
    for (i = 0; i < 13; i++) CHECK(by_code(codes[i]) != NULL, "missing %02X", codes[i]);
    CHECK(by_code(0x0E)->mem16_layout == VC_MEM16_STD, "0E");
    CHECK(by_code(0xB3)->mem16_layout == VC_MEM16_LEGACY_B3, "B3");
    CHECK(by_code(0xB4)->mem16_layout == VC_MEM16_EXT, "B4");
    CHECK(by_code(0xB7)->mem16_layout == VC_MEM16_EXT, "B7");
    CHECK(by_code(0xC8)->modes == (VC_FW_150 | VC_FW_152 | VC_FW_16C) && by_code(0xC8)->mem16_layout == VC_MEM16_STD, "C8");
    CHECK(by_code(0xE3)->mem16_layout == VC_MEM16_EXT_E3E3, "E3");
    CHECK(by_code(0xE6)->mem16_layout == VC_MEM16_STD, "E6");
    CHECK(by_code(0xE8)->mem16_layout == VC_MEM16_EXT && by_code(0xE8)->key160 == 5, "E8");
    CHECK(by_code(0xE9)->mem16_layout == VC_MEM16_EXT && by_code(0xE9)->per_mode_slots && by_code(0xE9)->key160 == 1 && by_code(0xE9)->key152 == 3, "E9");
    CHECK(by_code(0xEA)->mem16_layout == VC_MEM16_LAYOUT206 && by_code(0xEA)->per_mode_slots && by_code(0xEA)->key152 == 3, "EA");
    CHECK(by_code(0xE9)->per_mode_slots && by_code(0xEA)->per_mode_slots, "slots");
    for (i = 0; i < 13; i++) {
        int ce = vc_firmware_profile_at(i)->checksum;
        if (ce != 0xE9 && ce != 0xEA) CHECK(!vc_firmware_profile_at(i)->per_mode_slots, "%02X should not keep mode memory", ce);
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
    CHECK(!has(a.candidates, VC_MEM16_EXT_E3E3), "E3E3 is not an automatic hypothesis");
    CHECK(has(a.candidates, VC_MEM16_STD) && has(a.candidates, VC_MEM16_EXT) && has(a.candidates, VC_MEM16_LAYOUT206), "blank fits all three");
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

static void fingerprint_and_convert(void) {
    uint8_t img[VC_MEMORY_SIZE], wide[VC_MEMORY_SIZE], back[VC_MEMORY_SIZE], stock[VC_MEMORY_SIZE], e3[VC_MEMORY_SIZE];
    vc_mem16_analysis a;
    int i;
    printf("== 616 fingerprint, stock/206 ambiguity, conversion\n");
    blank(img);
    set_lines(img, 7);
    img[0x6E0] = 0x0B;                         /* one program cell at the 176 top; the high window stays empty */
    for (i = 1; i < 7; i++) img[0x6E0 + i] = 0; /* leading bytes may be the legal LBL 0 opcode */
    img[0x6E3] = 0x22;
    CHECK(vc_memory_analyze16(img, &a) == VC_OK, "616 analyze");
    CHECK(a.upper_empty && a.pointers_valid && a.lines == 7, "616 window lines %d", a.lines);
    CHECK(has(a.program_heads, VC_MEM16_EXT), "176 program head");
    CHECK(a.primary_candidates == (1u << VC_MEM16_EXT), "primary is 176, not stock");
    CHECK(!has(a.candidates, VC_MEM16_EXT_E3E3), "E3 not automatic");
    memcpy(wide, img, sizeof wide);
    CHECK(vc_memory_convert_pool(wide, VC_MEM16_EXT, VC_MEM16_LAYOUT206) == 1, "176 → 206");
    CHECK(memcmp(wide, img, 0x168) == 0, "header kept");
    CHECK(memcmp(wide + 0x7D8, img + 0x7D8, VC_MEMORY_SIZE - 0x7D8) == 0, "FD/FE/FF, magic and tail kept");
    CHECK(memcmp(wide + 0x7F0, MAGIC, 8) == 0, "magic still AF5439FED967BC12");
    CHECK(wide[0x7D0] == 0x0B && wide[0x7D3] == 0x22, "program cell moved to the 206 top");
    CHECK(wide[0x6E0] == 0, "old 176 program cell cleared");
    memcpy(back, wide, sizeof back);
    CHECK(vc_memory_convert_pool(back, VC_MEM16_LAYOUT206, VC_MEM16_EXT) == 1, "206 → 176");
    CHECK(memcmp(back, img, VC_MEMORY_SIZE) == 0, "round trip exact");

    /* A one-cell program at +7D0 is stock and sparse 206 at once. Both shrink to the same 176 bytes. */
    blank(stock);
    set_lines(stock, 7);
    stock[0x7D0] = 0x0B;
    vc_memory_analyze16(stock, &a);
    CHECK(has(a.program_heads, VC_MEM16_STD) && has(a.program_heads, VC_MEM16_LAYOUT206), "shared program head");
    CHECK(!has(a.program_heads, VC_MEM16_EXT), "176 does not own +7D0");
    CHECK(a.primary_candidates == a.candidates, "not a 616 fingerprint");
    memcpy(wide, stock, sizeof wide);
    CHECK(vc_memory_convert_pool(wide, VC_MEM16_STD, VC_MEM16_EXT) == 1, "stock → 176");
    memcpy(back, stock, sizeof back);
    CHECK(vc_memory_convert_pool(back, VC_MEM16_LAYOUT206, VC_MEM16_EXT) == 1, "206 → 176");
    CHECK(memcmp(wide, back, VC_MEMORY_SIZE) == 0, "both interpretations become the same 176 image");
    CHECK(vc_memory_convert_pool(wide, VC_MEM16_EXT, VC_MEM16_STD) == 1, "176 → stock");
    CHECK(memcmp(wide, stock, VC_MEMORY_SIZE) == 0, "back to the sparse image");

    /* Low data only: 176 and 206 are the same bytes. Do not lift them into the stock pool. */
    blank(img);
    img[0x168] = 0x42;
    vc_memory_analyze16(img, &a);
    CHECK(a.upper_empty, "low data leaves the high window empty");
    CHECK(!has(a.candidates, VC_MEM16_STD), "stock cannot own +168");
    memcpy(wide, img, sizeof wide);
    CHECK(vc_memory_convert_pool(wide, VC_MEM16_LAYOUT206, VC_MEM16_EXT) == 1, "low 206 → 176");
    CHECK(memcmp(wide, img, VC_MEMORY_SIZE) == 0, "low data stays where it is");

    /* Stock program and an early E3E3 program both survive a trip through 206. */
    blank(stock);
    set_lines(stock, 3);
    stock[0x7D0] = 0x21; stock[0x7D1] = 0x01; stock[0x7D2] = 0x22;   /* three program bytes */
    stock[0x6F0] = 0x11;                                              /* R0 */
    memcpy(wide, stock, sizeof wide);
    CHECK(vc_memory_convert_pool(wide, VC_MEM16_STD, VC_MEM16_LAYOUT206) == 1, "stock program → 206");
    memcpy(back, wide, sizeof back);
    CHECK(vc_memory_convert_pool(back, VC_MEM16_LAYOUT206, VC_MEM16_STD) == 1, "206 → stock");
    CHECK(memcmp(back, stock, VC_MEMORY_SIZE) == 0, "stock program round trip");

    blank(e3);
    set_lines(e3, 3);
    e3[0x6E8] = 0x21; e3[0x6E9] = 0x01; e3[0x6EA] = 0x22;            /* E3E3 program top */
    e3[0x170] = 0x11;
    memcpy(wide, e3, sizeof wide);
    CHECK(vc_memory_convert_pool(wide, VC_MEM16_EXT_E3E3, VC_MEM16_LAYOUT206) == 1, "E3 → 206");
    memcpy(back, wide, sizeof back);
    CHECK(vc_memory_convert_pool(back, VC_MEM16_LAYOUT206, VC_MEM16_EXT_E3E3) == 1, "206 → E3");
    CHECK(memcmp(back, e3, VC_MEMORY_SIZE) == 0, "E3 program round trip");
}

static void refusals(void) {
    uint8_t img[VC_MEMORY_SIZE], bad[VC_MEMORY_SIZE];
    printf("== refusals stay atomic\n");
    blank(img);
    set_lines(img, 90);                        /* 13 program slots */
    img[0x168 + (176 - 13) * 8] = 1;           /* would be lost when shrinking 206 → 176 */
    img[0x168 + (206 - 13) * 8] = 0x0B;
    memcpy(bad, img, sizeof bad);
    CHECK(vc_memory_convert_pool(bad, VC_MEM16_LAYOUT206, VC_MEM16_EXT) == 0, "lossy shrink refused");
    CHECK(memcmp(bad, img, VC_MEMORY_SIZE) == 0, "refusal does not write");
    memcpy(bad, img, sizeof bad);
    bad[0x4E] |= 0x80;
    CHECK(vc_memory_convert_pool(bad, VC_MEM16_LAYOUT206, VC_MEM16_EXT) == 0, "active capture refused");
    CHECK(bad[0x4E] & 0x80, "active image unchanged");
    memcpy(bad, img, sizeof bad);
    bad[0x7E1] = 8;
    CHECK(vc_memory_convert_pool(bad, VC_MEM16_LAYOUT206, VC_MEM16_EXT) == 0, "bad pointer refused");
    memset(bad, 0, sizeof bad);
    CHECK(vc_memory_convert_pool(bad, VC_MEM16_EXT, VC_MEM16_LAYOUT206) == 0, "no magic refused");
    blank(img);
    set_lines(img, 7);
    img[0x6E0] = 0x0B;
    CHECK(vc_memory_convert_pool(img, VC_MEM16_STD, VC_MEM16_LAYOUT206) == 0, "176 bytes are not a stock image");
    CHECK(img[0x6E0] == 0x0B, "failed stock read left the 176 cell");
    CHECK(vc_memory_convert_pool(img, VC_MEM16_EXT, VC_MEM16_EXT) == 0, "same layout is not a conversion");
    CHECK(vc_memory_analyze16(NULL, NULL) == VC_ERR_ARG, "analyze args");
    CHECK(vc_set_mem16_layout(NULL, VC_MEM16_EXT) == VC_ERR_ARG, "override args");
}

static void zero_opcode(void) {
    uint8_t m[VC_MEMORY_SIZE];
    vc_mem16_analysis a;
    int i;
    printf("== a legal all-zero opcode is not a layout tag\n");
    blank(m);
    set_lines(m, 14);
    for (i = 0; i < 7; i++) m[0x6E0 + i] = 0;  /* first cell is LBL 0 */
    m[0x6D8] = 0x22;                           /* a later program byte still counts */
    vc_memory_analyze16(m, &a);
    CHECK(a.upper_empty && has(a.program_heads, VC_MEM16_EXT), "later program byte is enough");
    CHECK(a.primary_candidates == (1u << VC_MEM16_EXT), "primary still 176");
    m[0x6D8] = 0;
    vc_memory_analyze16(m, &a);
    CHECK(!has(a.program_heads, VC_MEM16_EXT), "all-zero program sets no head");
    CHECK(a.primary_candidates != (1u << VC_MEM16_EXT) || a.candidates == (1u << VC_MEM16_EXT), "no invented 616 tag");
    CHECK(has(a.candidates, VC_MEM16_EXT) && has(a.candidates, VC_MEM16_LAYOUT206), "both pools can hold an all-zero program below +6E8");
}

int main(void) {
    catalogue();
    window_and_empty();
    fingerprint_and_convert();
    refusals();
    zero_opcode();
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
