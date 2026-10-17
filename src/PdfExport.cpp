// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
#include "PdfExport.h"
#include "KeyGlyphs.h"
#include <QBuffer>
#include <QFile>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QtCore/QtGlobal>
#include <functional>

static void initPdfResources() { Q_INIT_RESOURCE(voyagercalc); }

namespace vc {

PdfFonts PdfFonts::fromResources() {
    initPdfResources();
    PdfFonts f;
    auto load = [](const char *path) -> QString {
        QFile file(QString::fromLatin1(path));
        if (!file.open(QIODevice::ReadOnly)) return {};
        int id = QFontDatabase::addApplicationFontFromData(file.readAll());
        auto fam = id < 0 ? QStringList() : QFontDatabase::applicationFontFamilies(id);
        return fam.isEmpty() ? QString() : fam.first();
    };
    f.display = load(":/fonts/HP15CSimulatorDisplay.ttf");
    f.keys = load(":/fonts/HP15CSimulatorKeys.ttf");
    return f;
}

namespace {

enum class Role { Regular, Bold, Mono, MonoBold, Display, Keys };
struct Style {
    Role role; double size; QColor colour;
    Style(Role r, double s, QColor c) : role(r), size(s), colour(c) {}
};

const QColor kInk(0x1A1A1A), kGrey(0x6B6B6B), kRule(0xC9C9C9), kHeadFill(0xECECEC), kLabelBand(0xFBF1D9), kLcd(0xDFE5D3), kOrphan(0x9A3B2A);

QColor poolColour(const Pool15::Segment &s) {
    switch (s.kind) {
    case Pool15::Kind::Workspace: return QColor(0xC77DCC);
    case Pool15::Kind::Free: return QColor(0xE4E4E0);
    case Pool15::Kind::Complex: return QColor(0xE07A5F);
    case Pool15::Kind::Storage: return QColor(0x5AA9E6);
    case Pool15::Kind::Matrix: { static const QColor m[5] = {0xE3A33B, 0xD4B04C, 0xB9B957, 0x9DBF62, 0x7FBF73}; return m[std::clamp(s.matrix, 0, 4)]; }
    default: return QColor(0x4CBF6B);
    }
}

QString firstFamily(const QStringList &wanted, const QString &fallback) {
    const QStringList have = QFontDatabase::families();
    for (const QString &w : wanted) if (have.contains(w, Qt::CaseInsensitive)) return w;
    return fallback;
}

struct Column {
    QString title; double width; Style style; std::optional<QColor> fill;
    std::function<std::optional<std::vector<Glyph>>(int)> glyphs;
    Column(QString t, double w, Style s) : title(std::move(t)), width(w), style(s) {}
};

class Renderer {
public:
    Renderer(const ProgramDocumentContent &c, Paper paper, const PdfFonts &fonts)
        : c_(c), fonts_(fonts), paper_(paper), dev_(&buffer_) {
        W = paper == Paper::A4 ? 595.28 : 612.0;
        H = paper == Paper::A4 ? 841.89 : 792.0;
        right = W - 46; bottom = H - 48;
        dev_.setPageSize(QPageSize(paper == Paper::A4 ? QPageSize::A4 : QPageSize::Letter));
        dev_.setPageMargins(QMarginsF(0, 0, 0, 0));
        dev_.setResolution(72);
        dev_.setTitle(c.title());
        dev_.setCreator(QStringLiteral("VoyagerCalc"));
        QStringList sans{"Helvetica", "Arial", "Liberation Sans", "DejaVu Sans", "Noto Sans", "Open Sans", "Roboto"};
        QStringList mono{"Courier New", "Courier", "Liberation Mono", "DejaVu Sans Mono", "Noto Sans Mono", "Roboto Mono"};
        sans_ = firstFamily(sans, QFontDatabase::systemFont(QFontDatabase::GeneralFont).family());
        mono_ = firstFamily(mono, QFontDatabase::systemFont(QFontDatabase::FixedFont).family());
    }

