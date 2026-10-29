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

> Status: version 0.7.3, a test release. Reading and writing the calculator's memory over USB works with a real calculator on both
> platforms in the author's tests: Linux, and Windows (tested on Windows 11 ARM with a real HP-15C). The Windows x64 build has been tested
> under wine only, with a fake HID device. Reports from other systems and calculators are welcome.

## Where to get calculator firmware

This repository and its builds contain **no HP firmware**; you supply the firmware file of your own calculator. The Calculator Store
(a retailer of HP calculators, run by the author) keeps pages with firmware downloads and links:

* Official HP firmware: <https://www.thecalculatorstore.com/c/resources-and-links/official-hp-calculator-firmware>
* Test and extended firmware (HP-15C CE C8C8, HP-16C CE B7B7, 10bII+ EDED, HP-12C CE multi E6E6): <https://www.thecalculatorstore.com/c/resources-and-links/hp-calculator-test-firmware>
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
HP-15C and HP-16C calculator photographs by Warren Young.
Qt is used under the LGPL v3.

Copyright (c) 2026 Divasson2. MIT licence.
