// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
//
// CalculatorLink.h - the real calculator over USB HID: read and write the 2 KB Continuous Memory exactly as
// the Mac app does (port of CalculatorLink.swift; protocol notes there and in ANALYSIS.md section 2):
//   * 64-byte reports, VID 0x03F0, PID 0x1341, report id 0. The calculator is a HID device only after
//     f + ENTER + ON (calculator off, hold f and ENTER, press ON).
//   * Output report whose first byte is >= 0x40: that byte is a command; 0xFE = send the 2,048 bytes.
//   * Otherwise byte 0 is a length n and bytes 1...n go to the command queue; 'C' (0x43), block, 32 bytes:
//     copy them to 0x20000018 + block * 32. No checksum, no reply.
//   * The dump arrives as input reports [n, n bytes] with n < 64.
// Write rules: only 0x70..0x7EF of the image is written (the calculator keeps its own CPU state and
// trailer); HP-15C flag 8 (complex mode) is one CPU status bit, image byte 0x4C bit 3, set or cleared on its own.
#pragma once
#include "HidTransport.h"
#include <QByteArray>
#include <memory>
#include <vector>

namespace vc {

class CalculatorLink {
public:
    static constexpr int vendorId = 0x03F0, productId = 0x1341, memorySize = 2048, blockSize = 32;
    static constexpr uint8_t dumpCommand = 0xFE, writeCommand = 'C';
    static constexpr int memoryStart = 0x70, memoryEnd = 0x7F0;           // the part of the image that is memory
    static constexpr int flag8Offset = 0x4C; static constexpr uint8_t flag8Mask = 0x08;

    explicit CalculatorLink(HidTransport *transport) : transport_(transport) {}

    int readTimeoutMs = 3000;
    int commandPauseMs = 100, packetPauseMs = 1;
    int writeAttempts = 3;
    int retryAfterSilence = 300;            // ms; > 0: a read that got no report at all sends its request once more (0 = never)

    // framing (static, tested on their own)
    static Report dumpReport();
    static std::vector<Report> writeReports(const QByteArray &data);      // 64 reports [34, 'C', block, 32 bytes]
    /// Add one input report [n, n bytes] to the dump. True when the dump is complete. Throws LinkError::BadReport.
    static bool append(const Report &r, QByteArray *dump);
    struct Difference { int count, first; };
    static bool compare(const QByteArray &a, const QByteArray &b, Difference *out);
    static bool flag8(const QByteArray &image);
    /// What is sent: the calculator's own bytes outside memoryRange, the new image inside it; flag8 (15C images
    /// only) also sets or clears that one CPU status bit and its S0A copy (0xC4 bit 0).
    static QByteArray merge(const QByteArray &data, const QByteArray &current, int flag8 = -1);

    // operations (blocking: call them off the UI thread)
    QByteArray readMemory();
    /// Read the calculator first, keep its CPU state and trailer, send only the 32-byte blocks that change, read
    /// back and compare; rewrites up to writeAttempts times. Returns the read-back. flag8: -1 = leave it.
    QByteArray writeMemory(const QByteArray &data, int flag8 = -1);

private:
    QByteArray dump();
    HidTransport *transport_;
};

// --- the write rules around the link (port of the 0.6.x checks in AppModel / VoyagerInterface) -----------

/// "HP-15C calculator 2026-10-16 08.12.33.15c" in ~/Documents/VoyagerCalc Backups (falls back to the home folder).
QString saveBackup(const QByteArray &image, int variant, QString *error = nullptr);

}  // namespace vc
