// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
#include "InspectorPanel.h"
#include <QAbstractListModel>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QDateTime>
#include <QFontDatabase>
#include <QPainterPath>
#include <QClipboard>
#include <QElapsedTimer>
#include <QHBoxLayout>
#include <QLabel>
#include <QListView>
#include <QMenu>
#include <QPainter>
#include <QScrollBar>
#include <QSettings>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace vc {

namespace {

enum class RowKind { Header, Reg, Pool, Matrix, Grid, Text, Prog };

struct Row {
    RowKind kind = RowKind::Text;
    QString name, value, sub, suffix, help;      // Reg: name, shown text, decimal under it, h/o/b/d
    QString keys, prefix, mnemonic, comment;      // Prog
    int step = -1;                                // Prog: line number, Matrix: index
    bool current = false, running = false, expanded = false, result = false, empty = false;
    std::shared_ptr<Pool15> pool;
    QString copyText;                             // context menu
    qint64 flashUntil = 0;
    quint64 stamp = 0;                            // value signature, to see a register change
    bool sameAs(const Row &o) const {
        return kind == o.kind && name == o.name && value == o.value && sub == o.sub && suffix == o.suffix && keys == o.keys
            && prefix == o.prefix && mnemonic == o.mnemonic && comment == o.comment && step == o.step && current == o.current
            && running == o.running && expanded == o.expanded && result == o.result && empty == o.empty
            && ((!pool && !o.pool) || (pool && o.pool && *pool == *o.pool));
    }
};

bool darkPalette() { return QApplication::palette().color(QPalette::Window).lightness() < 128; }
QColor fColour() { return darkPalette() ? QColor(0xE3A33B) : QColor(0x7F4F00); }
/// Section header background (STACK, MEMORY, MATRICES, STORAGE REGISTERS, PROGRAM): the panel's own background with the
/// header tint composed on top, always fully opaque (palettes may give AlternateBase an alpha; scrolled rows showed through).
QColor sectionHeaderColor(const QPalette &pal) {
    QColor base = pal.color(QPalette::Base); base.setAlpha(255);
    QColor tint = pal.color(QPalette::AlternateBase).darker(darkPalette() ? 100 : 104);
    const double a = tint.alphaF();
    QColor out = QColor::fromRgbF(float(tint.redF() * a + base.redF() * (1 - a)), float(tint.greenF() * a + base.greenF() * (1 - a)), float(tint.blueF() * a + base.blueF() * (1 - a)));
    out.setAlpha(255);
    return out;
}

QColor gColour() { return darkPalette() ? QColor(0x5AA9E6) : QColor(0x0A4F8C); }
QColor runningColour() { return darkPalette() ? QColor(0x4CBF6B) : QColor(0x1B7A3C); }
QColor pointerBand(bool running) {
    return darkPalette() ? QColor(running ? 0x3A7D4A : 0x2E5E3A) : QColor(running ? 0xB4E5B4 : 0xD3F1D3);
}
QColor poolColour(const Pool15::Segment &s) {
    switch (s.kind) {
    case Pool15::Kind::Workspace: return QColor(0xC77DCC);
    case Pool15::Kind::Free: { QColor c = QApplication::palette().color(QPalette::Text); c.setAlphaF(0.18); return c; }
    case Pool15::Kind::Complex: return QColor(0xE07A5F);
    case Pool15::Kind::Storage: return QColor(0x5AA9E6);
    case Pool15::Kind::Matrix: { static const QColor m[5] = {0xE3A33B, 0xD4B04C, 0xB9B957, 0x9DBF62, 0x7FBF73}; return m[std::clamp(s.matrix, 0, 4)]; }
    default: return QColor(0x4CBF6B);
    }
}
QString hex2(int v) { return QStringLiteral("%1").arg(v, 2, 16, QLatin1Char('0')).toUpper(); }

}  // namespace


