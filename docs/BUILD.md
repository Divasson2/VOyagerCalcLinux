# Building VoyagerCalc for Linux

## Requirements
* CMake ≥ 3.21, Ninja (or Make), a C11 and C++17 compiler (GCC or Clang)
* Qt 6.4 or newer: Core, Gui, Widgets, Test (Debian/Ubuntu: `qt6-base-dev`; Fedora: `qt6-qtbase-devel`)
* Runtime: a desktop with X11 or Wayland; USB access needs a udev rule (see INSTALL.md)

## Build and run
    cmake -S . -B build -G Ninja
    cmake --build build
    build/voyagercalc                 # first start: File ▸ Choose Firmware… (your own .bin)

With a Qt that is not the distribution's: `-DCMAKE_PREFIX_PATH=/path/to/Qt/6.x/gcc_64`.

Headless screenshot (key codes are 10 × row + column, see `core/include/voyager_core.h`):

    QT_QPA_PLATFORM=offscreen build/voyagercalc --firmware F.bin --press 26,27,28,37,16,17,25 --screenshot out.png

## Tests
    cd build && QT_QPA_PLATFORM=offscreen ctest --output-on-failure
    # also boot a real firmware in the emulator tests:
    VOYAGERCALC_FIRMWARE_DIR=/folder/with/your/firmware QT_QPA_PLATFORM=offscreen ctest

Core only (C): `make -C core` builds the library, the `vcore` command line tool and the C tests.

## Generated files
`src/FaceGeometry.h` comes from `assets/geometry.json`: `python3 tools/gen_face_geometry.py`.
`src/ModelData.h` is generated from the model tables of the author's macOS app and is committed as is.
`core/src/optables.h` comes from `core/tools/gen_optables.py`.

## Release tarball
Build with `-DVC_PORTABLE_RPATH=ON`, then run `packaging/build-tarball.py` (see its header for the options). The script
refuses to put any `.bin` file into the package.
