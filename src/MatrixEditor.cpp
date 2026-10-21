// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
#include "MatrixEditor.h"
#include "InspectorPanel.h"
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QShortcut>
#include <QSpinBox>
#include <QVBoxLayout>

namespace vc {

namespace {

/// Enter inside an open cell editor: commit, close, move on (like a spreadsheet).
class CellDelegate : public QStyledItemDelegate {
public:
    CellDelegate(MatrixGrid *g) : QStyledItemDelegate(g), grid_(g) {}
    bool eventFilter(QObject *o, QEvent *e) override {
        if (e->type() == QEvent::KeyPress) {
            auto *k = static_cast<QKeyEvent *>(e);
            if (k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) {
                auto *w = qobject_cast<QWidget *>(o);
                emit commitData(w);
                emit closeEditor(w, QAbstractItemDelegate::NoHint);
                grid_->moveAfterEnter(k->modifiers() & Qt::ShiftModifier);
                return true;
            }
        }
        return QStyledItemDelegate::eventFilter(o, e);
    }
private:
    MatrixGrid *grid_;
};

class PoolBar : public QWidget {
public:
    explicit PoolBar(const std::optional<Pool15> *pool, QWidget *parent = nullptr) : QWidget(parent), pool_(pool) { setMinimumHeight(74); }
protected:
    void paintEvent(QPaintEvent *) override {
        if (!*pool_) return;
        QPainter p(this);
        paintPool15Bar(&p, rect(), **pool_, font());
    }
private:
    const std::optional<Pool15> *pool_;
};

}  // namespace

MatrixGrid::MatrixGrid(QWidget *parent) : QTableWidget(parent) {
    setItemDelegate(new CellDelegate(this));
    setTabKeyNavigation(true);
    setEditTriggers(QAbstractItemView::AnyKeyPressed | QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed | QAbstractItemView::SelectedClicked);
    setSelectionMode(QAbstractItemView::ContiguousSelection);
    horizontalHeader()->setDefaultSectionSize(92);
    verticalHeader()->setDefaultSectionSize(24);
}

void MatrixGrid::moveAfterEnter(bool back) {
    int r = currentRow(), c = currentColumn();
    if (rowCount() == 0 || columnCount() == 0) return;
    if (!back) {
        if (++r >= rowCount()) { r = 0; if (++c >= columnCount()) c = 0; }
    } else {
        if (--r < 0) { r = rowCount() - 1; if (--c < 0) c = columnCount() - 1; }
    }
    setCurrentCell(r, c);
}

void MatrixGrid::keyPressEvent(QKeyEvent *e) {
    if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && state() != QAbstractItemView::EditingState) {
        moveAfterEnter(e->modifiers() & Qt::ShiftModifier);
        return;
    }
    if (e->matches(QKeySequence::Paste)) { emit pasteRequested(); return; }
    if (e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) {
        if (state() != QAbstractItemView::EditingState) { for (auto *it : selectedItems()) it->setText(QString()); return; }
    }
    QTableWidget::keyPressEvent(e);
}

