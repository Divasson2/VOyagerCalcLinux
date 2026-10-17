#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 José González Divassón
#
# Lets the logged-in user use the calculator's USB connection (installs /etc/udev/rules.d/99-voyagercalc.rules).
# Used by install.sh and by the app's "Fix USB Permissions". It never stalls silently and always ends with one
# clear result line. Ways to get administrator rights, tried in this order:
#   1. pkexec  - only when a graphical polkit password agent is really running (with a time limit)
#   2. sudo    - password asked in the terminal (only if sudo exists and this user may use it)
#   3. su      - the administrator (root) password in the terminal (the Debian default has no sudo)
#   4. otherwise one copy-paste command is printed.
# Exit codes: 0 installed, 2 cancelled / did not work, 3 nothing could ask for a password (command printed).
# Options: --verbose (show the steps), --gui (called from the app: may open a terminal window for the password).
# Log: ~/.local/share/voyagercalc/install.log
HERE=$(cd "$(dirname "$0")" && pwd)
SELF="$HERE/$(basename "$0")"
RULE_SRC="$HERE/99-voyagercalc.rules"
RULES_DIR="${VC_RULES_DIR:-/etc/udev/rules.d}"
RULE="$RULES_DIR/99-voyagercalc.rules"
LOG="${VC_LOG:-${XDG_DATA_HOME:-$HOME/.local/share}/voyagercalc/install.log}"
PKEXEC_SECONDS="${VC_PKEXEC_TIMEOUT:-90}"
VERBOSE=""; GUI=""; STATUS_FILE=""
for a in "$@"; do
    case "$a" in
        --verbose) VERBOSE=1 ;;
        --gui) GUI=1 ;;
        --in-terminal=*) STATUS_FILE="${a#--in-terminal=}" ;;
    esac
done
mkdir -p "$(dirname "$LOG")" 2>/dev/null || true

log() { printf '%s %s\n' "$(date '+%Y-%m-%d %H:%M:%S' 2>/dev/null)" "$*" >> "$LOG" 2>/dev/null || true; [ -n "$VERBOSE" ] && printf '[debug] %s\n' "$*"; return 0; }
say() { printf '%s\n' "$*"; printf '%s %s\n' "$(date '+%Y-%m-%d %H:%M:%S' 2>/dev/null)" "$*" >> "$LOG" 2>/dev/null || true; }
have() { command -v "$1" >/dev/null 2>&1; }
is_tty() { [ -n "$VC_TEST_TTY" ] || [ -t 0 ]; }
installed_ok() { [ -f "$RULE" ] && [ -f "$RULE_SRC" ] && cmp -s "$RULE" "$RULE_SRC"; }

# the calculator's hidraw node, if it is plugged in now (made accessible at once, no replug needed)
find_device() {
    for d in /sys/class/hidraw/hidraw*; do
        [ -e "$d/device/uevent" ] || continue
        if grep -qi '0003:000003F0:00001341' "$d/device/uevent" 2>/dev/null; then printf '/dev/%s' "$(basename "$d")"; return; fi
    done
}

# What the administrator part does. It travels as TEXT (sh -c), never as a file the user could change meanwhile.
PAYLOAD=$(cat <<'EOS'
set -e
PATH="$PATH:/usr/sbin:/sbin:/usr/bin:/bin"
RULES_DIR="${VC_RULES_DIR:-/etc/udev/rules.d}"
USER_NAME="$1"; DEV="$2"; RULE_B64="$3"
[ -n "$USER_NAME" ] || { [ -n "$PKEXEC_UID" ] && USER_NAME="$(id -nu "$PKEXEC_UID")"; } || true
mkdir -p "$RULES_DIR"
printf '%s' "$RULE_B64" | base64 -d > "$RULES_DIR/99-voyagercalc.rules"
chmod 0644 "$RULES_DIR/99-voyagercalc.rules"
if [ -z "$VC_SKIP_SYSTEM" ]; then
  getent group plugdev >/dev/null || groupadd plugdev || true
  if [ -n "$USER_NAME" ] && ! id -nG "$USER_NAME" | tr ' ' '\n' | grep -qx plugdev; then usermod -aG plugdev "$USER_NAME" || true; fi
  udevadm control --reload-rules || true
  udevadm trigger --subsystem-match=hidraw --action=change || true
  if [ -n "$DEV" ] && [ -e "$DEV" ]; then
    chgrp plugdev "$DEV" 2>/dev/null || true; chmod 0660 "$DEV" || true
    if command -v setfacl >/dev/null 2>&1 && [ -n "$USER_NAME" ]; then setfacl -m "u:$USER_NAME:rw" "$DEV" || true; fi
  fi
fi
echo done
EOS
)

ME="${USER:-$(id -un)}"
DEV="$(find_device)"
RULE_B64="$(base64 < "$RULE_SRC" 2>/dev/null | tr -d '\n')"
PAYLOAD_B64="$(printf '%s' "$PAYLOAD" | base64 | tr -d '\n')"

manual_text() {
    say ""
    say "USB permissions are NOT installed yet. Open a terminal and run this one line (it asks for the password there):"
    say ""
    say "    sh '$SELF'"
    say ""
    say "(What happened is written to $LOG)"
}

# a graphical polkit password agent is running (GNOME Shell has one built in)
agent_running() {
    [ -n "$DISPLAY$WAYLAND_DISPLAY" ] || return 1
    ps -eo comm= 2>/dev/null | grep -Eq '^(gnome-shell|cinnamon|polkit-gnome-au|polkit-kde-auth|polkit-mate-aut|lxpolkit|xfce-polkit|lxqt-policykit|budgie-polkit|ukui-polkit|pantheon-agent-)'
}

