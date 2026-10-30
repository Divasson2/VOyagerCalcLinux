; SPDX-License-Identifier: MIT
; Copyright (c) 2026 José González Divassón
; VoyagerCalc per-user installer (NSIS): no administrator rights, installs into %LOCALAPPDATA%\Programs\VoyagerCalc, Start menu entry, uninstaller.
; Built by build-package.py with -DVERSION -DSRCDIR -DOUTFILE -DARCH.
Unicode true
!include "MUI2.nsh"
Name "VoyagerCalc ${VERSION}"
OutFile "${OUTFILE}"
RequestExecutionLevel user
InstallDir "$LOCALAPPDATA\Programs\VoyagerCalc"
InstallDirRegKey HKCU "Software\VoyagerCalc" "InstallDir"
SetCompressor /SOLID lzma
!define MUI_ICON "${ICON}"
!define MUI_FINISHPAGE_RUN "$INSTDIR\VoyagerCalc.exe"
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Section "VoyagerCalc"
  SetOutPath "$INSTDIR"
  File /r "${SRCDIR}\*.*"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  CreateDirectory "$SMPROGRAMS\VoyagerCalc"
  CreateShortcut "$SMPROGRAMS\VoyagerCalc\VoyagerCalc.lnk" "$INSTDIR\VoyagerCalc.exe"
  CreateShortcut "$SMPROGRAMS\VoyagerCalc\VoyagerCalc USB Test.lnk" "$INSTDIR\VoyagerCalc USB Test.bat"
  CreateShortcut "$SMPROGRAMS\VoyagerCalc\VoyagerCalc User Manual.lnk" "$INSTDIR\VoyagerCalc User Manual.pdf"
  CreateShortcut "$SMPROGRAMS\VoyagerCalc\Uninstall VoyagerCalc.lnk" "$INSTDIR\Uninstall.exe"
  WriteRegStr HKCU "Software\VoyagerCalc" "InstallDir" "$INSTDIR"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\VoyagerCalc" "DisplayName" "VoyagerCalc ${VERSION}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\VoyagerCalc" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\VoyagerCalc" "Publisher" "José González Divassón"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\VoyagerCalc" "DisplayIcon" "$INSTDIR\VoyagerCalc.exe"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\VoyagerCalc" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\VoyagerCalc" "NoModify" 1
SectionEnd

Section "Uninstall"
  ; the program files only: your firmware, saved state and notes stay in %LOCALAPPDATA%\VoyagerCalc
  RMDir /r "$INSTDIR"
  RMDir /r "$SMPROGRAMS\VoyagerCalc"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\VoyagerCalc"
  DeleteRegKey HKCU "Software\VoyagerCalc"
SectionEnd
