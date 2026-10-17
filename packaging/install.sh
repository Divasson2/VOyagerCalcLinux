#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 José González Divassón
#
# VoyagerCalc installer. Installs for the current user only (nothing is written outside your home folder),
# adds VoyagerCalc to the applications menu, and asks for your password ONCE to allow USB access to the calculator.
set -e

VERBOSE=""
for a in "$@"; do
    case "$a" in
        --verbose|-v) VERBOSE="--verbose" ;;
        -h|--help) echo "Usage: ./install.sh [--verbose]"; exit 0 ;;
    esac
done
HERE=$(cd "$(dirname "$0")" && pwd)
SRC="$HERE/VoyagerCalc"
OPT="$HOME/.local/opt/VoyagerCalc"
BIN="$HOME/.local/bin"
DATA="${XDG_DATA_HOME:-$HOME/.local/share}"
APPS="$DATA/applications"
ICONS="$DATA/icons/hicolor/256x256/apps"

LOG="${XDG_DATA_HOME:-$HOME/.local/share}/voyagercalc/install.log"
mkdir -p "$(dirname "$LOG")" 2>/dev/null || true
say() { printf '%s\n' "$*"; printf '%s %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$*" >> "$LOG" 2>/dev/null || true; }
dbg() { [ -n "$VERBOSE" ] && printf '[debug] %s\n' "$*"; printf '%s [debug] %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$*" >> "$LOG" 2>/dev/null || true; return 0; }
fail() { say ""; say "VoyagerCalc was NOT installed."; say "$*"; exit 1; }

if [ ! -x "$SRC/bin/voyagercalc" ]; then
    fail "Please unpack the whole .tar.gz first, then run install.sh from inside the unpacked folder."
fi
if [ "$(id -u)" = "0" ]; then
    fail "Please run this as your normal user (without sudo). It asks for the password itself when needed."
fi

WANT=$(cat "$SRC/share/ARCH")
HAVE=$(uname -m)
case "$HAVE" in aarch64|arm64) HAVE=arm64 ;; x86_64|amd64) HAVE=x86_64 ;; esac
if [ "$WANT" != "$HAVE" ]; then
    fail "This package is for $WANT computers, and this computer is $HAVE. Please download the $HAVE package."
fi

# The program must be able to start here (right CPU, new enough system).
dbg "checking that the program starts on this system"
if ! "$SRC/bin/voyagercalc" --version >/dev/null 2>/tmp/voyagercalc-check.$$; then
    say ""
    say "VoyagerCalc cannot start on this system:"
    sed 's/^/    /' /tmp/voyagercalc-check.$$ | head -5
    rm -f /tmp/voyagercalc-check.$$
    fail "It needs Debian 13 or newer, or Ubuntu 24.04 or newer (64-bit). Please tell the author which system you have."
fi
rm -f /tmp/voyagercalc-check.$$

say "Installing VoyagerCalc $("$SRC/bin/voyagercalc" --version | sed 's/^VoyagerCalc //') for $USER ..."

# 1. the program (replaces an older installation; your firmware and files are kept)
mkdir -p "$HOME/.local/opt" "$BIN" "$APPS" "$ICONS"
NEW="$OPT.new.$$"
rm -rf "$NEW"
cp -a "$SRC" "$NEW"
cp "$HERE/uninstall.sh" "$NEW/uninstall.sh"
chmod +x "$NEW/uninstall.sh"
rm -rf "$OPT"
mv "$NEW" "$OPT"

# 2. menu entry, icon, command
cp "$OPT/share/voyagercalc.png" "$ICONS/voyagercalc.png"
cat > "$APPS/voyagercalc.desktop" <<DESK
[Desktop Entry]
Type=Application
Name=VoyagerCalc
GenericName=Calculator
Comment=HP 12C, 15C and 16C calculator
Exec="$OPT/bin/voyagercalc"
Icon=$ICONS/voyagercalc.png
Terminal=false
Categories=Utility;Calculator;Science;Education;
StartupWMClass=VoyagerCalc
DESK
chmod 644 "$APPS/voyagercalc.desktop"
ln -sf "$OPT/bin/voyagercalc" "$BIN/voyagercalc"
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$APPS" >/dev/null 2>&1 || true
command -v gtk-update-icon-cache >/dev/null 2>&1 && gtk-update-icon-cache -q -t "$DATA/icons/hicolor" >/dev/null 2>&1 || true

# 3. USB access for the calculator. This step can never stop the installation: whatever happens, the program stays installed.
chmod +x "$OPT/share/usb-access.sh" 2>/dev/null || true
USB=ok
say ""
say "Setting up USB access for the calculator..."
dbg "running $OPT/share/usb-access.sh $VERBOSE"
set +e
sh "$OPT/share/usb-access.sh" $VERBOSE
RC=$?
set -e
dbg "usb-access.sh exit code $RC"
if [ "$RC" != "0" ]; then USB=later; fi

say ""
say "VoyagerCalc is installed."
say "  Start it from the applications menu (search for \"VoyagerCalc\"), or type: $OPT/bin/voyagercalc"
say "  The first time, it asks for your firmware file."
if [ "$USB" = "later" ]; then
    say ""
    say "USB access is NOT set up yet (VoyagerCalc itself is installed and works). To set it up later, either"
    say "  - open VoyagerCalc and choose  Calculator > USB > Fix USB Permissions , or"
    say "  - run:  sh $OPT/share/usb-access.sh"
    say "Details of this installation are in: $LOG"
fi
case ":$PATH:" in *":$BIN:"*) ;; *) : ;; esac
say ""
say "To remove VoyagerCalc later, run: $OPT/uninstall.sh"
