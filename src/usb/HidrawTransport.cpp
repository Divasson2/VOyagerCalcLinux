// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
#include "HidrawTransport.h"
#include "StepGuard.h"
#include "UsbLog.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTextStream>
#include <array>
#include <cerrno>
#include <memory>
#include <optional>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

namespace vc {

QStringList HidrawTransport::findDevices(const QString &sysfs, const std::function<void(const QString &)> &log) {
    const QString want = QStringLiteral("HID_ID=0003:%1:%2").arg(0x03F0, 8, 16, QLatin1Char('0')).arg(0x1341, 8, 16, QLatin1Char('0')).toUpper();
    QStringList found;
    QDir d(sysfs);
    const QStringList names = d.entryList({QStringLiteral("hidraw*")}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    if (log) log(QStringLiteral("discovery: %1 hidraw node(s) in %2").arg(names.size()).arg(sysfs));
    for (const QString &n : names) {
        QFile f(d.filePath(n + QStringLiteral("/device/uevent")));
        if (!f.open(QIODevice::ReadOnly)) { if (log) log(QStringLiteral("discovery: %1: cannot read its uevent (skipped)").arg(n)); continue; }
        QString hidId, name; bool match = false;
        // readAll(), NOT "while (!atEnd()) readLine()": a sysfs file reports a size of 4096, so QFile::atEnd() never becomes true after the real end of
        // the text and that loop spins forever on the first hidraw node that is not the calculator (this was the test4 hang).
        for (const QByteArray &raw : f.readAll().split('\n')) {
            const QString l = QString::fromLatin1(raw).trimmed();
            if (l.toUpper() == want) match = true;
            if (l.startsWith(QLatin1String("HID_ID="))) hidId = l.mid(7);
            if (l.startsWith(QLatin1String("HID_NAME="))) name = l.mid(9);
        }
        if (log) log(QStringLiteral("discovery: %1: HID_ID %2 \"%3\"%4").arg(n, hidId, name, match ? QStringLiteral("  <- the calculator") : QStringLiteral("  (not the calculator)")));
        if (match) found << n;
    }
    return found;
}

QString HidrawTransport::findDevice(const QString &sysfs) {
    const QStringList l = findDevices(sysfs);
    return l.isEmpty() ? QString() : QStringLiteral("/dev/") + l.first();
}

void HidrawTransport::open() {
    if (given_) return;
    close();
    UsbLog::write(QStringLiteral("open: begin"));
    QStringList candidates;
    if (!forced_.isEmpty()) { candidates << forced_; UsbLog::write(QStringLiteral("open: using the node given on the command line: %1").arg(forced_)); }
    else {
        // The scan of /sys runs under a time limit too: a sysfs read can block while the USB device is being reset.
        auto names = std::make_shared<QStringList>();
        const QString sysfs = sysfs_, devRoot = devRoot_;
        StepOutcome o = runStep(QStringLiteral("discovery in %1").arg(sysfs), discoverTimeoutMs, [sysfs, names] {
            *names = findDevices(sysfs, [](const QString &l) { UsbLog::write(l); });
            return 0L;
        });
        if (!o.finished) throw LinkError::stuck(QStringLiteral("look for the calculator in %1").arg(sysfs), discoverTimeoutMs);
        for (const QString &n : *names) candidates << devRoot + QLatin1Char('/') + n;
        if (candidates.isEmpty()) { UsbLog::write(QStringLiteral("open: no hidraw device with USB 03F0:1341 found")); throw LinkError::noDevice(); }
    }
    // Try every candidate; one that cannot be opened is skipped (and said so), the most telling error is reported if none works.
    std::optional<LinkError> denied, stuck, other;
    for (const QString &cand : candidates) {
        UsbLog::write(QStringLiteral("open: candidate %1").arg(cand));
        int e = 0;
        const int fd = guardedOpen(cand, openTimeoutMs, &e, openFn);
        if (fd >= 0) {
            fd_ = fd; path_ = cand;
            UsbLog::write(QStringLiteral("open %1 ok (fd %2)").arg(path_).arg(fd_));
            Report r; int flushed = 0;
            while (receive(0, &r)) ++flushed;       // flush anything left over
            if (flushed) UsbLog::write(QStringLiteral("open: discarded %1 stale report(s)").arg(flushed));
            return;
        }
        if (fd == -2) {
            const QStringList who = nodeHolders(cand);
            UsbLog::write(QStringLiteral("open %1: no answer within %2 ms; processes that have it open: %3").arg(cand).arg(openTimeoutMs).arg(who.isEmpty() ? QStringLiteral("none found") : who.join(QStringLiteral(", "))));
            if (!stuck) stuck = LinkError::stuck(QStringLiteral("open %1").arg(cand), openTimeoutMs);
        } else {
            UsbLog::write(QStringLiteral("open %1 failed: %2 (skipped)").arg(cand, QString::fromLocal8Bit(std::strerror(e))));
            if (e == EACCES || e == EPERM) { if (!denied) denied = LinkError::accessDenied(cand); }
            else if (!other) other = LinkError(LinkError::NoDevice, QStringLiteral("The calculator went away (%1). ").arg(QString::fromLocal8Bit(std::strerror(e))) + LinkError::connectHint());
        }
    }
    if (denied) throw *denied;
    if (stuck) throw *stuck;
    if (other) throw *other;
    throw LinkError::noDevice();
}

void HidrawTransport::close() {
    if (fd_ >= 0 && !given_) {
        const int fd = fd_; fd_ = -1;
        UsbLog::write(QStringLiteral("close fd %1").arg(fd));
        StepOutcome o = runStep(QStringLiteral("close %1").arg(path_), closeTimeoutMs, [fd] { return long(::close(fd)); }, {}, false);
        if (!o.finished) UsbLog::write(QStringLiteral("close: did not return within %1 ms; abandoned").arg(closeTimeoutMs));
    }
}

void HidrawTransport::send(const Report &report) {
    if (fd_ < 0) throw LinkError::noDevice();
    auto buf = std::make_shared<std::array<uint8_t, kReportSize + 1>>();
    (*buf)[0] = 0;                                      // report id 0: hidraw wants it as the first byte (65 bytes); the kernel strips it
    std::memcpy(buf->data() + 1, report.data(), kReportSize);
    const int fd = fd_;
    const auto wfn = writeFn;
    StepOutcome o = runStep(QStringLiteral("write to %1").arg(path_), writeTimeoutMs, [fd, buf, wfn] {
        long n;
        QElapsedTimer t; t.start();
        do { n = wfn ? wfn(fd, buf->data(), buf->size()) : long(::write(fd, buf->data(), buf->size())); } while (n < 0 && (errno == EINTR || (errno == EAGAIN && t.elapsed() < 2000)));
        return n;
    }, {}, false);
    if (!o.finished) {
        fd_ = -1;                                       // the descriptor is in use by the stuck call: leave it, do not close it under that call
        throw LinkError::stuck(QStringLiteral("send the request to %1").arg(path_), writeTimeoutMs);
    }
    if (o.value < 0) {
        UsbLog::write(QStringLiteral("send failed: %1").arg(o.why()));
        throw LinkError::io(QStringLiteral("sending a report failed (%1); the calculator may have left USB mode").arg(o.why()));
    }
    UsbLog::write(QStringLiteral("send %1 byte(s): %2").arg(o.value).arg(UsbLog::hex(buf->data(), size_t(o.value))));
}

bool HidrawTransport::receive(int timeoutMs, Report *out) {
    if (fd_ < 0) throw LinkError::noDevice();
    pollfd p{fd_, POLLIN, 0};
    int r;
    do { r = ::poll(&p, 1, timeoutMs < 0 ? 0 : timeoutMs); } while (r < 0 && errno == EINTR);
    if (r == 0) return false;
    if (r < 0 || (p.revents & (POLLERR | POLLHUP | POLLNVAL))) UsbLog::write(QStringLiteral("poll: error or hang-up (revents 0x%1)").arg(p.revents, 0, 16));
    if (r < 0 || (p.revents & (POLLERR | POLLHUP | POLLNVAL))) throw LinkError(LinkError::NoDevice, QStringLiteral("The calculator went away. ") + LinkError::connectHint());
    uint8_t buf[kReportSize + 1];
    ssize_t n = ::read(fd_, buf, sizeof buf);
    if (n < 0) {
        if (errno == EAGAIN || errno == EINTR) return false;
        throw LinkError(LinkError::NoDevice, QStringLiteral("The calculator went away (%1). ").arg(QString::fromLocal8Bit(std::strerror(errno))) + LinkError::connectHint());
    }
    if (n == 0) throw LinkError(LinkError::NoDevice, QStringLiteral("The calculator went away. ") + LinkError::connectHint());
    UsbLog::write(QStringLiteral("recv %1 byte(s): %2").arg(n).arg(UsbLog::hex(buf, size_t(n))));
    out->fill(0);
    const uint8_t *src = buf; size_t len = size_t(n);
    if (len == kReportSize + 1) { src++; len--; }       // a leading report id
    std::memcpy(out->data(), src, std::min<size_t>(len, kReportSize));
    return true;
}

}  // namespace vc
