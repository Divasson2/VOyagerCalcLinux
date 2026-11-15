// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
//
// PasteProgramDialog.h - Edit ▸ Paste Program Text… (0.7.8; port: PasteProgramView.swift). A text box for one or more
// program listings (typed, pasted, or added from .txt / .15c files), a live line-by-line check (step, keycodes, key
// names, and what is wrong with a line), a choice for key names that fit more than one step, the label clashes with the
// program already in the calculator (clashing digit labels are renumbered; letter labels only when asked), the memory
// check, and where to insert. Nothing is keyed into the calculator here: the window returns the steps.
#pragma once
#include "ProgramText.h"
#include <QDialog>
#include <functional>

class QCheckBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTimer;

namespace vc {

class PasteProgramDialog : public QDialog {
    Q_OBJECT
public:
    /// `existing`: the program in the calculator; `after`: insert after this step (0 = at the top).
    PasteProgramDialog(int model, std::vector<ProgStep> existing, int after, QWidget *parent = nullptr);
    void setText(const QString &t);
    QString text() const;
    void addFiles(const QStringList &paths);
    /// The memory check for the whole new program (set by the window; empty: no check).
    std::function<FitReport(const std::vector<ProgStep> &program)> fitCheck;
    /// Re-read the text now (it is also re-read shortly after each change).
    void refresh();
    void choose(int line, int choice);               // an ambiguous line: take choice n
    void setRenumberLetters(bool on);
    void setAfter(int step);

    int after() const;
    std::vector<ProgStep> steps() const;             // the pasted steps, labels renumbered
    std::vector<ProgStep> newProgram() const;        // the whole program after the insert
    const ProgramParse &parse() const { return parse_; }
    const RenumberPlan &plan() const { return plan_; }
    const FitReport &fit() const { return fit_; }
    bool canInsert() const;
    // for tests and screenshots
    QPlainTextEdit *editor() const { return edit_; }
    QTableWidget *table() const { return table_; }
    QPushButton *insertButton() const { return insert_; }
    QString statusText() const;

private:
    void rebuildTable();
    int model_;
    std::vector<ProgStep> existing_;
    ProgramParse parse_;
    RenumberPlan plan_;
    FitReport fit_;
    std::vector<ProgStep> renumbered_;
    QMap<int, QString> choices_;
    QPlainTextEdit *edit_;
    QTableWidget *table_;
    QSpinBox *after_;
    QCheckBox *letters_;
    QLabel *summary_, *labels_, *memory_;
    QPushButton *insert_;
    QTimer *timer_;
    bool filling_ = false;
};

}  // namespace vc