MatrixEditor::MatrixEditor(Hooks hooks, QWidget *parent) : QDialog(parent), hooks_(std::move(hooks)) {
    setWindowTitle(QStringLiteral("HP-15C Matrix Editor"));
    setAttribute(Qt::WA_DeleteOnClose, false);
    if (!hooks_.clipboardText) hooks_.clipboardText = [] { return QApplication::clipboard()->text(); };
    if (!hooks_.copyText) hooks_.copyText = [](const QString &t) { QApplication::clipboard()->setText(t); };

    letter_ = new QComboBox;
    for (int m = 0; m < 5; ++m) letter_->addItem(QStringLiteral("Matrix %1").arg(QChar('A' + m)));
    rows_ = new QSpinBox; cols_ = new QSpinBox;
    for (QSpinBox *s : {rows_, cols_}) { s->setRange(0, matrix15::kMaxSide); s->setAccelerated(true); }
    open_ = new QPushButton(QStringLiteral("Reload from Memory"));
    open_->setToolTip(QStringLiteral("Show the selected matrix as it is in the emulator's memory now"));
    auto *top = new QHBoxLayout;
    top->addWidget(letter_);
    top->addSpacing(12);
    top->addWidget(new QLabel(QStringLiteral("Rows")));
    top->addWidget(rows_);
    top->addWidget(new QLabel(QStringLiteral("×  Columns")));
    top->addWidget(cols_);
    top->addStretch(1);
    top->addWidget(open_);

    bar_ = new PoolBar(&preview_);
    memory_ = new QLabel; memory_->setWordWrap(true);
    grid_ = new MatrixGrid;
    grid_->setMinimumSize(420, 220);

    paste_ = new QPushButton(QStringLiteral("Paste Matrix"));
    paste_->setToolTip(QStringLiteral("Fill the grid from a range copied in Excel, LibreOffice Calc or Google Sheets (tab-separated); the size is taken from the range"));
    copy_ = new QPushButton(QStringLiteral("Copy as TSV"));
    copy_->setToolTip(QStringLiteral("Copy the grid for a spreadsheet (tab-separated, the calculator's own digits)"));
    apply_ = new QPushButton(QStringLiteral("Apply"));
    apply_->setDefault(false); apply_->setAutoDefault(false);
    apply_->setToolTip(QStringLiteral("Put this matrix into the emulator's memory (then Real Calculator (USB) ▸ Write to Calculator… sends it)"));
    auto *close = new QPushButton(QStringLiteral("Close"));
    close->setAutoDefault(false);
    for (QPushButton *b : {open_, paste_, copy_}) b->setAutoDefault(false);
    status_ = new QLabel; status_->setWordWrap(true);
    auto *bottom = new QHBoxLayout;
    bottom->addWidget(paste_); bottom->addWidget(copy_); bottom->addStretch(1); bottom->addWidget(apply_); bottom->addWidget(close);

    auto *v = new QVBoxLayout(this);
    v->addLayout(top);
    v->addWidget(bar_);
    v->addWidget(memory_);
    v->addWidget(grid_, 1);
    v->addWidget(status_);
    v->addLayout(bottom);
    resize(620, 520);

    connect(letter_, &QComboBox::currentIndexChanged, this, [this] { if (!updating_) reload(); });
    auto sizeChanged = [this] { if (!updating_) { resizeGrid(rows_->value(), cols_->value()); updateMemory(); } };
    connect(rows_, &QSpinBox::valueChanged, this, sizeChanged);
    connect(cols_, &QSpinBox::valueChanged, this, sizeChanged);
    connect(open_, &QPushButton::clicked, this, &MatrixEditor::reload);
    connect(paste_, &QPushButton::clicked, this, &MatrixEditor::pasteMatrix);
    connect(grid_, &MatrixGrid::pasteRequested, this, &MatrixEditor::pasteMatrix);
    connect(copy_, &QPushButton::clicked, this, &MatrixEditor::copyTsv);
    connect(apply_, &QPushButton::clicked, this, [this] { applyNow(); });
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    connect(grid_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *it) { if (!updating_) markCell(it); });
    auto *sc = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V), this);
    connect(sc, &QShortcut::activated, this, &MatrixEditor::pasteMatrix);
    auto *ap = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), this);
    connect(ap, &QShortcut::activated, this, [this] { applyNow(); });
    reload();
}

int MatrixEditor::matrix() const { return letter_->currentIndex(); }
int MatrixEditor::rows() const { return rows_->value(); }
int MatrixEditor::cols() const { return cols_->value(); }
QString MatrixEditor::status() const { return status_->text(); }

