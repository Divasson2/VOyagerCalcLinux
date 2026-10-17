// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
#include "PortTransport.h"
#include "StepGuard.h"
#include "UsbLog.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <optional>

namespace vc {

void PortTransport::open() {
    close();
    UsbLog::write(QStringLiteral("open: begin"));
    if (!backend_) throw LinkError::noDevice();
    auto list = std::make_shared<std::vector<HidDeviceInfo>>();
    auto err = std::make_shared<QString>();
    const auto backend = backend_;
    StepOutcome o = runStep(QStringLiteral("list the HID devices"), enumerateTimeoutMs, [backend, list, err] {
        try { *list = backend->enumerate(); } catch (const Error &e) { *err = e.message(); return -1L; }
        return 0L;
    });
    if (!o.finished) throw LinkError::stuck(QStringLiteral("list the USB devices"), enumerateTimeoutMs);
    if (o.value < 0) throw LinkError::io(QStringLiteral("could not list the USB devices (%1)").arg(*err));
    std::vector<HidDeviceInfo> candidates;
    for (const HidDeviceInfo &d : *list) {
        UsbLog::write(QStringLiteral("discovery: %1:%2 \"%3\"%4  report lengths in %5 / out %6").arg(QString::number(d.vendor, 16).rightJustified(4, QLatin1Char('0')).toUpper(),
                      QString::number(d.product, 16).rightJustified(4, QLatin1Char('0')).toUpper(), d.name, d.isCalculator() ? QStringLiteral("  <- the calculator") : QString())
                      .arg(d.inputReportBytes).arg(d.outputReportBytes));
        if (d.isCalculator()) candidates.push_back(d);
    }
    if (candidates.empty()) { UsbLog::write(QStringLiteral("open: no HID device 03F0:1341 found")); throw LinkError::noDevice(); }
    std::optional<LinkError> denied, stuck, other;
    for (const HidDeviceInfo &d : candidates) {
        UsbLog::write(QStringLiteral("open: candidate %1").arg(d.path));
        auto port = std::make_shared<std::unique_ptr<HidPort>>();
        auto openErr = std::make_shared<std::optional<LinkError>>();
        StepOutcome oo = runStep(QStringLiteral("open %1").arg(d.path), openTimeoutMs, [backend, d, port, openErr] {
            try { *port = backend->open(d); } catch (const LinkError &e) { *openErr = e; return -1L; } catch (const Error &e) { *openErr = LinkError::io(e.message()); return -1L; }
            return 0L;
        });
        if (!oo.finished) { if (!stuck) stuck = LinkError::stuck(QStringLiteral("open the calculator"), openTimeoutMs); UsbLog::write(QStringLiteral("open %1: no answer within %2 ms (skipped)").arg(d.path).arg(openTimeoutMs)); continue; }
        if (oo.value < 0 || !*port) {
            const LinkError e = openErr->value_or(LinkError::noDevice());
            UsbLog::write(QStringLiteral("open %1 failed: %2 (skipped)").arg(d.path, e.message()));
            if (e.kind == LinkError::AccessDenied) { if (!denied) denied = e; } else if (!other) other = e;
            continue;
        }
        port_ = std::shared_ptr<HidPort>(port->release());
        info_ = d; path_ = d.path;
        UsbLog::write(QStringLiteral("open %1 ok").arg(path_));
        Report r; int flushed = 0;
        while (receive(0, &r)) ++flushed;        // flush anything left over
        if (flushed) UsbLog::write(QStringLiteral("open: discarded %1 stale report(s)").arg(flushed));
        return;
    }
    if (denied) throw *denied;
    if (stuck) throw *stuck;
    if (other) throw *other;
    throw LinkError::noDevice();
}

void PortTransport::close() {
    if (!port_) return;
    auto p = std::move(port_);
    port_.reset();
    UsbLog::write(QStringLiteral("close"));
    StepOutcome o = runStep(QStringLiteral("close %1").arg(path_), closeTimeoutMs, [p] { p->close(); return 0L; }, {}, false);
    if (!o.finished) UsbLog::write(QStringLiteral("close: did not return within %1 ms; abandoned").arg(closeTimeoutMs));
}

void PortTransport::send(const Report &report) {
    if (!port_) throw LinkError::noDevice();
    // report id 0 first, then the 64 bytes; padded to the length the device declares (a report id is part of every Windows HID write)
    const size_t len = std::max<size_t>(size_t(info_.outputReportBytes), kReportSize + 1);
    auto buf = std::make_shared<std::vector<uint8_t>>(len, uint8_t(0));
    std::memcpy(buf->data() + 1, report.data(), kReportSize);
    const auto port = port_;
    std::shared_ptr<std::optional<LinkError>> werr = std::make_shared<std::optional<LinkError>>();
    StepOutcome o = runStep(QStringLiteral("write to %1").arg(path_), writeTimeoutMs, [port, buf, werr] {
        try { port->write(buf->data(), buf->size(), 5000); } catch (const LinkError &e) { *werr = e; return -1L; }
        return long(buf->size());
    }, {}, false);
    if (!o.finished) { port_.reset(); throw LinkError::stuck(QStringLiteral("send the request to the calculator"), writeTimeoutMs); }
    if (o.value < 0) {
        const LinkError e = werr->value_or(LinkError::io(QStringLiteral("sending a report failed")));
        UsbLog::write(QStringLiteral("send failed: %1").arg(e.message()));
        throw LinkError(e.kind == LinkError::Stuck ? LinkError::Stuck : LinkError::Io, e.kind == LinkError::Stuck ? e.message() : QStringLiteral("USB error: ") + e.message() + QStringLiteral(" (the calculator may have left USB mode)"));
    }
    UsbLog::write(QStringLiteral("send %1 byte(s): %2").arg(o.value).arg(UsbLog::hex(buf->data(), size_t(o.value))));
}

bool PortTransport::receive(int timeoutMs, Report *out) {
    if (!port_) throw LinkError::noDevice();
    std::array<uint8_t, 520> buf{};
    const size_t cap = std::min<size_t>(buf.size(), std::max<size_t>(size_t(info_.inputReportBytes), kReportSize + 1));
    const size_t n = port_->read(buf.data(), cap, std::max(0, timeoutMs));
    if (n == 0) return false;
    UsbLog::write(QStringLiteral("recv %1 byte(s): %2").arg(n).arg(UsbLog::hex(buf.data(), n)));
    out->fill(0);
    const uint8_t *src = buf.data(); size_t len = n;
    if (len == kReportSize + 1) { src++; len--; }       // the leading report id (0)
    std::memcpy(out->data(), src, std::min<size_t>(len, kReportSize));
    return true;
}

}  // namespace vc
