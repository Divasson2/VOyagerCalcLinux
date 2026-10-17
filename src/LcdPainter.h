// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
//
// LcdPainter.h - draws the LCD segments the firmware lit (ten slanted seven-segment digits, points,
// comma tails, minus sign, annunciators): a port of LCDView.swift (the "over a photo" variant).
#pragma once
#include "Emulator.h"
#include <QPainter>
#include <QRectF>

namespace vc {

/// Paint `s` into `glass` (the empty LCD window of the face, in widget pixels).
void paintLcd(QPainter &p, const QRectF &glass, const DisplaySnapshot &s);

}  // namespace vc
