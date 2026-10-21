// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
#include "Matrix15.h"
#include <QRegularExpression>

namespace vc {
namespace matrix15 {

bool parseCell(const QString &text, uint64_t *raw, QString *why) {
    QString t = text.trimmed();
    t.remove(QChar(0x00A0)); t.remove(QChar(0x202F)); t.remove(QLatin1Char(' ')); t.remove(QLatin1Char('\''));   // thousands spaces / apostrophes
    t.replace(QChar(0x2212), QLatin1Char('-'));                                  // the typographic minus some sheets copy
    if (t.isEmpty()) { *raw = 0; return true; }
    const int comma = int(t.count(QLatin1Char(','))), dot = int(t.count(QLatin1Char('.')));
    if (comma && dot) {
        // both: the one that comes last is the decimal separator, the other groups thousands ("1.234,5" / "1,234.5")
        const bool commaDecimal = t.lastIndexOf(QLatin1Char(',')) > t.lastIndexOf(QLatin1Char('.'));
        if (commaDecimal) { if (comma != 1) goto bad; t.remove(QLatin1Char('.')); t.replace(QLatin1Char(','), QLatin1Char('.')); }
        else { if (dot != 1) goto bad; t.remove(QLatin1Char(',')); }
    } else if (comma == 1) t.replace(QLatin1Char(','), QLatin1Char('.'));    // "1,5"
    else if (comma > 1) {
        // "1,234,567": thousands groups only
        if (!QRegularExpression(QStringLiteral("^[+-]?\\d{1,3}(,\\d{3})+$")).match(t).hasMatch()) goto bad;
        t.remove(QLatin1Char(','));
    }
    {
        const QByteArray a = t.toLatin1();
        uint64_t v = 0;
        if (t.contains(QLatin1Char('i')) || t.contains(QLatin1Char('j'))) { if (why) *why = QStringLiteral("complex numbers are not supported (real numbers only)"); return false; }
        if (vc_bcd_from_text(a.constData(), &v) == VC_OK) { *raw = v; return true; }
        // too large?
        QRegularExpression big(QStringLiteral("^[+-]?(\\d+\\.?\\d*|\\.\\d+)[eE][+]?\\d+$"));
        if (big.match(t).hasMatch()) { if (why) *why = QStringLiteral("too large (the HP-15C goes up to 9.999999999e99)"); return false; }
    }
bad:
    if (why) *why = QStringLiteral("not a number");
    return false;
}

QString cellText(uint64_t raw) {
    char b[64];
    vc_bcd_text(raw, b, sizeof b);
    return QString::fromLatin1(b);
}

static QString unquote(QString c) {
    c = c.trimmed();
    if (c.size() >= 2 && c.startsWith(QLatin1Char('"')) && c.endsWith(QLatin1Char('"'))) c = c.mid(1, c.size() - 2).replace(QStringLiteral("\"\""), QStringLiteral("\""));
    return c;
}

Table parseTable(const QString &text) {
    Table t;
    QString s = text;
    s.replace(QStringLiteral("\r\n"), QStringLiteral("\n")).replace(QLatin1Char('\r'), QLatin1Char('\n'));
    QStringList lines = s.split(QLatin1Char('\n'));
    while (!lines.isEmpty() && lines.last().trimmed().isEmpty()) lines.removeLast();
    while (!lines.isEmpty() && lines.first().trimmed().isEmpty()) lines.removeFirst();
    if (lines.isEmpty()) { t.error = QStringLiteral("The clipboard holds no cells. Copy a range of numbers in the spreadsheet first."); return t; }
    std::vector<QStringList> rows;
    bool anyTab = s.contains(QLatin1Char('\t'));
    for (const QString &l : lines) {
        QStringList cells;
        if (anyTab) cells = l.split(QLatin1Char('\t'));
        else if (l.contains(QLatin1Char(';'))) cells = l.split(QLatin1Char(';'));       // "CSV" of a European spreadsheet
        else cells = QStringList{l};
        for (QString &c : cells) c = unquote(c);
        rows.push_back(cells);
    }
    // trailing empty columns that every row has (some apps add a tab at the end of a line)
    int cols = 0;
    for (auto &r : rows) { while (r.size() > 1 && r.last().isEmpty()) r.removeLast(); cols = std::max(cols, int(r.size())); }
    for (auto &r : rows) if (int(r.size()) != cols) t.ragged = true;
    t.rows = int(rows.size()); t.cols = cols;
    if (t.rows > kMaxSide || t.cols > kMaxSide) {
        t.error = QStringLiteral("The clipboard holds %1 × %2 cells; an HP-15C matrix has at most 99 rows and 99 columns.").arg(t.rows).arg(t.cols);
        return t;
    }
    for (auto &r : rows) { for (int c = 0; c < cols; ++c) t.cells << (c < r.size() ? r[c] : QString()); }
    return t;
}

QStringList badCells(const Table &t) {
    QStringList out;
    for (int r = 0; r < t.rows; ++r)
        for (int c = 0; c < t.cols; ++c) {
            uint64_t v; QString why;
            if (!parseCell(t.cell(r, c), &v, &why)) out << QStringLiteral("%1,%2: “%3” (%4)").arg(r + 1).arg(c + 1).arg(t.cell(r, c), why);
        }
    return out;
}

QString toTsv(int rows, int cols, const std::vector<uint64_t> &raw) {
    QStringList lines;
    for (int r = 0; r < rows; ++r) {
        QStringList cells;
        for (int c = 0; c < cols; ++c) { size_t i = size_t(r * cols + c); cells << (i < raw.size() ? cellText(raw[i]) : QStringLiteral("0")); }
        lines << cells.join(QLatin1Char('\t'));
    }
    return lines.join(QLatin1Char('\n'));
}

static const uint8_t *bytes(const QByteArray &a) { return reinterpret_cast<const uint8_t *>(a.constData()); }

bool read(const QByteArray &image, int variant, int m, Stored *out) {
    vc_pool15 p;
    if (image.size() != VC_MEMORY_SIZE || vc_image_pool15(bytes(image), variant, &p) != VC_OK || m < 0 || m > 4) return false;
    out->rows = p.rows[m]; out->cols = p.cols[m]; out->raw.clear();
    for (int r = 1; r <= p.rows[m]; ++r)
        for (int c = 1; c <= p.cols[m]; ++c) {
            vc_value v{};
            if (vc_image_matrix15(bytes(image), variant, m, r, c, &v) != VC_OK) return false;
            out->raw.push_back(v.raw);
        }
    return true;
}

std::optional<Pool15> pool(const QByteArray &image, int variant) {
    vc_pool15 p;
    if (image.size() != VC_MEMORY_SIZE || vc_image_pool15(bytes(image), variant, &p) != VC_OK) return std::nullopt;
    return pool15FromCore(p);
}

int room(const QByteArray &image, int variant, int m) {
    int avail = -1;
    if (image.size() != VC_MEMORY_SIZE) return -1;
    int r = vc_matrix15_room(bytes(image), variant, m, 0, 0, &avail);
    return r == VC_OK ? avail : -1;
}

std::optional<Pool15> poolAfter(const QByteArray &image, int variant, int m, int rows, int cols) {
    QByteArray copy = image;
    if (copy.size() != VC_MEMORY_SIZE) return std::nullopt;
    if (vc_matrix15_write(reinterpret_cast<uint8_t *>(copy.data()), variant, m, rows, cols, nullptr) != VC_OK) return std::nullopt;
    return pool(copy, variant);
}

QString write(QByteArray *image, int variant, int m, int rows, int cols, const std::vector<uint64_t> &raw) {
    if (!image || image->size() != VC_MEMORY_SIZE) return QStringLiteral("The emulator's memory could not be read.");
    if (m < 0 || m > 4) return QStringLiteral("There is no matrix %1.").arg(m);
    if (rows < 0 || cols < 0 || rows > kMaxSide || cols > kMaxSide) return QStringLiteral("An HP-15C matrix has 0 to 99 rows and 0 to 99 columns.");
    if (rows * cols != int(raw.size())) return QStringLiteral("The grid does not have %1 × %2 values.").arg(rows).arg(cols);
    vc_pool15 p;
    if (vc_image_pool15(bytes(*image), variant, &p) != VC_OK) return QStringLiteral("The emulator's memory is not an HP-15C memory.");
    const int avail = room(*image, variant, m);
    if (rows * cols > avail)
        return QStringLiteral("Matrix %1 %2 × %3 needs %4 registers, but only %5 are free for it (the calculator would show Error 10). "
                              "Make it smaller, or free memory on the calculator (smaller DIM (i), other matrices, program).")
            .arg(QChar('A' + m)).arg(rows).arg(cols).arg(rows * cols).arg(avail);
    QByteArray copy = *image;
    if (vc_matrix15_write(reinterpret_cast<uint8_t *>(copy.data()), variant, m, rows, cols, raw.empty() ? nullptr : raw.data()) != VC_OK)
        return QStringLiteral("Matrix %1 could not be written.").arg(QChar('A' + m));
    *image = copy;
    return QString();
}

}  // namespace matrix15
}  // namespace vc
