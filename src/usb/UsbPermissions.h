// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
//
// UsbPermissions.h - "Fix USB Permissions": installs the udev rule that lets the logged-in user open the
// calculator (USB 03F0:1341) without being root, through one graphical password prompt (pkexec). The same
// rule and steps as install.sh and VoyagerInterface; the app itself never runs as root.
#pragma once
#include <QString>

namespace vc {

/// The udev rule: access for the logged-in desktop user (uaccess) and for the plugdev group.
QString udevRule();
/// Shell script that, run as root, installs the rule, reloads udev and gives access to a calculator that is
/// already connected. Takes the user name as $1 (or $PKEXEC_UID's user) and the device node as $2 (optional).
/// Environment for tests: VC_RULES_DIR (default /etc/udev/rules.d), VC_SKIP_SYSTEM=1 (no udevadm/group changes).
QString fixScript();
/// The same thing as one command the user can paste into a terminal if there is no pkexec.
QString manualCommand();
QString accessScriptPath();

struct FixResult { bool ok = false; bool noPkexec = false; QString message; };
/// Run the script through pkexec (blocking; the password dialog is shown by polkit). `device` may be empty.
FixResult fixPermissions(const QString &device);

}  // namespace vc
