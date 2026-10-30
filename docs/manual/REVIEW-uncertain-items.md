# VoyagerCalc 0.7.3 manual – items to review

Statements in the manual that the sources did not fully support, or where the sources disagree:

1. **Windows SmartScreen / signing – the sources disagree.** The 0.7.x release notes say the builds are signed with Azure Artifact Signing (so there is no "unknown publisher" warning). INSTALL-WINDOWS.md (attached to v0.7.3) and README-windows.txt still say "Test builds are not signed yet … More info → Run anyway". The manual says the builds are signed and keeps "More info → Run anyway" as a fallback.
2. **INSTALL-WINDOWS.md file names** use `VoyagerCalc-0.7.3-windows-<arch>-<tag>-setup.exe`. The real asset names have no `<tag>` part (`…-windows-x64-setup.exe`). The manual uses the real asset names.
3. **The Linux INSTALL.md menu name** says *Calculator ▸ USB*. The code's menu is *Calculator ▸ Real Calculator (USB)*. The manual uses the name from the code.
4. **"Up to 999 steps"** is the limit of the `.15c` program-file format (Torsten Manz's simulator), taken from Manz15C.h and Manz15CProgram.swift. It is not the capacity of the calculator. The capacity depends on the firmware and memory mode. The source comment says "15.0: 448 one-byte steps at most, 15.2 double memory more". The manual says this. Confirm the wording and the 448 figure. No source gives a figure for the 16C.
5. **macOS installation steps** (open the DMG and drag to Applications) are not documented in the repo. They are the standard practice and are assumed.
6. **macOS Gatekeeper troubleshooting** (System Settings ▸ Privacy & Security ▸ Open Anyway, or Control-click ▸ Open) is general macOS advice. The sources only say the app is "Developer ID signed; notarized and stapled".
7. **macOS uninstall and data location.** "Move to Trash" and "data in the sandbox container under ~/Library/Containers/" are inferred from the App Sandbox entitlement, bundle ID com.josegonzalezdivasson.VoyagerCalc and Application Support use in the code. Nothing documents them.
8. **macOS minimum version (macOS 14)** comes from the Xcode deployment target (`project.yml`: macOS "14.0"). No user-facing document states it.
9. **Checksum commands** (`sha256sum -c --ignore-missing`, `shasum -a 256`, `Get-FileHash`) are standard OS tools. The repo only says "SHA256SUMS lists the checksums".
10. **Quit shortcut.** The code uses QKeySequence::Quit. That is Ctrl+Q on Linux and Cmd+Q on macOS. On Windows Qt has no standard Quit key, which is a Qt platform convention, not something tested.
11. **Scope.** The brief named only the 16C and 15C. The app also supports the HP-12C CE (including HP's original 12C firmware, with limits). The manual mentions this briefly.
12. **"Published by The Calculator Store".** The repo copyright holder is José González Divassón / Divasson2. The README calls The Calculator Store "run by the author". The two shop pages hold almost no text (one sentence on the Linux/Windows page, none on the Mac page), so they add no facts beyond the GitHub sources.
13. **Screenshots** show the macOS app (traffic-light window buttons). The Linux and Windows window frames look different.
14. **Service-menu speed settings ("12 / 48 MHz", item 5) and extended-firmware extras** (stopwatch, menus, extended memory) are described only as general behaviour of the firmware. Exact per-firmware features were not verified.
15. **Windows command-line tool.** `VoyagerCalc-cli.exe --usb-selftest` comes from the USB Test .bat file. `--version` on Windows is assumed to work the same as on Linux.

## Added for 0.7.4
16. **Appendix A** lists checksums only if the release's `SHA256SUMS` is placed next to build.py before building; the draft PDF has no table (0.7.4 is not released yet).
17. **Screenshots**: the two Mac screenshots were updated to the v4 face photos by putting the new faces over the v3 faces at the same place (same key geometry); the displayed digits and the inspector are the original 0.7.3 screenshots.
18. **12C and the Back window** behave differently: Windows/Linux close the window, the Mac shows a text. The manual says so.
