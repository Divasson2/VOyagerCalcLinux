// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
//
// HidTransport.h - the USB side of the calculator link, behind an interface so that the protocol
// (CalculatorLink) is the same on Linux (hidraw), Windows (Win32 HID, later) and in tests (a fake calculator).
#pragma once
#include "Emulator.h"
#include <QString>
#include <array>

namespace vc {

constexpr int kReportSize = 64;
using Report = std::array<uint8_t, kReportSize>;

/// A failure of the USB link, with a message in plain words (port of LinkError in CalculatorLink.swift).
class LinkError : public Error {
public:
    enum Kind { NoDevice, AccessDenied, Io, Timeout, BadReport, WrongSize, Mismatch, Stuck };
    static QString connectHint() { return QStringLiteral("Connect the calculator by USB, turn it off, hold f and ENTER and press ON."); }
    LinkError(Kind k, const QString &message, int a = 0, int b = 0) : Error(message), kind(k), a(a), b(b) {}
    static LinkError noDevice() { return {NoDevice, QStringLiteral("No calculator found – press f ENTER ON. ") + connectHint()}; }
    static LinkError accessDenied(const QString &path) {
        return {AccessDenied, QStringLiteral("Permission denied%1 – click Fix USB Permissions. "
                                             "Linux does not yet allow VoyagerCalc to use the calculator's USB connection; "
                                             "“Fix USB Permissions” (Calculator → USB) asks for one password, once. Then try again.")
                                  .arg(path.isEmpty() ? QString() : QStringLiteral(" opening %1").arg(path))};
    }
    static LinkError io(const QString &s) { return {Io, QStringLiteral("USB error: ") + s}; }
    static LinkError timeout(int received) {
        return {Timeout, QStringLiteral("The calculator stopped answering (%1 of 2048 bytes received). ").arg(received) + connectHint()
                             + QStringLiteral(" If it still does not read, open Help → Show USB Log, or run  voyagercalc --usb-selftest  in a terminal."), received};
    }
    /// The 25-second watchdog of the window (the USB work never finished) - not the same as a calculator that stays silent.
    /// One call into Linux (looking for the device, open, write, close) did not return within its limit: the USB device or its driver is stuck.
    static LinkError stuck(const QString &what, int ms) {
        return {Stuck, QStringLiteral("Linux did not answer when VoyagerCalc tried to %1 (no reply within %2 seconds): the calculator's USB connection is stuck. "
                                      "Turn the calculator off, disconnect it from the virtual machine (in your virtualization software, e.g. its USB devices menu) and connect it to Linux again, "
                                      "then hold f and ENTER and press ON, and try again. If it keeps happening, run  voyagercalc --usb-selftest  in a terminal and send me the output.")
                           .arg(what).arg(ms / 1000.0, 0, 'g', 3), ms};
    }
    static LinkError hung(const QString &lastStep = QString()) {
        return {Timeout, QStringLiteral("The USB operation did not finish within 25 seconds (the program was waiting for the USB device%1). ")
                             .arg(lastStep.isEmpty() ? QString() : QStringLiteral(", last step: ") + lastStep) + connectHint()
                             + QStringLiteral(" Open Help → Show USB Log, or run  voyagercalc --usb-selftest  in a terminal."), 0};
    }
    static LinkError badReport(int length) { return {BadReport, QStringLiteral("The calculator sent an unexpected USB report (length byte %1).").arg(length), length}; }
    static LinkError wrongSize(int n) { return {WrongSize, QStringLiteral("A calculator memory image is 2048 bytes, not %1.").arg(n), n}; }
    static LinkError mismatch(int differences, int firstOffset) {
        return {Mismatch, QStringLiteral("Read-back check failed: %1 of 2048 bytes differ (first at offset 0x%2).").arg(differences).arg(QString::number(firstOffset, 16).rightJustified(3, QLatin1Char('0')).toUpper()),
                differences, firstOffset};
    }
    Kind kind;
    int a, b;       // received / length / differences, first offset
};

class HidTransport {
public:
    virtual ~HidTransport() = default;
    /// Find and open the calculator. Throws LinkError (NoDevice, AccessDenied).
    virtual void open() = 0;
    virtual void close() = 0;
    /// Send one output report (kReportSize bytes, report id 0).
    virtual void send(const Report &report) = 0;
    /// One input report, or false when none came within the timeout.
    virtual bool receive(int timeoutMs, Report *out) = 0;
};

}  // namespace vc