// The pool bar of the flyout (also used by the Matrix Editor's free-memory preview): segments, g MEM caption, legend.
void paintPool15Bar(QPainter *p, const QRect &rc, const Pool15 &pl, const QFont &base) {
    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    const QPalette pal = QApplication::palette();
    QColor text = pal.color(QPalette::Text), dim = text; dim.setAlphaF(0.62);
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSizeF(base.pointSizeF());
    const int pad = 10;
    auto segs = pl.segments();
    double total = 0; for (auto &s : segs) total += s.count;
    total = std::max(1.0, total);
    QRectF bar(rc.left() + pad, rc.top() + 6, rc.width() - 2 * pad, 14);
    QPainterPath clip; clip.addRoundedRect(bar, 3, 3); p->setClipPath(clip);
    double x = bar.left(), avail = bar.width() - (segs.size() - 1);
    for (auto &s : segs) { double w = std::max(2.0, avail * s.count / total); p->fillRect(QRectF(x, bar.top(), w, bar.height()), poolColour(s)); x += w + 1; }
    p->setClipping(false);
    QFont f = mono; f.setPointSizeF(base.pointSizeF() * 0.9); p->setFont(f); p->setPen(dim);
    p->drawText(QRect(rc.left() + pad, rc.top() + 24, rc.width() - 2 * pad, 16), Qt::AlignVCenter, p->fontMetrics().elidedText(pl.caption(), Qt::ElideRight, rc.width() - 2 * pad));
    // legend, up to three per line
    QStringList seen; struct Item { QString name; QColor c; int n; };
    std::vector<Item> items;
    for (auto &s : segs) {
        QString name = s.kind == Pool15::Kind::Matrix ? QStringLiteral("matrices") : s.kind == Pool15::Kind::Storage ? QStringLiteral("R2…") : s.label();
        auto it = std::find_if(items.begin(), items.end(), [&](const Item &a) { return a.name == name; });
        if (it == items.end()) items.push_back({name, poolColour(s), s.count}); else it->n += s.count;
    }
    QFont lf = base; lf.setPointSizeF(base.pointSizeF() * 0.85); p->setFont(lf);
    QFontMetrics fm(lf);
    int lx = rc.left() + pad, ly = rc.top() + 44;
    for (auto &it : items) {
        QString t = QStringLiteral("%1 %2").arg(it.name).arg(it.n);
        int w = 13 + fm.horizontalAdvance(t) + 10;
        if (lx + w > rc.right() - pad && lx > rc.left() + pad) { lx = rc.left() + pad; ly += 14; }
        p->setPen(Qt::NoPen); p->setBrush(it.c); p->drawRoundedRect(QRect(lx, ly + 2, 9, 9), 2, 2);
        p->setPen(dim); p->drawText(QRect(lx + 13, ly, w, 13), Qt::AlignVCenter, t);
        lx += w;
    }
    p->restore();
}

class RowModel : public QAbstractListModel {
public:
    explicit RowModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex &p = {}) const override { return p.isValid() ? 0 : int(rows.size()); }
    QVariant data(const QModelIndex &i, int role) const override {
        if (!i.isValid() || i.row() >= int(rows.size())) return {};
        const Row &r = rows[i.row()];
        if (role == Qt::DisplayRole) {
            switch (r.kind) {
            case RowKind::Header: return r.name.toUpper();
            case RowKind::Reg: return r.name + QLatin1Char('\t') + r.value + (r.suffix.isEmpty() ? QString() : r.suffix);
            case RowKind::Prog: return r.name + QStringLiteral("-\t") + r.keys + QLatin1Char('\t') + (r.prefix.isEmpty() ? QString() : r.prefix + QLatin1Char(' ')) + r.mnemonic + (r.comment.isEmpty() ? QString() : QStringLiteral("\t") + r.comment);
            default: return r.name + QLatin1Char('\t') + r.value;
            }
        }
        if (role == Qt::ToolTipRole) return r.help;
        return {};
    }
    Qt::ItemFlags flags(const QModelIndex &) const override { return Qt::ItemIsEnabled | Qt::ItemIsSelectable; }
    void setRows(std::vector<Row> n) {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        bool sameShape = n.size() == rows.size();
        if (sameShape) for (size_t i = 0; i < n.size(); ++i) if (n[i].kind != rows[i].kind) { sameShape = false; break; }
        if (!sameShape) {
            beginResetModel(); rows = std::move(n); endResetModel();
            return;
        }
        for (size_t i = 0; i < n.size(); ++i) {
            if (n[i].kind == RowKind::Reg && n[i].name == rows[i].name && n[i].stamp != rows[i].stamp) n[i].flashUntil = now + 1500;
            else if (n[i].kind == RowKind::Reg && n[i].name == rows[i].name) n[i].flashUntil = rows[i].flashUntil;
            if (!n[i].sameAs(rows[i]) || n[i].flashUntil != rows[i].flashUntil) { rows[i] = std::move(n[i]); emit dataChanged(index(int(i)), index(int(i))); }
        }
    }
    bool flashing() const { qint64 now = QDateTime::currentMSecsSinceEpoch(); for (auto &r : rows) if (r.flashUntil > now) return true; return false; }
    std::vector<Row> rows;
};

