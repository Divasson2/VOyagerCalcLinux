// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
#include "ProgramInfoDialog.h"
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace vc {

ProgramInfoDialog::ProgramInfoDialog(const ProgramDocument &doc, const std::vector<ProgramLine> &lines, int model,
                                     const QString &modelName, const QString &notesFileName, QWidget *parent)
    : QDialog(parent), doc_(doc) {
    setWindowTitle(QStringLiteral("Program Info"));
    resize(680, 720);
    ProgramAnalysis a(lines, model);
    auto steps = [&](const std::vector<int> &s) {
        QString f = model == 12 ? QStringLiteral("%1") : QStringLiteral("%1");
        QStringList shown;
        for (size_t i = 0; i < s.size() && i < 8; ++i) shown << QStringLiteral("%1").arg(s[i], model == 12 ? 2 : 3, 10, QLatin1Char('0'));
        return shown.join(QStringLiteral(", ")) + (s.size() > 8 ? QStringLiteral(" …") : QString());
    };

    auto *outer = new QVBoxLayout(this);
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
    auto *body = new QWidget;
    auto *col = new QVBoxLayout(body);
    scroll->setWidget(body);
    outer->addWidget(scroll, 1);

    auto *g1 = new QGroupBox(QStringLiteral("Program"), body);
    auto *f1 = new QFormLayout(g1);
    title_ = new QLineEdit(doc_.title, g1); title_->setObjectName("title");
    author_ = new QLineEdit(doc_.author, g1); author_->setPlaceholderText(QStringLiteral("optional")); author_->setObjectName("author");
    f1->addRow(QStringLiteral("Title"), title_);
    f1->addRow(QStringLiteral("Author"), author_);
    f1->addRow(QStringLiteral("Model"), new QLabel(modelName, g1));
    f1->addRow(QStringLiteral("Steps"), new QLabel(QString::number(lines.size()), g1));
    col->addWidget(g1);

    auto *g2 = new QGroupBox(QStringLiteral("Description"), body);
    auto *l2 = new QVBoxLayout(g2);
    description_ = new QPlainTextEdit(doc_.description, g2);
    description_->setObjectName("description");
    description_->setMinimumHeight(130);
    l2->addWidget(description_);
    auto *hint = new QLabel(QStringLiteral("What the program does and how to use it. A line starting with ## is a section heading (for example ## Usage); a line starting with - is a bullet."), g2);
    hint->setWordWrap(true);
    QFont hf = hint->font(); hf.setPointSizeF(hf.pointSizeF() * 0.9); hint->setFont(hf);
    hint->setForegroundRole(QPalette::PlaceholderText);
    l2->addWidget(hint);
    col->addWidget(g2);

    auto info = [&](const QString &t, bool red = false) {
        auto *l = new QLabel(t); l->setWordWrap(true);
        QFont f = l->font(); f.setPointSizeF(f.pointSizeF() * 0.9); l->setFont(f);
        if (red) l->setStyleSheet("color:#c0392b"); else l->setForegroundRole(QPalette::PlaceholderText);
        return l;
    };

    if (!a.labels.empty()) {
        auto *g = new QGroupBox(model == 12 ? QStringLiteral("Branch targets") : QStringLiteral("Labels"), body);
        auto *v = new QVBoxLayout(g);
        for (const auto &l : a.labels) {
            auto *name = new QLabel(QStringLiteral("<b>%1</b>").arg((model == 12 && !l.isIndirect ? QStringLiteral("line ") + l.name : (l.isIndirect ? l.name : QStringLiteral("LBL ") + l.name)).toHtmlEscaped()));
            QString d = l.defined.empty() ? (l.isIndirect ? QStringLiteral("computed in I") : QStringLiteral("not defined")) : QStringLiteral("at ") + steps(l.defined);
            QString c = l.called.empty() ? QStringLiteral("not called") : QStringLiteral("called from ") + steps(l.called) + QStringLiteral(" (") + l.callKinds.join(QStringLiteral(", ")) + QStringLiteral(")");
            auto *edit = new QLineEdit(doc_.labels.value(l.name)); edit->setPlaceholderText(QStringLiteral("Notes"));
            labelEdits_[l.name] = edit;
            v->addWidget(name); v->addWidget(info(d + QStringLiteral(" · ") + c, l.defined.empty() && !l.isIndirect)); v->addWidget(edit);
        }
        col->addWidget(g);
    }
    if (!a.registers.empty()) {
        auto *g = new QGroupBox(QStringLiteral("Registers"), body);
        auto *v = new QVBoxLayout(g);
        for (const auto &r : a.registers) {
            v->addWidget(new QLabel(QStringLiteral("<b>%1</b>").arg(r.name.toHtmlEscaped())));
            v->addWidget(info(r.operations.join(QStringLiteral(", ")) + QStringLiteral(" — ") + steps(r.steps)));
            auto *row = new QWidget; auto *hl = new QHBoxLayout(row); hl->setContentsMargins(0, 0, 0, 0);
            auto note = doc_.registers.value(r.name);
            auto *n = new QLineEdit(note.name); n->setPlaceholderText(QStringLiteral("Name")); n->setMaximumWidth(170);
            auto *m = new QLineEdit(note.meaning); m->setPlaceholderText(QStringLiteral("Meaning"));
            hl->addWidget(n); hl->addWidget(m);
            registerEdits_[r.name] = {n, m};
            v->addWidget(row);
        }
        col->addWidget(g);
    }
    if (!a.flags.empty()) {
        auto *g = new QGroupBox(QStringLiteral("Flags"), body);
        auto *v = new QVBoxLayout(g);
        for (const auto &f : a.flags) {
            auto *row = new QWidget; auto *hl = new QHBoxLayout(row); hl->setContentsMargins(0, 0, 0, 0);
            auto *name = new QLabel(QStringLiteral("<b>Flag %1</b>").arg(f.name.toHtmlEscaped())); name->setMinimumWidth(80);
            auto *e = new QLineEdit(doc_.flags.value(f.name)); e->setPlaceholderText(QStringLiteral("Meaning"));
            flagEdits_[f.name] = e;
            hl->addWidget(name); hl->addWidget(e, 1); hl->addWidget(info(f.operations.join(QStringLiteral(", "))));
            v->addWidget(row);
        }
        col->addWidget(g);
    }
    {
        int n = 0; for (auto &c : doc_.comments) if (!c.orphaned) ++n;
        auto orphans = doc_.orphanedComments();
        auto *g = new QGroupBox(orphans.empty() ? QStringLiteral("Comments") : QStringLiteral("Comments that no longer match a step"), body);
        auto *v = new QVBoxLayout(g);
        v->addWidget(info(QStringLiteral("Comments per step: double-click a line in the flyout's program list. %1 step comment(s) so far.").arg(n)));
        for (auto &c : orphans) v->addWidget(new QLabel(QStringLiteral("was step %1 (%2): %3").arg(c.step, 3, 10, QLatin1Char('0')).arg(c.keys, c.text)));
        if (!orphans.empty()) {
            auto *rm = new QPushButton(QStringLiteral("Remove Comments That No Longer Match"), g);
            connect(rm, &QPushButton::clicked, this, [this, rm] {
                doc_.comments.erase(std::remove_if(doc_.comments.begin(), doc_.comments.end(), [](const StepComment &c) { return c.orphaned; }), doc_.comments.end());
                rm->setEnabled(false);
            });
            v->addWidget(rm);
        }
        col->addWidget(g);
    }
    col->addStretch();

    auto *bar = new QHBoxLayout;
    if (!notesFileName.isEmpty()) { auto *l = new QLabel(notesFileName); l->setForegroundRole(QPalette::PlaceholderText); bar->addWidget(l); }
    bar->addStretch();
    auto *save = new QPushButton(QStringLiteral("Save Notes…"));
    auto *pdf = new QPushButton(QStringLiteral("Export PDF…"));
    auto *done = new QPushButton(QStringLiteral("Done")); done->setDefault(true);
    bar->addWidget(save); bar->addWidget(pdf); bar->addWidget(done);
    outer->addLayout(bar);
    connect(save, &QPushButton::clicked, this, [this] { next_ = SaveNotes; accept(); });
    connect(pdf, &QPushButton::clicked, this, [this] { next_ = ExportPdf; accept(); });
    connect(done, &QPushButton::clicked, this, &QDialog::accept);
}

ProgramDocument ProgramInfoDialog::document() const {
    ProgramDocument d = doc_;
    d.title = title_->text(); d.author = author_->text(); d.description = description_->toPlainText();
    for (auto it = labelEdits_.begin(); it != labelEdits_.end(); ++it) d.labels[it.key()] = it.value()->text();
    for (auto it = flagEdits_.begin(); it != flagEdits_.end(); ++it) d.flags[it.key()] = it.value()->text();
    for (auto it = registerEdits_.begin(); it != registerEdits_.end(); ++it) d.registers[it.key()] = RegisterNote{it.value().first->text(), it.value().second->text()};
    return d;
}

}  // namespace vc
