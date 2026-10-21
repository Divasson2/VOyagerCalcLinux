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
 *                Pool: Path-C 176 registers (logical Nut 0x4D..0xFC; B5/B7B7 and the multi E8E8 at
 *                dump 0x168, the first extended multi E3E3 at 0x170), stock 29 registers (Nut
 *                0xE0..0xFC, 0x6F0; standalone stock 16C and the multi E6E6). vc_mem16_layout() tells.
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
    int mem_layout;     /* 16C: VC_MEM16_* */
} layout;

static int get_layout(vc_calc *c, layout *L) {
    memset(L, 0, sizeof *L);
    L->d = vci_dump(c);
    if (!L->d) return VC_ERR_STATE;
    L->model = vc_model(c); L->variant = vc_model_variant(c);
    if (L->model == VC_MODEL_16C) {
        int lay = vc_mem16_layout(c);
        L->mem_layout = lay;
        L->pathc = lay == VC_MEM16_EXT || lay == VC_MEM16_EXT_E3E3;
        if (L->pathc) { L->first = 0x4D; L->slots = 176; L->pool = lay == VC_MEM16_EXT ? 0x168 : 0x170; }
        else { L->pool = 0x6F0; L->first = 0xE0; L->slots = 29; }
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
        o->mem_layout = L.mem_layout; o->pool_offset = (int)L.pool;
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

/* The pool of a 15C image (live dump or a 2 KB memory file); variant 150 or 152. */
static void pool15_decode(const uint8_t *d, int variant, vc_pool15 *o) {
    int k;
    const uint8_t *mp = d + 0x118;
    memset(o, 0, sizeof *o);
    o->variant = variant == 152 ? 152 : 150;
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
    o->complex = (d[0x4C] >> 3) & 1;
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
}

int vc_inspect_pool15(vc_calc *c, vc_pool15 *o) {
    layout L; int r;
    if (!c || !o) return VC_ERR_ARG;
    memset(o, 0, sizeof *o);
    if ((r = get_layout(c, &L))) return r;
    if (L.model != VC_MODEL_15C) return VC_ERR_STATE;
    pool15_decode(L.d, L.variant, o);
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

/* ------------------------------------------------------------ 16C memory image layouts */
/* Where the data of a 2 KB image sits tells which layout wrote it (found with the dumps of B7B7,
 * E3E3, E8E8, E6E6 and the stock 16C, core/tests/test_mem16.c):
 *   standard   data only in 0x6F0..0x7D7 (29 registers; the pool is the top 232 bytes), nothing below;
 *   extended   data in 0x168..0x6EF, nothing in 0x6F0..0x7D7. B7B7 / E8E8: R0 starts at 0x168 and the
 *              program starts at the top register (0x6E0..0x6E7), never 0x6E8..0x6EF. E3E3: 8 bytes later:
 *              R0 at 0x170 (0x168..0x16F stay empty) and the program top at 0x6E8..0x6EF.
 * Nothing in the pool areas (a cleared calculator, only the stack / flags): fits every layout.
 * Data in both pool areas (or in both end slots): not a 16C image of any layout (a 15C memory is like that). */
static int nz(const uint8_t *m, int a, int b) { int n = 0, i; for (i = a; i < b; i++) n += m[i] != 0; return n; }

int vc_memory_layout(const uint8_t mem[VC_MEMORY_SIZE]) {
    if (!mem) return VC_MEM16_NONE;
    int std = nz(mem, 0x6F0, 0x7D8), ext = nz(mem, 0x168, 0x6F0);
    if (!std && !ext) return VC_MEM16_ANY;
    if (std && ext) return VC_MEM16_NONE;
    if (std) return VC_MEM16_STD;
    int first = nz(mem, 0x168, 0x170), last = nz(mem, 0x6E8, 0x6F0);
    if (first && last) return VC_MEM16_NONE;
    if (last) return VC_MEM16_EXT_E3E3;
    return VC_MEM16_EXT;      /* first slot used, or (R0 = 0 and no program) only the middle: undecidable, taken as the main layout */
}

static const char *layout_name(int l) {
    return l == VC_MEM16_STD ? "standard 16C memory (203 bytes, HP-16C CE stock / multi E6E6)" :
           l == VC_MEM16_EXT ? "extended 16C memory (B5B5 / B7B7 / multi E8E8 layout)" :
           l == VC_MEM16_EXT_E3E3 ? "extended 16C memory in the early multi E3E3 layout (8 bytes later)" : "an unknown layout";
}

int vc_memory_precheck(const vc_calc *c, const uint8_t mem[VC_MEMORY_SIZE], vc_memcheck *o) {
    if (!c || !mem || !o) return VC_ERR_ARG;
    memset(o, 0, sizeof *o);
    o->image_layout = vc_memory_layout(mem);
    o->firmware_layout = vc_mem16_layout(c);
    int f = o->firmware_layout, i = o->image_layout;
    if (f == VC_MEM16_NONE || i == VC_MEM16_ANY || i == f) return VC_OK;        /* 12C / 15C: nothing to check here */
    if (i == VC_MEM16_NONE) {
        o->verdict = VC_MEMCHK_WARN;
        snprintf(o->message, sizeof o->message, "This does not look like an HP-16C memory file: data is stored in the areas of both 16C memory layouts (a 15C or damaged file?).");
    } else if (f == VC_MEM16_EXT && i == VC_MEM16_EXT_E3E3) {
        o->verdict = VC_MEMCHK_CONVERT;
        snprintf(o->message, sizeof o->message, "This memory file was saved on the early extended-memory multi firmware (E3E3): its registers sit 8 bytes later than on this firmware. It can be converted (nothing else changes).");
    } else {
        o->verdict = VC_MEMCHK_WARN;
        snprintf(o->message, sizeof o->message, "This memory file is %s, but this firmware's 16C memory is %s. It would load without an error and show wrong or empty registers and program.", layout_name(i), layout_name(f));
    }
    return VC_OK;
}

int vc_memory_convert_e3e3(uint8_t mem[VC_MEMORY_SIZE]) {
    if (!mem || vc_memory_layout(mem) != VC_MEM16_EXT_E3E3) return 0;
    memmove(mem + 0x168, mem + 0x170, 0x6F0 - 0x170);       /* 175 registers of the pool, 8 bytes down */
    memset(mem + 0x6E8, 0, 8);                               /* the last register slot: outside the pool in the new layout */
    return 1;
}

/* ------------------------------------------------------------ HP-15C matrices in a memory image (0.7.2)
 * The Matrix Editor writes a matrix into a 2 KB image the way the firmware's DIM does it
 * (docs/HP15C-POOL-COMPLEX-MATRICES.md §3.2): the matrix grows or shrinks at its own end, the
 * matrices above it and the free boundary (map byte 0) move with it, the program never moves, and
 * a matrix that does not fit between the pool end and the program is refused (the firmware's
 * Error 10). A fresh 15.2 cold start leaves a free gap below R2; like the firmware's first
 * reallocation, the write packs the window down onto the pool base first. */
static void put_slot(uint8_t *d, uint32_t off, uint64_t v) {
    int i;
    for (i = 0; i < 7; i++) { d[off + (uint32_t)i] = (uint8_t)(v & 0xFF); v >>= 8; }
    d[off + 7] = 0;
}
static const uint32_t DIM15_OFF[5][2] = { {0x13E, 0x13D}, {0x13C, 0x13B}, {0x13A, 0x139}, {0x146, 0x145}, {0x144, 0x143} };

int vc_image_pool15(const uint8_t mem[VC_MEMORY_SIZE], int variant, vc_pool15 *out) {
    if (!mem || !out || (variant != 150 && variant != 152)) return VC_ERR_ARG;
    pool15_decode(mem, variant, out);
    if (out->data < out->base || out->data > 0x100 || out->pool < out->data || out->pool > 0x100) return VC_ERR_STATE;
    return VC_OK;
}

int vc_image_matrix15(const uint8_t mem[VC_MEMORY_SIZE], int variant, int m, int row, int col, vc_value *out) {
    vc_pool15 p; int r;
    if (!out || m < 0 || m > 4 || row < 1 || col < 1) return VC_ERR_ARG;
    if ((r = vc_image_pool15(mem, variant, &p))) return r;
    memset(out, 0, sizeof *out);
    if (row > p.rows[m] || col > p.cols[m]) return VC_ERR_ARG;
    int at = p.start[m] + (row - 1) * p.cols[m] + (col - 1);
    if (at < p.base || at > 0xFF) return VC_ERR_STATE;
    bcd_value(slot(mem, offp(at)), out);
    snprintf(out->name, sizeof out->name, "%c %d,%d", 'A' + m, row % 100, col % 100);
    return VC_OK;
}

int vc_matrix15_room(const uint8_t mem[VC_MEMORY_SIZE], int variant, int m, int rows, int cols, int *available) {
    vc_pool15 p; int r;
    if (m < 0 || m > 4 || rows < 0 || cols < 0 || rows > 99 || cols > 99) return VC_ERR_ARG;
    if ((r = vc_image_pool15(mem, variant, &p))) return r;
    int old = p.rows[m] * p.cols[m];
    /* contiguous room after packing the cold-start gap: what DIM can take */
    int room = p.prog_low - p.pool + (p.scratch ? 0 : p.gap) + old;
    if (available) *available = room;
    return rows * cols <= room ? VC_OK : VC_ERR_ARG;
}

int vc_matrix15_write(uint8_t mem[VC_MEMORY_SIZE], int variant, int m, int rows, int cols, const uint64_t *raw) {
    vc_pool15 p; int r, k, i;
    if (!mem || m < 0 || m > 4 || rows < 0 || cols < 0 || rows > 99 || cols > 99) return VC_ERR_ARG;
    if (rows == 0 || cols == 0) rows = cols = 0;
    if ((r = vc_matrix15_room(mem, variant, m, rows, cols, NULL))) return r;
    if ((r = vc_image_pool15(mem, variant, &p))) return r;
    uint8_t *mp = mem + 0x118;
    if (p.gap > 0 && !p.scratch) {
        /* pack: imaginary stack, R2.., matrices move down onto the pool base (values move with them) */
        int lo = p.data - (p.complex ? 5 : 0), g = p.gap;
        memmove(mem + offp(lo - g), mem + offp(lo), 8u * (uint32_t)(p.pool - lo));
        memset(mem + offp(p.pool - g), 0, 8u * (uint32_t)g);
        for (k = 1; k <= 6; k++) mp[k] = (uint8_t)(mp[k] - g);
        mp[0] = (uint8_t)((p.pool - g) & 0xFF);
        pool15_decode(mem, variant, &p);
    }
    int old = p.rows[m] * p.cols[m], now = rows * cols, delta = now - old;
    int end = p.start[m] + old;
    if (p.pool + delta > p.prog_low) return VC_ERR_ARG;
    if (delta) {
        memmove(mem + offp(end + delta), mem + offp(end), 8u * (uint32_t)(p.pool - end));
        if (delta < 0) memset(mem + offp(p.pool + delta), 0, 8u * (uint32_t)(-delta));
        else memset(mem + offp(end), 0, 8u * (uint32_t)delta);
        for (k = m + 1; k < 5; k++) mp[5 - k] = (uint8_t)(p.start[k] + delta);
        int pool = p.pool + delta;
        mp[0] = (uint8_t)(pool >= 0x100 ? 0 : pool);
    }
    mem[DIM15_OFF[m][0]] = (uint8_t)rows; mem[DIM15_OFF[m][1]] = (uint8_t)cols;
    if (raw) for (i = 0; i < now; i++) put_slot(mem, offp(p.start[m] + i), raw[i] & MASK56);
    return VC_OK;
}

/* Text -> the 15C's BCD register value: optional sign, digits with an optional '.', optional
 * exponent (e or E, optional sign). Rounded to 10 significant digits (half up). Exponents past
 * +-99 are refused (VC_ERR_ARG), anything smaller than 1e-99 becomes 0 (as the calculator does). */
int vc_bcd_from_text(const char *s, uint64_t *out) {
    int neg = 0, dig[64], nd = 0, point = -1, seen = 0, lead = 1, intdigits = 0, e = 0, eneg = 0, i;
    if (!s || !out) return VC_ERR_ARG;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '+' || *s == '-') neg = *s++ == '-';
    for (;; s++) {
        if (*s >= '0' && *s <= '9') {
            seen = 1;
            if (lead && *s == '0') { if (point >= 0) intdigits--; continue; }
            lead = 0;
            if (nd < 64) dig[nd++] = *s - '0';
            if (point < 0) intdigits++;
        } else if (*s == '.' && point < 0) point = 1;
        else break;
    }
    if (!seen) return VC_ERR_ARG;
    if (*s == 'e' || *s == 'E') {
        int any = 0; s++;
        if (*s == '+' || *s == '-') eneg = *s++ == '-';
        while (*s >= '0' && *s <= '9') { if (e < 10000) e = e * 10 + (*s - '0'); s++; any = 1; }
        if (!any) return VC_ERR_ARG;
        if (eneg) e = -e;
    }
    while (*s == ' ' || *s == '\t') s++;
    if (*s) return VC_ERR_ARG;
    if (nd == 0) { *out = 0; return VC_OK; }
    int exp = intdigits - 1 + e;               /* scientific exponent of the first significant digit */
    int m[11] = {0};
    for (i = 0; i < 10; i++) m[i] = i < nd ? dig[i] : 0;
    if (nd > 10 && dig[10] >= 5) {
        for (i = 9; i >= 0; i--) { if (++m[i] < 10) break; m[i] = 0; }
        if (i < 0) { for (i = 9; i > 0; i--) m[i] = m[i - 1]; m[0] = 1; exp++; }
    }
    if (exp > 99) return VC_ERR_ARG;
    if (exp < -99) { *out = 0; return VC_OK; }
    uint64_t v = neg ? 9 : 0;
    for (i = 0; i < 10; i++) v = v << 4 | (uint64_t)m[i];
    int ee = exp < 0 ? 1000 + exp : exp;
    v = v << 12 | (uint64_t)(ee / 100) << 8 | (uint64_t)(ee / 10 % 10) << 4 | (uint64_t)(ee % 10);
    *out = v;
    return VC_OK;
}

int vc_bcd_text(uint64_t raw, char *buf, size_t n) {
    vc_value v;
    if (!buf || !n) return VC_ERR_ARG;
    memset(&v, 0, sizeof v);
    bcd_value(raw & MASK56, &v);
    snprintf(buf, n, "%s", v.text);
    return VC_OK;
}