class RowDelegate : public QStyledItemDelegate {
public:
    RowDelegate(const RowModel *m, QObject *parent) : QStyledItemDelegate(parent), model_(m) {}
    QSize sizeHint(const QStyleOptionViewItem &opt, const QModelIndex &i) const override {
        const Row &r = model_->rows[i.row()];
        int h = 22;
        switch (r.kind) {
        case RowKind::Header: h = 20; break;
        case RowKind::Reg: h = r.sub.isEmpty() ? 22 : 34; break;
        case RowKind::Pool: h = 74; break;
        case RowKind::Grid: h = 16; break;
        case RowKind::Prog: h = 20; break;
        case RowKind::Text: h = 54; break;
        default: break;
        }
        return {opt.rect.width(), h};
    }
    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &i) const override {
        const Row &r = model_->rows[i.row()];
        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);
        QRect rc = opt.rect;
        const QPalette pal = QApplication::palette();
        QColor text = pal.color(QPalette::Text), dim = text; dim.setAlphaF(0.62);
        QFont base = opt.font, mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        mono.setPointSizeF(base.pointSizeF());
        const int pad = 10;
        switch (r.kind) {
        case RowKind::Header: {
            p->fillRect(rc, sectionHeaderColor(pal));      // opaque: the pinned header covers the rows scrolling under it
            QFont f = base; f.setBold(true); f.setPointSizeF(base.pointSizeF() * 0.82);
            p->setFont(f); p->setPen(dim);
            p->drawText(rc.adjusted(pad, 0, -pad, 0), Qt::AlignVCenter | Qt::AlignLeft, r.name.toUpper());
            break;
        }
        case RowKind::Reg: {
            qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (r.flashUntil > now) { QColor c(0xF2C14E); c.setAlphaF(0.35 * std::min(1.0, (r.flashUntil - now) / 1100.0)); p->fillRect(rc, c); }
            p->setFont(base); p->setPen(dim);
            p->drawText(QRect(rc.left() + pad, rc.top(), 58, rc.height()), Qt::AlignVCenter | Qt::AlignLeft, r.name);
            p->setFont(mono); p->setPen(text);
            QRect vr = rc.adjusted(70, 2, -pad, r.sub.isEmpty() ? -2 : -14);
            QString shown = p->fontMetrics().elidedText(r.value, Qt::ElideLeft, vr.width() - (r.suffix.isEmpty() ? 0 : 12));
            QRect used;
            p->drawText(vr.adjusted(0, 0, r.suffix.isEmpty() ? 0 : -12, 0), Qt::AlignVCenter | Qt::AlignRight, shown, &used);
            if (!r.suffix.isEmpty()) { p->setFont(base); p->setPen(dim); p->drawText(QRect(vr.right() - 10, vr.top(), 12, vr.height()), Qt::AlignVCenter | Qt::AlignLeft, r.suffix); }
            if (!r.sub.isEmpty()) {
                QFont f = mono; f.setPointSizeF(base.pointSizeF() * 0.85); p->setFont(f); p->setPen(dim);
                p->drawText(QRect(rc.left() + 70, rc.bottom() - 15, rc.width() - 70 - pad, 14), Qt::AlignVCenter | Qt::AlignRight, r.sub);
            }
            break;
        }
        case RowKind::Pool: paintPool15Bar(p, rc, *r.pool, base); break;
        case RowKind::Matrix: {
            p->setOpacity(r.empty ? 0.45 : 1.0);
            p->setFont(base); p->setPen(dim);
            p->drawText(QRect(rc.left() + pad, rc.top(), 12, rc.height()), Qt::AlignVCenter, r.empty ? QString() : (r.expanded ? QStringLiteral("▾") : QStringLiteral("▸")));
            QFont b = base; b.setBold(true); p->setFont(b); p->setPen(text);
            p->drawText(QRect(rc.left() + pad + 14, rc.top(), 16, rc.height()), Qt::AlignVCenter, r.name);
            p->setFont(mono);
            QString dim_ = r.value;
            p->drawText(QRect(rc.left() + pad + 32, rc.top(), 50, rc.height()), Qt::AlignVCenter, dim_);
            QFont sf = base; sf.setPointSizeF(base.pointSizeF() * 0.82); p->setFont(sf); p->setPen(dim);
            QRect sr(rc.left() + pad + 82, rc.top(), rc.width() - pad * 2 - 82 - (r.result ? 56 : 0), rc.height());
            p->drawText(sr, Qt::AlignVCenter, p->fontMetrics().elidedText(r.sub, Qt::ElideRight, sr.width()));
            if (r.result) tag(p, QRect(rc.right() - pad - 52, rc.top() + 4, 52, rc.height() - 8), QStringLiteral("RESULT"), fColour(), base);
            break;
        }
        case RowKind::Grid: {
            p->setFont(mono); p->setPen(text);
            p->drawText(rc.adjusted(pad + 24, 0, -pad, 0), Qt::AlignVCenter | Qt::AlignRight, r.value);
            break;
        }
        case RowKind::Text: {
            p->setFont(base); p->setPen(dim);
            p->drawText(rc.adjusted(pad, 6, -pad, -6), Qt::AlignTop | Qt::AlignLeft | Qt::TextWordWrap, r.value);
            break;
        }
        case RowKind::Prog: {
            if (r.current) p->fillRect(rc, pointerBand(r.running));
            QFont f = mono; if (r.current) f.setWeight(QFont::DemiBold); p->setFont(f);
            QColor numc = r.current ? text : dim;
            p->setPen(numc);
            QFontMetrics fm(f);
            int x = rc.left() + pad;
            QString num = r.name + QLatin1Char('-');
            p->drawText(QRect(x, rc.top(), fm.horizontalAdvance(num) + 2, rc.height()), Qt::AlignVCenter, num);
            x += fm.horizontalAdvance(num) + 6;
            p->setPen(text);
            p->drawText(QRect(x, rc.top(), 86, rc.height()), Qt::AlignVCenter, r.keys);
            x += 92;
            QFont nf = base; if (r.current) nf.setWeight(QFont::DemiBold); p->setFont(nf);
            if (!r.prefix.isEmpty()) {
                QFont bf = nf; bf.setBold(true); p->setFont(bf);
                QColor pc = r.prefix == QLatin1String("f") ? fColour() : gColour();
                if (r.current && darkPalette()) pc = r.prefix == QLatin1String("f") ? QColor(0xF2C060) : QColor(0x8CC8F5);
                p->setPen(pc);
                p->drawText(QRect(x, rc.top(), 14, rc.height()), Qt::AlignVCenter, r.prefix);
                x += QFontMetrics(bf).horizontalAdvance(r.prefix) + 6;
                p->setFont(nf);
            }
            p->setPen(text);
            QFontMetrics nfm(nf);
            p->drawText(QRect(x, rc.top(), nfm.horizontalAdvance(r.mnemonic) + 2, rc.height()), Qt::AlignVCenter, r.mnemonic);
            x += nfm.horizontalAdvance(r.mnemonic) + 12;
            if (!r.comment.isEmpty() && x < rc.right() - pad) {
                QFont itf = base; itf.setItalic(true); p->setFont(itf); p->setPen(dim);
                p->drawText(QRect(x, rc.top(), rc.right() - pad - x, rc.height()), Qt::AlignVCenter, QFontMetrics(itf).elidedText(r.comment, Qt::ElideRight, rc.right() - pad - x));
            }
            break;
        }
        }
        p->restore();
    }