void MatrixEditor::setMatrix(int m) { letter_->setCurrentIndex(std::clamp(m, 0, 4)); }
void MatrixEditor::setSize(int r, int c) {
    updating_ = true; rows_->setValue(r); cols_->setValue(c); updating_ = false;
    resizeGrid(r, c); updateMemory();
}
void MatrixEditor::setCell(int r, int c, const QString &text) {
    QTableWidgetItem *it = grid_->item(r, c);
    if (!it) { it = new QTableWidgetItem; grid_->setItem(r, c, it); }
    it->setText(text);
}
QString MatrixEditor::cell(int r, int c) const { QTableWidgetItem *it = grid_->item(r, c); return it ? it->text() : QString(); }

void MatrixEditor::setStatus(const QString &t, bool error) {
    status_->setText(t);
    status_->setStyleSheet(error ? QStringLiteral("color: #C0392B;") : QString());
}

void MatrixEditor::setAvailable(bool on, const QString &why) {
    available_ = on;
    for (QWidget *w : std::initializer_list<QWidget *>{letter_, rows_, cols_, open_, paste_, apply_, grid_}) w->setEnabled(on);
    if (!on) setStatus(why.isEmpty() ? QStringLiteral("The Matrix Editor needs the HP-15C (switch the model back to the 15C).") : why, true);
    else reload();
}

void MatrixEditor::reload() {
    image_.clear();
    auto got = hooks_.fetch ? hooks_.fetch() : std::nullopt;
    if (!got) { setStatus(QStringLiteral("The emulator's memory could not be read (is the calculator running as an HP-15C?)."), true); return; }
    image_ = got->first; variant_ = got->second;
    matrix15::Stored s;
    if (!matrix15::read(image_, variant_, matrix(), &s)) { setStatus(QStringLiteral("This memory is not an HP-15C memory."), true); return; }
    fillFrom(s);
    setStatus(s.rows * s.cols ? QStringLiteral("Matrix %1 (%2 × %3) from the emulator's memory.").arg(QChar('A' + matrix())).arg(s.rows).arg(s.cols)
                              : QStringLiteral("Matrix %1 is not dimensioned (0 × 0). Set rows and columns, type or paste the values, then Apply.").arg(QChar('A' + matrix())), false);
}

void MatrixEditor::fillFrom(const matrix15::Stored &s) {
    updating_ = true;
    rows_->setValue(s.rows); cols_->setValue(s.cols);
    grid_->clearContents();
    grid_->setRowCount(s.rows); grid_->setColumnCount(s.cols);
    for (int r = 0; r < s.rows; ++r)
        for (int c = 0; c < s.cols; ++c) {
            auto *it = new QTableWidgetItem(matrix15::cellText(s.raw[size_t(r * s.cols + c)]));
            it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            grid_->setItem(r, c, it);
        }
    updating_ = false;
    if (s.rows && s.cols) grid_->setCurrentCell(0, 0);
    updateMemory();
}

void MatrixEditor::fillFromTable(const matrix15::Table &t) {
    updating_ = true;
    rows_->setValue(t.rows); cols_->setValue(t.cols);
    grid_->clearContents();
    grid_->setRowCount(t.rows); grid_->setColumnCount(t.cols);
    for (int r = 0; r < t.rows; ++r)
        for (int c = 0; c < t.cols; ++c) {
            auto *it = new QTableWidgetItem(t.cell(r, c));
            it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            grid_->setItem(r, c, it);
        }
    updating_ = false;
    for (int r = 0; r < t.rows; ++r) for (int c = 0; c < t.cols; ++c) markCell(grid_->item(r, c));
    if (t.rows && t.cols) grid_->setCurrentCell(0, 0);
    updateMemory();
}

void MatrixEditor::resizeGrid(int r, int c) {
    // like a spreadsheet: cells keep their row and column; new ones are empty (= 0)
    updating_ = true;
    grid_->setRowCount(r); grid_->setColumnCount(c);
    for (int i = 0; i < r; ++i)
        for (int j = 0; j < c; ++j)
            if (!grid_->item(i, j)) { auto *it = new QTableWidgetItem; it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter); grid_->setItem(i, j, it); }
    updating_ = false;
}

