/* inspect.c - read-only view of what the emulated calculator holds: stack, LAST X, I,
 * storage registers, modes, and the program listing.
 *
 * Everything is read from the 2 KB Continuous Memory window (the VoyagerSave image at SRAM
 * 0x20000018): the firmware's Nut CPU structure (A, B, C, M, N ...) followed by the Nut RAM
 * chips. The live X register is the Nut CPU's M register; the rest is in Nut RAM. Nothing here
 * writes to the calculator or runs emulated code.
 *
 * Layouts (dump offsets; Nut register k of chip 0 is at 0x70 + 8k; 7 data bytes + 1 pad, LE):
 *   all models   X = M (0x20), Y = Nut 0 (0x70), Z = Nut 1, T = Nut 2.
 *   HP-16C       LAST X = Nut 3, status = Nut 6, high bytes of 64-bit words = Nut 7,
 *                I = Nut 0xFD (+ high byte in Nut 0xFE), last line = Nut 0xFE, line = Nut 0xFF.
 *                Pool: Path-C 176 registers (logical Nut 0x4D..0xFC; B5/B7B7 at dump 0x168,
 *                the 12C multi's 16C mode at 0x170), stock 29 registers (Nut 0xE0..0xFC, 0x6F0).
 *                R0 = nibble 0 of the pool, registers packed linearly, ceil(WSIZE/4) nibbles each;
 *                the program grows down from the top register, 7 lines per register.
 *   HP-15C       R0 = Nut 0x10, R1 = 0x11, I = 0x12, LAST X = 0x13, Nut 0x15 = data-register
 *                window (top byte: first address + 0x10; low byte: last address + 0x10),
 *                R2.. linear from there; flags in Nut 0x1A; program from Nut 0xFF down
 *                (dump 0x7E8), pointers in Nut 0x16 (last) / 0x17 (current).
 *   HP-12C       LAST X 0x740, R0 0x720, R1-R6 0x6F0.., R7-R.9 0x770.., FV 0x748 .. n 0x768.
 *                Program: line 01 = byte 6 of dump 0x7E0, lines 02-08 = bytes 0-6 of 0x7D8, then 7 lines
 *                per register downwards (0x7D0 = lines 09-15 ... 0x770 = R7 = lines 93-99). Dump 0x7E0
 *                = current line (low nibble: register code, 0xE = 0x7D8 .. ; high nibble: byte; 0x00 =
 *                line 00, 0x6F = line 01); low nibble of 0x7E1 = the lowest program register code, so
 *                lines = 8 + 7 * (0xD - it).
 * All of this was found by keying values into the firmware and looking where they land
 * (core/tests/test_inspect.c checks it).
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 José González Divassón */
#include "internal.h"
#include "optables.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define MASK56 ((1ull << 56) - 1)

static uint64_t slot(const uint8_t *d, uint32_t off) {
    uint64_t v = 0; int i;
    if (off + 7 > VC_MEMORY_SIZE) return 0;
    for (i = 6; i >= 0; i--) v = v << 8 | d[off + (uint32_t)i];
    return v;
}
static int nib(uint64_t v, int i) { return (int)(v >> (4 * i)) & 15; }

/* ------------------------------------------------------------ which layout */
typedef struct {
    const uint8_t *d;
    int model, variant;
    uint32_t pool;      /* 16C: dump offset of the pool's first register */
    int first, slots;   /* 16C: logical Nut address of the first pool register, pool size */
    int pathc;
} layout;

static int get_layout(vc_calc *c, layout *L) {
    memset(L, 0, sizeof *L);
    L->d = vci_dump(c);
    if (!L->d) return VC_ERR_STATE;
    L->model = vc_model(c); L->variant = vc_model_variant(c);
    if (L->model == VC_MODEL_16C) {
        int pathc;
        if (vc_firmware_family(c) == VC_MODEL_16C) { pathc = vci_ext16(c); L->pool = 0x168; }
        else { pathc = vci_rom_word(c, vci_rom_base(c), 0x1D4) == 0x15C; L->pool = 0x170; }   /* Path-C 16C ROM */
        if (pathc) { L->first = 0x4D; L->slots = 176; }
        else { L->pool = 0x6F0; L->first = 0xE0; L->slots = 29; }
        L->pathc = pathc;
    }
    return L->model ? VC_OK : VC_ERR_STATE;
}

