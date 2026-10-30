// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
//
// BackWindow.h - View ▸ Show Calculator Back (0.7.4): the back of the HP-15C or HP-16C (photographs by Warren Young,
// serial numbers and HP / Collector's Edition marks removed) in a separate window that can be resized; the picture keeps
// its proportions and follows the model.
#pragma once
#include <QPixmap>
#include <QWidget>

namespace vc {

class BackWindow : public QWidget {
    Q_OBJECT
public:
    explicit BackWindow(QWidget *parent = nullptr);
    /// 15 or 16; anything else: nothing to show (the window hides).
    void setModel(int model);
    int model() const { return model_; }
    static QString resourceFor(int model);       // ":/faces/back_15c.png", "" for other models
    QSize sizeHint() const override { return {900, 560}; }

protected:
    void paintEvent(QPaintEvent *) override;
    void closeEvent(QCloseEvent *e) override;

private:
    int model_ = 0;
    QPixmap pix_;
};

}  // namespace vc
