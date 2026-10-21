// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
//
// CalculatorWidget.h - the calculator as a product photo (PhotoFaceView.swift): the photo with its LCD glass
// emptied, the live display painted over the glass, an invisible click area over every key with three zones
// (gold f legend above the cap, the cap, blue g legend on its front), a pressed key that sinks, latched keys,
// hover help, the computer keyboard (HardwareKeyRouter); optional f/g legend clicks.
#pragma once
#include "Emulator.h"
#include "FaceModel.h"
#include "HardwareKeyRouter.h"
#include <QImage>
#include <memory>
#include <QPixmap>
#include <QSet>
#include <QWidget>

namespace vc {

enum class KeyPart { Face, FLabel, GLabel };

class CalculatorWidget : public QWidget {
    Q_OBJECT
public:
    explicit CalculatorWidget(QWidget *parent = nullptr);
    ~CalculatorWidget() override;

    int modelId() const { return model_; }
    void setModelId(int id);                          // 12, 15 or 16
    void setDisplay(const DisplaySnapshot &s);
    void setPressed(const QSet<int> &codes) { pressed_ = codes; update(); }
    void setLatched(const QSet<int> &codes) { latched_ = codes; update(); }
    /// Clicks on the gold / blue legends press f / g + the key (View ▸ Click f/g Labels). Off: the whole key area is the key.
    void setLegendClicks(bool on) { legendClicks_ = on; update(); }
    bool legendClicks() const { return legendClicks_; }
    static QColor lcdLift(int model);                 // screened over the photo's glass (invalid = none: the 12C keeps its original LCD)
    /// Key help on hover: the tooltip with the key's functions and the keyboard-shortcut bubble (View ▸ Show Key Help, off by default).
    void setShowKeyHelp(bool on) { showKeyHelp_ = on; update(); }
    bool showKeyHelp() const { return showKeyHelp_; }
    void setKeysEnabled(bool on) { keysEnabled_ = on; }
    const DisplaySnapshot &display() const { return display_; }
    HardwareKeyRouter &router() { return router_; }

    QRectF faceRect() const;                          // where the photo sits inside the widget
    QRectF glassRect() const;                         // the LCD window, in widget pixels
    /// The key and part under a widget point (code -1: none).
    int keyAt(const QPointF &pos, KeyPart *part = nullptr) const;
    QRectF keyRect(int code) const;                   // the cap, in widget pixels
    QRectF zoneRect(int code, KeyPart part) const;    // a zone of a key, in widget pixels
    QString helpFor(int code, KeyPart part) const;

    double faceAspect() const { return faceModel_ ? faceModel_->aspect() : 0.0; }   // width / height of the face photo (0: no model yet)
    QSize sizeHint() const override { return {760, int(760 / faceModel_->aspect())}; }
    QSize minimumSizeHint() const override { return {360, 220}; }

    /// Keyboard events as the main window forwards them (also used by tests). True when used.
    bool handleKey(QKeyEvent *e);

signals:
    void keyPressed(int code);                        // the cap (or a keyboard key): hold until keyReleased
    void legendPressed(int code, int shift);          // a click on the gold (31) or blue (32) legend
    void keyReleased(int code);
    void latchToggled(int code);                      // right-click / Ctrl-click: hold the key down

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override { cache_ = QPixmap(); }
    void mousePressEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void leaveEvent(QEvent *) override { hoverCode_ = -1; update(); }
    void keyPressEvent(QKeyEvent *e) override { if (!handleKey(e)) QWidget::keyPressEvent(e); }
    void keyReleaseEvent(QKeyEvent *e) override { if (!handleKey(e)) QWidget::keyReleaseEvent(e); }
    void focusOutEvent(QFocusEvent *) override { router_.releaseAll(); }
    bool event(QEvent *e) override;

private:
    const FaceGeometry *geometry() const;
    QRectF toWidget(const Rect4 &r) const;
    void zonesOf(int code, Rect4 *face, Rect4 *f, Rect4 *g, bool *hasF, bool *hasG) const;

    int model_ = 15;
    std::unique_ptr<FaceModel> faceModel_;
    QImage face_;
    QPixmap cache_;
    DisplaySnapshot display_;
    QSet<int> pressed_, latched_;
    bool keysEnabled_ = true, showKeyHelp_ = false, legendClicks_ = true;
    int mouseKey_ = -1, hoverCode_ = -1;
    KeyPart hoverPart_ = KeyPart::Face;
    HardwareKeyRouter router_;
};

}  // namespace vc