/* ------------------------------------------------------------ HP-16C status */
typedef struct { int base, ws, comp, flt; uint32_t flags; } st16;
static st16 status16(const layout *L) {
    st16 s; uint64_t v = slot(L->d, 0x70 + 8 * 6);
    int b = nib(v, 13), hi = nib(v, 11), top = nib(v, 10), k;
    s.flt = b == 0xC;
    s.base = s.flt ? 0 : (b & 8) ? 10 : (b & 4) ? 2 : (b & 2) ? 8 : 16;
    int bits = 0; for (k = 0; k < 4; k++) bits += (top >> k) & 1;
    s.ws = 4 * (15 - hi) + bits;
    if (s.ws < 1 || s.ws > 64) s.ws = 16;
    s.comp = nib(v, 12) > 2 ? 2 : nib(v, 12);
    s.flags = (uint32_t)nib(v, 6) | (uint32_t)(nib(v, 7) & 3) << 4;
    return s;
}

/* ------------------------------------------------------------ formatting */
static void bcd_value(uint64_t raw, vc_value *o) {
    char m[11]; int i, e, neg = nib(raw, 13) >= 5;
    for (i = 0; i < 10; i++) { int dd = nib(raw, 12 - i); m[i] = (char)('0' + (dd > 9 ? 9 : dd)); }
    m[10] = 0;
    e = nib(raw, 1) * 10 + nib(raw, 0);
    if (nib(raw, 2) >= 5) e -= 100;
    o->kind = VC_VAL_FLOAT; o->raw = raw; o->negative = neg && strcmp(m, "0000000000");
    int n = 10; while (n > 1 && m[n - 1] == '0') n--;
    char *t = o->text; size_t cap = sizeof o->text; int p = 0;
    if (o->negative) t[p++] = '-';
    if (m[0] == '0') { snprintf(t, cap, "0"); }
    else if (e >= 0 && e < 10) {
        for (i = 0; i < n || i <= e; i++) { t[p++] = i < 10 ? m[i] : '0'; if (i == e && i + 1 < n) t[p++] = '.'; }
        t[p] = 0;
    } else if (e < 0 && e >= -5) {
        t[p++] = '0'; t[p++] = '.';
        for (i = 0; i < -e - 1; i++) t[p++] = '0';
        for (i = 0; i < n; i++) t[p++] = m[i];
        t[p] = 0;
    } else {
        t[p++] = m[0];
        if (n > 1) { t[p++] = '.'; for (i = 1; i < n; i++) t[p++] = m[i]; }
        snprintf(t + p, cap - (size_t)p, "e%d", e);
    }
    char sci[40]; snprintf(sci, sizeof sci, "%s%c.%se%d", o->negative ? "-" : "", m[0], m + 1, e);
    o->number = m[0] == '0' ? 0.0 : strtod(sci, NULL);
    snprintf(o->dec, sizeof o->dec, "%.31s", o->text);
}

