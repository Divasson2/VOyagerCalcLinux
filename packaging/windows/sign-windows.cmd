@echo off
rem Runs sign-windows.ps1 (Azure Artifact Signing) without changing the PowerShell execution policy for the machine.
rem Extra arguments are passed on, for example:  sign-windows.cmd -SkipInstaller
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0sign-windows.ps1" %*
echo.
pause
