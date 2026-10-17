// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
#include "Stores.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

namespace vc {

QString dataDir() {
    QString e = qEnvironmentVariable("VOYAGERCALC_DATA_DIR");
    if (!e.isEmpty()) return e;
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/VoyagerCalc");
}

static bool writeFile(const QString &path, const QByteArray &data, QString *error = nullptr) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit()) { if (error) *error = f.errorString(); return false; }
    return true;
}
static QByteArray readFile(const QString &path) { QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); }

QString FirmwareStore::rememberedName() { return QString::fromUtf8(readFile(dataDir() + QStringLiteral("/Firmware/name.txt"))).trimmed(); }

std::optional<StoredFirmware> FirmwareStore::load() {
    QString name = rememberedName();
    QByteArray d = readFile(dataDir() + QStringLiteral("/Firmware/firmware.bin"));
    if (name.isEmpty() || d.isEmpty()) return std::nullopt;
    return StoredFirmware{name, d};
}

bool FirmwareStore::remember(const StoredFirmware &fw, QString *error) {
    return writeFile(dataDir() + QStringLiteral("/Firmware/firmware.bin"), fw.data, error)
        && writeFile(dataDir() + QStringLiteral("/Firmware/name.txt"), fw.name.toUtf8(), error);
}

void FirmwareStore::forget() {
    QFile::remove(dataDir() + QStringLiteral("/Firmware/firmware.bin"));
    QFile::remove(dataDir() + QStringLiteral("/Firmware/name.txt"));
}

static QString statePath(const QString &id) { return dataDir() + QStringLiteral("/State/calculator-%1.json").arg(id.left(16)); }

std::optional<SavedState> StateStore::load(const QString &id) {
    SavedState s;
    if (SavedState::fromJson(readFile(statePath(id)), &s) && s.firmwareId == id) return s;
    return std::nullopt;
}

void StateStore::save(const SavedState &s) {
    if (!s.firmwareId.isEmpty()) writeFile(statePath(s.firmwareId), s.toJson());
}

void StateStore::clear(const QString &id) { QFile::remove(statePath(id)); }

QByteArray NotesStore::load(const QString &id) { return readFile(dataDir() + QStringLiteral("/Notes/programDoc-%1.json").arg(id.left(16))); }
void NotesStore::save(const QString &id, const QByteArray &json) { if (!id.isEmpty()) writeFile(dataDir() + QStringLiteral("/Notes/programDoc-%1.json").arg(id.left(16)), json); }

void NotesStore::forget(const QString &id) { if (!id.isEmpty()) QFile::remove(dataDir() + QStringLiteral("/Notes/programDoc-%1.json").arg(id.left(16))); }

void ExportNameMemory::remember(const QString &kind, const QString &path) {
    QString base = QFileInfo(path).completeBaseName();
    if (!base.isEmpty()) names_[kind] = base;
    const QString e = QFileInfo(path).suffix();
    if (e.isEmpty()) endings_.remove(kind); else endings_[kind] = e;
}

}  // namespace vc
