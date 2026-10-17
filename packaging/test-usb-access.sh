#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 José González Divassón
# Tests usb-access.sh in a fake environment: a PATH with only basic tools plus stub pkexec / sudo / su / ps, no root needed.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
T=$(mktemp -d)
BASE="$T/base"; mkdir -p "$BASE"
for t in sh cat cmp base64 tr grep date mkdir id getent timeout sleep dirname basename ls rm mktemp head cut sed chmod; do
    p=$(command -v $t) && ln -s "$p" "$BASE/$t"
done
PKG="$T/pkg"; mkdir -p "$PKG"
cp "$HERE/usb-access.sh" "$PKG/"
if [ -f "$HERE/../build/voyagercalc" ]; then :; fi
printf '%s\n' '# test rule' 'KERNEL=="hidraw*", MODE="0660"' > "$PKG/99-voyagercalc.rules"
pass=0; fail=0
ok() { pass=$((pass+1)); echo "PASS: $1"; }
bad() { fail=$((fail+1)); echo "FAIL: $1"; }

# stubs:  S=<dir>  with: pkexec(hang|ok|cancel) sudo(ok|notsudoer) su(ok) ps(agent|none) terminal
mk() { S="$T/stub.$1"; rm -rf "$S"; mkdir -p "$S"; }
stub_pkexec() { # mode
    case "$1" in
        ok) printf '#!/bin/sh\necho pkexec >> "$VC_TRACE"\nexec "$@"\n' ;;
        hang) printf '#!/bin/sh\necho pkexec-hang >> "$VC_TRACE"\nsleep 30\n' ;;
        cancel) printf '#!/bin/sh\necho pkexec-cancel >> "$VC_TRACE"\nexit 126\n' ;;
    esac > "$S/pkexec"; chmod +x "$S/pkexec"; }
stub_sudo() {
    case "$1" in
        ok) printf '#!/bin/sh\nif [ "$1" = -v ]; then echo sudo-v >> "$VC_TRACE"; exit 0; fi\necho sudo >> "$VC_TRACE"\nexec "$@"\n' ;;
        notsudoer) printf '#!/bin/sh\necho sudo-refused >> "$VC_TRACE"\nexit 1\n' ;;
    esac > "$S/sudo"; chmod +x "$S/sudo"; }
stub_su() { printf '#!/bin/sh\necho su >> "$VC_TRACE"\nshift\nexec sh -c "$1"\n' > "$S/su"; chmod +x "$S/su"; }
stub_ps() { if [ "$1" = agent ]; then printf '#!/bin/sh\necho gnome-shell\necho polkitd\n'; else printf '#!/bin/sh\necho bash\necho polkitd\n'; fi > "$S/ps"; chmod +x "$S/ps"; }

run() { # name expect_rc expect_trace_regex [env...]   (uses $S, $TTY, $GUI)
    name="$1"; erc="$2"; etrace="$3"; shift 3
    RD="$T/rules.$$.$name"; rm -rf "$RD"
    : > "$T/trace"
    out=$(env -i PATH="$S:$BASE" HOME="$T/home" USER=tester VC_RULES_DIR="$RD" VC_SKIP_SYSTEM=1 VC_LOG="$T/log" VC_TRACE="$T/trace" \
          VC_PKEXEC_TIMEOUT=2 $TTY DISPLAY="${DISP-:0}" "$@" /bin/sh "$PKG/usb-access.sh" $GUI 2>&1)
    rc=$?
    tr=$(tr '\n' ' ' < "$T/trace")
    if [ "$rc" = "$erc" ] && printf '%s\n' "$tr" | grep -Eq "$etrace"; then ok "$name (rc=$rc trace: $tr)"; else bad "$name (rc=$rc expected $erc; trace: $tr; out: $out)"; fi
    if [ "$erc" = 0 ]; then
        if cmp -s "$RD/99-voyagercalc.rules" "$PKG/99-voyagercalc.rules"; then ok "$name: rule file written"; else bad "$name: rule file missing"; fi
        printf '%s' "$out" | grep -q "USB permissions installed" && ok "$name: result line" || bad "$name: no result line: $out"
    else
        printf '%s' "$out" | grep -Eq "NOT installed" && ok "$name: failure line + instructions" || bad "$name: no failure line: $out"
    fi
}
TTY="VC_TEST_TTY=1"; GUI=""

mk A; stub_su;                                                  run "no pkexec, no sudo, su present" 0 '^su $'
mk B; stub_pkexec ok; stub_sudo ok; stub_su; stub_ps agent;     run "pkexec + agent" 0 '^pkexec $'
mk C; stub_pkexec ok; stub_sudo ok; stub_su; stub_ps none;      run "pkexec but no agent -> sudo" 0 '^sudo-v sudo $'
mk D; stub_ps none;                                             run "nothing available -> command printed" 3 '^$'
mk E; stub_pkexec hang; stub_sudo ok; stub_ps agent;            run "pkexec hangs -> timeout -> sudo" 0 'pkexec-hang sudo-v sudo'
mk F; stub_sudo notsudoer; stub_su; stub_ps none;               run "not a sudoer -> su" 0 'sudo-refused su'
mk G; stub_pkexec cancel; stub_sudo ok; stub_ps agent;          run "pkexec cancelled -> sudo" 0 'pkexec-cancel sudo-v sudo'
mk H; stub_pkexec ok; stub_ps agent; DISP=""                    run "no graphical session -> no pkexec, nothing -> command" 3 '^$'
unset DISP
mk I; stub_pkexec ok; stub_ps none; TTY="VC_NONE=1"; GUI="--gui"; run "app, no tty, no agent, no terminal program -> command" 3 '^$'
stub_term() { printf '#!/bin/sh\necho terminal >> "$VC_TRACE"\nshift\nexec "$@" </dev/null\n' > "$S/x-terminal-emulator"; chmod +x "$S/x-terminal-emulator"; }
mk K; stub_ps none; stub_sudo ok; stub_term; TTY="VC_NONE=1"; GUI="--gui"; run "app, no agent -> terminal window -> sudo" 0 'terminal sudo-v sudo'
mk L; stub_ps none; stub_sudo notsudoer; stub_su; stub_term;     run "app, terminal window -> not sudoer -> su" 0 'terminal sudo-refused su'
mk M; stub_ps none; stub_sudo notsudoer; stub_term;              run "app, terminal window, nothing works -> failure" 2 'terminal sudo-refused'
TTY="VC_TEST_TTY=1"; GUI=""
# already installed: no tools needed
mk J; RD="$T/rules.J"; mkdir -p "$RD"; cp "$PKG/99-voyagercalc.rules" "$RD/"
out=$(env -i PATH="$S:$BASE" HOME="$T/home" USER=tester VC_RULES_DIR="$RD" VC_LOG="$T/log" /bin/sh "$PKG/usb-access.sh" 2>&1); rc=$?
[ $rc = 0 ] && printf '%s' "$out" | grep -q "already set up" && ok "already installed" || bad "already installed: $out"
# log file written
[ -s "$T/log" ] && ok "log file written" || bad "no log"
echo; echo "$pass passed, $fail failed"
[ $fail = 0 ]
