// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
//
// UsbSelfTest.h - `voyagercalc --usb-selftest`: lists the hidraw devices (VID/PID, name, permissions, who holds them open), looks for the
// calculator (USB 03F0:1341, only after f-ENTER-ON), tries a real read the way the program does it, and - if nothing comes back - tries
// the other ways a hidraw device can be asked. Prints plain text; nothing is written to the calculator except the read request.
#pragma once
#include <QString>
#include <functional>

namespace vc {

/// Returns 0 when the whole 2048-byte memory was read, 1 when not, 2 when no calculator was found.
/// The roots are parameters for tests; `readTimeoutMs` is the wait for the replies.
int runUsbSelfTest(const std::function<void(const QString &)> &out, const QString &sysfs = QStringLiteral("/sys/class/hidraw"),
                   const QString &devRoot = QStringLiteral("/dev"), const QString &usbSysfs = QStringLiteral("/sys/bus/usb/devices"),
                   int readTimeoutMs = 3000);

}  // namespace vc
