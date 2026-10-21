/* voyager_core.h - native emulator core for the HP-12C / 15C / 16C CE (ARM "Voyager") calculators.
 *
 * The core emulates the calculator's ATSAM4L Cortex-M4 chip and runs the user's own
 * CE firmware file. No HP firmware or ROM is part of this code.
 *
 * Plain C11, no globals, one opaque handle per calculator: easy to call from Swift
 * (import this header through a bridging header or a module map).
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 José González Divassón
 */
#ifndef VOYAGER_CORE_H
#define VOYAGER_CORE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VC_API_VERSION 6
#define VC_MEMORY_SIZE 2048          /* a VoyagerSave .16c / .15c file */
#define VC_LCD_BYTES   32            /* LCD controller segment registers DRL0..DRH3 */

typedef struct vc_calc vc_calc;

/* Results */
enum {
    VC_OK = 0,
    VC_ERR_ARG = -1,        /* bad argument (NULL, wrong size, unknown key) */
    VC_ERR_FIRMWARE = -2,   /* firmware file not recognised */
    VC_ERR_STATE = -3,      /* call not valid now (no firmware, wrong engine) */
    VC_ERR_FAULT = -4,      /* the emulated chip crashed; see vc_last_error() */
    VC_ERR_HOLD = -5,       /* runner: the key waits for a release/timer it cannot model */
    VC_ERR_UNSUPPORTED = -6 /* firmware recognised but not run (an original-layout image other than the HP-12C); see vc_last_error() */
};

/* Engines */
enum {
    VC_ENGINE_RUNNER = 0,   /* program runner: each key runs to completion (fast, no timers) */
    VC_ENGINE_WHOLE  = 1    /* whole calculator: power-on, key matrix, 10 ms timer, sleep */
};

/* Results of vc_key() */
enum {
    VC_KEY_DONE = 0,        /* calculator waits for the next key */
    VC_KEY_LIMIT = 1,       /* max_nut_steps used up (program still running) */
    VC_KEY_HELD = 2         /* display-hold function (f MEM, f SHOW...) shown and ended */
};

/* Models */
enum { VC_MODEL_NONE = 0, VC_MODEL_12C = 12, VC_MODEL_15C = 15, VC_MODEL_16C = 16,
       VC_FAMILY_MULTI = 99, /* firmware family: HP-12C CE multi (12C / 15C / 16C chosen on the calculator) */
       VC_FAMILY_STOCK = 98  /* HP's original firmware layout (e.g. the 2015 HP-12C): the 12C image runs, with limits (see vc_capabilities) */ };

/* What a loaded firmware supports (vc_capabilities). HP's original 12C runs the keyboard and display only:
 * its RAM layout differs from the CE builds, so the inspector, memory files and the model switch do not apply. */
enum {
    VC_CAP_KEYS_DISPLAY = 1 << 0,  /* keyboard, LCD, timers: the calculator runs */
    VC_CAP_INSPECT      = 1 << 1,  /* vc_inspect*(): stack, registers, program from the Continuous Memory image */
    VC_CAP_MEMORY       = 1 << 2,  /* vc_load_memory / vc_save_memory (VoyagerSave image) */
    VC_CAP_MODEL_SWITCH = 1 << 3   /* the firmware reports which calculator runs (vc_model follows it) */
};

/* Annunciator bits returned by vc_get_annunciators() */
enum {
    VC_ANN_F    = 1 << 0,
    VC_ANN_G    = 1 << 1,
    VC_ANN_C    = 1 << 2,   /* carry */
    VC_ANN_G_OV = 1 << 3,   /* "G" out-of-range */
    VC_ANN_PRGM = 1 << 4,
    VC_ANN_BATT = 1 << 5    /* low battery */
};

typedef struct vc_stats {
    uint64_t arm_instructions;   /* ARM instructions executed */
    uint64_t nut_instructions;   /* Nut (HP) instructions interpreted by the firmware */
    uint64_t sim_ms;             /* whole engine: simulated time in ms */
    uint64_t sleeps;             /* whole engine: WFI sleeps */
} vc_stats;

/* --- life cycle ------------------------------------------------------------ */
vc_calc   *vc_create(void);
void       vc_destroy(vc_calc *c);

/* Copy and identify a firmware image: the .bin a firmware updater writes at 0x4000 (114,688 bytes),
 * or a full dump of the 128 KB flash read from address 0 (the first 0x4000 bytes are the bootloader
 * and are dropped). On failure vc_last_error() explains what was wrong and what is expected. HP's
 * original 12C image loads as family VC_FAMILY_STOCK and runs (limited, see vc_capabilities); other
 * original-layout images return VC_ERR_UNSUPPORTED (recognised, not verified). */
