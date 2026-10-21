// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
#include "CalculatorWidget.h"
#include "LcdPainter.h"
#include "Model.h"
#include <QHelpEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>

// Resources live in a static library: register them explicitly (must be done at global scope).
static void initResources() { Q_INIT_RESOURCE(voyagercalc); }

namespace vc {

static const QColor kGold(0xE3, 0xA6, 0x3A), kBlueLegend(0x5D, 0xA9, 0xE8);

const FaceGeometry *CalculatorWidget::geometry() const {
    switch (model_) { case 12: return &kFace12C; case 16: return &kFace16C; default: return &kFace15C; }
}

CalculatorWidget::CalculatorWidget(QWidget *parent) : QWidget(parent) {
    ::initResources();
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setModelId(15);
    router_.model = [this]() -> const Model & { return Model::with(model_); };
}

CalculatorWidget::~CalculatorWidget() = default;

void CalculatorWidget::setModelId(int id) {
    if (id != 12 && id != 15 && id != 16) id = 15;
    if (faceModel_ && id == model_) return;
    model_ = id;
    faceModel_ = std::make_unique<FaceModel>(*geometry());
    face_.load(QString::fromLatin1(geometry()->image));
    cache_ = QPixmap();
    updateGeometry();
    update();
}

void CalculatorWidget::setDisplay(const DisplaySnapshot &s) {
    display_ = s;
    if (s.model && s.model != model_) setModelId(s.model);
    update();
}

QRectF CalculatorWidget::faceRect() const {
    double a = faceModel_->aspect(), w = width(), h = height();
    if (w / h > a) { double fw = h * a; return {(w - fw) / 2, 0, fw, h}; }
    double fh = w / a;
    return {0, (h - fh) / 2, w, fh};
}

QRectF CalculatorWidget::toWidget(const Rect4 &r) const {
    QRectF fr = faceRect();
    return {fr.x() + r.x * fr.width(), fr.y() + r.y * fr.height(), r.w * fr.width(), r.h * fr.height()};
}

QRectF CalculatorWidget::glassRect() const { return toWidget(geometry()->lcd); }

QRectF CalculatorWidget::keyRect(int code) const {
    return faceModel_->hasKey(code) ? toWidget(faceModel_->keyRect(code)) : QRectF();
}

// The zones of a key on the photo (KeyZones.swift): its hit area split at the cap's top edge (gold legend above)
// and at the top of the blue legend on the cap's front.
void CalculatorWidget::zonesOf(int code, Rect4 *face, Rect4 *f, Rect4 *g, bool *hasF, bool *hasG) const {
    const Model &m = Model::with(model_);
    const KeySpec *k = m.key(code);
    Rect4 area = faceModel_->hitRect(code), cap = faceModel_->keyRect(code);
    const auto &geo = faceModel_->geometry();
    double capH = geo.hasKey[24] ? geo.key[24].h : cap.h;
    *hasF = k && k->f[0]; *hasG = k && k->g[0];
    double minY = area.y, maxY = area.y + area.h;
    double top = *hasF ? std::min(std::max(cap.y, minY), maxY) : minY;
    double gTop = cap.y + cap.h - capH * m.gZoneFraction();
    double bottom = *hasG ? std::max(std::min(gTop, maxY), top) : maxY;
    *face = {area.x, top, area.w, bottom - top};
    *f = {area.x, minY, area.w, top - minY};
    *g = {area.x, bottom, area.w, maxY - bottom};
}

QRectF CalculatorWidget::zoneRect(int code, KeyPart part) const {
    if (!faceModel_->hasKey(code)) return {};
    Rect4 face, f, g; bool hasF, hasG;
    zonesOf(code, &face, &f, &g, &hasF, &hasG);
    switch (part) {
    case KeyPart::Face: return toWidget(face);
    case KeyPart::FLabel: return hasF ? toWidget(f) : QRectF();
    default: return hasG ? toWidget(g) : QRectF();
    }
}

QColor CalculatorWidget::lcdLift(int model) {
    // per model (Jose, 0.7.1 test13): 15C and 16C lighter grey; the 12C keeps the photo's original glass (no lift)
    return model == 12 ? QColor() : QColor(0x66, 0x66, 0x66);
}

int CalculatorWidget::keyAt(const QPointF &pos, KeyPart *part) const {
    QRectF fr = faceRect();
    if (!fr.contains(pos)) return -1;
    double px = (pos.x() - fr.x()) / fr.width(), py = (pos.y() - fr.y()) / fr.height();
    int code = faceModel_->keyAt(px, py);
    if (code < 0) return -1;
    const KeySpec *k = Model::with(model_).key(code);
    if (!k) return -1;
    if (part) {
        Rect4 face, f, g; bool hasF, hasF2;
        zonesOf(k->code, &face, &f, &g, &hasF, &hasF2);
        auto in = [&](const Rect4 &r) { return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h; };
        *part = !legendClicks_ ? KeyPart::Face : hasF && in(f) ? KeyPart::FLabel : hasF2 && in(g) ? KeyPart::GLabel : KeyPart::Face;
    }
    return k->code;
}

QString CalculatorWidget::helpFor(int code, KeyPart) const {
    const KeySpec *k = Model::with(model_).key(code);
    if (!k) return {};
    QString t = shortcutHelp(*k);
    if (k->f[0]) t += QStringLiteral(" — gold legend: f %1").arg(QString::fromUtf8(k->f));
    if (k->g[0]) t += QStringLiteral(" — blue legend: g %1").arg(QString::fromUtf8(k->g));
    return t;
}

void CalculatorWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.fillRect(rect(), QColor(0x1C, 0x17, 0x14));
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.setRenderHint(QPainter::Antialiasing, true);
    QRectF fr = faceRect();
    QSize px = fr.size().toSize() * devicePixelRatioF();
    if (cache_.isNull() || cache_.size() != px) {
        cache_ = QPixmap::fromImage(face_.scaled(px, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
        cache_.setDevicePixelRatio(devicePixelRatioF());
    }
    p.drawPixmap(fr.topLeft(), cache_);
    if (const QColor lift = lcdLift(model_); lift.isValid()) {
        // the LCD background: the photo's glass lifted to a lighter grey (fixed per model, no option).
        // Screen blend keeps the photo's texture and edges; the segments are drawn on top, unchanged.
        p.save();
        p.setCompositionMode(QPainter::CompositionMode_Screen);
        QRectF g = glassRect();
        p.setPen(Qt::NoPen); p.setBrush(lift);
        p.drawRoundedRect(g, g.height() * 0.04, g.height() * 0.04);
        p.restore();
    }
    paintLcd(p, glassRect(), display_);

    // pressed keys sink: shadow where the cap was, the cap shifted down and darkened
    for (int c : pressed_) {
        int code = c == 35 ? 25 : c;
        if (!faceModel_->hasKey(code)) continue;
        QRectF cap = keyRect(code);
        double dy = std::max(1.0, cap.height() * 0.045), corner = std::min(cap.width(), cap.height()) * 0.08;
        p.save();
        QPainterPath shadow; shadow.addRoundedRect(cap, corner, corner);
        p.fillPath(shadow, QColor(0, 0, 0, int(0.55 * 255)));
        QPainterPath clip; clip.addRoundedRect(QRectF(cap.x(), cap.y() + dy, cap.width(), cap.height() - dy), corner, corner);
        p.setClipPath(clip);
        p.drawPixmap(QPointF(fr.x(), fr.y() + dy), cache_);
        p.fillPath(clip, QColor(0, 0, 0, int(0.12 * 255)));
        p.restore();
    }
    // latched keys: a gold frame
    for (int c : latched_) {
        int code = c == 35 ? 25 : c;
        if (!faceModel_->hasKey(code)) continue;
        QRectF cap = keyRect(code).adjusted(-1, -1, 1, 1);
        p.setPen(QPen(kGold, std::max(1.5, cap.height() * 0.07)));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(cap, cap.height() * 0.12, cap.height() * 0.12);
    }
    // hover: the legend under the pointer lit, the keyboard shortcut over the cap
    if (hoverCode_ >= 0) {
        const KeySpec *k = Model::with(model_).key(hoverCode_);
        if (k && hoverPart_ != KeyPart::Face) {
            // 0.7.1: a brighter highlight (fill + outline) so the clickable legend is obvious
            QRectF r = zoneRect(hoverCode_, hoverPart_);
            QColor c = hoverPart_ == KeyPart::FLabel ? kGold : kBlueLegend, line = c.lighter(125);
            c.setAlphaF(0.40); line.setAlphaF(0.95);
            p.setPen(QPen(line, std::max(1.2, r.height() * 0.06))); p.setBrush(c);
            p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
        } else if (k && k->nShortcuts > 0 && showKeyHelp_) {
            QRectF cap = keyRect(hoverCode_);
            QString hint = shortcutHint(*k);
            QFont f = font(); f.setBold(true); f.setPixelSize(int(std::max(9.0, cap.height() * 0.28)));
            p.setFont(f);
            QFontMetricsF fm(f);
            double w = fm.horizontalAdvance(hint) + 10, h = fm.height() + 2;
            QRectF bubble(cap.center().x() - w / 2, zoneRect(hoverCode_, KeyPart::Face).top(), w, h);
            p.setPen(Qt::NoPen); p.setBrush(QColor(0, 0, 0, 190));
            p.drawRoundedRect(bubble, h / 2, h / 2);
            p.setPen(Qt::white);
            p.drawText(bubble, Qt::AlignCenter, hint);
        }
    }
}

void CalculatorWidget::mousePressEvent(QMouseEvent *e) {
    setFocus();
    if (!keysEnabled_) return;
    KeyPart part;
    int c = keyAt(e->position(), &part);
    if (c < 0) return;
    if (e->button() == Qt::RightButton || (e->button() == Qt::LeftButton && (e->modifiers() & Qt::ControlModifier))) {
        emit latchToggled(c);
        return;
    }
    if (e->button() != Qt::LeftButton) return;
    mouseKey_ = c;
    if (part == KeyPart::FLabel) emit legendPressed(c, 31);
    else if (part == KeyPart::GLabel) emit legendPressed(c, 32);
    else emit keyPressed(c);
}

void CalculatorWidget::mouseReleaseEvent(QMouseEvent *e) {
    if (e->button() == Qt::LeftButton && mouseKey_ >= 0) { emit keyReleased(mouseKey_); mouseKey_ = -1; }
}

void CalculatorWidget::mouseMoveEvent(QMouseEvent *e) {
    KeyPart part = KeyPart::Face;
    int c = keyAt(e->position(), &part);
    if (c != hoverCode_ || part != hoverPart_) { hoverCode_ = c; hoverPart_ = part; update(); }
}

bool CalculatorWidget::event(QEvent *e) {
    if (e->type() == QEvent::ToolTip) {
        auto *h = static_cast<QHelpEvent *>(e);
        if (!showKeyHelp_) { QToolTip::hideText(); e->ignore(); return true; }
        KeyPart part;
        int c = keyAt(h->pos(), &part);
        if (c >= 0) QToolTip::showText(h->globalPos(), helpFor(c, part), this);
        else QToolTip::hideText();
        return true;
    }
    return QWidget::event(e);
}

bool CalculatorWidget::handleKey(QKeyEvent *e) {
    if (!keysEnabled_) return false;
    const bool down = e->type() == QEvent::KeyPress;
    switch (e->key()) {
    case Qt::Key_Shift: router_.modifier(HardwareKeyRouter::ShiftKey, down); return false;
    case Qt::Key_Alt: router_.modifier(HardwareKeyRouter::AltKey, down); return false;
    default: break;
    }
    if (e->modifiers() & Qt::ControlModifier) return false;       // menu shortcuts
    const int id = e->nativeScanCode() ? int(e->nativeScanCode()) : e->key();
    if (!down) return router_.keyUp(id);
    Shortcut s;
    if (!shortcutFromEvent(e, &s)) return false;
    return router_.keyDown(id, s, e->isAutoRepeat());
}

}  // namespace vc
