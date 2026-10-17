// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
//
// HidrawTransport.h - the calculator (USB 03F0:1341) on /dev/hidrawN: found through /sys/class/hidraw, no libusb.
#pragma once
#include "HidTransport.h"
#include <QStringList>
#include <functional>

namespace vc {

class HidrawTransport : public HidTransport {
public:
    HidrawTransport() = default;
    /// For tests: use an already open descriptor (a socket or pipe standing in for the device).
    explicit HidrawTransport(int fd) : fd_(fd), given_(true) {}
    ~HidrawTransport() override { close(); }
    /// "/dev/hidrawN" of the calculator, or empty. `sysfs` is a parameter for tests.
    static QString findDevice(const QString &sysfs = QStringLiteral("/sys/class/hidraw"));
    /// Every hidraw node of the calculator (03F0:1341), as "hidrawN". `log` (optional) gets one line per node looked at.
    static QStringList findDevices(const QString &sysfs, const std::function<void(const QString &)> &log = {});
    void open() override;
    void close() override;
    void send(const Report &report) override;
    bool receive(int timeoutMs, Report *out) override;
    QString devicePath() const { return path_; }
    /// Open exactly this node instead of searching (the --usb-selftest and tests).
    void setPath(const QString &p) { forced_ = p; }
    /// Tests: where sysfs and the device nodes are.
    void setRoots(const QString &sysfs, const QString &devRoot) { sysfs_ = sysfs; devRoot_ = devRoot; }

    // Every call that can block inside the kernel runs under a time limit (see StepGuard.h); a call that does not return gives LinkError::Stuck.
    int discoverTimeoutMs = 3000, openTimeoutMs = 6000, writeTimeoutMs = 6000, closeTimeoutMs = 2000;
    /// Tests: stand-ins for ::open and ::write (a node whose open() blocks).
    std::function<int(const char *)> openFn;
    std::function<long(int, const void *, size_t)> writeFn;

private:
    int fd_ = -1;
    bool given_ = false;
    QString path_, forced_;
    QString sysfs_ = QStringLiteral("/sys/class/hidraw"), devRoot_ = QStringLiteral("/dev");
};

}  // namespace vc
