// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
//
// ProgramDocument.h - the notes that document a program, kept in a sidecar file next to the memory file
// ("FinancePac.15c" -> "FinancePac.15c.voyagerdoc", JSON, the same format as the Mac app), and everything the
// PDF and the text export show (port of ProgramDocument.swift).
#pragma once
#include "ProgramAnalysis.h"
#include <QDate>
#include <QMap>
#include <QString>
#include <optional>
#include <vector>

namespace vc {

struct RegisterNote {
    QString name, meaning;
    bool isEmpty() const { return name.isEmpty() && meaning.isEmpty(); }
    bool operator==(const RegisterNote &o) const { return name == o.name && meaning == o.meaning; }
};

struct StepComment {
    int step = 0;
    QString text, keys;
    QStringList before, after;
    bool orphaned = false;
    bool operator==(const StepComment &o) const {
        return step == o.step && text == o.text && keys == o.keys && before == o.before && after == o.after && orphaned == o.orphaned;
    }
};

struct ProgramDocument {
    QString title, author, description, model;
    QMap<QString, QString> labels, flags;
    QMap<QString, RegisterNote> registers;
    std::vector<StepComment> comments;

    bool isEmpty() const;
    QByteArray encoded() const;
    static std::optional<ProgramDocument> decode(const QByteArray &json);
    static QString sidecarPath(const QString &memoryFile) { return memoryFile + QStringLiteral(".voyagerdoc"); }

    QString comment(int step) const;                 // "" when none
    std::vector<StepComment> orphanedComments() const;
    void setComment(const QString &text, int step, const std::vector<ProgramLine> &lines);
    struct RematchReport { int kept = 0, moved = 0, orphaned = 0; };
    RematchReport rematch(const std::vector<ProgramLine> &lines);
    void prefill(const ProgramAnalysis &a);
    bool operator==(const ProgramDocument &o) const;
    bool operator!=(const ProgramDocument &o) const { return !(*this == o); }
};

struct ProgramDocumentInfo {
    QString modelName;       // "HP-15C (15.2)"
    QString firmware;        // firmware file name
    QString memoryFile;      // "FinancePac.15c" or ""
    int model = 15;
    QDate date = QDate::currentDate();
    static QString modelNameFor(int variant, int model);
    QString dateText() const;                        // "16 October 2026"
};

/// Everything the PDF and the text export show, worked out once.
struct ProgramDocumentContent {
    ProgramDocument doc;
    ProgramDocumentInfo info;
    std::vector<ProgramLine> lines;
    ProgramAnalysis analysis;
    std::optional<Pool15> pool;
    std::vector<int> setFlags;

    ProgramDocumentContent(ProgramDocument d, ProgramDocumentInfo i, std::vector<ProgramLine> l,
                           std::optional<Pool15> p = std::nullopt, std::vector<int> flags = {});

    struct LabelRow { QString label, notes, defined, called; };
    struct RegisterRow { QString reg, name, use, steps; };
    struct ListingRow { QString step, screen, keys, comment; bool isLabel; };
    struct MatrixOpRow { QString item, operations, steps, notes; };

    QString title() const;
    std::vector<std::pair<QString, QString>> memoryRows() const;
    std::vector<MatrixOpRow> matrixOpRows() const;
    std::vector<LabelRow> labelRows() const;
    std::vector<RegisterRow> registerRows() const;
    std::vector<RegisterRow> flagRows() const;
    std::vector<ListingRow> listingRows() const;
    QString plainText() const;
    QString steps(const std::vector<int> &s) const;
};

}  // namespace vc
