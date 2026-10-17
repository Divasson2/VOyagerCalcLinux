// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
//
// PortTransport.h - the calculator (USB 03F0:1341, HID mode: off, hold f + ENTER, press ON) over a HidBackend. Same protocol as HidrawTransport:
// output = report id 0 + 64 bytes ([00 FE 00 ...] asks for the memory), input = [id][n, n bytes] reports (the id byte is dropped here).
#pragma once
#include "HidPort.h"

namespace vc {

class PortTransport : public HidTransport {
public:
    explicit PortTransport(std::shared_ptr<HidBackend> backend) : backend_(std::move(backend)) {}
    ~PortTransport() override { close(); }
    void open() override;
    void close() override;
    void send(const Report &report) override;
    bool receive(int timeoutMs, Report *out) override;
    QString devicePath() const { return path_; }

    /// Every step that can block in the operating system runs under a time limit on its own thread (StepGuard.h), like the hidraw transport.
    int enumerateTimeoutMs = 4000, openTimeoutMs = 6000, writeTimeoutMs = 6000, closeTimeoutMs = 2000;

private:
    std::shared_ptr<HidBackend> backend_;
    std::shared_ptr<HidPort> port_;
    HidDeviceInfo info_;
    QString path_;
};

}  // namespace vc
