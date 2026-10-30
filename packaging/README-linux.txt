VoyagerCalc for Linux
=====================

A calculator app for the HP 12C, 15C and 16C Collector's Edition (CE) calculators. It runs the calculator
firmware you supply on your computer, shows the calculator on screen, and can move the calculator's memory
between the app and your real calculator over USB.

The user manual is "VoyagerCalc User Manual.pdf" in this folder.

Install (about one minute)
--------------------------
1. Unpack this .tar.gz (right-click > Extract, or:  tar xzf VoyagerCalc-*.tar.gz).
2. Open the new folder in a terminal and run:   ./install.sh
3. The installer allows USB access to the calculator (administrator rights, once): a password window opens, or it asks in the
   terminal (your password, or the administrator/root password). If neither works it prints one line to run. The app is installed anyway.
4. Start "VoyagerCalc" from your applications menu.
5. The first time, choose your firmware file (the .bin file for your calculator). The app remembers it.

The firmware is NOT included. You need the firmware file for your own calculator.

Everything is installed in your own home folder (~/.local), nothing else on the computer is changed except the
small USB access rule in /etc/udev/rules.d/99-voyagercalc.rules.

Memory files
------------
Saved with the ending .bin12c / .bin15c / .bin16c by default; you can type any ending. Opening checks the file, not its ending.

If reading does not work
-----------------------
Run  voyagercalc --usb-selftest  in a terminal and send the output. In the program: Help > Show USB Log.

Remove
------
Run  ~/.local/opt/VoyagerCalc/uninstall.sh   Your firmware, saved state and notes are kept.

Using the real calculator
-------------------------
Connect it with the USB cable, turn it off, hold f and ENTER, and press ON. Then use the Calculator > USB menu.
If the app says it has no access to the calculator, choose Calculator > USB > Fix USB Permissions.

Needs a 64-bit Debian 13 / Ubuntu 24.04 (or newer) desktop. No other software has to be installed.

VoyagerCalc is free software (MIT license). See LICENSE.txt and THIRD-PARTY.txt.
