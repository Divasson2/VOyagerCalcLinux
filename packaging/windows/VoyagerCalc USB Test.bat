@echo off
rem VoyagerCalc USB test: shows the USB devices Windows sees and tries to read the calculator.
rem Turn the calculator off, hold f and ENTER, press ON (the display stays blank), then double-click this file.
rem The result is shown here and saved on your Desktop as VoyagerCalc-USB-Test.txt - attach that file to a bug report if reading does not work.
cd /d "%~dp0"
set OUT=%USERPROFILE%\Desktop\VoyagerCalc-USB-Test.txt
echo Running the USB test, this takes up to a minute...
"%~dp0VoyagerCalc-cli.exe" --usb-selftest > "%OUT%" 2>&1
type "%OUT%"
echo.
echo (Saved as %OUT%)
pause
