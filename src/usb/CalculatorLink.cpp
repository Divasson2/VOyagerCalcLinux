// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
#include "CalculatorLink.h"
#include "Machine.h"
#include "UsbLog.h"
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QStandardPaths>
#include <QThread>
#include <cstring>

namespace vc {

Report CalculatorLink::dumpReport() { Report r{}; r[0] = dumpCommand; return r; }

std::vector<Report> CalculatorLink::writeReports(const QByteArray &data) {
    if (data.size() != memorySize) throw LinkError::wrongSize(int(data.size()));
    std::vector<Report> out;
    for (int pos = 0; pos < memorySize; pos += blockSize) {
        Report r{};
        r[0] = uint8_t(blockSize + 2); r[1] = writeCommand; r[2] = uint8_t(pos / blockSize);
        std::memcpy(r.data() + 3, data.constData() + pos, blockSize);
        out.push_back(r);
    }
    return out;
}

bool CalculatorLink::append(const Report &r, QByteArray *dump) {
    int n = r[0];
    if (n == 0) return dump->size() == memorySize;
    if (n >= kReportSize || dump->size() + n > memorySize) throw LinkError::badReport(n);
    dump->append(reinterpret_cast<const char *>(r.data()) + 1, n);
    return dump->size() == memorySize;
}

bool CalculatorLink::compare(const QByteArray &a, const QByteArray &b, Difference *out) {
    int n = 0, first = -1;
    int len = int(std::max(a.size(), b.size()));
    for (int i = 0; i < len; ++i)
        if (i >= a.size() || i >= b.size() || a[i] != b[i]) { ++n; if (first < 0) first = i; }
    if (n == 0) return false;
    *out = {n, first};
    return true;
}

bool CalculatorLink::flag8(const QByteArray &image) { return image.size() > flag8Offset && (uint8_t(image[flag8Offset]) & flag8Mask) != 0; }

QByteArray CalculatorLink::merge(const QByteArray &data, const QByteArray &current, int f8) {
    QByteArray out = current;
    std::memcpy(out.data() + memoryStart, data.constData() + memoryStart, memoryEnd - memoryStart);
    if (f8 >= 0) {
        uint8_t &b = reinterpret_cast<uint8_t &>(out.data()[flag8Offset]);
        b = f8 ? (b | flag8Mask) : (b & uint8_t(~flag8Mask));
        uint8_t &s = reinterpret_cast<uint8_t &>(out.data()[0xC4]);       // keep the S0A copy consistent, as the firmware would
        s = f8 ? (s | 1) : (s & uint8_t(~1));
    }
    return out;
}

QByteArray CalculatorLink::readMemory() {
    UsbLog::write(QStringLiteral("readMemory: looking for and opening the calculator"));
    transport_->open();
    struct Closer { HidTransport *t; ~Closer() { t->close(); } } closer{transport_};
    return dump();
}

QByteArray CalculatorLink::dump() {
    // One request, then wait. A calculator that sent NOTHING at all gets the request once more after a short pause (a first
    // request can be lost while the USB mode wakes up); a calculator that answered part of the image is a real failure.
    for (int attempt = 1;; ++attempt) {
        UsbLog::write(QStringLiteral("dump request %1: send FE, wait up to %2 ms").arg(attempt).arg(readTimeoutMs));
        transport_->send(dumpReport());
        if (commandPauseMs > 0) QThread::msleep(ulong(commandPauseMs));
        QByteArray d;
        int reports = 0;
        QElapsedTimer t; t.start();
        while (t.elapsed() < readTimeoutMs) {
            Report r;
            int wait = int(std::min<qint64>(250, std::max<qint64>(10, readTimeoutMs - t.elapsed())));
            if (!transport_->receive(wait, &r)) continue;
            ++reports;
            if (append(r, &d)) { UsbLog::write(QStringLiteral("dump complete: %1 reports, %2 bytes in %3 ms").arg(reports).arg(d.size()).arg(t.elapsed())); return d; }
        }
        UsbLog::write(QStringLiteral("dump timeout: %1 report(s), %2 of 2048 bytes").arg(reports).arg(d.size()));
        if (reports > 0 || attempt >= 2 || retryAfterSilence <= 0) throw LinkError::timeout(int(d.size()));
        QThread::msleep(ulong(retryAfterSilence));
        Report junk; while (transport_->receive(0, &junk)) {}
    }
}

QByteArray CalculatorLink::writeMemory(const QByteArray &data, int f8) {
    if (data.size() != memorySize) throw LinkError::wrongSize(int(data.size()));
    UsbLog::write(QStringLiteral("writeMemory: looking for and opening the calculator"));
    transport_->open();
    struct Closer { HidTransport *t; ~Closer() { t->close(); } } closer{transport_};
    QByteArray current = dump();
    const QByteArray target = merge(data, current, f8);
    const std::vector<Report> reports = writeReports(target);
    std::optional<LinkError> last = LinkError::timeout(0);
    for (int attempt = 0; attempt < std::max(1, writeAttempts); ++attempt) {
        for (size_t b = 0; b < reports.size(); ++b) {
            if (current.mid(int(b) * blockSize, blockSize) == target.mid(int(b) * blockSize, blockSize)) continue;
            transport_->send(reports[b]);
            if (packetPauseMs > 0) QThread::msleep(ulong(packetPauseMs));
        }
        try {
            QByteArray back = dump();
            current = back;
            Difference d;
            if (compare(back.mid(memoryStart, memoryEnd - memoryStart), target.mid(memoryStart, memoryEnd - memoryStart), &d)) {
                last = LinkError::mismatch(d.count, d.first + memoryStart); continue;
            }
            if (f8 >= 0 && flag8(back) != (f8 != 0)) { last = LinkError::mismatch(1, flag8Offset); continue; }
            return back;
        } catch (const LinkError &e) {
            if (e.kind == LinkError::NoDevice) throw;
            last = e;
        }
    }
    throw *last;
}

QString saveBackup(const QByteArray &image, int variant, QString *error) {
    QString ext = variant == 120 ? QStringLiteral("bin12c") : variant == 160 ? QStringLiteral("bin16c") : variant ? QStringLiteral("bin15c") : QStringLiteral("bin");   // 0.6.9 endings
    QString name = variant ? QStringLiteral("HP-") + Machine::variantName(variant).section(QStringLiteral(" ("), 0, 0) : QStringLiteral("Unknown");
    QString file = name + QStringLiteral(" calculator ") + QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH.mm.ss")) + QLatin1Char('.') + ext;
    QStringList homes;
    QString envDir = qEnvironmentVariable("VOYAGERCALC_BACKUP_DIR");
    if (!envDir.isEmpty()) homes << envDir;
    else {
        QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        if (!docs.isEmpty()) homes << docs + QStringLiteral("/VoyagerCalc Backups");
        homes << QDir::homePath() + QStringLiteral("/VoyagerCalc Backups");
    }
    QString lastErr = QStringLiteral("no folder to save in");
    for (const QString &dir : homes) {
        if (!QDir().mkpath(dir)) { lastErr = QStringLiteral("could not create %1").arg(dir); continue; }
        QFile f(dir + QLatin1Char('/') + file);
        if (f.exists()) { lastErr = QStringLiteral("%1 exists").arg(file); continue; }
        if (f.open(QIODevice::WriteOnly) && f.write(image) == image.size()) return f.fileName();
        lastErr = f.errorString();
    }
    if (error) *error = lastErr;
    return QString();
}

}  // namespace vc