static void int_value(uint64_t w, const st16 *s, int base, vc_value *o) {
    int ws = s->ws; uint64_t mask = ws >= 64 ? ~0ull : (1ull << ws) - 1;
    w &= mask;
    o->kind = VC_VAL_INT; o->word = w;
    int sign = ws >= 1 && (w >> (ws - 1)) & 1;
    uint64_t mag = w;
    o->negative = 0;
    if (s->comp == 2 && sign) { mag = (~w + 1) & mask; o->negative = 1; }
    else if (s->comp == 1 && sign) { mag = ~w & mask; o->negative = mag != 0; }
    if (mag == 0 && s->comp == 1 && sign) snprintf(o->dec, sizeof o->dec, "-0");
    else snprintf(o->dec, sizeof o->dec, "%s%llu", o->negative ? "-" : "", (unsigned long long)mag);
    o->number = o->negative ? -(double)mag : (double)mag;
    if (base == 10) { snprintf(o->text, sizeof o->text, "%s", o->dec); return; }
    int sh = base == 16 ? 4 : base == 8 ? 3 : 1, digits = (ws + sh - 1) / sh, i, p = 0, lead = 1;
    for (i = digits - 1; i >= 0; i--) {
        int dg = (int)((w >> (i * sh)) & (uint64_t)((1 << sh) - 1));
        if (dg == 0 && lead && i > 0) continue;
        lead = 0;
        if (p < (int)sizeof o->text - 1) o->text[p++] = "0123456789ABCDEF"[dg];
    }
    o->text[p] = 0;
}

static void value16(const layout *L, uint64_t raw, int ext, vc_value *o) {
    st16 s = status16(L);
    if (s.flt) { bcd_value(raw & MASK56, o); return; }
    int_value((raw & MASK56) | (uint64_t)(ext & 0xFF) << 56, &s, s.base, o);
    o->raw = raw & MASK56;
}

/* ------------------------------------------------------------ HP-16C pool */
static uint32_t reg16_off(const layout *L, int logical) { return L->pool + 8u * (uint32_t)(logical - L->first); }
static int ptr_line16(const layout *L, uint64_t p) {
    int reg = (int)(p & 0xFF), pos = (int)((p >> 8) & 0xF);
    if (reg == 0 || reg > 0xFC || reg < L->first) return 0;
    return (0xFC - reg) * 7 + pos;
}
static int lines16(const layout *L) { return ptr_line16(L, slot(L->d, 0x7E0)); }

static int lines12(const layout *L) {
    int low = L->d[0x7E1] & 0xF;
    return low > 0xD ? 8 : 8 + 7 * (0xD - low);
}
static int line12(const layout *L) {
    int p = L->d[0x7E0], r = p & 0xF, b = p >> 4;
    if (p == 0) return 0;
    if (r == 0xF) return 1;
    return 2 + 7 * (0xE - r) + b;
}
static uint8_t byte12(const layout *L, int line) {
    if (line == 1) return L->d[0x7E6];
    int i = line - 2;
    return L->d[0x7D8 - 8 * (uint32_t)(i / 7) + (uint32_t)(i % 7)];
}

static int nibble16(const layout *L, int j) {        /* nibble j of the pool, from R0 upwards */
    int s = j / 14, n = j % 14;
    uint8_t b = L->d[L->pool + 8u * (uint32_t)s + (uint32_t)(n / 2)];
    return n & 1 ? b >> 4 : b & 15;
}
static int regs16(const layout *L, int ws) {
    int prog_slots = (lines16(L) + 6) / 7, size = (ws + 3) / 4;
    int free_nibbles = (L->slots - prog_slots) * 14;
    return free_nibbles > 0 ? free_nibbles / size : 0;
}

/* ------------------------------------------------------------ HP-15C */
static uint32_t off15(int addr) { return 0x70u + 8u * (uint32_t)addr; }
/* DIM (i): R2 starts at map byte 6 and storage ends where matrix A starts (map byte 5).
 * (Until 0.5.7 the end came from byte 0, the free boundary, so dimensioned matrices showed
 * up as R20, R21 ...; see docs/HP15C-POOL-COMPLEX-MATRICES.md.) */