private:
    static void tag(QPainter *p, const QRect &r, const QString &t, QColor c, const QFont &base) {
        QFont f = base; f.setBold(true); f.setPointSizeF(base.pointSizeF() * 0.8);
        p->setFont(f); p->setPen(QPen(c, 1)); p->setBrush(Qt::NoBrush);
        p->drawRoundedRect(r, 3, 3); p->drawText(r, Qt::AlignCenter, t);
    }
    const RowModel *model_;
};

/// The section header that is scrolled past the top stays pinned there (the next header pushes it up when it arrives), so the "Program (n lines)" bar,
/// STACK, MEMORY, MATRICES and STORAGE REGISTERS are always visible for the part of the list under them. The pinned bar is an overlay on the viewport
/// painted by the same delegate as the row itself; it lets clicks and the wheel through.
class StickyListView : public QListView {
public:
    StickyListView(const RowModel *m, QWidget *parent) : QListView(parent), model_(m) {
        sticky_ = new QWidget(viewport());
        sticky_->setAttribute(Qt::WA_TransparentForMouseEvents);
        sticky_->setObjectName(QStringLiteral("stickyHeader"));
        sticky_->setAttribute(Qt::WA_OpaquePaintEvent);
        sticky_->hide();
        sticky_->installEventFilter(this);
        connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { place(); });
        connect(verticalScrollBar(), &QScrollBar::rangeChanged, this, [this] { place(); });
        connect(m, &QAbstractItemModel::modelReset, this, [this] { place(); });
        connect(m, &QAbstractItemModel::dataChanged, this, [this] { place(); });
        connect(m, &QAbstractItemModel::layoutChanged, this, [this] { place(); });
    }
    /// Row of the header shown pinned (or -1) and its geometry in the viewport; for tests.
    int stickyRow() const { return stickyRow_; }
    QRect stickyRect() const { return sticky_->isVisible() ? sticky_->geometry() : QRect(); }
    bool stickyVisible() const { return sticky_->isVisible(); }