with_timeout() { secs="$1"; shift; if have timeout; then timeout "$secs" "$@"; else "$@"; fi; }

try_pkexec() {
    have pkexec || { log "pkexec: not installed"; return 1; }
    agent_running || { log "pkexec: no graphical polkit agent running (DISPLAY='$DISPLAY' WAYLAND_DISPLAY='$WAYLAND_DISPLAY')"; return 1; }
    say "A password window should open now. Please enter your password there (waiting up to ${PKEXEC_SECONDS} seconds)..."
    with_timeout "$PKEXEC_SECONDS" pkexec sh -c "$PAYLOAD" voyagercalc-fix "$ME" "$DEV" "$RULE_B64" >>"$LOG" 2>&1
    rc=$?
    log "pkexec exit code $rc"
    [ $rc -eq 0 ] && installed_ok && return 0
    case $rc in
        124) say "No password window answered in time; trying the terminal instead." ;;
        126|127) say "The password window was cancelled or not allowed; trying the terminal instead." ;;
        *) say "The password window did not work; trying the terminal instead." ;;
    esac
    return 1
}

try_sudo() {
    have sudo || { log "sudo: not installed"; return 1; }
    say "Asking for your password in this terminal (sudo)..."
    if ! sudo -v -p "Password for %p (to allow USB access to the calculator): " 2>>"$LOG"; then
        log "sudo: -v failed (wrong password or this user is not allowed to use sudo)"
        say "sudo is not available for this user; trying the administrator (root) password instead."
        return 1
    fi
    sudo sh -c "$PAYLOAD" voyagercalc-fix "$ME" "$DEV" "$RULE_B64" >>"$LOG" 2>&1
    rc=$?
    log "sudo exit code $rc"
    [ $rc -eq 0 ] && installed_ok && return 0
    return 1
}

try_su() {
    have su || { log "su: not installed"; return 1; }
    say "Enter the ADMINISTRATOR (root) password of this computer (the one chosen when it was installed)."
    su -c "echo '$PAYLOAD_B64' | base64 -d | sh -s -- '$ME' '$DEV' '$RULE_B64'" >>"$LOG" 2>&1
    rc=$?
    log "su exit code $rc"
    [ $rc -eq 0 ] && installed_ok && return 0
    return 1
}

# find a terminal window program so the app can ask for a password even without a polkit agent
terminal_cmd() {
    for t in x-terminal-emulator gnome-terminal kgx konsole xfce4-terminal mate-terminal lxterminal xterm kitty alacritty; do
        have "$t" || continue
        case "$t" in
            gnome-terminal|kgx) echo "$t --wait --" ;;
            xfce4-terminal|mate-terminal) echo "$t -x" ;;
            konsole) echo "konsole --nofork -e" ;;
            *) echo "$t -e" ;;
        esac
        return
    done
}

run_chain_in_terminal() {
    if [ -n "$STATUS_FILE" ]; then :; fi
    try_sudo && return 0
    try_su && return 0
    return 1
}

finish() {
    if [ "$1" = 0 ]; then say "USB permissions installed. (If the calculator is already plugged in, unplug it and plug it in again.)"; else
        say "USB permissions were NOT installed."; fi
    exit "$1"
}

log "start: user=$ME device='$DEV' gui='$GUI' tty=$(is_tty && echo yes || echo no) DISPLAY='$DISPLAY' WAYLAND='$WAYLAND_DISPLAY' PATH=$PATH"

if installed_ok; then say "USB permissions installed (they were already set up)."; exit 0; fi
if [ ! -f "$RULE_SRC" ]; then say "The package is incomplete (99-voyagercalc.rules is missing)."; exit 2; fi

# running inside the helper terminal window opened below
if [ -n "$STATUS_FILE" ]; then
    run_chain_in_terminal; rc=$?
    echo $rc > "$STATUS_FILE"
    if [ $rc -eq 0 ]; then say "USB permissions installed."; else say "USB permissions were NOT installed. Please tell the author what is shown above (also in $LOG)."; fi
    printf 'Press Enter to close this window. '; read _x
    exit $rc
fi

try_pkexec && finish 0

if is_tty; then
    run_chain_in_terminal && finish 0
    if have sudo || have su; then
        say "The password was not accepted or the step was cancelled."
        manual_text; exit 2
    fi
    manual_text; exit 3
fi

# no terminal attached (started from the menu / the app): open a terminal window for the password
TERM_CMD="$(terminal_cmd)"
if [ -n "$GUI" ] && [ -n "$TERM_CMD" ] && [ -n "$DISPLAY$WAYLAND_DISPLAY" ]; then
    SF="$(mktemp "${TMPDIR:-/tmp}/voyagercalc-usb.XXXXXX")"
    rm -f "$SF"
    say "Opening a terminal window to ask for the password..."
    log "terminal: $TERM_CMD sh '$SELF' --in-terminal=$SF"
    $TERM_CMD sh "$SELF" "--in-terminal=$SF" >/dev/null 2>&1 &
    n=0
    while [ ! -s "$SF" ] && [ $n -lt 600 ]; do sleep 1; n=$((n + 1)); done
    if [ -s "$SF" ]; then rc=$(cat "$SF"); rm -f "$SF"; if [ "$rc" = 0 ] && installed_ok; then finish 0; fi; say "USB permissions were NOT installed."; exit 2; fi
    say "The terminal window did not finish."
fi
manual_text
exit 3
