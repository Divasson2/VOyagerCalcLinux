// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
#include "UsbLog.h"
#include "../Stores.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutexLocker>

namespace vc {

UsbLog &UsbLog::instance() { static UsbLog *l = new UsbLog; return *l; }

QString UsbLog::filePath() { return dataDir() + QStringLiteral("/usb.log"); }

void UsbLog::write(const QString &line) {
    UsbLog &l = instance();
    const QDateTime now = QDateTime::currentDateTime();
    const QString stamped = now.toString(QStringLiteral("HH:mm:ss.zzz")) + QStringLiteral("  ") + line;
    {
        QMutexLocker lock(&l.mutex_);
        l.lines_ << stamped;
        if (l.lines_.size() > 4000) l.lines_.erase(l.lines_.begin(), l.lines_.begin() + 1000);
    }
    {
        QMutexLocker lock(&l.fileMutex_);       // a slow disk can delay the file, never the log window or the USB work's decisions
        QDir().mkpath(dataDir());
        QFile f(filePath());
        if (f.open(QIODevice::WriteOnly | QIODevice::Append)) f.write((now.toString(Qt::ISODate) + QStringLiteral("  ") + stamped + QLatin1Char('\n')).toUtf8());
    }
    std::function<void(const QString &)> echo;
    { QMutexLocker lock(&l.echoMutex_); echo = l.echo_; }
    if (echo) echo(stamped);
    emit l.lineAdded(stamped);              // queued to the window's thread by the connection
}

void UsbLog::setEcho(std::function<void(const QString &)> sink) { UsbLog &l = instance(); QMutexLocker lock(&l.echoMutex_); l.echo_ = std::move(sink); }
QString UsbLog::lastLine() { UsbLog &l = instance(); QMutexLocker lock(&l.mutex_); return l.lines_.isEmpty() ? QString() : l.lines_.last(); }

QString UsbLog::text() { UsbLog &l = instance(); QMutexLocker lock(&l.mutex_); return l.lines_.join(QLatin1Char('\n')); }
void UsbLog::clear() { UsbLog &l = instance(); QMutexLocker lock(&l.mutex_); l.lines_.clear(); }

QString UsbLog::hex(const uint8_t *p, size_t n) {
    size_t end = n;
    while (end > 0 && p[end - 1] == 0) --end;
    QString s;
    for (size_t i = 0; i < end; ++i) s += QStringLiteral("%1 ").arg(p[i], 2, 16, QLatin1Char('0')).toUpper();
    s = s.trimmed();
    if (end < n) s += QStringLiteral(" +%1x00").arg(n - end);
    return s.isEmpty() ? QStringLiteral("(%1x00)").arg(n) : s;
}

}  // namespace vc
