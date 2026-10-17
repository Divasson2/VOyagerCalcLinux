// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
//
// ProgramInfoDialog.h - File ▸ Program Info… (ProgramInfoView.swift): the program notes that go into the
// exported documentation: title, author, description ("## Usage" starts a section, "- " a bullet), a note per
// label, a name and meaning per register, a note per flag. The tables list what the program uses (detected from
// the listing); comments per step are edited in the flyout's listing (double-click a line).
#pragma once
#include "ProgramDocument.h"
#include <QDialog>

class QLineEdit;
class QPlainTextEdit;

namespace vc {

class ProgramInfoDialog : public QDialog {
    Q_OBJECT
public:
    ProgramInfoDialog(const ProgramDocument &doc, const std::vector<ProgramLine> &lines, int model, const QString &modelName,
                      const QString &notesFileName, QWidget *parent = nullptr);
    /// The notes as edited (valid after accept()).
    ProgramDocument document() const;
    enum Next { Done, SaveNotes, ExportPdf };
    Next next() const { return next_; }

private:
    ProgramDocument doc_;
    Next next_ = Done;
    QLineEdit *title_, *author_;
    QPlainTextEdit *description_;
    QMap<QString, QLineEdit *> labelEdits_, flagEdits_;
    QMap<QString, std::pair<QLineEdit *, QLineEdit *>> registerEdits_;
};

}  // namespace vc