void MatrixEditor::markCell(QTableWidgetItem *it) {
    if (!it) return;
    uint64_t v; QString why;
    const bool ok = matrix15::parseCell(it->text(), &v, &why);
    it->setBackground(ok ? QBrush() : QBrush(QColor(0xF5, 0xB7, 0xB1)));
    it->setForeground(ok ? QBrush() : QBrush(QColor(0x1A, 0x1A, 0x1A)));
    it->setToolTip(ok ? QString() : QStringLiteral("“%1” is %2").arg(it->text(), why));
    it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
}

void MatrixEditor::updateMemory() {
    if (image_.isEmpty()) { preview_.reset(); bar_->update(); return; }
    const int m = matrix(), r = rows(), c = cols();
    room_ = matrix15::room(image_, variant_, m);
    fits_ = r * c <= room_;
    preview_ = fits_ ? matrix15::poolAfter(image_, variant_, m, r, c) : matrix15::pool(image_, variant_);
    const auto now = matrix15::pool(image_, variant_);
    const QString limit = QStringLiteral("g MEM now %1").arg(now ? now->gmem : QStringLiteral("?"));
    if (fits_)
        memory_->setText(QStringLiteral("%1 × %2 = %3 registers; matrix %4 can have up to %5 here (%6). The bar shows the memory after Apply.")
                         .arg(r).arg(c).arg(r * c).arg(QChar('A' + m)).arg(room_).arg(limit));
    else
        memory_->setText(QStringLiteral("<span style='color:#C0392B'>%1 × %2 = %3 registers does not fit: only %4 are free for matrix %5 "
                                        "(the calculator would show Error 10).</span>").arg(r).arg(c).arg(r * c).arg(room_).arg(QChar('A' + m)));
    apply_->setEnabled(available_ && fits_);
    bar_->update();
}

bool MatrixEditor::collect(std::vector<uint64_t> *raw, QString *why) {
    raw->clear();
    for (int r = 0; r < rows(); ++r)
        for (int c = 0; c < cols(); ++c) {
            uint64_t v; QString w;
            if (!matrix15::parseCell(cell(r, c), &v, &w)) {
                grid_->setCurrentCell(r, c);
                *why = QStringLiteral("Row %1, column %2: “%3” is %4. Use numbers like 1.5, 1,5 or 2E-3.").arg(r + 1).arg(c + 1).arg(cell(r, c), w);
                return false;
            }
            raw->push_back(v);
        }
    return true;
}

bool MatrixEditor::applyNow() {
    if (!available_) return false;
    std::vector<uint64_t> raw; QString why;
    if (!collect(&raw, &why)) { setStatus(why, true); return false; }
    // check against the memory as it is now (the calculator may have run since the window opened)
    if (auto got = hooks_.fetch ? hooks_.fetch() : std::nullopt) { image_ = got->first; variant_ = got->second; }
    QByteArray test = image_;
    const int m = matrix(), r = rows(), c = cols();
    QString err = matrix15::write(&test, variant_, m, r, c, raw);
    if (!err.isEmpty()) { setStatus(err, true); updateMemory(); return false; }
    if (!hooks_.apply) return false;
    setStatus(QStringLiteral("Writing…"), false);
    hooks_.apply(m, r, c, raw, [this, m, r, c](const QString &e) {
        if (!e.isEmpty()) { setStatus(e, true); return; }
        if (auto got = hooks_.fetch ? hooks_.fetch() : std::nullopt) { image_ = got->first; variant_ = got->second; }
        updateMemory();
        setStatus(r * c ? QStringLiteral("Matrix %1 (%2 × %3) is now in the emulator's memory. To send it to the real calculator: "
                                         "Calculator ▸ Real Calculator (USB) ▸ Write to Calculator…").arg(QChar('A' + m)).arg(r).arg(c)
                        : QStringLiteral("Matrix %1 was set to 0 × 0 (its registers are free again).").arg(QChar('A' + m)), false);
        emit applied(m);
    });
    return true;
}

