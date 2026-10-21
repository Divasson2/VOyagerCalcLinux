/* internal.h - what inspect.c needs from machine.c (not part of the public API).
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 José González Divassón */
#ifndef VC_INTERNAL_H
#define VC_INTERNAL_H
#include "voyager_core.h"
/* The 2 KB Continuous Memory window in the emulated SRAM (the VoyagerSave image), or NULL. */
const uint8_t *vci_dump(const vc_calc *c);
/* Flash address of the Nut ROM the firmware runs now. */
uint32_t       vci_rom_base(const vc_calc *c);
/* Nut ROM word i of the ROM at flash address rb (-1 outside the firmware image). */
int            vci_rom_word(const vc_calc *c, uint32_t rb, uint32_t i);
/* 1 = 16C CE firmware with the Path-C extended memory (B5B5 / B7B7 builds). */
int            vci_ext16(const vc_calc *c);
/* Halfword of the firmware image at flash address addr (little endian), -1 outside it. */
int            vci_flash_u16(const vc_calc *c, uint32_t addr);
/* Whole engine: 10 ms ticks in a row without a WFI sleep; any key held (1) or not (0). */
uint32_t       vci_busy_ticks(const vc_calc *c);
int            vci_key_held(const vc_calc *c);
#endif
