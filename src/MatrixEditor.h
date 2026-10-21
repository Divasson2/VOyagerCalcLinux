// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
//
// MatrixEditor.h - the HP-15C Matrix Editor window (Calculator ▸ Matrix Editor…, 0.7.1): pick matrix A–E, set
// rows × columns with a free-memory check (the flyout's pool bar, as it would be after the DIM), edit the values in
// a spreadsheet-like grid (Tab / Enter move on; "1,5", "1.5" and "2E-3" are all accepted; real numbers only), open
// a matrix that is in the emulator's memory, paste a tab-separated range from Excel / LibreOffice / Google Sheets
// (size worked out, previewed), copy as TSV, and Apply: the matrix goes into the emulator's memory with the
// firmware's DIM bookkeeping, so Write to Calculator sends it over USB like any other memory.
#pragma once
#include "Matrix15.h"
#include <QDialog>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <functional>
#include <optional>

class QComboBox;
class QSpinBox;
class QLabel;
class QPushButton;

namespace vc {

/// The grid: Enter moves down (then to the top of the next column), Tab moves right (Qt's own), Ctrl+V pastes a range.
class MatrixGrid : public QTableWidget {
    Q_OBJECT
public:
    explicit MatrixGrid(QWidget *parent = nullptr);
    void moveAfterEnter(bool back);
signals:
    void pasteRequested();
protected:
    void keyPressEvent(QKeyEvent *e) override;
};

class MatrixEditor : public QDialog {
    Q_OBJECT
public:
    struct Hooks {
        /// The emulator's current memory image and the running 15C variant (150 / 152); nullopt when there is none.
        std::function<std::optional<std::pair<QByteArray, int>>()> fetch;
        /// Write the matrix into the emulator's memory (on the engine thread); done("") or done(error).
        std::function<void(int m, int rows, int cols, std::vector<uint64_t> raw, std::function<void(const QString &)> done)> apply;
        /// The clipboard text (tests replace it) and the paste preview (true = fill the grid; tests answer it).
        std::function<QString()> clipboardText;
        std::function<bool(const matrix15::Table &, const QStringList &bad)> confirmPaste;
        std::function<void(const QString &)> copyText;
    };
    explicit MatrixEditor(Hooks hooks, QWidget *parent = nullptr);

    // what the window shows (also for tests)
    int matrix() const;
    int rows() const;
    int cols() const;
    void setMatrix(int m);                  // and open it from memory
    void setSize(int rows, int cols);
    void setCell(int r, int c, const QString &text);
    QString cell(int r, int c) const;
    MatrixGrid *grid() const { return grid_; }
    QString status() const;
    bool fits() const { return fits_; }
    int available() const { return room_; }
    const std::optional<Pool15> &previewPool() const { return preview_; }

    void reload();                          // fetch the memory again and open the selected matrix
    void pasteMatrix();
    QString tsv() const;                    // the grid as Copy as TSV puts it on the clipboard
    void copyTsv();
    /// Validate and write. Returns false (and says why in the status line) when nothing was written.
    bool applyNow();
    /// Called by the window when the calculator is not a 15C any more (or again): the editor is disabled meanwhile.
    void setAvailable(bool on, const QString &why = QString());

signals:
    void applied(int m);

private:
    void fillFrom(const matrix15::Stored &s);
    void fillFromTable(const matrix15::Table &t);
    void resizeGrid(int rows, int cols);
    void updateMemory();
    void markCell(QTableWidgetItem *it);
    void setStatus(const QString &t, bool error);
    bool collect(std::vector<uint64_t> *raw, QString *why);

    Hooks hooks_;
    QComboBox *letter_;
    QSpinBox *rows_, *cols_;
    QPushButton *open_, *paste_, *copy_, *apply_;
    MatrixGrid *grid_;
    QWidget *bar_;
    QLabel *memory_, *status_;
    QByteArray image_;
    int variant_ = 150, room_ = 0;
    bool fits_ = true, updating_ = false, available_ = true;
    std::optional<Pool15> preview_;
};

}  // namespace vc
