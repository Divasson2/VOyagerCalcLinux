// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
//
// WelcomeWidget.h - the first-run card (WelcomeView.swift): choose the firmware file; the app remembers it.
#pragma once
#include <QWidget>

class QLabel;
class QPushButton;

namespace vc {

class WelcomeWidget : public QWidget {
    Q_OBJECT
public:
    explicit WelcomeWidget(QWidget *parent = nullptr);
    void setStarting(bool on);
signals:
    void chooseFirmware();
private:
    QLabel *hint_, *starting_;
    QPushButton *button_;
};

}  // namespace vc
