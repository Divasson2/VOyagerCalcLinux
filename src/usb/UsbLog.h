// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
//
// UsbLog.h - a timestamped log of everything the USB link does (open, every report sent and received, errors), kept in memory
// (Help > Show USB Log) and appended to <data folder>/usb.log. Safe to call from any thread.
#pragma once
#include <QMutex>
#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>

namespace vc {

class UsbLog : public QObject {
    Q_OBJECT
public:
    static UsbLog &instance();
    static void write(const QString &line);              // any thread
    static QString text();                               // the whole log
    static void clear();
    static QString lastLine();                           // the newest line (for the watchdog message)
    /// Also hand every new line to `sink` (the --usb-selftest prints them as they happen); an empty function stops it. Called from any thread.
    static void setEcho(std::function<void(const QString &)> sink);
    static QString filePath();                           // usb.log in the data folder
    static QString hex(const uint8_t *p, size_t n);      // "FE +63x00": trailing zeros folded
signals:
    void lineAdded(const QString &line);
private:
    UsbLog() = default;
    QMutex mutex_;           // the lines in memory only: never held while writing the file, so the window can always read them
    QMutex fileMutex_;
    QMutex echoMutex_;
    std::function<void(const QString &)> echo_;
    QStringList lines_;
};

}  // namespace vc