int        vc_load_firmware(vc_calc *c, const uint8_t *data, size_t len);
/* Active model (12, 15 or 16). In the whole-calculator engine this follows the
 * calculator's own model switch at run time (it looks at the Nut ROM the firmware runs).
 * Apps should poll it (it is cheap) and switch the keyboard skin when it changes. */
int        vc_model(const vc_calc *c);
/* Active ROM in more detail: 120, 150 (15C 15.0), 152 (15C 15.2 long memory), 160. */
int        vc_model_variant(const vc_calc *c);
/* Firmware family: VC_MODEL_16C, VC_MODEL_15C or VC_FAMILY_MULTI. A 15C-family firmware
 * can also be a multi-ROM build (15 / 15.2 / 16): vc_model always tells what runs now. */
int        vc_firmware_family(const vc_calc *c);
/* Bit set of VC_CAP_*; 0 when no firmware is loaded. */
unsigned   vc_capabilities(const vc_calc *c);
/* Calculator images (Nut ROMs) in the firmware, as variants (120, 150, 152, 160), in the order
 * the calculator's own model menu (g-ENTER-ON, 4) lists them; up to max. Returns the count
 * (1 for single-image firmware), or an error. Found by the ROM header in the flash image. */
int        vc_firmware_images(const vc_calc *c, int *variants, int max);
/* The checksum the calculator shows as XYXY, as the byte XY (0..255), or -1 (image too short). */
int        vc_firmware_checksum(const vc_calc *c);
/* Bytes dropped from the front of the file (0x4000 for a full flash dump, else 0). */
uint32_t   vc_firmware_stripped(const vc_calc *c);
/* Diagnostics: read a 32-bit word of the emulated chip's memory. */
uint32_t   vc_debug_read32(vc_calc *c, uint32_t addr);
/* Text such as "HP-16C CE firmware (extended memory)". */
const char*vc_firmware_info(const vc_calc *c);

/* Start an engine. RUNNER: power-on initialisation; with cold=1 also ON, BSP (clean
 * display) like the Python runner. WHOLE: chip reset and boot; the calculator is off
 * until ON is pressed (cold is ignored). */
int        vc_start(vc_calc *c, int engine, int cold);
int        vc_engine(const vc_calc *c);

/* WHOLE engine: simulated battery voltage (default 3.0 V); takes effect at vc_start. */
int        vc_set_battery(vc_calc *c, double volts);
/* Speed of the emulated CPU: ARM instructions executed per CPU clock, so one 10 ms tick runs MHz x 10,000 x ipc
 * instructions (the firmware's clock setting 4 / 12 / 48 MHz still applies). Timers are unaffected (they count
 * ticks). Default VC_IPC_REFERENCE (0.75, the Python reference's estimate: keeps instruction-for-instruction parity
 * with it). The apps set VC_IPC_REAL, calibrated so a 4 MHz program runs as fast as a real 15C CE. 0 = default. */
#define VC_IPC_REFERENCE 0.75
#define VC_IPC_REAL      0.577        /* 0.75 / 1.3: VoyagerCalc ran ~30 % fast at 4 MHz (stopwatch on a real 15C CE) */
int        vc_set_clock_ipc(vc_calc *c, double ipc);
double     vc_clock_ipc(const vc_calc *c);

/* --- memory (VoyagerSave image) -------------------------------------------- */
int        vc_load_memory(vc_calc *c, const uint8_t mem[VC_MEMORY_SIZE]);
int        vc_save_memory(vc_calc *c, uint8_t out[VC_MEMORY_SIZE]);

/* --- keys ------------------------------------------------------------------- */
/* Key code = 10 * row + column (row 0 = top). ON is 30. Returns -1 for unknown names.
 * HP-16C names: "A".."F","7","8","9","/","GSB","GTO","HEX","DEC","OCT","BIN","4","5",
 * "6","*","R/S","SST","RDN","X<>Y","BSP","ENTER","1","2","3","-","ON","f","g","STO",
 * "RCL","0",".","CHS","+" (and the Python simulator's aliases). */
int        vc_key_code(int model, const char *name);
const char*vc_key_name(int model, int code);

/* WHOLE engine: hold / let go of a key (several may be held, e.g. a chord). */
int        vc_press_key(vc_calc *c, int code);
int        vc_release_key(vc_calc *c, int code);
/* Advance simulated time (WHOLE engine), in ms (10 ms steps). */
int        vc_run_ms(vc_calc *c, uint32_t ms);

