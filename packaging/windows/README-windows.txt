VoyagerCalc for Windows (test build)
====================================
A Windows front end for the HP-12C, HP-15C and HP-16C emulator, with USB to your real calculator.
Not affiliated with Hewlett-Packard. MIT licence (see LICENSE.txt).
User manual: "VoyagerCalc User Manual.pdf" (also in the Start menu when installed).

Start: double-click VoyagerCalc.exe. The first time, choose your firmware file (a .bin you already have; it is not included). It is remembered.

Windows may show "Windows protected your PC" (SmartScreen) because test builds are not signed yet: click "More info", then "Run anyway".

Real calculator over USB: connect it, turn it OFF, hold f and ENTER, press ON (the display stays blank), then use
Calculator > Real Calculator (USB) > Read from Calculator. No driver and no setup are needed on Windows.
In a virtual machine (Parallels): connect the calculator to Windows, not to the Mac (Devices > USB & Bluetooth).

If reading does not work: double-click "VoyagerCalc USB Test.bat" (calculator in USB mode) and attach the file VoyagerCalc-USB-Test.txt (from your Desktop) to a bug report on the project page.
In the program: Help > Show USB Log.

Your firmware, saved state and notes are in  %LOCALAPPDATA%\VoyagerCalc  (removing the program does not delete them).
