# Building VoyagerCalc for Windows

The Windows version is the same code base as the Linux one (Qt 6 Widgets + the C emulator core). Only the USB layer
differs: `src/usb/WinHidBackend.cpp` talks to the calculator through Windows' own HID driver (SetupAPI, `HidD_*`,
`ReadFile` / `WriteFile`); no driver or libusb is needed. Everything above it (`PortTransport`, the calculator link) is shared
with Linux and is tested on Linux with a fake HID device (`tests/tst_portusb.cpp`).

You can build natively on Windows, or cross-compile from Linux (what the author does; it needs no Windows machine).

## Native build on Windows
* Qt 6.4+ (MSVC or MinGW kit), CMake ≥ 3.21, Ninja or Visual Studio.

      cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/mingw_64
      cmake --build build

  This produces `voyagercalc.exe` (GUI) and `voyagercalc-cli.exe` (the same program as a console program: `--version`,
  `--usb-selftest`). Run `windeployqt` next to them to collect the Qt DLLs.
* The tests (`tst_*.exe`) are console programs; run them with `ctest` or directly.

## Cross-compile from Linux (llvm-mingw + Qt for Windows)
Needs: [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) (clang, UCRT, libc++), CMake, Ninja, Python 3, and
* a **Linux** Qt 6 of the same version as the Windows Qt (its `moc` / `rcc` run on the build machine): `QT_HOST_PATH`,
* **Qt 6 for Windows built with llvm-mingw**: `QT_WIN`.
  * x64: `aqt install-qt windows desktop 6.8.3 win64_llvm_mingw` ([aqtinstall](https://github.com/miurahr/aqtinstall)).
  * arm64: there is no ready-made llvm-mingw Qt (the official Windows-on-ARM Qt is MSVC-only and its ABI does not match),
    so build `qtbase` yourself with the arm64 toolchain file:

        cmake -S qtbase-everywhere-src-6.8.3 -B qtbase-arm64 -G Ninja -DCMAKE_BUILD_TYPE=Release \
          -DCMAKE_TOOLCHAIN_FILE=packaging/windows/cmake/toolchain-win-arm64.cmake -DQT_HOST_PATH=$QT_HOST_PATH \
          -DCMAKE_INSTALL_PREFIX=$HOME/qt-win-arm64 -DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF -DINPUT_opengl=no \
          -DFEATURE_vulkan=OFF -DFEATURE_sql=OFF -DFEATURE_network=OFF -DFEATURE_dbus=OFF -DFEATURE_printsupport=OFF -DFEATURE_testlib=ON
        cmake --build qtbase-arm64 && cmake --install qtbase-arm64

    (the toolchain file needs `QT_WIN` only to find Qt; set it to the install prefix, or to any existing folder while building Qt itself).

Then, from the repository root:

    export LLVM_MINGW=/path/to/llvm-mingw QT_WIN=/path/to/Qt/6.8.3/llvm-mingw_64 QT_HOST_PATH=/path/to/Qt/6.8.3/gcc_64
    packaging/windows/build-windows.sh x64 [tag]       # or: arm64 (with QT_WIN pointing at the arm64 Qt)

The script builds, then runs `packaging/windows/build-package.py`, which produces in `dist-windows/`
* `VoyagerCalc-<version>-windows-<arch>[-<tag>].zip`: a portable folder (no installation),
* `…-setup.exe`: a per-user NSIS installer (no administrator rights), when `MAKENSIS` is set (and `NSISDIR` for a relocated NSIS).

The DLLs to bundle are found by reading the import tables of the program, the Qt plugins (`platforms/qwindows.dll`,
`styles/qmodernwindowsstyle.dll`) are added, and the script **refuses to package** if a `.bin` or firmware-sized file ends up in
the folder. No firmware is ever included.

## Running the tests
* Windows x64 binaries run under [wine](https://www.winehq.org/) (64-bit / wow64 build): `wine tst_portusb.exe`. With no real HID
  device the self-test reports "No calculator found". Tests that call the poppler tools (`pdfinfo`, `pdffonts`) skip themselves.
  Console output of Windows programs is not shown by wine unless redirected: use `-o result.txt,txt`.
* arm64 binaries can only be run on Windows on ARM (for example Windows 11 in Parallels on an Apple Silicon Mac).
* On Linux, the whole suite (including the Windows transport logic with a fake device) runs with `ctest`.

## Signing (optional)
Test builds are unsigned, so Windows SmartScreen says "Windows protected your PC" (More info ▸ Run anyway). For releases, sign
`voyagercalc.exe`, `voyagercalc-cli.exe`, the DLLs and the installer with `signtool` (for example Azure Trusted Signing:
`signtool sign /fd SHA256 /tr http://timestamp.acs.microsoft.com /td SHA256 /dlib Azure.CodeSigning.Dlib.dll /dmdf metadata.json file.exe`).