QString MatrixEditor::tsv() const {
    QStringList lines;
    for (int r = 0; r < rows(); ++r) {
        QStringList cells;
        for (int c = 0; c < cols(); ++c) {
            uint64_t v; QString t = cell(r, c);
            cells << (matrix15::parseCell(t, &v) ? matrix15::cellText(v) : t);
        }
        lines << cells.join(QLatin1Char('\t'));
    }
    return lines.join(QLatin1Char('\n'));
}

void MatrixEditor::copyTsv() {
    hooks_.copyText(tsv());
    setStatus(QStringLiteral("Copied %1 × %2 cells (tab-separated): paste them into a spreadsheet.").arg(rows()).arg(cols()), false);
}

void MatrixEditor::pasteMatrix() {
    if (!available_) return;
    const matrix15::Table t = matrix15::parseTable(hooks_.clipboardText());
    if (!t.ok()) { setStatus(t.error.isEmpty() ? QStringLiteral("The clipboard holds no cells.") : t.error, true); return; }
    const QStringList bad = matrix15::badCells(t);
    bool go;
    if (hooks_.confirmPaste) go = hooks_.confirmPaste(t, bad);
    else {
        QDialog d(this);
        d.setWindowTitle(QStringLiteral("Paste Matrix"));
        auto *v = new QVBoxLayout(&d);
        QString head = QStringLiteral("<b>%1 rows × %2 columns</b> from the clipboard, for matrix %3.").arg(t.rows).arg(t.cols).arg(QChar('A' + matrix()));
        if (t.ragged) head += QStringLiteral("<br>The rows have different lengths: the missing cells become 0.");
        const int need = t.rows * t.cols, avail = image_.isEmpty() ? -1 : matrix15::room(image_, variant_, matrix());
        if (avail >= 0 && need > avail) head += QStringLiteral("<br><span style='color:#C0392B'>%1 registers needed, only %2 free for this matrix: Apply will refuse it until it is smaller.</span>").arg(need).arg(avail);
        if (!bad.isEmpty()) head += QStringLiteral("<br><span style='color:#C0392B'>%1 cell(s) are not real numbers (marked red; fix them before Apply): %2</span>")
                                        .arg(bad.size()).arg(QStringList(bad.mid(0, 4)).join(QStringLiteral("; ")) + (bad.size() > 4 ? QStringLiteral(" …") : QString()));
        auto *lab = new QLabel(head); lab->setWordWrap(true); v->addWidget(lab);
        auto *pv = new QTableWidget(t.rows, t.cols);
        pv->setEditTriggers(QAbstractItemView::NoEditTriggers);
        for (int r = 0; r < t.rows; ++r) for (int c = 0; c < t.cols; ++c) {
            uint64_t x; auto *it = new QTableWidgetItem(t.cell(r, c));
            it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            if (!matrix15::parseCell(t.cell(r, c), &x)) it->setBackground(QColor(0xF5, 0xB7, 0xB1));
            pv->setItem(r, c, it);
        }
        pv->setMinimumSize(360, 160);
        v->addWidget(pv, 1);
        auto *bb = new QDialogButtonBox;
        bb->addButton(QStringLiteral("Fill the Grid"), QDialogButtonBox::AcceptRole);
        bb->addButton(QDialogButtonBox::Cancel);
        connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
        v->addWidget(bb);
        d.resize(520, 360);
        go = d.exec() == QDialog::Accepted;
    }
    if (!go) return;
    fillFromTable(t);
    setStatus(QStringLiteral("Pasted %1 × %2 cells into the grid%3. Apply puts them into matrix %4.")
              .arg(t.rows).arg(t.cols).arg(bad.isEmpty() ? QString() : QStringLiteral(" (%1 to fix, marked red)").arg(bad.size())).arg(QChar('A' + matrix())), !bad.isEmpty());
}

}  // namespace vc
