// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
//
// Helpers for the tests: where the fixtures are, and where the user's firmware is (never in the repository:
// set VOYAGERCALC_FIRMWARE_DIR; firmware-dependent tests skip when it is not there).
#pragma once
#include <QDir>
#include <QFile>
#include <QTest>

#ifndef VC_FIXTURES_DIR
#define VC_FIXTURES_DIR ""
#endif

namespace vct {

inline QByteArray fixture(const QString &name) {
    QFile f(QString::fromUtf8(VC_FIXTURES_DIR) + "/" + name);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

inline QByteArray firmwareMatching(const QString &glob) {
    QString dir = qEnvironmentVariable("VOYAGERCALC_FIRMWARE_DIR");
    if (dir.isEmpty()) return {};
    auto list = QDir(dir).entryList({glob}, QDir::Files, QDir::Name);
    if (list.isEmpty()) return {};
    QFile f(dir + "/" + list.first());
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}
inline QByteArray firmware15() { return firmwareMatching("hp15c-3rom*"); }
inline QByteArray firmwareMulti() { return firmwareMatching("HP12c-multi*E3E3.bin"); }
inline QByteArray firmware16() { return firmwareMatching("Voyager16-ext-path-c-trim4-show-B7B7.bin"); }

#define VC_REQUIRE(fw, what) do { if ((fw).isEmpty()) QSKIP("no " what " firmware (set VOYAGERCALC_FIRMWARE_DIR)"); } while (0)

}  // namespace vct