    QByteArray run() {
        newPage();
        cover(); labels(); registers(); matrixOps(); listing(); orphans();
        // header and page numbers now that the count is known
        const int n = int(pages_.size());
        Style small(Role::Regular, 8, kGrey);
        for (int i = 0; i < n; ++i) {
            cur_ = i;
            text(c_.title(), left, 32, Style(Role::Bold, 8.5, kInk));
            QString tag = QStringLiteral("%1 program documentation").arg(c_.info.modelName);
            text(tag, right - width(tag, small), 32, small);
            line(left, 38, right, 38, 0.5, kRule);
            QString num = QStringLiteral("Page %1 of %2").arg(i + 1).arg(n);
            text(num, (W - width(num, small)) / 2, H - 26, small);
        }
        buffer_.open(QIODevice::WriteOnly);
        {
            QPainter p(&dev_);
            p.setRenderHint(QPainter::TextAntialiasing);
            for (int i = 0; i < n; ++i) {
                if (i) dev_.newPage();
                for (auto &op : pages_[i]) op(p);
            }
        }
        return buffer_.data();
    }

private:
    // ---- drawing primitives (recorded per page)
    using Op = std::function<void(QPainter &)>;
    QFont font(const Style &s) const {
        QFont f;
        switch (s.role) {
        case Role::Regular: f.setFamily(sans_); break;
        case Role::Bold: f.setFamily(sans_); f.setBold(true); break;
        case Role::Mono: f.setFamily(mono_); break;
        case Role::MonoBold: f.setFamily(mono_); f.setBold(true); break;
        case Role::Display: f.setFamily(fonts_.display); break;
        case Role::Keys: f.setFamily(fonts_.keys); break;
        }
        f.setPointSizeF(s.size);
        f.setStyleStrategy(QFont::PreferDefault);
        return f;
    }
    double width(const QString &s, const Style &st) const { return QFontMetricsF(font(st), const_cast<QPdfWriter *>(&dev_)).horizontalAdvance(s); }
    void text(const QString &s, double x, double y, const Style &st) {
        if (s.isEmpty()) return;
        QFont f = font(st); QColor col = st.colour;
        pages_[cur_].push_back([=](QPainter &p) { p.setFont(f); p.setPen(col); p.drawText(QPointF(x, y), s); });
    }
    void line(double x1, double y1, double x2, double y2, double w, QColor col) {
        pages_[cur_].push_back([=](QPainter &p) { p.setPen(QPen(col, w)); p.drawLine(QPointF(x1, y1), QPointF(x2, y2)); });
    }
    void fill(double x, double y_, double w, double h, QColor col) {
        pages_[cur_].push_back([=](QPainter &p) { p.fillRect(QRectF(x, y_, w, h), col); });
    }
    double glyphsWidth(const std::vector<Glyph> &g, const Style &st, double gap) const {
        double w = 0;
        for (size_t i = 0; i < g.size(); ++i) w += width(QString(g[i].first), st) + (i ? gap : 0);
        return w;
    }
    double glyphs(const std::vector<Glyph> &g, double x, double y_, const Style &st, double gap) {
        double start = x;
        for (const Glyph &gl : g) { text(QString(gl.first), x, y_, st); x += width(QString(gl.first), st) + gap; }
        return x - gap - start;
    }
    QStringList wrap(const QString &s, const Style &st, double w) const {
        QStringList lines;
        for (const QString &para : s.split(QLatin1Char('\n'))) {
            QString cur;
            for (const QString &word : para.split(QLatin1Char(' '))) {
                QString cand = cur.isEmpty() ? word : cur + QLatin1Char(' ') + word;
                if (width(cand, st) <= w || (cur.isEmpty() && width(word, st) <= w)) { cur = cand; continue; }
                if (!cur.isEmpty()) { lines << cur; cur.clear(); }
                QString wd = word;
                while (width(wd, st) > w && wd.size() > 1) {            // cut a too-long word
                    int n = int(wd.size()) - 1;
                    while (n > 1 && width(wd.left(n), st) > w) --n;
                    lines << wd.left(n); wd = wd.mid(n);
                }
                cur = wd;
            }
            lines << cur;
        }
        return lines;
    }

    void newPage() { pages_.emplace_back(); cur_ = int(pages_.size()) - 1; y = top; }
    bool need(double h) { if (y + h > bottom) { newPage(); return true; } return false; }

    // ---- blocks
    void paragraph(const QString &s, const Style &st, double lineHeight, double indent = 0) {
        for (const QString &l : wrap(s, st, contentWidth() - indent)) {
            need(lineHeight);
            text(l, left + indent, y + st.size, st);
            y += lineHeight;
        }
    }
    double contentWidth() const { return right - left; }

