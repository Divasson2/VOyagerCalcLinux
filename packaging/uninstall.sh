#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 José González Divassón
#
# Removes VoyagerCalc for the current user. Your firmware, saved calculator state, notes and exported files are kept.
set -e
OPT="$HOME/.local/opt/VoyagerCalc"
BIN="$HOME/.local/bin"
DATA="${XDG_DATA_HOME:-$HOME/.local/share}"
RULE=/etc/udev/rules.d/99-voyagercalc.rules

say() { printf '%s\n' "$*"; }

say "Removing VoyagerCalc ..."
[ -L "$BIN/voyagercalc" ] && rm -f "$BIN/voyagercalc"
rm -f "$DATA/applications/voyagercalc.desktop" "$DATA/icons/hicolor/256x256/apps/voyagercalc.png"
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$DATA/applications" >/dev/null 2>&1 || true

if [ -f "$RULE" ]; then
    removed=""
    if command -v pkexec >/dev/null 2>&1 && [ -n "$DISPLAY$WAYLAND_DISPLAY" ] && command -v timeout >/dev/null 2>&1; then
        say "A password window may open to remove the USB access rule (you can cancel it; leaving the rule is harmless)."
        timeout 60 pkexec /bin/rm -f "$RULE" >/dev/null 2>&1 && removed=1
    fi
    if [ -z "$removed" ] && [ -f "$RULE" ]; then
        say "The USB access rule was left in place (it is harmless). To remove it too, run:  sudo rm $RULE   (or: su -c 'rm $RULE')"
    else
        say "USB access rule removed."
    fi
fi

# last: this script lives inside the folder it removes
if [ -d "$OPT" ]; then
    case "$0" in "$OPT"/*) cd "$HOME" ;; esac
    rm -rf "$OPT"
fi

say ""
say "VoyagerCalc is removed."
say "Your firmware, saved state and notes are still in: $DATA/VoyagerCalc"
say "(Delete that folder if you want them gone too.)"