static int dim15(const layout *L, int *first) {
    const uint8_t *w = L->d + off15(0x15);
    int top = w[6], last = w[5];
    if (first) *first = top - 0x10;
    return last >= top ? last - top + 1 : 0;
}
/* Flag 8 (complex mode). The firmware keeps it as a status bit of the Nut CPU, in the CE's
 * CPU struct: image byte 0x4C bit 3 (struct + 0x2C). S0A nibble 8 bit 0 (byte 0xC4) is only a
 * copy that the firmware rewrites from that bit at the next key or power-on (checked by running
 * the C8C8 / E3E3 firmware: an image with 0xC4 = 1 but 0x4C bit 3 = 0 comes up with flag 8
 * clear and 0xC4 back to 0; 0x4C bit 3 alone turns it on). 0.6.4; until 0.6.3 0xC4 was used. */
static int complex15(const layout *L) { return (L->d[0x4C] >> 3) & 1; }
/* The SOLVE / integrate workspace exists only right after SOLVE / integrate ran: S14 nibble 13
 * ('Run') is F and S03 holds the solver's scratch pointer. Without both, registers below R2 (and
 * below the imaginary stack) are a free gap, which g MEM counts as free: the 15.2 cold-start gap,
 * or the 5 registers an imaginary stack left behind when flag 8 got lost (0.6.3 write bug). */
static int solver_ran15(const uint8_t *d) {
    int i, s03 = 0;
    for (i = 0; i < 7; i++) s03 |= d[0x88 + i];
    return (d[0x116] >> 4) == 0xF && s03 != 0;
}
static int prog15_index(uint64_t p) {     /* pointer -> byte index from the top (or -1) */
    int reg = (int)(p & 0xFF), pos = (int)((p >> 8) & 0xF);
    if (reg < 0x80 || pos == 0) return -1;
    return (0xFF - reg) * 7 + pos - 1;
}
static int byte15(const layout *L, int i) {
    int r = i / 7; uint32_t off = 0x7E8u - 8u * (uint32_t)r + (uint32_t)(i % 7);
    return off < VC_MEMORY_SIZE ? L->d[off] : 0;
}
/* walks the program: number of lines up to byte index stop (inclusive start), and total */
static int walk15(const layout *L, int stop_index, int *line_at) {
    int end = prog15_index(slot(L->d, off15(0x16))), i = 0, n = 0;
    if (line_at) *line_at = 0;
    if (end < 0) return 0;
    while (i <= end && n < 1000) {
        n++;
        if (line_at && i == stop_index) *line_at = n;
        i += OP15_LEN[byte15(L, i)] == 2 ? 2 : 1;
    }
    return n;
}

/* ------------------------------------------------------------ public */
int vc_inspect(vc_calc *c, vc_inspect_info *o) {
    layout L; int r;
    if (!c || !o) return VC_ERR_ARG;
    memset(o, 0, sizeof *o);
    if ((r = get_layout(c, &L))) return r;
    o->model = L.model; o->variant = L.variant; o->base = 10;
    o->prgm_mode = (vc_get_annunciators(c) & VC_ANN_PRGM) != 0;
    o->running = vc_engine(c) == VC_ENGINE_WHOLE && vci_busy_ticks(c) >= 3 && !vci_key_held(c);
    if (L.model == VC_MODEL_16C) {
        st16 s = status16(&L);
        o->base = s.base; o->word_size = s.flt ? 56 : s.ws; o->complement = s.comp; o->flags = s.flags;
        o->lines = lines16(&L); o->line = ptr_line16(&L, slot(L.d, 0x7E8));
        if (o->line > o->lines) o->line = o->lines;
        o->registers = regs16(&L, o->word_size);
        o->has_i = 1; o->has_program = 1; o->ext_memory = L.pathc;
    } else if (L.model == VC_MODEL_15C) {
        uint64_t f = slot(L.d, off15(0x1A));
        o->flags = (uint32_t)nib(f, 2) | (uint32_t)nib(f, 3) << 4 | (uint32_t)((nib(f, 4) >> 1) & 1) << 9;
        if (complex15(&L)) o->flags |= 1u << 8;      /* complex mode: CPU struct status bit, image 0x4C bit 3 */
        int cur = prog15_index(slot(L.d, off15(0x17))), at;
        o->lines = walk15(&L, cur, &at); o->line = cur < 0 ? 0 : at;
        o->registers = dim15(&L, NULL) + 1;
        o->has_i = 1; o->has_program = 1;
    } else if (L.model == VC_MODEL_12C) {
        o->lines = lines12(&L); o->line = line12(&L);
        if (o->line > o->lines) o->line = o->lines;
        o->registers = 20 - (o->lines - 8) / 7;       /* program lines take R.9, R.8 ... downwards */
        o->has_program = 1;
    }
    return VC_OK;
}

