// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
//
// Stores.h - what the app remembers between launches, in ~/.local/share/VoyagerCalc (the user's own firmware is
// copied there, never into the program folder): the firmware, the calculator's memory per firmware, the program
// notes per firmware, and the file name last used for each kind of export (ports of FirmwareStore / StateStore /
// ExportNames). Set VOYAGERCALC_DATA_DIR to put it somewhere else (tests).
#pragma once
#include "Machine.h"
#include <QMap>
#include <QString>

namespace vc {

QString dataDir();

struct StoredFirmware { QString name; QByteArray data; };

class FirmwareStore {
public:
    static std::optional<StoredFirmware> load();
    static QString rememberedName();
    /// Keep a copy of a firmware file the core accepted.
    static bool remember(const StoredFirmware &fw, QString *error = nullptr);
    static void forget();
};

class StateStore {
public:
    static std::optional<SavedState> load(const QString &firmwareId);
    static void save(const SavedState &s);
    static void clear(const QString &firmwareId);
};

/// The program notes (.voyagerdoc content) kept per firmware between launches.
class NotesStore {
public:
    static QByteArray load(const QString &firmwareId);
    static void save(const QString &firmwareId, const QByteArray &json);
    static void forget(const QString &firmwareId);     // 0.6.9: a reset / model switch / another firmware leaves no stored notes
};

class ExportNameMemory {
public:
    QString name(const QString &kind, const QString &fallback) const { auto v = names_.value(kind); return v.isEmpty() ? fallback : v; }
    void remember(const QString &kind, const QString &path);       // the name is kept without its ending; the ending typed last time is kept apart
    QString ending(const QString &kind) const { return endings_.value(kind); }       // empty: none yet (0.6.9: .bin15c is only the default)
    void forgetAll() { names_.clear(); endings_.clear(); }
private:
    QMap<QString, QString> names_, endings_;
};

}  // namespace vc
