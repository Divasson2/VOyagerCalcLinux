# Voyager native core (Milestone 1)

A small C program library that **runs your own HP-16C CE firmware file on a simulated
calculator chip**, without Unicorn and without any HP code inside. It is the engine for the
planned Mac calculator app.

Written by José González Divassón, 2026. Licence: **MIT** (see "Licence" below).

## In plain words

* The real HP-16C CE has an Atmel ATSAM4L chip (an ARM Cortex-M4). Its firmware imitates the
  old HP "Nut" processor, which runs the original HP-16C program.
* This core imitates **the ARM chip**: it reads each ARM instruction of the firmware file and
  does what the chip would do. So the calculator behaves exactly as the firmware says,
  including our own changes (extended memory, stopwatch, menus and so on).
* You give it the firmware `.bin` file at run time. The source contains **no firmware and no
  ROM bytes**. It recognises a firmware file by SHA-256 fingerprints of a few short pieces of
  code (the same method as our public Python simulator).
* It can load and save the 2 KB **VoyagerSave** memory (`.16c` files), take key presses, and
  tell you what the display shows (digits, annunciators and the raw LCD segments).

## Two engines (both from the Python simulator, which they copy exactly)

| | **Program runner** (`VC_ENGINE_RUNNER`) | **Whole calculator** (`VC_ENGINE_WHOLE`) |
|---|---|---|
| Copies | `voyager/runner16c.py` | `voyager/wholecalc.py` |
| How | each key goes straight to the firmware's key routine and runs to the end | starts from the chip's reset; key matrix, 10 ms timer, sleep, battery reading, clock speed |
| Good for | programs, memory, `.16c` files, fast tests | the real feel: timing, f MEM while held, menus, stopwatch, service screens |
| For the app | "turbo"/batch runs and tests | the normal mode of the app |

## Build

Linux (here) or macOS, any C11 compiler:

```
make            # uses $CC (clang recommended): build/libvoyagercore.a and build/vcore
```

Only standard C is used (`stdlib`, `string`, `stdio`). For Xcode: add `src/*.c` and
`include/voyager_core.h` to the target, then import the header through the bridging header,
or use `include/module.modulemap` (`import VoyagerCore`).

Sizes (clang -O2, x86-64): code 33 KB (cpu 15.6 KB, machine 16.8 KB, sha256 1.3 KB).
About 1,800 lines of C. Memory per calculator: about 2.5 MB in the program runner, 0.5 MB in
the whole-calculator engine.

## The command-line test driver

```
build/vcore -f FIRMWARE.bin --info
build/vcore -f FIRMWARE.bin --keys "1 ENTER 1 +" --show
build/vcore -f FIRMWARE.bin --load miller-rabin-ext-ready.16c --fmem --keys "2147483647 GSB A" --show --save out.16c
build/vcore -f FIRMWARE.bin --whole --keys "ON g+ENTER+ON wait:2000" --show --frames --lcd
build/vcore -f FIRMWARE.bin --protocol seqs.txt [--load M.16c] [--whole]   # per-key log for comparisons
```

Options: `--trace` (display after each key), `--count` (Nut/ARM instructions and memory
hash), `--bench` (speed), `--lcd` (raw segments), `--max-steps N`, `--battery V`.
Key names are the Python simulator's (`A`…`F`, `0`…`9`, `ENTER`, `CHS`, `f`, `g`, `GSB`,
`R/S`, `BSP`, `x<>y`, `RDN`, `*`, `/`, `-`, `+`, `.`, `ON` …). A number like `2147483647` is
typed digit by digit. In `--whole`, `a+b+c` is a chord and `wait:MS` lets time pass.
`--save` never overwrites a file.

## The API (`include/voyager_core.h`)

```
vc_calc *c = vc_create();
vc_load_firmware(c, bytes, len);         // the user's .bin, copied; returns VC_ERR_FIRMWARE if unknown
vc_start(c, VC_ENGINE_WHOLE, 0);         // or VC_ENGINE_RUNNER (cold=1: ON, BSP)
vc_load_memory(c, mem2048);  vc_save_memory(c, out2048);
vc_press_key(c, code); vc_run_ms(c, 100); vc_release_key(c, code); vc_run_ms(c, 250);
vc_key(c, code, max_nut_steps);          // one complete key stroke (both engines)
vc_get_display(c, buf, 16);              // "       1 d"
vc_get_annunciators(c);                  // VC_ANN_F | VC_ANN_G | VC_ANN_C | VC_ANN_G_OV | VC_ANN_PRGM
vc_get_lcd_segments(c, seg32);           // raw LCD controller memory, every segment
vc_last_frame(c, buf, n);                // menus / stopwatch / messages painted by our firmware
vc_get_stats(c, &stats); vc_last_error(c); vc_destroy(c);
```

Key code = 10 × row + column (row 0 = top row, ON = 30), as in the Python simulator;
`vc_key_code(16, "ENTER")` converts names. One `vc_calc` per calculator; no global state.

Annunciator bits in the LCD memory (found with the simulator): f = bit 6, g = bit 9,
C (carry) = bit 21, G (out of range) = bit 14, PRGM = bit 24. The low-battery mark is **not
mapped yet**.

## Test results (2026-10-10)

These results were obtained by comparing against the author's Python simulator (voyager-sim) with the same
firmware file. The comparison scripts are not part of this repository; the C tests in `tests/` need a firmware file you own
(set the environment variables named at the top of each test) and are skipped without one.

