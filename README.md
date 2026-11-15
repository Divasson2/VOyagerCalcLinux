# VoyagerCalc for Linux and Windows

A Qt 6 calculator app for Linux and Windows (x64 and arm64) that runs **your own** HP 12C / 15C / 16C Collector's Edition firmware on a native
emulator of the calculator's ARM chip, shows the calculator on screen, and can read and write the calculator's memory
over USB (`hidraw` on Linux, the standard Windows HID driver on Windows).

* **No firmware is included** in this repository or in any build. You supply the firmware file of your own calculator
  (File ▸ Choose Firmware…). It is remembered in `~/.local/share/VoyagerCalc` (Linux) or `%LOCALAPPDATA%\VoyagerCalc` (Windows).
* Emulator core: plain C11 in [`core/`](core/README.md) (no third-party code).
* Front end: C++17 + Qt 6 Widgets in [`src/`](src/).
* Also: registers / program inspector, program documentation export (PDF and text), calculator memory files, USB link
  with backup and verify.
* Licence: [MIT](LICENSE). Not affiliated with, endorsed by or sponsored by HP Inc.; "HP" and the calculator model names
  belong to their owners.

> Status: version 0.7.8, a test release. New in 0.7.8: program editing on the HP-15C and HP-16C (copy, cut, delete and paste program steps, paste program text and insert program files; see below). HP-16C memory files use one of two layouts, detected automatically: stock (29 slots) or the 206-slot extended layout of EAEA, C9C9 and ECEC (R0–R720). At the firmware's 48 MHz speed setting a running program is no longer held to the 48 MHz instruction budget. Reading and writing the calculator's memory over USB works with a real calculator on both
> platforms in the author's tests: Linux, and Windows (tested on Windows 11 ARM with a real HP-15C). The Windows x64 build has been tested
> under wine only, with a fake HID device. Reports from other systems and calculators are welcome.

## Where to get calculator firmware

This repository and its builds contain **no HP firmware**; you supply the firmware file of your own calculator. The Calculator Store
(a retailer of HP calculators, run by the author) keeps pages with firmware downloads and links:

* Official HP firmware: <https://www.thecalculatorstore.com/c/resources-and-links/official-hp-calculator-firmware>
* Test and extended firmware (HP-15C CE C8C8, HP-16C CE extended (EAEA, C9C9, ECEC), 10bII+ EDED, HP-12C CE multi E6E6): <https://www.thecalculatorstore.com/c/resources-and-links/hp-calculator-test-firmware>
* Simulators and emulators: <https://www.thecalculatorstore.com/c/resources-and-links/simulators-and-emulators>

Using firmware is at your own responsibility; the project is not affiliated with HP Inc.

## Build

Needs CMake ≥ 3.21, a C11 and a C++17 compiler, and Qt 6.4+ (Widgets, Test).

    cmake -S . -B build -G Ninja                       # add -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x/gcc_64 for a Qt outside the distro
    cmake --build build
    build/voyagercalc

Debian / Ubuntu: `sudo apt install build-essential cmake ninja-build qt6-base-dev`. More in [docs/BUILD.md](docs/BUILD.md).

## Install and use

* Linux: [docs/INSTALL.md](docs/INSTALL.md): installing from a release tarball, USB access to the calculator, memory files and
  troubleshooting (`voyagercalc --usb-selftest`, Help ▸ Show USB Log).
* Windows: [docs/INSTALL-WINDOWS.md](docs/INSTALL-WINDOWS.md): per-user installer or zip, USB, troubleshooting ("VoyagerCalc USB Test").
  Build instructions (native or cross-compiled from Linux, x64 and arm64): [docs/BUILD-WINDOWS.md](docs/BUILD-WINDOWS.md);
  signing the release builds: [docs/SIGNING-WINDOWS.md](docs/SIGNING-WINDOWS.md).

## Editing programs: copy, cut, paste (0.7.8, HP-15C and HP-16C)

In View ▸ Registers & Program, select program steps (Shift-click for a range). The Edit menu or right-click menu then has
**Copy**, **Cut**, **Delete** and **Paste After Step**. Copy puts a text listing on the clipboard, one step per line with
keycodes, key names and `; comment`. **Paste Program Text…** (Ctrl+Shift+V) and **Insert Program File(s)…** read:

* our copied steps
* *Export Listing as Text*
* HP-15C Simulator `.15c` files
* listings with step numbers and keycodes (`001-42,21,11`, `12 { 43 22 A }`)
* listings with key names only (`LBL A`, `STO 1`, `x<>y`, `SL`)

Every line is checked before anything is keyed in. A line in error is named with its line number. A key name that fits more
than one step offers a list of choices. Several files can be added one after another to build a library:

* a clashing digit label (`LBL 1`) is renumbered automatically, along with its `GTO` / `GSB`
* a clashing letter label is renumbered only when you tick the box
* the memory check refuses a program that does not fit
* on the HP-16C, the memory check warns about storage registers that a longer program would take

The new program is keyed in through the calculator's own firmware (program mode, CLEAR PRGM, every step). The calculator
therefore encodes the steps and takes the memory they need. If anything goes wrong, the memory from before is put back. Edit
▸ Undo / Redo Program Edit (Ctrl+Z / Ctrl+Shift+Z) keeps 20 levels. Nothing is written to a real calculator: *Write to
Calculator* stays a separate, confirmed step. The memory file format is unchanged.

## Converting old HP-16C memory files

16C memory files saved on the retired 176-slot extended builds (B4B4–B7B7, E3E3, E8E8, E9E9) no longer open in 0.7.7. `tools/convert_16c_memory.py` (Python 3.8+, standard library only; also in the `tools` folder of the Linux and Windows packages) converts them to the 206-slot layout of EAEA, C9C9 and ECEC, and converts a stock file with `--from stock`:

```
python3 tools/convert_16c_memory.py OLD.16c            # writes OLD-206.16c, the original is kept
python3 tools/convert_16c_memory.py --check *.16c      # report the detected layout only
```

See `tools/README.md` and `--help`.

## Tests

    cd build && QT_QPA_PLATFORM=offscreen ctest --output-on-failure

The tests that need a real firmware file are skipped unless you point `VOYAGERCALC_FIRMWARE_DIR` at a folder with your own
firmware files. `tests/fixtures/` holds small 2 KB calculator memory images used by the tests.

## Layout

| Path | Content |
|---|---|
| `src/` | the application (`usb/` = calculator link, hidraw transport (Linux), Windows HID backend, self test) |
| `core/` | the emulator core (C11), its tests and table generator |
| `assets/` | calculator face pictures, fonts (with notice), icon, measured key geometry |
| `resources/` | Qt resource file |
| `packaging/` | Linux install / uninstall scripts and the release-tarball builder; `packaging/windows/` = installer script, zip / installer builder, cross-compile toolchain files |
| `tools/` | `gen_face_geometry.py` (regenerates `src/FaceGeometry.h`), `run-tests.sh` |
| `tests/` | QtTest suites and fixtures |

## Credits

Key-cap font by Eric Rechlin, seven-segment display font by Torsten Manz (see `assets/fonts/README.md`).
HP-15C and HP-16C calculator photographs (front and back) by Warren Young.
Qt is used under the LGPL v3.

Copyright (c) 2026 Divasson2. MIT licence.