static const char *const STACK_NAMES[VC_REG_COUNT] = { "X", "Y", "Z", "T", "LAST X", "I", "n", "i", "PV", "PMT", "FV" };

int vc_inspect_stack(vc_calc *c, int reg, vc_value *o) {
    layout L; int r;
    if (!c || !o || reg < 0 || reg >= VC_REG_COUNT) return VC_ERR_ARG;
    memset(o, 0, sizeof *o);
    if ((r = get_layout(c, &L))) return r;
    snprintf(o->name, sizeof o->name, "%s", STACK_NAMES[reg]);
    const uint8_t *d = L.d;
    if (L.model == VC_MODEL_16C) {
        uint64_t hi = slot(d, 0x70 + 8 * 7);
        static const int EXT[5] = {0, 3, 5, 7, 9};
        if (reg <= VC_REG_LASTX) {
            uint64_t raw = reg == VC_REG_X ? slot(d, 0x20) : slot(d, 0x70 + 8u * (uint32_t)(reg - 1));
            if (reg == VC_REG_LASTX) raw = slot(d, 0x70 + 8 * 3);
            value16(&L, raw, nib(hi, EXT[reg]) | nib(hi, EXT[reg] + 1) << 4, o);
        } else if (reg == VC_REG_I) {
            uint64_t ih = slot(d, 0x7E0);
            value16(&L, slot(d, 0x7D8), nib(ih, 3) | nib(ih, 4) << 4, o);
        } else { o->kind = VC_VAL_NONE; return VC_ERR_ARG; }
        return VC_OK;
    }
    uint32_t off;
    if (L.model == VC_MODEL_15C) {
        static const uint32_t O15[6] = {0x20, 0x70, 0x78, 0x80, 0x70 + 8 * 0x13, 0x70 + 8 * 0x12};
        if (reg > VC_REG_I) return VC_ERR_ARG;
        off = O15[reg];
    } else {
        static const uint32_t O12[VC_REG_COUNT] = {0x20, 0x70, 0x78, 0x80, 0x740, 0, 0x768, 0x760, 0x758, 0x750, 0x748};
        if (reg == VC_REG_I) return VC_ERR_ARG;
        off = O12[reg];
    }
    bcd_value(slot(d, off), o);
    return VC_OK;
}

int vc_inspect_register(vc_calc *c, int n, vc_value *o) {
    layout L; int r;
    if (!c || !o || n < 0) return VC_ERR_ARG;
    memset(o, 0, sizeof *o);
    if ((r = get_layout(c, &L))) return r;
    if (L.model == VC_MODEL_16C) {
        st16 s = status16(&L);
        int ws = s.flt ? 56 : s.ws, size = (ws + 3) / 4, k;
        if (n >= regs16(&L, ws)) return VC_ERR_ARG;
        uint64_t lo = 0, hi = 0;
        for (k = 0; k < size; k++) {
            uint64_t v = (uint64_t)nibble16(&L, n * size + k);
            if (k < 14) lo |= v << (4 * k); else hi |= v << (4 * (k - 14));
        }
        value16(&L, lo, (int)hi, o);
        if (n < 16) snprintf(o->name, sizeof o->name, "R%X", n);
        else if (n < 32) snprintf(o->name, sizeof o->name, "R.%X", n - 16);
        else snprintf(o->name, sizeof o->name, "R%d", n);
        return VC_OK;
    }
    uint32_t off;
    if (L.model == VC_MODEL_15C) {
        int first, dim = dim15(&L, &first);
        if (n > dim) return VC_ERR_ARG;
        off = n == 0 ? off15(0x10) : n == 1 ? off15(0x11) : off15(first + n - 2);
    } else if (L.model == VC_MODEL_12C) {
        if (n >= 20) return VC_ERR_ARG;
        off = n == 0 ? 0x720u : n <= 6 ? 0x6F0u + 8u * (uint32_t)(n - 1) : 0x770u + 8u * (uint32_t)(n - 7);
    } else return VC_ERR_STATE;
    bcd_value(slot(L.d, off), o);
    if (n < 10) snprintf(o->name, sizeof o->name, "R%d", n);
    else if (n < 20) snprintf(o->name, sizeof o->name, "R.%d", n - 10);
    else snprintf(o->name, sizeof o->name, "R%d", n);
    return VC_OK;
}