/* One complete key stroke.
 * RUNNER: hands the key to the firmware and runs until it waits for the next key, or
 *         until max_nut_steps Nut instructions (0 = no limit). Returns VC_KEY_* or an error.
 * WHOLE:  press, hold 100 ms, release, wait 250 ms (max_nut_steps ignored). */
int        vc_key(vc_calc *c, int code, uint64_t max_nut_steps);
/* Continue a program after VC_KEY_LIMIT (runner) is done by pressing keys as usual. */

/* --- display ---------------------------------------------------------------- */
/* HP-16C number display decoded to text, e.g. "       1 d" (10 characters + NUL). */
int        vc_get_display(vc_calc *c, char *buf, size_t n);
/* Display text shown by the last display-hold key (f MEM etc.), runner engine. */
int        vc_get_held_display(vc_calc *c, char *buf, size_t n);
/* Raw LCD controller segment memory (SAM4L LCDCA DRL0, DRH0, ..., DRL3, DRH3; 4 x 8
 * bytes, little-endian), i.e. every segment and annunciator as the firmware drew it. */
int        vc_get_lcd_segments(vc_calc *c, uint8_t out[VC_LCD_BYTES]);
uint32_t   vc_get_annunciators(vc_calc *c);   /* HP-16C meanings (VC_ANN_*) */

/* The display as drawn on the LCD: what an app needs to paint it. Index 0 = leftmost. */
typedef struct vc_display {
    uint8_t  seg[10];        /* 7-segment bits: a=1, b=2, c=4, d=8, e=16, f=32, g=64 */
    uint8_t  dot[10];        /* 1 = decimal point after this digit */
    uint8_t  comma[10];      /* 1 = comma tail after this digit (drawn under the point) */
    uint8_t  minus;          /* leading minus sign */
    uint8_t  reserved[3];
    uint32_t annunciators;   /* HP-16C meanings (VC_ANN_*) */
    uint64_t ann_bits;       /* raw annunciator LCD bits (bits 0..63 of the LCD memory that are
                                not digits, points, commas or the minus): model tables give names */
} vc_display;
int        vc_get_display_cells(vc_calc *c, vc_display *out);
/* Plain text of a vc_display, e.g. "-1,234.50" (for tests and accessibility). */
int        vc_display_text(const vc_display *d, char *buf, size_t n);
/* The two Nut display registers (56 bits each) the digit decoder uses. */
int        vc_get_nut_display(vc_calc *c, uint64_t *reg9, uint64_t *reg10);
/* WHOLE engine: last text frame painted by the firmware (menus, stopwatch, messages). */
int        vc_last_frame(vc_calc *c, char *buf, size_t n);
uint32_t   vc_frame_count(const vc_calc *c);

/* --- inspection (read-only) ----------------------------------------------------
 * What the calculator holds now: stack, LAST X, I, storage registers, modes and the
 * program. These calls only read the emulated memory: they never change the calculator,
 * its timing or its statistics, so an app can call them at any time (e.g. 10 times a second
 * while a program runs). Supported: HP-16C (Path-C extended memory and stock layout),
 * HP-15C (15.0 and 15.2) and HP-12C (stack and registers; no program listing yet).
 * While a program runs the values are a live snapshot: X may hold an intermediate result. */

enum { VC_VAL_NONE = 0, VC_VAL_INT = 1, VC_VAL_FLOAT = 2 };
typedef struct vc_value {
    int      kind;          /* VC_VAL_INT (16C integer modes) or VC_VAL_FLOAT */
    uint64_t raw;           /* the 56-bit Nut register as stored (BCD for floats) */
    uint64_t word;          /* VC_VAL_INT: the word, word_size bits, zero-extended */
    int      negative;      /* VC_VAL_INT: negative under the complement mode; floats: sign */
    double   number;        /* the value as a double (16C integers: per the complement mode) */
    char     text[72];      /* as the calculator would show it: integer in the current base
                               (HEX/DEC/OCT/BIN, no suffix), floats with all 10 digits */
    char     dec[32];       /* decimal: 16C integers per the complement mode; floats = text */
    char     name[12];      /* "X", "LAST X", "I", "R0", "R.F", "R615", "PMT" ... */
} vc_value;

/* Stack-like registers for vc_inspect_stack(). */
enum { VC_REG_X = 0, VC_REG_Y, VC_REG_Z, VC_REG_T, VC_REG_LASTX, VC_REG_I,
       VC_REG_FIN_N, VC_REG_FIN_I, VC_REG_FIN_PV, VC_REG_FIN_PMT, VC_REG_FIN_FV,  /* HP-12C */
       VC_REG_COUNT };

