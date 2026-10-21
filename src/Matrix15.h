// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
//
// Matrix15.h - the logic behind the HP-15C Matrix Editor (0.7.1), without widgets (unit-tested in tst_matrix):
// number cells as people type or paste them ("1,5", "1.5", "2E-3", "1.234,5", "1,234.5"), tab-separated
// clipboard ranges from Excel / LibreOffice / Google Sheets, and a matrix read from / written into a 2 KB
// 15C memory image with the firmware's DIM bookkeeping (vc_matrix15_* in the core).
#pragma once
#include "Inspector.h"
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <cstdint>
#include <vector>

namespace vc {

namespace matrix15 {

constexpr int kMaxSide = 99;                 // the firmware's limit for rows and columns

/// One cell -> the 15C register value (10 digits, rounded). Empty text is 0. False (with a reason) when it is not a real number.
bool parseCell(const QString &text, uint64_t *raw, QString *why = nullptr);
/// The register value as the calculator / the inspector shows it ("1.5", "-2e-3"); parseCell reads it back exactly.
QString cellText(uint64_t raw);

/// A rectangular block of text cells from the clipboard.
struct Table {
    int rows = 0, cols = 0;
    QStringList cells;                       // row-major, rows * cols (short rows are padded with "")
    bool ragged = false;                     // the rows had different lengths
    QString error;                           // not usable (empty clipboard, too big ...)
    QString cell(int r, int c) const { return cells.value(r * cols + c); }
    bool ok() const { return error.isEmpty() && rows > 0 && cols > 0; }
};
/// Tab-separated text (lines by \n, \r\n or \r; a trailing line break is ignored; "quoted" cells are unquoted).
/// A single column of numbers separated by ';' is also accepted. More than 99 rows or columns is an error.
Table parseTable(const QString &text);
/// The bad cells of a table (1-based "row,col: text"), empty when every cell is a number.
QStringList badCells(const Table &t);

/// The inverse of parseTable for a matrix: rows of tab-separated cellText values (what "Copy as TSV" puts on the clipboard).
QString toTsv(int rows, int cols, const std::vector<uint64_t> &raw);

/// A matrix as stored in a 15C memory image.
struct Stored {
    int rows = 0, cols = 0;
    std::vector<uint64_t> raw;               // row-major
};
/// Matrix m (0..4 = A..E) of the image; variant 150 / 152 (the running 15C mode).
bool read(const QByteArray &image, int variant, int m, Stored *out);
/// The pool of the image (what the flyout's pool bar shows).
std::optional<Pool15> pool(const QByteArray &image, int variant);
/// The largest rows*cols matrix m can have in this image (its own registers count as free); -1 when the image is not a 15C image.
int room(const QByteArray &image, int variant, int m);
/// The pool as it would be after dimensioning m to rows x cols (nothing written to `image`).
std::optional<Pool15> poolAfter(const QByteArray &image, int variant, int m, int rows, int cols);
/// DIM matrix m to rows x cols and store raw (row-major, rows*cols values) into image. "" or why it was refused.
QString write(QByteArray *image, int variant, int m, int rows, int cols, const std::vector<uint64_t> &raw);

}  // namespace matrix15
}  // namespace vc
