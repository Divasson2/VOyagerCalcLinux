# Installing VoyagerCalc on Windows

For Windows 10 / 11, 64-bit. There are two builds: **arm64** for Windows on ARM (for example Windows 11 in Parallels on an
Apple Silicon Mac) and **x64** for Intel / AMD PCs (it also runs on Windows on ARM, slower, through emulation).

* **Installer** (`VoyagerCalc-<version>-windows-<arch>-setup.exe`): double-click it. No administrator rights are needed; it installs for
  your user only (`%LOCALAPPDATA%\Programs\VoyagerCalc`) and adds a Start menu entry and an uninstall entry.
* **Zip** (`VoyagerCalc-<version>-windows-<arch>.zip`): *Extract All…*, open the folder, double-click `VoyagerCalc.exe`. Nothing is installed.

Unsigned builds make Windows SmartScreen show "Windows protected your PC": click **More info**, then **Run anyway**.

## First start
Choose your **own** firmware file (File ▸ Choose Firmware…). No firmware is included. It is remembered in
`%LOCALAPPDATA%\VoyagerCalc`, together with saved state, notes and the USB log; uninstalling does not delete that folder.

## Using the real calculator over USB
Nothing to install: Windows' standard HID driver is used.
1. Connect the calculator. Turn it **off**, hold **f** and **ENTER**, press **ON**: the display stays blank (USB mode).
2. Calculator ▸ Real Calculator (USB) ▸ Read from Calculator (or Write…).
3. Only one program can use the calculator at a time: close VoyagerInterface, HP software or another copy of VoyagerCalc.
4. In a virtual machine (Parallels, VMware, VirtualBox) connect the calculator to **Windows**, not to the host (for Parallels:
   Devices ▸ USB & Bluetooth).

### If it does not read
* Double-click **VoyagerCalc USB Test.bat** in the VoyagerCalc folder with the calculator in USB mode. It lists the HID devices
  Windows knows, finds VID 03F0 / PID 1341, tries to read, and saves the result as `VoyagerCalc-USB-Test.txt` on your Desktop
  (the same as `voyagercalc-cli.exe --usb-selftest`).
* Help ▸ Show USB Log shows every USB step of the program.
* Please attach both files to a bug report.

## Memory files and export
Memory files are suggested as `.bin12c`, `.bin15c` or `.bin16c` by model; any ending can be typed and opened. Program documentation
exports as PDF (A4 or Letter, fonts embedded) or text.