    void heading(const QString &s) {
        if (!need(90)) y += 16;
        text(s, left, y + 13, Style(Role::Bold, 13, kInk));
        y += 19;
        line(left, y, right, y, 0.8, kInk);
        y += 8;
    }

    void cover() {
        const auto &c = c_;
        text(c.title(), left, y + 22, Style(Role::Bold, 22, kInk));
        y += 32;
        text(QStringLiteral("Program documentation"), left, y + 11, Style(Role::Regular, 11, kGrey));
        y += 26;
        std::vector<std::pair<QString, QString>> rows{{"Model", c.info.modelName}};
        if (!c.info.firmware.isEmpty()) rows.push_back({"Firmware", c.info.firmware});
        if (!c.info.memoryFile.isEmpty()) rows.push_back({"Memory file", c.info.memoryFile});
        rows.push_back({"Steps", QString::number(c.lines.size())});
        int nl = 0; for (const auto &l : c.analysis.labels) if (!l.defined.empty() && !l.isIndirect) ++nl;
        rows.push_back({"Labels", QString::number(nl)});
        rows.push_back({"Date", c.info.dateText()});
        if (!c.doc.author.isEmpty()) rows.push_back({"Author", c.doc.author});
        Style k(Role::Regular, 9, kGrey), v(Role::Regular, 10, kInk);
        fill(left, y - 4, contentWidth(), rows.size() * 15 + 8, QColor(0xF5F5F2));
        for (auto &[a, b] : rows) { text(a, left + 8, y + 10, k); text(b, left + 92, y + 10, v); y += 15; }
        y += 10;
        memory();
        if (!c.doc.description.isEmpty()) { heading(QStringLiteral("Description")); description(c.doc.description); }
    }

    void description(const QString &textIn) {
        Style body(Role::Regular, 10, kInk);
        for (const QString &raw : textIn.split(QLatin1Char('\n'))) {
            QString l = raw.trimmed();
            if (l.isEmpty()) { y += 6; continue; }
            if (l.startsWith(QLatin1Char('#'))) {
                int i = 0; while (i < l.size() && (l[i] == QLatin1Char('#') || l[i] == QLatin1Char(' '))) ++i;
                need(30); y += 6;
                paragraph(l.mid(i), Style(Role::Bold, 10.5, kInk), 14);
            } else if (l.startsWith(QStringLiteral("- ")) || l.startsWith(QStringLiteral("• "))) {
                need(13);
                text(QStringLiteral("•"), left + 4, y + 10, body);
                paragraph(l.mid(2), body, 13, 14);
            } else paragraph(l, body, 13);
        }
    }

    // ---- tables
    void tableHeader(const std::vector<Column> &cols) {
        const double h = 15;
        fill(left, y, contentWidth(), h, kHeadFill);
        double x = left;
        for (const auto &c : cols) { text(c.title, x + 4, y + 10.5, Style(Role::Bold, 8.5, kInk)); x += c.width; }
        y += h;
    }