protected:
    void resizeEvent(QResizeEvent *e) override { QListView::resizeEvent(e); place(); }
    void showEvent(QShowEvent *e) override { QListView::showEvent(e); place(); }
    void scrollContentsBy(int dx, int dy) override { QListView::scrollContentsBy(dx, dy); place(); }
    bool eventFilter(QObject *o, QEvent *e) override {
        if (o == sticky_ && e->type() == QEvent::Paint && stickyRow_ >= 0) {
            QPainter p(sticky_);
            QStyleOptionViewItem opt; initViewItemOption(&opt);
            opt.rect = sticky_->rect();
            itemDelegate()->paint(&p, opt, model()->index(stickyRow_, 0));
            return true;
        }
        return QListView::eventFilter(o, e);
    }

private:
    void place() {
        const auto &rows = model_->rows;
        int cur = -1, next = -1;
        // headers sit at the top of their section: the pinned one is the last header whose row starts at or above the top edge
        for (int i = 0; i < int(rows.size()); ++i) {
            if (rows[i].kind != RowKind::Header) continue;
            const QRect r = visualRect(model()->index(i, 0));
            if (r.top() <= 0) cur = i;
            else { next = i; break; }
        }
        if (cur < 0) { stickyRow_ = -1; sticky_->hide(); return; }
        const int h = visualRect(model()->index(cur, 0)).height();
        int y = 0;
        if (next >= 0) y = std::min(0, visualRect(model()->index(next, 0)).top() - h);       // pushed up by the next header
        // the header's own row is still fully visible at the top: nothing to pin yet
        if (visualRect(model()->index(cur, 0)).top() == 0 && y == 0) { /* identical to the row below it: harmless, keeps the logic simple */ }
        stickyRow_ = cur;
        sticky_->setGeometry(0, y, viewport()->width(), h);
        sticky_->show(); sticky_->raise(); sticky_->update();
    }
    const RowModel *model_;
    QWidget *sticky_;
    int stickyRow_ = -1;
};