typedef struct vc_inspect_info {
    int      model;         /* 12, 15, 16 (0 = nothing to inspect) */
    int      variant;       /* 120, 150, 152, 160 */
    int      base;          /* 16C: 16, 10, 8, 2, or 0 in FLOAT mode; others 10 */
    int      word_size;     /* 16C: 1..64 (56 in FLOAT mode); others 0 */
    int      complement;    /* 16C: 0 = UNSGN, 1 = 1's, 2 = 2's */
    uint32_t flags;         /* bit n = flag n set (16C 0..5, 15C 0..9) */
    int      prgm_mode;     /* program mode (PRGM annunciator) */
    int      running;       /* a program is running (whole engine) */
    int      line;          /* current program line (0 = line 000) */
    int      lines;         /* program length (lines) */
    int      registers;     /* storage registers now available: R0 .. R(registers-1) */
    int      has_i;         /* the model has an index register I */
    int      has_program;   /* vc_inspect_program() works for this model */
    int      ext_memory;    /* 16C Path-C extended memory */
    int      mem_layout;    /* 16C: VC_MEM16_* layout of the memory image (0 = not a 16C) */
    int      pool_offset;   /* 16C: dump offset of the first pool register (0x6F0, 0x168, 0x170) */
    int      reserved[4];
} vc_inspect_info;
int vc_inspect(vc_calc *c, vc_inspect_info *out);
int vc_inspect_stack(vc_calc *c, int reg, vc_value *out);      /* VC_REG_* */
int vc_inspect_register(vc_calc *c, int n, vc_value *out);      /* storage register R n */

/* HP-15C memory pool (15.0 and 15.2), from the system registers (docs/HP15C-POOL-COMPLEX-MATRICES.md).
 * Addresses are Nut register addresses (0x40/0xA0 .. 0xFF); matrices 0..4 = A..E. */
typedef struct vc_pool15 {
    int  variant;           /* 150 or 152 (the running mode, from the core) */
    int  base, top;         /* pool: base .. top-1 (top = 0x100) */
    int  data;              /* R2 (first storage register in the pool) */
    int  start[5];          /* first register of matrix A..E (= end of the block before) */
    int  pool;              /* first uncommitted register (free starts here) */
    int  rows[5], cols[5];  /* matrix dimensions (0x0 = not dimensioned) */
    int  result;            /* RESULT matrix 0..4 */
    int  complex;           /* flag 8: 5 imaginary registers just below R2 */
    int  scratch;           /* SOLVE (5) / integrate (23) workspace at the bottom, freed at the next key */
    int  gap;               /* free registers below R2 (a fresh 15.2 cold start), 0 normally */
    int  storage;           /* R2 .. R(DIM (i)) */
    int  dim_i;             /* DIM (i) = storage + 1 */
    int  matrices;          /* sum of rows x cols */
    int  prog_low;          /* lowest program register (0x100 = no program) */
    int  prog_regs, prog_bytes;
    int  free;              /* g MEM second field */
    char gmem[24];          /* the g MEM line: "19 78 00-0" */
    int  reserved[4];
} vc_pool15;
int vc_inspect_pool15(vc_calc *c, vc_pool15 *out);
int vc_inspect_imag(vc_calc *c, int reg, vc_value *out);   /* VC_REG_X..VC_REG_LASTX, complex mode only */
int vc_inspect_matrix(vc_calc *c, int m, int row, int col, vc_value *out);   /* 1-based row, col */

/* --- HP-15C matrices in a 2 KB memory image (0.7.2, the Matrix Editor) --------------------------
 * The same pool as vc_inspect_pool15, read from an image (variant 150 or 152: the running 15C mode). */
int vc_image_pool15(const uint8_t mem[VC_MEMORY_SIZE], int variant, vc_pool15 *out);
int vc_image_matrix15(const uint8_t mem[VC_MEMORY_SIZE], int variant, int m, int row, int col, vc_value *out);
/* Can matrix m (0..4 = A..E) be dimensioned rows x cols (0..99 each)? VC_OK or VC_ERR_ARG (does not
 * fit: the firmware would give Error 10). *available = the largest rows*cols that fits (its own
 * registers count as free). */
int vc_matrix15_room(const uint8_t mem[VC_MEMORY_SIZE], int variant, int m, int rows, int cols, int *available);
/* DIM matrix m to rows x cols in the image, as the firmware does (matrices above and the free
 * boundary move, the program does not), then store raw[0 .. rows*cols-1] row-major (BCD register
 * values; NULL keeps the old elements in their linear order, new ones are 0). 0 x 0 frees it. */