    using BandFn = std::function<std::optional<QColor>(int)>;
    using StyleFn = std::function<std::optional<Style>(int, int)>;
    void table(const QString &title, const std::vector<Column> &cols, const std::vector<QStringList> &rows,
               BandFn band = nullptr, StyleFn styles = nullptr) {
        const Style capStyle(Role::Keys, 10, kInk), capText(Role::Regular, 7, kGrey);
        need(15 + 2 * 14);
        tableHeader(cols);
        for (int ri = 0; ri < int(rows.size()); ++ri) {
            const QStringList &row = rows[ri];
            struct Cap { std::vector<Glyph> g; bool beside; };
            std::map<int, Cap> caps;
            for (int i = 0; i < int(cols.size()); ++i) {
                if (!cols[i].glyphs) continue;
                auto g = cols[i].glyphs(ri);
                if (!g) continue;
                double w = glyphsWidth(*g, capStyle, 1);
                bool beside = w + 5 + width(row[i], capText) <= cols[i].width - 8;
                caps[i] = {*g, beside};
            }
            std::vector<QStringList> wrapped;
            size_t most = 1;
            for (int i = 0; i < int(cols.size()); ++i) {
                if (caps.count(i)) { wrapped.emplace_back(); continue; }
                Style st = (styles ? styles(ri, i) : std::nullopt).value_or(cols[i].style);
                wrapped.push_back(wrap(row[i], st, cols[i].width - 8));
                most = std::max<size_t>(most, wrapped.back().size());
            }
            const double lineH = 11.5;
            double h = most * lineH + 3;
            for (auto &[i, cp] : caps) h = std::max(h, cp.beside ? 15.0 : 23.0);
            if (need(h)) {
                text(title + QStringLiteral(" (continued)"), left, y + 9, Style(Role::Regular, 8.5, kGrey));
                y += 14;
                tableHeader(cols);
            }
            if (band) if (auto b = band(ri)) fill(left, y, contentWidth(), h, *b);
            double x = left;
            for (int i = 0; i < int(cols.size()); ++i) {
                const Column &c = cols[i];
                if (c.fill) fill(x + 2, y + 1.5, c.width - 4, h - 3, *c.fill);
                Style st = (styles ? styles(ri, i) : std::nullopt).value_or(c.style);
                if (auto it = caps.find(i); it != caps.end()) {
                    double w = glyphs(it->second.g, x + 4, y + 11, capStyle, 1);
                    if (it->second.beside) text(row[i], x + 4 + w + 5, y + 10.5, capText);
                    else text(row[i], x + 4, y + 20, capText);
                }
                for (int li = 0; li < wrapped[i].size(); ++li) text(wrapped[i][li], x + 4, y + 10 + li * lineH, st);
                x += c.width;
            }
            y += h;
            line(left, y, right, y, 0.3, kRule);
        }
    }

    void memory() {
        auto rows = c_.memoryRows();
        if (rows.empty() || !c_.pool) return;
        heading(QStringLiteral("Memory configuration"));
        Style k(Role::Regular, 9, kGrey), v(Role::Regular, 9.5, kInk);
        for (auto &[a, b] : rows) {
            QStringList w = wrap(b, v, contentWidth() - 100);
            need(w.size() * 12.5);
            text(a, left + 4, y + 9.5, k);
            for (int i = 0; i < w.size(); ++i) text(w[i], left + 100, y + 9.5 + i * 12.5, v);
            y += w.size() * 12.5;
        }
        need(40);
        y += 6;
        auto segs = c_.pool->segments();
        double total = 0; for (auto &s : segs) total += s.count;
        total = std::max(1.0, total);
        double x = left;
        for (auto &s : segs) {
            double w = contentWidth() * s.count / total;
            fill(x, y, std::max(0.8, w - 0.6), 10, poolColour(s));
            x += w;
        }
        y += 22;
        double lx = left;
        QStringList seen;
        Style small(Role::Regular, 7.5, kGrey);
        for (auto &sg : segs) {
            QString name = sg.kind == Pool15::Kind::Matrix ? QStringLiteral("matrices")
                         : sg.kind == Pool15::Kind::Storage ? QStringLiteral("R2…") : sg.label();
            if (seen.contains(name)) continue;
            seen << name;
            int n = 0;
            for (auto &a : segs) if ((sg.kind == Pool15::Kind::Matrix && a.kind == Pool15::Kind::Matrix) || (a.kind == sg.kind && (a.kind != Pool15::Kind::Matrix))) n += a.count;
            QString t = QStringLiteral("%1 %2").arg(name).arg(n);
            fill(lx, y - 6.5, 7, 7, poolColour(sg));
            text(t, lx + 10, y, small);
            lx += 10 + width(t, small) + 12;
        }
        y += 10;
    }

    void matrixOps() {
        auto rows = c_.matrixOpRows();
        if (rows.empty()) return;
        heading(QStringLiteral("Matrices and complex"));
        Style s(Role::Regular, 9, kInk);
        std::vector<QStringList> r;
        for (auto &m : rows) r.push_back({m.item, m.operations, m.steps, m.notes});
        table(QStringLiteral("Matrices and complex"),
              {Column("Item", 84, Style(Role::Bold, 9, kInk)), Column("Operations", 120, s), Column("Steps", 100, s), Column("Notes", contentWidth() - 304, s)}, r);
    }