static void fill(vc_prog_line *p, int line, const vc_op *op, const uint8_t *code, int len) {
    memset(p, 0, sizeof *p);
    p->line = line; p->len = (uint8_t)len; p->code[0] = code[0]; if (len > 1) p->code[1] = code[1];
    p->prefix = op->pfx;
    snprintf(p->keys, sizeof p->keys, "%s", op->keys);
    snprintf(p->mnemonic, sizeof p->mnemonic, "%s", op->mn);
}

int vc_inspect_program(vc_calc *c, int first, vc_prog_line *out, int max) {
    layout L; int r, n = 0;
    if (!c || (!out && max > 0) || first < 1 || max < 0) return VC_ERR_ARG;
    if ((r = get_layout(c, &L))) return r;
    if (L.model == VC_MODEL_16C) {
        int lines = lines16(&L), ln;
        for (ln = first; ln <= lines && n < max; ln++) {
            int i = ln - 1, logical = 0xFC - i / 7;
            uint8_t b = L.d[reg16_off(&L, logical) + (uint32_t)(i % 7)];
            fill(&out[n++], ln, &OP16[b], &b, 1);
        }
        return n;
    }
    if (L.model == VC_MODEL_15C) {
        int end = prog15_index(slot(L.d, off15(0x16))), i = 0, ln = 0;
        if (end < 0) return 0;
        while (i <= end && n < max && ln < 1000) {
            uint8_t b[2] = {(uint8_t)byte15(&L, i), (uint8_t)byte15(&L, i + 1)};
            int len = OP15_LEN[b[0]] == 2 && OP15X[b[0]] ? 2 : 1;
            ln++;
            if (ln >= first) fill(&out[n++], ln, len == 2 ? &OP15X[b[0]][b[1]] : &OP15[b[0]], b, len);
            i += len;
        }
        return n;
    }
    if (L.model == VC_MODEL_12C) {
        int lines = lines12(&L), ln;
        for (ln = first; ln <= lines && n < max; ln++) {
            uint8_t b = byte12(&L, ln);
            fill(&out[n++], ln, &OP12[b], &b, 1);
        }
        return n;
    }
    return 0;
}

/* ------------------------------------------------------------ HP-15C memory pool (0.6.0)
 * Port of docs/pool15c/pool15.py. Every address here is a Nut register address p
 * (dump offset 0x70 + 8 * (p - 0x10)). Pool, from the bottom: SOLVE/integrate workspace,
 * imaginary stack (5, flag 8), R2..R(DIM (i)), matrices A..E, free, program (top, growing down). */
static uint32_t offp(int p) { return 0x70u + 8u * (uint32_t)(p - 0x10); }