int vc_matrix15_write(uint8_t mem[VC_MEMORY_SIZE], int variant, int m, int rows, int cols, const uint64_t *raw);
/* Text ("1.5", "-2E-3", "1e5"; '.' as the decimal point) -> a 15C register value (10 digits, rounded). */
int vc_bcd_from_text(const char *text, uint64_t *raw);
/* A 15C register value as the inspector shows it ("1.5", "-2e-3"): vc_bcd_from_text reads it back exactly. */
int vc_bcd_text(uint64_t raw, char *buf, size_t n);

typedef struct vc_prog_line {
    int      line;          /* 1 .. lines */
    uint8_t  code[2];       /* the stored byte(s) */
    uint8_t  len;           /* 1 or 2 */
    char     prefix;        /* 'f', 'g' or 0: shift key of the function (for colouring) */
    char     keys[16];      /* keycodes as the calculator shows them: "43,22, A", "42 44", "6" */
    char     mnemonic[28];  /* readable: "LBL A", "WSIZE", "DBL×", "STO + 1" (UTF-8) */
} vc_prog_line;
/* Up to max lines starting at line first (1-based). Returns the number filled, or an error. */
int vc_inspect_program(vc_calc *c, int first, vc_prog_line *out, int max);


/* --- 16C memory layouts (what a .16c / .bin16c image contains) ------------------------------
 * The 2 KB image has three 16C layouts. Same size, same extension, but the registers and the
 * program sit at different offsets, so an image from one layout read as another shows wrong or
 * empty registers (no error, the firmware accepts it):
 *   VC_MEM16_STD        stock 16C memory: 29 registers (203 bytes) at 0x6F0. HP-16C CE firmware
 *                       without extended memory and the multi E6E6 (12C / 15.2 / 16C).
 *   VC_MEM16_EXT        Path-C extended memory: 176 registers (1232 bytes) at 0x168. The 16C CE
 *                       builds B5B5 / B7B7 and the multi E8E8 (identical layout: images move both ways).
 *   VC_MEM16_EXT_E3E3   the first multi with extended memory (E3E3): the same pool at 0x170
 *                       (8 bytes later). vc_memory_convert_e3e3() makes it VC_MEM16_EXT.
 *   VC_MEM16_ANY        (images only) nothing stored in the pool areas: fits every layout. */
enum { VC_MEM16_NONE = 0, VC_MEM16_STD = 1, VC_MEM16_EXT = 2, VC_MEM16_EXT_E3E3 = 3, VC_MEM16_ANY = 4 };
/* Layout of the active 16C memory of the loaded firmware (VC_MEM16_NONE when 12C / 15C is active). */
int vc_mem16_layout(const vc_calc *c);
/* Same for the firmware file, whatever model is active (NONE: no 16C in it, e.g. the 12C-only images). */
int vc_firmware_mem16(const vc_calc *c);
/* Layout a 2 KB image appears to have (structural guess from where data sits; VC_MEM16_NONE when
 * it cannot be a 16C image or its pool areas contradict each other). */
int vc_memory_layout(const uint8_t mem[VC_MEMORY_SIZE]);
enum { VC_MEMCHK_OK = 0,        /* nothing wrong found (or not a 16C: nothing to check) */
       VC_MEMCHK_WARN = 1,      /* the image is another 16C layout: it would read wrongly here */
       VC_MEMCHK_CONVERT = 2 }; /* old E3E3 image into the extended layout: vc_memory_convert_e3e3 fixes it */
typedef struct vc_memcheck {
    int  verdict;               /* VC_MEMCHK_* */
    int  image_layout;          /* VC_MEM16_* of the image */
    int  firmware_layout;       /* VC_MEM16_* of the active 16C memory (NONE: not in 16C mode) */
    char message[320];          /* explanation to show the user (empty when OK) */
} vc_memcheck;
/* Structural pre-check of a memory image against the running firmware, to run before the
 * trial load (a trial load cannot see this: the firmware does not reject it). Read-only. */
int vc_memory_precheck(const vc_calc *c, const uint8_t mem[VC_MEMORY_SIZE], vc_memcheck *out);
/* Old E3E3 image -> VC_MEM16_EXT layout (pool 8 bytes earlier), in place. Returns 1 when it
 * converted, 0 when the image is not an E3E3-layout image (left unchanged). */
int vc_memory_convert_e3e3(uint8_t mem[VC_MEMORY_SIZE]);

/* --- misc -------------------------------------------------------------------- */
void       vc_get_stats(const vc_calc *c, vc_stats *out);
const char*vc_last_error(const vc_calc *c);

#ifdef __cplusplus
}
#endif
#endif
