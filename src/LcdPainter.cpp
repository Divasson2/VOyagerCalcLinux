// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
#include "LcdPainter.h"
#include "Model.h"
#include <QPainterPath>
#include <QFont>
#include <vector>

namespace vc {

// Path of segment s (0 = a ... 6 = g) in a digit box, slanted like the Voyager LCD.
static QPainterPath segment(int s, const QRectF &r) {
    const double t = r.width() * 0.17, g = t * 0.18, w = r.width(), h = r.height(), mid = h / 2, slant = 0.10;
    auto p = [&](double x, double y) { return QPointF(r.left() + x + (h - y) * slant, r.top() + y); };
    QPainterPath path;
    auto horizontal = [&](double y) {
        path.moveTo(p(t / 2 + g, y)); path.lineTo(p(t + g, y - t / 2)); path.lineTo(p(w - t - g, y - t / 2));
        path.lineTo(p(w - t / 2 - g, y)); path.lineTo(p(w - t - g, y + t / 2)); path.lineTo(p(t + g, y + t / 2));
        path.closeSubpath();
    };
    auto vertical = [&](double x, double y0, double y1) {
        path.moveTo(p(x, y0 + g)); path.lineTo(p(x + t / 2, y0 + t / 2 + g)); path.lineTo(p(x + t / 2, y1 - t / 2 - g));
        path.lineTo(p(x, y1 - g)); path.lineTo(p(x - t / 2, y1 - t / 2 - g)); path.lineTo(p(x - t / 2, y0 + t / 2 + g));
        path.closeSubpath();
    };
    switch (s) {
    case 0: horizontal(t / 2); break;
    case 1: vertical(w - t / 2, t / 2, mid); break;
    case 2: vertical(w - t / 2, mid, h - t / 2); break;
    case 3: horizontal(h - t / 2); break;
    case 4: vertical(t / 2, mid, h - t / 2); break;
    case 5: vertical(t / 2, t / 2, mid); break;
    default: horizontal(mid); break;
    }
    return path;
}

// Comma tail, in dot units (same measured points as LCDView.commaTailPoints).
static QPainterPath commaTail(const QRectF &dot) {
    static const double pts[6][2] = {{-0.10, 1.06}, {0.66, 1.06}, {0.12, 1.68}, {-0.08, 1.86}, {-0.30, 1.84}, {-0.34, 1.66}};
    QPainterPath path;
    for (int i = 0; i < 6; ++i) {
        QPointF q(dot.left() + pts[i][0] * dot.width(), dot.top() + pts[i][1] * dot.width());
        if (i == 0) path.moveTo(q); else path.lineTo(q);
    }
    path.closeSubpath();
    return path;
}

void paintLcd(QPainter &p, const QRectF &glass, const DisplaySnapshot &s) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    const QColor ink(0x10, 0x14, 0x10, int(0.92 * 255)), faint(0x10, 0x14, 0x10, int(0.025 * 255));
    const double cellW = glass.width() / 11.2, digitH = glass.height() * 0.39, top = glass.top() + glass.height() * 0.22;
    const double left = glass.left() + cellW * -0.13;
    p.setPen(Qt::NoPen);
    for (int i = 0; i < 10; ++i) {
        double x = left + (i + 1) * cellW;
        QRectF rect(x + cellW * 0.14, top, cellW * 0.62, digitH);
        p.setBrush(faint);
        for (int k = 0; k < 7; ++k) p.drawPath(segment(k, rect));
        p.setBrush(ink);
        for (int k = 0; k < 7; ++k) if (s.seg[i] >> k & 1) p.drawPath(segment(k, rect));
        double d = cellW * 0.13;
        QRectF dot(rect.right() + cellW * 0.06, rect.bottom() - d, d, d);
        if (s.dot[i] || s.comma[i]) p.drawRect(dot);
        if (s.comma[i]) p.drawPath(commaTail(dot));
    }
    QRectF signRect(left + cellW * 0.15, top, cellW * 0.62, digitH);
    p.setBrush(s.minus ? ink : faint);
    p.drawPath(segment(6, signRect));

    const double annY = top + digitH + glass.height() * 0.08;
    QFont f; f.setPixelSize(int(std::max(6.0, glass.height() * 0.15))); f.setWeight(QFont::DemiBold);
    p.setFont(f);
    for (const Annunciator &a : Model::with(s.model).annunciators()) {
        bool on = s.annBits >> a.bit & 1;
        p.setPen(on ? ink : faint);
        p.drawText(QPointF(left + (a.cell + 1) * cellW, annY + f.pixelSize()), QString::fromLatin1(a.label));
    }
    p.restore();
}

}  // namespace vc