InspectorPanel::InspectorPanel(QWidget *parent) : QWidget(parent) {
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0); outer->setSpacing(0);
    empty_ = new QLabel(this);
    empty_->setWordWrap(true); empty_->setAlignment(Qt::AlignCenter); empty_->setMargin(20);
    outer->addWidget(empty_);
    content_ = new QWidget(this);
    auto *col = new QVBoxLayout(content_);
    col->setContentsMargins(0, 0, 0, 0); col->setSpacing(0);
    header_ = new QLabel(content_);
    header_->setTextFormat(Qt::RichText);
    header_->setContentsMargins(10, 8, 10, 4);
    header_->setWordWrap(true);
    col->addWidget(header_);

    auto *seg = new QWidget(content_);
    auto *sl = new QHBoxLayout(seg); sl->setContentsMargins(10, 0, 10, 6); sl->setSpacing(0);
    paneButtons_ = new QButtonGroup(this);
    const char *names[3] = {"Both", "Registers", "Program"};
    for (int i = 0; i < 3; ++i) {
        auto *b = new QToolButton(seg);
        b->setText(QString::fromLatin1(names[i])); b->setCheckable(true); b->setAutoRaise(false);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        b->setObjectName(QStringLiteral("pane%1").arg(i));
        paneButtons_->addButton(b, i); sl->addWidget(b);
    }
    paneButtons_->setExclusive(true);
    col->addWidget(seg);

    auto *zeroBar = new QWidget(content_);
    auto *zl = new QHBoxLayout(zeroBar); zl->setContentsMargins(10, 0, 10, 2);
    zeros_ = new QCheckBox(QStringLiteral("Non-zero registers only"), zeroBar);
    zl->addWidget(zeros_); zl->addStretch();
    col->addWidget(zeroBar);

    regModel_ = new RowModel(this); progModel_ = new RowModel(this);
    auto makeView = [&](RowModel *m) {
        auto *v = new StickyListView(m, content_);
        v->setModel(m); v->setItemDelegate(new RowDelegate(m, v));
        v->setUniformItemSizes(false); v->setFrameShape(QFrame::NoFrame);
        v->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        v->setSelectionMode(QAbstractItemView::NoSelection);
        v->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        v->setContextMenuPolicy(Qt::CustomContextMenu);
        v->setMouseTracking(true);
        return v;
    };
    regView_ = makeView(regModel_); progView_ = makeView(progModel_);
    col->addWidget(regView_, 1); col->addWidget(progView_, 1);
    outer->addWidget(content_, 1);

    QSettings st;
    pane_ = Pane(std::clamp(st.value(QStringLiteral("inspectorPane"), 0).toInt(), 0, 2));
    hideZeros_ = st.value(QStringLiteral("inspectorHideZeros"), false).toBool();
    zeros_->setChecked(hideZeros_);
    paneButtons_->button(pane_)->setChecked(true);
    connect(paneButtons_, &QButtonGroup::idClicked, this, [this](int id) { setPane(Pane(id)); });
    connect(zeros_, &QCheckBox::toggled, this, [this](bool on) { setHideZeros(on); });
    connect(progView_, &QListView::doubleClicked, this, [this](const QModelIndex &i) {
        const Row &r = progModel_->rows[i.row()];
        if (r.kind == RowKind::Prog && r.step > 0) emit editComment(r.step);
    });
    connect(regView_, &QListView::clicked, this, [this](const QModelIndex &i) {
        const Row &r = regModel_->rows[i.row()];
        if (r.kind == RowKind::Matrix && !r.empty) {
            if (expanded_.contains(r.step)) expanded_.remove(r.step); else expanded_.insert(r.step);
            rebuild();
        }
    });
    connect(regView_, &QWidget::customContextMenuRequested, this, [this](const QPoint &pt) {
        QModelIndex i = regView_->indexAt(pt);
        if (!i.isValid()) return;
        const Row &r = regModel_->rows[i.row()];
        QMenu m;
        if (r.kind == RowKind::Reg) {
            QString copy = r.value, dec = r.sub;
            m.addAction(QStringLiteral("Copy %1").arg(copy), [copy] { QApplication::clipboard()->setText(copy); });
            if (!dec.isEmpty()) m.addAction(QStringLiteral("Copy decimal %1").arg(dec), [dec] { QApplication::clipboard()->setText(dec); });
        } else if (r.kind == RowKind::Matrix && !r.empty) {
            QString t = r.copyText;
            m.addAction(QStringLiteral("Copy Matrix %1 as TSV").arg(r.name), [t] { QApplication::clipboard()->setText(t); });
        }
        if (!m.isEmpty()) m.exec(regView_->viewport()->mapToGlobal(pt));
    });
    connect(progView_, &QWidget::customContextMenuRequested, this, [this](const QPoint &pt) {
        QModelIndex i = progView_->indexAt(pt);
        if (!i.isValid()) return;
        const Row &r = progModel_->rows[i.row()];
        if (r.kind != RowKind::Prog || r.step <= 0) return;
        QMenu m; int step = r.step;
        m.addAction(r.comment.isEmpty() ? QStringLiteral("Add Comment…") : QStringLiteral("Edit Comment…"), [this, step] { emit editComment(step); });
        m.exec(progView_->viewport()->mapToGlobal(pt));
    });
    auto *flash = new QTimer(this);
    flash->setInterval(60);
    connect(flash, &QTimer::timeout, this, [this] { if (regModel_->flashing()) regView_->viewport()->update(); });
    flash->start();
    setEmptyText(QStringLiteral("Nothing to inspect."));
    rebuild();
}

InspectorPanel::~InspectorPanel() = default;

void InspectorPanel::setEmptyText(const QString &t) { empty_->setText(t); }
void InspectorPanel::setSnapshot(std::shared_ptr<const InspectorSnapshot> s) { snap_ = std::move(s); rebuild(); }
void InspectorPanel::setComments(const QMap<int, QString> &c) { if (c == comments_) return; comments_ = c; rebuild(); }
void InspectorPanel::setPane(Pane p) {
    pane_ = p; QSettings().setValue(QStringLiteral("inspectorPane"), int(p));
    paneButtons_->button(p)->setChecked(true); rebuild();
}
void InspectorPanel::setHideZeros(bool on) {
    hideZeros_ = on; QSettings().setValue(QStringLiteral("inspectorHideZeros"), on);
    if (zeros_->isChecked() != on) zeros_->setChecked(on);
    rebuild();
}

