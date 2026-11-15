// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
//
// PasteProgramDialog.cpp - see PasteProgramDialog.h.
#include "PasteProgramDialog.h"
#include "ProgramEntry.h"
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace vc {

namespace {
QColor errorColour() { return QApplication::palette().color(QPalette::Window).lightness() < 128 ? QColor(0xFF8A80) : QColor(0xB3261E); }
QColor okColour() { return QApplication::palette().color(QPalette::Window).lightness() < 128 ? QColor(0x81C995) : QColor(0x1B7A3C); }
QColor warnColour() { return QApplication::palette().color(QPalette::Window).lightness() < 128 ? QColor(0xF2C14E) : QColor(0x8A5A00); }
}

PasteProgramDialog::PasteProgramDialog(int model, std::vector<ProgStep> existing, int after, QWidget *parent)
    : QDialog(parent), model_(model), existing_(std::move(existing)) {
    const StepCatalog *cat = StepCatalog::forModel(model);
    setWindowTitle(QStringLiteral("Paste Program Text — %1").arg(cat ? cat->modelName() : QString()));
    resize(860, 640);
    auto *col = new QVBoxLayout(this);
    auto *help = new QLabel(QStringLiteral(
        "Paste or type a program listing: our copied steps, “Export Listing as Text”, HP-15C Simulator files, step numbers with keycodes "
        "(<tt>001-42,21,11</tt>, <tt>12 {&nbsp;43 22 A&nbsp;}</tt>) or key names only (<tt>LBL A</tt>, <tt>STO 1</tt>, <tt>x&lt;&gt;y</tt>). "
        "Text after <tt>;</tt> is a step comment. Several files can be added one after the other."), this);
    help->setWordWrap(true);
    col->addWidget(help);

    auto *split = new QSplitter(Qt::Vertical, this);
    edit_ = new QPlainTextEdit(split);
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    edit_->setFont(mono);
    edit_->setLineWrapMode(QPlainTextEdit::NoWrap);
    edit_->setPlaceholderText(QStringLiteral("001-42,21,11  f LBL A\n002-44 1      STO 1   ; radius\n…"));
    table_ = new QTableWidget(split);
    table_->setColumnCount(5);
    table_->setHorizontalHeaderLabels({QStringLiteral("Line"), QStringLiteral("Step"), QStringLiteral("Keycodes"), QStringLiteral("Key names"), QStringLiteral("Check")});
    table_->verticalHeader()->setVisible(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionMode(QAbstractItemView::NoSelection);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setColumnWidth(0, 46); table_->setColumnWidth(1, 46); table_->setColumnWidth(2, 110); table_->setColumnWidth(3, 150);
    split->addWidget(edit_); split->addWidget(table_);
    split->setStretchFactor(0, 1); split->setStretchFactor(1, 1);
    col->addWidget(split, 1);

    auto *tools = new QHBoxLayout;
    auto *fromClip = new QPushButton(QStringLiteral("Paste from Clipboard"), this);
    auto *addFiles = new QPushButton(QStringLiteral("Add File(s)…"), this);
    auto *clear = new QPushButton(QStringLiteral("Clear"), this);
    tools->addWidget(fromClip); tools->addWidget(addFiles); tools->addWidget(clear); tools->addStretch();
    tools->addWidget(new QLabel(QStringLiteral("Insert after step"), this));
    after_ = new QSpinBox(this);
    after_->setRange(0, int(existing_.size()));
    after_->setValue(std::clamp(after, 0, int(existing_.size())));
    after_->setSpecialValueText(QStringLiteral("000 (top)"));
    after_->setToolTip(QStringLiteral("The pasted steps go after this step, as when keying them in on the calculator. %1 = after the last step.").arg(existing_.size()));
    tools->addWidget(after_);
    col->addLayout(tools);

    summary_ = new QLabel(this); summary_->setWordWrap(true);
    labels_ = new QLabel(this); labels_->setWordWrap(true);
    letters_ = new QCheckBox(QStringLiteral("Also renumber clashing letter labels (%1) — this changes which key starts the pasted program").arg(model == 15 ? QStringLiteral("A–E") : QStringLiteral("A–F")), this);
    letters_->setVisible(false);
    memory_ = new QLabel(this); memory_->setWordWrap(true);
    col->addWidget(summary_); col->addWidget(labels_); col->addWidget(letters_); col->addWidget(memory_);

    auto *buttons = new QDialogButtonBox(this);
    insert_ = buttons->addButton(QStringLiteral("Insert"), QDialogButtonBox::AcceptRole);
    buttons->addButton(QDialogButtonBox::Cancel);
    insert_->setDefault(true);
    col->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] { if (canInsert()) accept(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    timer_ = new QTimer(this); timer_->setSingleShot(true); timer_->setInterval(200);
    connect(timer_, &QTimer::timeout, this, &PasteProgramDialog::refresh);
    connect(edit_, &QPlainTextEdit::textChanged, this, [this] { if (!filling_) timer_->start(); });
    connect(after_, qOverload<int>(&QSpinBox::valueChanged), this, [this] { refresh(); });
    connect(letters_, &QCheckBox::toggled, this, [this] { refresh(); });
    connect(fromClip, &QPushButton::clicked, this, [this] {
        const QString t = QApplication::clipboard()->text();
        if (t.isEmpty()) return;
        QString cur = edit_->toPlainText();
        if (!cur.isEmpty() && !cur.endsWith(QLatin1Char('\n'))) cur += QLatin1Char('\n');
        setText(cur + t);
    });
    connect(addFiles, &QPushButton::clicked, this, [this] {
        const QStringList files = QFileDialog::getOpenFileNames(this, QStringLiteral("Add Program Files"), QString(),
                                                                QStringLiteral("Program listings (*.txt *.15c *.15C *.16c *.prg);;All files (*)"));
        if (!files.isEmpty()) this->addFiles(files);
    });
    connect(clear, &QPushButton::clicked, this, [this] { setText(QString()); });
    refresh();
}

void PasteProgramDialog::setText(const QString &t) { filling_ = true; edit_->setPlainText(t); filling_ = false; choices_.clear(); refresh(); }
QString PasteProgramDialog::text() const { return edit_->toPlainText(); }
void PasteProgramDialog::setRenumberLetters(bool on) { letters_->setChecked(on); refresh(); }
void PasteProgramDialog::setAfter(int step) { after_->setValue(step); refresh(); }
int PasteProgramDialog::after() const { return after_->value(); }
std::vector<ProgStep> PasteProgramDialog::steps() const { return renumbered_; }
std::vector<ProgStep> PasteProgramDialog::newProgram() const { return insertSteps(existing_, after(), renumbered_); }

void PasteProgramDialog::addFiles(const QStringList &paths) {
    QString cur = edit_->toPlainText();
    for (const QString &p : paths) {
        QFile f(p);
        if (!f.open(QIODevice::ReadOnly)) continue;
        if (!cur.isEmpty() && !cur.endsWith(QLatin1Char('\n'))) cur += QLatin1Char('\n');
        cur += QStringLiteral("# file: %1\n").arg(QFileInfo(p).fileName()) + decodeProgramFile(f.readAll());
    }
    setText(cur);
}

void PasteProgramDialog::choose(int line, int choice) {
    for (const ParsedLine &l : parse_.lines)
        if (l.line == line && choice >= 0 && choice < int(l.choices.size())) { choices_[line] = l.choices[size_t(choice)].keys; refresh(); return; }
}

bool PasteProgramDialog::canInsert() const { return parse_.ok() && fit_.fits; }

QString PasteProgramDialog::statusText() const {
    return QStringList{summary_->text(), labels_->text(), memory_->text()}.join(QLatin1Char('\n'));
}

void PasteProgramDialog::refresh() {
    timer_->stop();
    parse_ = parseProgramText(edit_->toPlainText(), model_, choices_);
    plan_ = planRenumber(model_, existing_, parse_.steps, parse_.stepBlocks, letters_->isChecked());
    renumbered_ = applyRenumber(model_, parse_.steps, parse_.stepBlocks, plan_);
    fit_ = FitReport();
    if (fitCheck && !parse_.steps.empty()) fit_ = fitCheck(newProgram());
    rebuildTable();
    // summary
    QString s;
    if (!parse_.modelError.isEmpty()) s = parse_.modelError;
    else if (parse_.steps.empty() && !parse_.errors && !parse_.ambiguous) s = QStringLiteral("No program steps yet.");
    else {
        s = parse_.summary();
        if (parse_.errors) s += QStringLiteral(" — correct the lines marked in red.");
        else if (parse_.ambiguous) s += QStringLiteral(" — choose the step for the lines marked in orange.");
        else s += QStringLiteral(", inserted after step %1.").arg(after(), 3, 10, QLatin1Char('0'));
    }
    summary_->setText(s);
    QPalette sp = summary_->palette();
    sp.setColor(QPalette::WindowText, (!parse_.modelError.isEmpty() || parse_.errors) ? errorColour() : parse_.ambiguous ? warnColour() : QApplication::palette().color(QPalette::WindowText));
    summary_->setPalette(sp);
    const QStringList d = plan_.describe();
    labels_->setText(d.join(QLatin1Char('\n')));
    labels_->setVisible(!d.isEmpty());
    letters_->setVisible(plan_.anyLetterClash() || letters_->isChecked());
    QString m;
    if (!fit_.fits) m = fit_.message;
    else if (!fit_.lostRegisters.isEmpty()) m = fit_.message;
    else if (fitCheck && !parse_.steps.empty())
        m = model_ == 15 ? QStringLiteral("Memory: the program will take %1 of %2 bytes.").arg(fit_.need).arg(fit_.available)
                         : QStringLiteral("Memory: %1 lines; %2 registers left at this word size.").arg(fit_.need).arg(fit_.registersAfter);
    memory_->setText(m);
    QPalette mp = memory_->palette();
    mp.setColor(QPalette::WindowText, !fit_.fits ? errorColour() : !fit_.lostRegisters.isEmpty() ? warnColour() : okColour());
    memory_->setPalette(mp);
    memory_->setVisible(!m.isEmpty());
    insert_->setEnabled(canInsert());
    insert_->setText(fit_.lostRegisters.isEmpty() ? QStringLiteral("Insert") : QStringLiteral("Insert (registers will be lost)"));
}

void PasteProgramDialog::rebuildTable() {
    const StepCatalog *cat = StepCatalog::forModel(model_);
    std::vector<const ParsedLine *> rows;
    for (const ParsedLine &l : parse_.lines) if (l.kind == ParsedLine::Step || l.kind == ParsedLine::Error || l.kind == ParsedLine::Ambiguous) rows.push_back(&l);
    table_->clearContents();
    table_->setRowCount(int(rows.size()));
    int stepNo = 0;
    size_t stepIndex = 0;
    for (int r = 0; r < int(rows.size()); ++r) {
        const ParsedLine &l = *rows[size_t(r)];
        auto item = [&](int c, const QString &t, QColor colour = QColor()) {
            auto *it = new QTableWidgetItem(t);
            if (colour.isValid()) it->setForeground(colour);
            table_->setItem(r, c, it);
        };
        item(0, QString::number(l.line));
        if (l.kind == ParsedLine::Step) {
            const ProgStep shown = stepIndex < renumbered_.size() ? renumbered_[stepIndex] : l.step;
            ++stepIndex;
            item(1, QStringLiteral("%1").arg(after() + ++stepNo, 3, 10, QLatin1Char('0')));
            item(2, cat ? cat->displayKeys(shown) : shown.keys);
            QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
            table_->item(r, 2)->setFont(mono);
            item(3, cat ? cat->mnemonic(shown) : QString());
            QString note = shown.keys != l.step.keys ? QStringLiteral("label renumbered") : QStringLiteral("ok");
            if (!shown.comment.isEmpty()) note += QStringLiteral(" · ; ") + shown.comment;
            item(4, note, shown.keys != l.step.keys ? warnColour() : okColour());
        } else if (l.kind == ParsedLine::Error) {
            item(2, l.text.trimmed(), errorColour());
            item(4, l.message, errorColour());
        } else {
            item(2, l.text.trimmed(), warnColour());
            auto *box = new QComboBox(table_);
            box->addItem(QStringLiteral("Choose…"));
            for (const ProgStep &c : l.choices) box->addItem(QStringLiteral("%1   {%2}").arg(cat ? cat->mnemonic(c) : QString(), c.keys));
            const int line = l.line;
            connect(box, qOverload<int>(&QComboBox::activated), this, [this, line](int i) { if (i > 0) QTimer::singleShot(0, this, [this, line, i] { choose(line, i - 1); }); });
            table_->setCellWidget(r, 4, box);
            box->setToolTip(l.message);
        }
    }
}

}  // namespace vc