* **Acceptance** (B7B7 and B5B5): load the Miller-Rabin `.16c`, f MEM = `P-2 r-0139`, key
  `2147483647 GSB A` → display **`1 d`**, and the 2 KB memory afterwards is **byte-identical**
  to `voyagersim.py` (MD5 `ee117c0d39b222485c47d1708d4f1286` on both builds).
* **`1 ENTER 1 +`** → `2 h`, 5,694 Nut instructions, same as Python. **f MEM** → `P-2 r-0139`
  (and `P-0 r-0616` on a cleared calculator).
* **Program runner, key by key** (display + SHA-256 of the 2 KB memory after every key):
  * fixed scripts: 7 + 7 sequences, 112 keys: identical;
  * 300 random sequences × 25 keys on B7B7 (cold): 6,750 keys: identical;
  * 100 random sequences on B7B7 with the Miller-Rabin memory: 2,229 keys: identical;
  * 100 random sequences on B5B5 with the B5B5 memory: 2,359 keys: identical.
  * The 11,338 random keys include 57 display-hold keys (f MEM / f SHOW…), 89 "waits for a
    timer" stops (e.g. PSE in a random program) and 2 runaway programs stopped by the step
    limit; all ended the same way in both.
* **Whole calculator, key by key** (display, memory hash, painted text frames, simulated time
  and number of sleeps after every key): 3 fixed + 60 random sequences on B7B7 and 40 random
  sequences on B5B5 with memory: 1,518 keys, identical. Also identical: the service screen
  (`ON g+ENTER+ON` → frame `1.L2.C3.H`, 130 frames) and the full Miller-Rabin run in the
  whole-calculator model (`1 d`, same memory, 126.5 simulated seconds, 4,614 sleeps).
* Also clean under AddressSanitizer + UndefinedBehaviorSanitizer, and builds without
  warnings with clang 19 and gcc (`-Wall -Wextra -Wpedantic`).

## Speed (this box, one core)

| | Miller-Rabin 2147483647 (5.3 million Nut instructions) |
|---|---|
| C core, program runner | **2.7 s** (about 87 million ARM instructions/s, 1.9–2.0 million Nut instructions/s) |
| Python simulator (Unicorn) | about 8.5 s |
| Real HP-16C CE at 48 MHz | about 14 s (from our measured 386,000 Nut instructions/s) |
| Original HP-16C | about 25 minutes (0.28 ms per Nut instruction) |

So the core is about **5× faster than the real CE at 48 MHz** and about 3× faster than the
Python simulator. In the whole-calculator engine an idle calculator runs about 1,000× faster
than real time (it skips the sleeps), and a busy one about 2–3× faster than the model's own
clock. The app will slow it down to real time (or offer "turbo"). There is room to make the
interpreter faster later (instruction decode cache) if needed; the Mac's M-series chip is
also faster per core than this box.

## Where the core differs from a real chip (on purpose: it copies the Python model)

These are the Python model's simplifications, copied so that results match exactly. None of
them changes what the calculator computes:

* **Timer interrupt**: called like a normal function every 10 ms of simulated time, between
  time slices, instead of a real interrupt entry. Time is counted from instructions
  (4 MHz → 30,000 per 10 ms; 12 and 48 MHz scaled the same way).
* **Instruction counting**: like Unicorn, an instruction skipped inside an IT block is not
  counted, and a time slice never ends inside an IT block. (Found and copied during the
  work; with it, every 10 ms slice ends on the same instruction as in the Python model.)
* **Sleep**: a slice ends at WFI; it counts as a sleep if the halfword before PC in flash is a
  WFI instruction (exactly the Python test).
* **Peripherals**: unknown registers store the last value written at that exact address; a
  register polled more than 40 times in a row flips its bits so "wait until ready" loops end.
  GPIO (key matrix), ADC (battery), NVIC enable bits and the LCD memory are modelled.
* **Program runner**: skips the clock-switch routine and detects "waiting for key release"
  after 20 million instructions without Nut progress, like the Python runner.
* Not modelled (same as the Python model): USB, writing flash through the flash controller,
  real interrupt priorities, the FPU (the firmware uses none).

## What was ported from our own Python code (marked `PORT:` in the source)

Logic and tables re-written in C from the author's own Python simulator (voyager-sim): the
key layout and aliases, the 7-segment table and decoder, the Nut register map
(`nut_ram`), the key-matrix pin map (`_pvr`), the polling-loop flip (`_ready_loop`), the
peripheral and tick logic (`_mmio_r`, `_mmio_w`, `_on_tick`, `wait`), the runner's key and
hold logic (`press`, `_call`), the 12-bit battery scale and the painted-frame text
(`cells`). The firmware addresses and fingerprints are facts about our firmware layout. The
ARM interpreter (`cpu.c`) and SHA-256 are new, written from the ARM and FIPS specifications.
No Unicorn or other third-party code is included.

## Licence

MIT (`../LICENSE`), Copyright (c) 2026 José González Divassón; every source file carries
`SPDX-License-Identifier: MIT`. The parts ported from the author's own Python simulator are
relicensed by him under MIT.

## Not done yet / next

* HP-15C CE (the whole-calculator engine is ready for it in principle; needs its key layout,
  display decoder and fingerprints).
* Low-battery annunciator, and a map from LCD segments to the picture (for drawing the
  display from the raw segments instead of the Nut registers, which also gives decimal
  points and commas).
* Real-time pacing and a background thread belong to the app layer (next milestone).
