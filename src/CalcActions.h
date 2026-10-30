// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
//
// CalcActions.h - what the inspector's context menus do (0.7.4): "GTO nnn" on a program line and RCL / STO / register
// arithmetic / x≷ on a register. Each item is the key sequence a person would press on the calculator, keyed into the
// running firmware, so stack lift, LAST X, program mode and errors behave exactly as on the real machine.
#pragma once
#include "Inspector.h"
#include <QString>
#include <vector>

namespace vc {

class Machine;

struct CalcAction {
    QString title;             // "RCL R.3", "STO+ R5", "x≷ I", "GTO 012"
    std::vector<int> keys;     // key codes (row * 10 + column), empty when !enabled
    bool enabled = false;
    bool separatorBefore = false;
};

namespace CalcActions {
/// The keys of GTO CHS nnn (15C) / GTO . nnn (16C); 12C: g GTO nn in run mode, g GTO . nn in program mode), empty when the line is not there.
std::vector<int> gtoKeys(int model, bool programMode, int line, int lines);
CalcAction gto(const InspectorSnapshot &s, int line);
/// The register's address keys after STO / RCL / x≷ (R0..R9, R.0..R.9, 16C RA..RF and R.A..R.F, I, 12C n/i/PV/PMT/FV); empty: none.
std::vector<int> addressKeys(int model, const QString &name);
/// The register menu for a row of the inspector (stack rows X..LAST X: all items disabled).
std::vector<CalcAction> registerMenu(const InspectorSnapshot &s, const QString &name);
/// Key `keys` on the calculator as taps (a showing Error is cleared first, like a person's first key press would).
void perform(Machine &m, const std::vector<int> &keys);
}  // namespace CalcActions

}  // namespace vc
