# Installing VoyagerCalc on Windows (test build)

For Windows 10 / 11, 64-bit. Use the **arm64** build on a Windows-on-ARM computer (for example Windows 11 in Parallels on an Apple Silicon Mac); the **x64** build runs on Intel/AMD computers, and also on Windows-on-ARM (slower, through emulation).

**Easiest: the installer** — `VoyagerCalc-0.7.2-windows-<arch>-<tag>-setup.exe`. Double-click it. It needs no administrator rights, installs for your user only, and adds a Start menu entry.
**Without installing: the zip** — right-click `VoyagerCalc-0.7.2-windows-<arch>-<tag>.zip`, *Extract All…*, open the folder and double-click `VoyagerCalc.exe`.

Test builds are not signed yet, so Windows SmartScreen says "Windows protected your PC": click **More info**, then **Run anyway**.

First start: choose your firmware file. It is remembered.

Real calculator: connect it by USB, turn it off, hold **f** and **ENTER**, press **ON**; then *Calculator → Real Calculator (USB)*. Nothing to install: Windows' own HID driver is used.
In Parallels connect the calculator to Windows (Devices → USB & Bluetooth), not to the Mac.

**If it does not read:** double-click **VoyagerCalc USB Test.bat** (with the calculator in USB mode), wait for it to finish, and attach `VoyagerCalc-USB-Test.txt` (from your Desktop) to a GitHub issue. Also: *Help → Show USB Log*.

Memory files are saved as `.bin12c`, `.bin15c` or `.bin16c` by model (only a suggestion; any ending opens).
To remove: Settings → Apps → VoyagerCalc → Uninstall (or delete the unzipped folder). Your firmware, state and notes stay in `%LOCALAPPDATA%\VoyagerCalc`.
