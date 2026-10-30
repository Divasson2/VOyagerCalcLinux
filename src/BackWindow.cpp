// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
#include "BackWindow.h"
#include <QCloseEvent>
#include <QPainter>
#include <QSettings>

namespace vc {

BackWindow::BackWindow(QWidget *parent) : QWidget(parent, Qt::Window) {
    setObjectName(QStringLiteral("calculatorBack"));
    setMinimumSize(320, 200);
    setAttribute(Qt::WA_DeleteOnClose, false);
    const QByteArray g = QSettings().value(QStringLiteral("backWindowGeometry")).toByteArray();
    if (g.isEmpty() || !restoreGeometry(g)) resize(sizeHint());
}

QString BackWindow::resourceFor(int model) {
    return model == 15 ? QStringLiteral(":/faces/back_15c.png") : model == 16 ? QStringLiteral(":/faces/back_16c.png") : QString();
}

void BackWindow::setModel(int model) {
    if (model == model_ && !pix_.isNull()) return;
    model_ = model;
    const QString r = resourceFor(model);
    pix_ = r.isEmpty() ? QPixmap() : QPixmap(r);
    setWindowTitle(r.isEmpty() ? QStringLiteral("Calculator Back") : QStringLiteral("HP-%1C Back").arg(model));
    if (pix_.isNull()) hide();
    update();
}

void BackWindow::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.fillRect(rect(), QColor(0x1E1E1E));
    if (pix_.isNull()) return;
    QSize s = pix_.size().scaled(size(), Qt::KeepAspectRatio);
    QRect r(QPoint((width() - s.width()) / 2, (height() - s.height()) / 2), s);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.drawPixmap(r, pix_);
}

void BackWindow::closeEvent(QCloseEvent *e) {
    QSettings().setValue(QStringLiteral("backWindowGeometry"), saveGeometry());
    QWidget::closeEvent(e);
}

}  // namespace vc
