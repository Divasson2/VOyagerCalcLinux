// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
//
// MemoryFileNames.h - memory file names and what is inside a file (0.6.9 / Linux test3; mirrors the Mac app's MemoryFileNames.swift).
//
// The default ending of a saved memory file is .bin12c / .bin15c / .bin16c (other programs use .12c / .15c / .16c for TEXT programs).
// The ending is only a default: the save dialog accepts any ending, and opening looks at the CONTENT (2048 bytes), never at the ending.
// The old endings (.12c .15c .16c, .12cmem .15cmem .16cmem) are still understood for finding the notes file (sidecar).
#pragma once
#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QStringList>

namespace vc {
namespace MemoryFileNames {

inline QString defaultExtension(int modelId) { return QStringLiteral("bin%1c").arg(modelId); }

// 12, 15 or 16 for bin15c / 15c / 15cmem (case-insensitive); 0 for anything else.
inline int modelHint(const QString &extension) {
    const QString e = extension.toLower();
    for (int id : {12, 15, 16})
        if (e == QStringLiteral("bin%1c").arg(id) || e == QStringLiteral("%1c").arg(id) || e == QStringLiteral("%1cmem").arg(id)) return id;
    return 0;
}

// Where the notes of a memory file may be: "<name>.voyagerdoc" first, then the same name with another memory ending, then "<base>.voyagerdoc".
inline QStringList sidecarCandidates(const QString &memoryPath) {
    const QFileInfo fi(memoryPath);
    const QString dir = fi.absolutePath() + QLatin1Char('/'), base = fi.completeBaseName(), own = fi.suffix().toLower();
    QStringList out{memoryPath + QStringLiteral(".voyagerdoc")};
    if (const int id = modelHint(own)) {
        for (const QString &e : {defaultExtension(id), QStringLiteral("%1c").arg(id), QStringLiteral("%1cmem").arg(id)})
            if (e != own) out << dir + base + QLatin1Char('.') + e + QStringLiteral(".voyagerdoc");
    }
    out << dir + base + QStringLiteral(".voyagerdoc");
    return out;
}

enum class Content { Image, Text, WrongSize };

// A byte-order mark of UTF-8 / UTF-16, or (nearly) only printable text with "{", "#" or line breaks.
inline bool looksLikeText(const QByteArray &d) {
    const QByteArray b = d.left(4096);
    if (b.size() < 8) return false;
    if (b.startsWith("\xFF\xFE") || b.startsWith("\xFE\xFF") || b.startsWith("\xEF\xBB\xBF")) return true;
    int printable = 0, control = 0;       // non-ASCII letters are fine; control bytes (NUL, 0x01 ...) are what a binary image is full of
    for (unsigned char c : b) {
        if (c == 9 || c == 10 || c == 13 || (c >= 32 && c < 127)) ++printable;
        else if (c < 32 || c == 127) ++control;
    }
    if (double(printable) / b.size() < 0.9 || control * 100 > b.size()) return false;
    return b.contains('{') || b.contains('#') || b.contains('\n');
}

inline Content classify(const QByteArray &d, int memorySize = 2048) {
    if (looksLikeText(d)) return Content::Text;
    return d.size() == memorySize ? Content::Image : Content::WrongSize;
}

// The message for a file that cannot be a memory file; an empty string for a memory image.
inline QString problem(const QByteArray &d, const QString &name, int memorySize = 2048) {
    switch (classify(d, memorySize)) {
    case Content::Image: return {};
    case Content::Text:
        return QStringLiteral("“%1” is a text program (the format other HP simulators write), not a calculator memory file. "
                              "VoyagerCalc opens the 2048-byte memory files it saves, that VoyagerInterface writes, or that a calculator backup produces. "
                              "Importing text programs is not available yet.").arg(name);
    case Content::WrongSize:
        return QStringLiteral("“%1” is not a calculator memory file (it must be exactly 2048 bytes; this one has %2).").arg(name).arg(d.size());
    }
    return {};
}

}  // namespace MemoryFileNames
}  // namespace vc