    void labels() {
        auto rows = c_.labelRows();
        if (rows.empty()) return;
        bool is12 = c_.info.model == 12;
        heading(is12 ? QStringLiteral("Branch targets") : QStringLiteral("Labels"));
        Style s(Role::Regular, 9, kInk);
        std::vector<QStringList> r;
        for (auto &l : rows) r.push_back({l.label, l.defined, l.called, l.notes});
        table(QStringLiteral("Labels"),
              {Column(is12 ? "Line" : "Label", 64, Style(Role::Bold, 9, kInk)), Column("Defined at", 62, s), Column("Called from", 150, s), Column("Notes", contentWidth() - 276, s)}, r,
              nullptr, [&](int ri, int i) -> std::optional<Style> {
                  if (i == 1 && rows[ri].defined == QLatin1String("not defined")) return Style(Role::Regular, 9, kOrphan);
                  return std::nullopt;
              });
    }

    void registers() {
        auto regs = c_.registerRows(); auto flags = c_.flagRows();
        regs.insert(regs.end(), flags.begin(), flags.end());
        if (regs.empty()) return;
        heading(flags.empty() ? QStringLiteral("Registers") : QStringLiteral("Registers and flags"));
        Style s(Role::Regular, 9, kInk);
        std::vector<QStringList> r;
        for (auto &x : regs) r.push_back({x.reg, x.name, x.use, x.steps});
        table(QStringLiteral("Registers"),
              {Column("Register", 64, Style(Role::Bold, 9, kInk)), Column("Name / meaning", contentWidth() - 64 - 112 - 140, s), Column("Use", 112, s), Column("Steps", 140, s)}, r);
    }

    void listing() {
        auto rows = c_.listingRows();
        heading(QStringLiteral("Program listing"));
        if (rows.empty()) { paragraph(QStringLiteral("The calculator holds no program."), Style(Role::Regular, 10, kGrey), 13); return; }
        Style s(Role::Regular, 9, kInk);
        const double screenW = c_.info.model == 12 ? 82 : 92;
        Style screen = fonts_.hasDisplay() ? Style(Role::Display, 11, kInk) : Style(Role::MonoBold, 9, kInk);
        const bool useCaps = c_.info.model == 15 && fonts_.hasKeys();
        const double keysW = useCaps ? 150 : 104;
        Column keys("Keys", keysW, s);
        if (useCaps) keys.glyphs = [this](int i) { return keyGlyphsFor(c_.lines[i]); };
        Column disp("Display", screenW, screen); disp.fill = kLcd;
        std::vector<QStringList> r;
        for (auto &l : rows) r.push_back({l.step, l.screen, l.keys, l.comment});
        table(QStringLiteral("Program listing"),
              {Column("Step", 34, Style(Role::Regular, 9, kGrey)), disp, keys, Column("Comment", contentWidth() - 34 - screenW - keysW, s)}, r,
              [&](int i) -> std::optional<QColor> { return rows[i].isLabel ? std::optional<QColor>(kLabelBand) : std::nullopt; },
              [&](int ri, int i) -> std::optional<Style> { return i == 2 && rows[ri].isLabel ? std::optional<Style>(Style(Role::Bold, 9, kInk)) : std::nullopt; });
    }

    void orphans() {
        auto o = c_.doc.orphanedComments();
        if (o.empty()) return;
        heading(QStringLiteral("Comments that no longer match a step"));
        Style s(Role::Regular, 9, kInk);
        std::vector<QStringList> r;
        for (auto &c : o) r.push_back({QStringLiteral("%1").arg(c.step, 3, 10, QLatin1Char('0')), c.keys, c.text});
        table(QStringLiteral("Comments"),
              {Column("Was step", 52, Style(Role::Regular, 9, kGrey)), Column("Keys then", 92, Style(Role::Mono, 9, kInk)), Column("Comment", contentWidth() - 144, s)}, r);
    }

    const ProgramDocumentContent &c_;
    PdfFonts fonts_;
    Paper paper_;
    QBuffer buffer_;
    QPdfWriter dev_;
    QString sans_, mono_;
    std::vector<std::vector<Op>> pages_;
    int cur_ = 0;
    double W = 0, H = 0, left = 46, right = 0, top = 58, bottom = 0, y = 0;
};

}  // namespace

QByteArray renderProgramDocumentPdf(const ProgramDocumentContent &content, Paper paper, const PdfFonts &fonts) {
    Renderer r(content, paper, fonts);
    return r.run();
}

}  // namespace vc