int vc_inspect_pool15(vc_calc *c, vc_pool15 *o) {
    layout L; int r, k;
    if (!c || !o) return VC_ERR_ARG;
    memset(o, 0, sizeof *o);
    if ((r = get_layout(c, &L))) return r;
    if (L.model != VC_MODEL_15C) return VC_ERR_STATE;
    const uint8_t *d = L.d, *mp = d + 0x118;
    o->variant = L.variant == 152 ? 152 : 150;
    o->base = o->variant == 152 ? 0x40 : 0xA0;
    o->top = 0x100;
    o->data = mp[6];
    for (k = 0; k < 5; k++) o->start[k] = mp[5 - k];
    o->pool = mp[0] ? mp[0] : 0x100;
    o->rows[0] = d[0x13E]; o->cols[0] = d[0x13D];
    o->rows[1] = d[0x13C]; o->cols[1] = d[0x13B];
    o->rows[2] = d[0x13A]; o->cols[2] = d[0x139];
    o->rows[3] = d[0x146]; o->cols[3] = d[0x145];
    o->rows[4] = d[0x144]; o->cols[4] = d[0x143];
    int rn = d[0x142] >> 4;
    o->result = rn >= 0xA && rn <= 0xE ? rn - 0xA : 0;
    o->complex = complex15(&L);
    uint64_t p16 = slot(d, 0x70 + 8 * 0x16);
    if (nib(p16, 2)) {
        o->prog_low = (int)(p16 & 0xFF); o->prog_regs = 0x100 - o->prog_low;
        o->prog_bytes = (0xFF - o->prog_low) * 7 + nib(p16, 2);
    } else o->prog_low = 0x100;
    int cx = o->complex ? 5 : 0, below = o->data - o->base - cx;
    /* below R2 (and the imaginary stack): the SOLVE (5) / integrate (23, 28 nested) workspace,
     * right after it ran; anything else is the free gap of a fresh 15.2 cold start */
    if ((below == 5 || below == 23 || below == 28) && solver_ran15(d)) o->scratch = below;
    else if (below > 0) o->gap = below;
    o->storage = o->start[0] - o->data;
    if (o->storage < 0) o->storage = 0;
    o->dim_i = o->storage + 1;
    for (k = 0; k < 5; k++) o->matrices += o->rows[k] * o->cols[k];
    o->free = (o->top - o->base) - o->storage - o->matrices - cx - o->prog_regs;
    snprintf(o->gmem, sizeof o->gmem, "%d %d %02d-%d", o->dim_i, o->free, o->prog_regs, o->prog_regs * 7 - o->prog_bytes);
    return VC_OK;
}

int vc_inspect_imag(vc_calc *c, int reg, vc_value *o) {
    vc_pool15 p; int r;
    static const char *const NM[5] = { "X", "Y", "Z", "T", "LAST X" };
    if (!o || reg < VC_REG_X || reg > VC_REG_LASTX) return VC_ERR_ARG;
    if ((r = vc_inspect_pool15(c, &p))) return r;
    memset(o, 0, sizeof *o);
    if (!p.complex) return VC_ERR_STATE;
    /* data-5 .. data-1 = LASTx, T, Z, Y, X */
    static const int AT[5] = { 4, 3, 2, 1, 0 };
    bcd_value(slot(vci_dump(c), offp(p.data - 5 + AT[reg])), o);
    snprintf(o->name, sizeof o->name, "%s i", NM[reg]);
    return VC_OK;
}

int vc_inspect_matrix(vc_calc *c, int m, int row, int col, vc_value *o) {
    vc_pool15 p; int r;
    if (!o || m < 0 || m > 4 || row < 1 || col < 1) return VC_ERR_ARG;
    if ((r = vc_inspect_pool15(c, &p))) return r;
    memset(o, 0, sizeof *o);
    if (row > p.rows[m] || col > p.cols[m]) return VC_ERR_ARG;
    int at = p.start[m] + (row - 1) * p.cols[m] + (col - 1);
    if (at < p.base || at > 0xFF) return VC_ERR_STATE;
    bcd_value(slot(vci_dump(c), offp(at)), o);
    snprintf(o->name, sizeof o->name, "%c %d,%d", 'A' + m, row % 100, col % 100);
    return VC_OK;
}
