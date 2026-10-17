# Installing VoyagerCalc on Linux

For a 64-bit Debian 13 / Ubuntu 24.04 (or newer) desktop, x86_64 or arm64. (Or build from source: BUILD.md.)

1. **Download** the release tarball for your computer (`VoyagerCalc-<version>-linux-x86_64.tar.gz` or `…-arm64.tar.gz`)
   from the GitHub releases page.
2. **Unpack** it (right-click ▸ Extract, or `tar xzf VoyagerCalc-*.tar.gz`).
3. In the new folder run `./install.sh`. It installs for your user only (`~/.local`) and adds VoyagerCalc to the applications menu.
4. **USB password step.** The installer allows USB access to the calculator (a udev rule in
   `/etc/udev/rules.d/99-voyagercalc.rules`) and needs administrator rights once: a password window opens, or it asks in the
   terminal (your password if you may use sudo, otherwise the root password). If neither works it prints one line to
   paste in a terminal. The program is installed in every case.
5. **Start VoyagerCalc** from the applications menu. The first time, choose your own firmware file; it is remembered.

## Using the real calculator
Connect it by USB, turn it off, hold **f** and **ENTER**, press **ON**, then use *Calculator ▸ USB* in the app. If the app
says it has no access, choose *Calculator ▸ USB ▸ Fix USB Permissions*.

## Memory files
Saved as `.bin12c`, `.bin15c` or `.bin16c` by default (any ending can be typed). Opening checks the file itself (2048 bytes);
the older `.12c`, `.15c`, `.16c` and `.12cmem` endings still open.

## If reading from the calculator does not work
Run `voyagercalc --usb-selftest` in a terminal and attach the output to an issue. It lists the USB devices, says whether
the calculator is there, whether you may use it and whether another program holds it, and tries the read in several ways;
a step that never returns is shown as STUCK. *Help ▸ Show USB Log* shows every USB step (also in
`~/.local/share/VoyagerCalc/usb.log`). In a virtual machine, connect the calculator's USB device to the Linux guest, not
the host.

## Remove
`~/.local/opt/VoyagerCalc/uninstall.sh`. Your firmware, saved state and notes stay in `~/.local/share/VoyagerCalc`.
Installer problems: run `./install.sh --verbose` and attach `~/.local/share/voyagercalc/install.log`.