QStringList InspectorPanel::registerRowTexts() const { QStringList l; for (auto &r : regModel_->rows) l << regModel_->data(regModel_->index(int(&r - regModel_->rows.data())), Qt::DisplayRole).toString(); return l; }
QStringList InspectorPanel::programRowTexts() const { QStringList l; for (auto &r : progModel_->rows) l << progModel_->data(progModel_->index(int(&r - progModel_->rows.data())), Qt::DisplayRole).toString(); return l; }
QListView *InspectorPanel::listView(Pane which) const { return which == Program ? progView_ : regView_; }
QRect InspectorPanel::pinnedHeaderRect(Pane which) const { return static_cast<StickyListView *>(listView(which))->stickyRect(); }
QString InspectorPanel::pinnedHeaderText(Pane which) const {
    auto *v = static_cast<StickyListView *>(listView(which));
    if (!v->stickyVisible()) return QString();
    const RowModel *m = which == Program ? progModel_ : regModel_;
    return m->rows[size_t(v->stickyRow())].name;
}
QString InspectorPanel::headerText() const { return header_->text(); }

void InspectorPanel::rebuild() {
    const InspectorSnapshot *s = snap_.get();
    empty_->setVisible(!s);
    content_->setVisible(s != nullptr);
    if (!s) return;

    // header
    QString modelName = s->model == 15 && s->variant == 152 ? QStringLiteral("HP-15C (15.2)") : QStringLiteral("HP-%1C").arg(s->model);
    QString tags;
    auto tag = [&](const QString &t, const QColor &c) { tags += QStringLiteral(" <span style=\"color:%1;border:1px solid %1;font-size:small\">&nbsp;%2&nbsp;</span>").arg(c.name(), t); };
    if (s->extendedMemory) tag(QStringLiteral("ext. memory"), QApplication::palette().color(QPalette::PlaceholderText));
    if (s->isComplex()) tag(QStringLiteral("Complex"), QColor(0xE07A5F));
    if (s->running) tag(QStringLiteral("RUNNING"), runningColour());
    else if (s->programMode) tag(QStringLiteral("PRGM"), fColour());
    QString flags;
    if (s->model != 12) {
        QStringList f;
        for (int n : s->setFlags()) f << (s->model == 16 && n == 4 ? QStringLiteral("4(C)") : s->model == 16 && n == 5 ? QStringLiteral("5(G)") : s->model == 15 && n == 8 ? QStringLiteral("8(C)") : QString::number(n));
        flags = QStringLiteral("flags ") + (f.isEmpty() ? QStringLiteral("–") : f.join(' '));
    }
    QStringList second;
    if (!s->modeSummary().isEmpty()) second << QStringLiteral("<tt>%1</tt>").arg(s->modeSummary().toHtmlEscaped());
    if (!flags.isEmpty()) second << flags;
    if (s->hasProgram) second << QStringLiteral("<tt>line %1 / %2</tt>").arg(s->line, 3, 10, QLatin1Char('0')).arg(s->lines, 3, 10, QLatin1Char('0'));
    header_->setText(QStringLiteral("<b>%1</b>%2<br><span style=\"color:gray\">%3</span>").arg(modelName, tags, second.join(QStringLiteral("&nbsp;&nbsp;·&nbsp;&nbsp;"))));

    const bool showReg = pane_ != Program, showProg = pane_ != Registers;
    zeros_->parentWidget()->setVisible(showReg);
    regView_->setVisible(showReg); progView_->setVisible(showProg);

    // registers
    {
        std::vector<Row> rows;
        auto header = [&](const QString &t) { Row r; r.kind = RowKind::Header; r.name = t; rows.push_back(r); };
        auto regRow = [&](const RegisterValue &v, const RegisterValue *imag = nullptr, const QString &descriptor = QString()) {
            Row r; r.kind = RowKind::Reg; r.name = v.name; r.stamp = v.stored() + (imag ? imag->raw * 31 : 0) + (v.text.size() * 131u);
            r.value = v.text;
            if (!descriptor.isEmpty()) r.value = descriptor;
            else if (imag) { bool neg = imag->text.startsWith('-'); r.value = QStringLiteral("%1 %2 %3i").arg(v.text, neg ? QStringLiteral("−") : QStringLiteral("+"), neg ? imag->text.mid(1) : imag->text); }
            else if (v.isInteger && s->base == 2 && v.text.size() > 8) {
                QString out; int n = 0;
                for (int i = int(v.text.size()) - 1; i >= 0; --i, ++n) { if (n && n % 4 == 0) out.prepend(' '); out.prepend(v.text[i]); }
                r.value = out;
            }
            if (v.isInteger) { r.suffix = s->base == 16 ? "h" : s->base == 8 ? "o" : s->base == 2 ? "b" : "d"; if (s->base != 10) r.sub = v.decimal; }
            r.help = v.isInteger ? QStringLiteral("%1 = %2 (decimal)").arg(v.name, v.decimal) : QStringLiteral("%1 = %2").arg(v.name, v.text);
            rows.push_back(r);
        };
        header(s->model == 12 ? QStringLiteral("Stack and financial registers") : QStringLiteral("Stack"));
        {
            // Shown top-to-bottom the way users think of the stack: I, LAST X, T, Z, Y, X, then any others (12C financial registers).
            static const QStringList top = {QStringLiteral("I"), QStringLiteral("LAST X"), QStringLiteral("T"), QStringLiteral("Z"), QStringLiteral("Y"), QStringLiteral("X")};
            std::vector<const RegisterValue *> ordered;
            for (const QString &n : top) for (auto &v : s->stack) if (v.name == n) ordered.push_back(&v);
            for (auto &v : s->stack) if (!top.contains(v.name)) ordered.push_back(&v);
            for (const RegisterValue *v : ordered) regRow(*v, s->imaginaryOf(v->name), s->descriptorText(*v));
        }
        if (s->pool) {
            header(QStringLiteral("Memory"));
            Row pr; pr.kind = RowKind::Pool; pr.pool = std::make_shared<Pool15>(*s->pool); rows.push_back(pr);
            header(QStringLiteral("Matrices"));
            for (auto &m : s->matrices) {
                Row r; r.kind = RowKind::Matrix; r.name = m.letter(); r.step = m.index; r.empty = m.isEmpty() || m.values.empty();
                r.value = QStringLiteral("%1×%2").arg(m.rows).arg(m.cols);
                if (!m.isEmpty()) r.sub = QStringLiteral("(%1 regs, Nut %2–%3)").arg(m.rows * m.cols).arg(hex2(m.first), hex2(m.first + m.rows * m.cols - 1));
                r.result = m.isResult; r.copyText = m.tsv();
                r.expanded = !r.empty && expanded_.contains(m.index);
                rows.push_back(r);
                if (r.expanded) {
                    int rr = std::min(m.rows, 99), cc = std::min(m.cols, 6);
                    for (int i = 1; i <= std::max(1, rr); ++i) {
                        QStringList cells; for (int j = 1; j <= std::max(1, cc); ++j) cells << (m.value(i, j) ? m.value(i, j)->text : QString());
                        Row g; g.kind = RowKind::Grid; g.value = cells.join(QStringLiteral("   ")); g.step = m.index; rows.push_back(g);
                    }
                    if (m.cols > cc) { Row t; t.kind = RowKind::Grid; t.value = QStringLiteral("%1×%2: copy as TSV to see all").arg(m.rows).arg(m.cols); rows.push_back(t); }
                }
            }
        }
        header(QStringLiteral("Storage registers (%1)").arg(s->registerCount));
        int shown = 0;
        for (auto &v : s->registers) { if (hideZeros_ && v.stored() == 0) continue; regRow(v); ++shown; }
        if (!shown) { Row t; t.kind = RowKind::Text; t.value = QStringLiteral("All registers are zero."); rows.push_back(t); }
        int keep = regView_->verticalScrollBar()->value();
        regModel_->setRows(std::move(rows));
        regView_->verticalScrollBar()->setValue(keep);
    }
    // program
    {
        std::vector<Row> rows;
        Row h; h.kind = RowKind::Header; h.name = s->hasProgram ? QStringLiteral("Program (%1 lines)").arg(s->lines) : QStringLiteral("Program"); rows.push_back(h);
        if (!s->hasProgram) {
            Row t; t.kind = RowKind::Text; t.value = QStringLiteral("No program listing: this calculator image has no program memory the inspector can read."); rows.push_back(t);
        } else {
            const QString zero = s->model == 12 ? QStringLiteral("00") : QStringLiteral("000");
            Row z; z.kind = RowKind::Prog; z.name = zero; z.current = s->line == 0; z.running = s->running; z.step = 0; rows.push_back(z);
            for (auto &p : s->program) {
                Row r; r.kind = RowKind::Prog; r.name = p.number(s->model); r.keys = p.keys; r.prefix = p.prefix; r.mnemonic = p.mnemonic;
                r.step = p.line; r.current = p.line == s->line; r.running = s->running; r.comment = comments_.value(p.line);
                rows.push_back(r);
            }
        }
        int cur = -1;
        for (size_t i = 0; i < rows.size(); ++i) if (rows[i].kind == RowKind::Prog && rows[i].current) cur = int(i);
        bool lineMoved = cur >= 0 && (lastLine_ != cur);
        int keep = progView_->verticalScrollBar()->value();
        progModel_->setRows(std::move(rows));
        progView_->verticalScrollBar()->setValue(keep);
        if (cur >= 0 && lineMoved) progView_->scrollTo(progModel_->index(cur), QAbstractItemView::PositionAtCenter);
        lastLine_ = cur;
    }
}

}  // namespace vc
