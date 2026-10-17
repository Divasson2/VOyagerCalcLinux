// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
#include "WelcomeWidget.h"
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace vc {

WelcomeWidget::WelcomeWidget(QWidget *parent) : QWidget(parent) {
    setAutoFillBackground(true);
    QPalette p = palette(); p.setColor(QPalette::Window, QColor(0x3B, 0x30, 0x2A)); setPalette(p);
    auto *col = new QVBoxLayout(this);
    col->setAlignment(Qt::AlignCenter); col->setSpacing(18);
    auto *title = new QLabel(QStringLiteral("VoyagerCalc"));
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet("color:#E3A63A; font-size:34px; font-weight:bold;");
    auto *text = new QLabel(QStringLiteral("This app runs the firmware of your own CE calculator (12C, 15C or 16C, including multi-model builds), "
                                           "or HP's original 12C firmware with limits. No firmware comes with the app."));
    text->setWordWrap(true); text->setAlignment(Qt::AlignCenter); text->setMaximumWidth(480);
    text->setStyleSheet("color:rgba(255,255,255,0.9); font-size:14px;");
    button_ = new QPushButton(QStringLiteral("Choose Firmware File…"));
    button_->setStyleSheet("QPushButton{background:#B79A63;color:white;font-weight:bold;font-size:15px;padding:10px 22px;border-radius:6px;}"
                           "QPushButton:hover{background:#C5A870;}");
    button_->setCursor(Qt::PointingHandCursor);
    hint_ = new QLabel(QStringLiteral("Pick the .bin file you flash to the calculator (about 112 KB). The app remembers it.\nYou can also drop the file on this window."));
    hint_->setAlignment(Qt::AlignCenter); hint_->setWordWrap(true); hint_->setMaximumWidth(480);
    hint_->setStyleSheet("color:rgba(255,255,255,0.7); font-size:12px;");
    starting_ = new QLabel(QStringLiteral("Starting the calculator…"));
    starting_->setAlignment(Qt::AlignCenter);
    starting_->setStyleSheet("color:rgba(255,255,255,0.85); font-size:14px;");
    starting_->hide();
    for (QWidget *w : {static_cast<QWidget *>(title), static_cast<QWidget *>(text)}) col->addWidget(w, 0, Qt::AlignHCenter);
    col->addWidget(button_, 0, Qt::AlignHCenter); col->addWidget(hint_, 0, Qt::AlignHCenter); col->addWidget(starting_, 0, Qt::AlignHCenter);
    connect(button_, &QPushButton::clicked, this, &WelcomeWidget::chooseFirmware);
}

void WelcomeWidget::setStarting(bool on) {
    button_->setVisible(!on); hint_->setVisible(!on); starting_->setVisible(on);
}

}  // namespace vc
