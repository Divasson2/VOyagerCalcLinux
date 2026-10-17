// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
//
// HidPort.h - the thin operating-system layer under the calculator link on platforms with a "HID API" (Windows): list HID devices, open one,
// write one output report, read one input report with a time limit. Everything above it (finding 03F0:1341, the 65-byte [00 FE ...] frame,
// stripping the report id from replies, step time limits, the USB log) is PortTransport and is the same code on every platform, so it is tested
// on Linux with a fake backend. The Windows backend (WinHidBackend.cpp) is the only file that includes Windows headers.
#pragma once
#include "HidTransport.h"
#include <QString>
#include <memory>
#include <vector>

namespace vc {

struct HidDeviceInfo {
    QString path;                   // what open() needs (Windows: \\?\hid#vid_03f0&pid_1341#...)
    QString name;                   // product string, may be empty
    int vendor = 0, product = 0;
    int inputReportBytes = 0;       // length of an input report INCLUDING the leading report id byte (Windows: HidP_GetCaps.InputReportByteLength)
    int outputReportBytes = 0;      // same for output reports
    bool isCalculator() const { return vendor == 0x03F0 && product == 0x1341; }
};

class HidPort {
public:
    virtual ~HidPort() = default;
    /// One output report: `n` bytes, the first being the report id. Throws LinkError (Io when the device went away, Stuck on timeout).
    virtual void write(const uint8_t *data, size_t n, int timeoutMs) = 0;
    /// One input report into buf (at most cap bytes, first byte = report id). Returns its length, or 0 when none came within timeoutMs.
    /// Throws LinkError when the device went away.
    virtual size_t read(uint8_t *buf, size_t cap, int timeoutMs) = 0;
    virtual void close() = 0;
};

class HidBackend {
public:
    virtual ~HidBackend() = default;
    /// All HID devices that are present (cheap queries only; no exclusive access taken).
    virtual std::vector<HidDeviceInfo> enumerate() = 0;
    /// Open for reading and writing. Throws LinkError (AccessDenied when another program has the device, NoDevice when it vanished).
    virtual std::unique_ptr<HidPort> open(const HidDeviceInfo &d) = 0;
};

/// The backend of this platform (Windows: SetupAPI + hid.dll); nullptr where there is none.
std::shared_ptr<HidBackend> platformHidBackend();

}  // namespace vc
