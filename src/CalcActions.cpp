// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
#include "CalcActions.h"
#include "Machine.h"

namespace vc {
namespace CalcActions {

namespace {
constexpr int kDigit[10] = {36, 26, 27, 28, 16, 17, 18, 6, 7, 8};
constexpr int kDot = 37, kSTO = 33, kRCL = 34, kF = 31, kG = 32, kGTO = 11, kRDN = 22, kBSP = 24;
constexpr int kCHS = 5;                         // 15C: GTO CHS nnn (16C: GTO . nnn)
constexpr int kPlus = 39, kMinus = 29, kTimes = 19, kDivide = 9;
constexpr int k15I = 14, k16I = 21;            // 15C: TAN (I), 16C: SST (I) - no f needed after STO / RCL / x≷
constexpr int k15XSwap = 16;                    // 15C: f 4 = x≷
constexpr int k16XSwapI = 11;                   // 16C: f GTO = x≷I
const char *const kOps = "+-×÷";
constexpr int kOpKeys[4] = {kPlus, kMinus, kTimes, kDivide};
const QString kMinusSign = QStringLiteral("−");

int hexDigitKey(QChar c) {
    if (c.isDigit()) return kDigit[c.digitValue()];
    if (c >= QLatin1Char('A') && c <= QLatin1Char('F')) return c.unicode() - 'A';      // 16C A..F = keys 0..5
    return -1;
}
QString opName(int i) { return i == 1 ? kMinusSign : QString::fromUtf8(i == 2 ? "×" : i == 3 ? "÷" : "+"); }
bool isStackRow(const QString &n) { return n == "X" || n == "Y" || n == "Z" || n == "T" || n == "LAST X"; }
}  // namespace

std::vector<int> gtoKeys(int model, bool programMode, int line, int lines) {
    if (line < 0 || line > lines) return {};
    std::vector<int> k;
    if (model == 12) {
        if (line > 99) return {};
        k = {kG, kRDN};
        if (programMode) k.push_back(kDot);
        k.push_back(kDigit[line / 10]); k.push_back(kDigit[line % 10]);
        return k;
    }
    if (model != 15 && model != 16) return {};
    if (line > 999) return {};
    k = {kGTO, model == 15 ? kCHS : kDot, kDigit[line / 100], kDigit[line / 10 % 10], kDigit[line % 10]};
    return k;
}

CalcAction gto(const InspectorSnapshot &s, int line) {
    CalcAction a;
    ProgramLine pl; pl.line = line;
    a.title = QStringLiteral("GTO %1").arg(pl.number(s.model));
    a.keys = s.running ? std::vector<int>{} : gtoKeys(s.model, s.programMode, line, s.lines);
    a.enabled = !a.keys.empty();
    return a;
}

std::vector<int> addressKeys(int model, const QString &name) {
    if (name == "I") {
        if (model == 15) return {k15I};
        if (model == 16) return {k16I};
        return {};
    }
    if (model == 12) {
        static const char *fin[5] = {"n", "i", "PV", "PMT", "FV"};
        for (int i = 0; i < 5; ++i) if (name == QLatin1String(fin[i])) return {i};
    }
    if (!name.startsWith(QLatin1Char('R')) || name.size() < 2) return {};
    QString rest = name.mid(1);
    std::vector<int> k;
    if (rest.startsWith(QLatin1Char('.'))) { k.push_back(kDot); rest = rest.mid(1); }
    if (rest.size() != 1) return {};                   // R20.. (15C) / R32.. (16C): reached only through (i)
    const QChar c = rest[0];
    if (model == 16 ? hexDigitKey(c) < 0 : !c.isDigit()) return {};
    k.push_back(hexDigitKey(c));
    return k;
}

std::vector<CalcAction> registerMenu(const InspectorSnapshot &s, const QString &name) {
    const int model = s.model;
    const std::vector<int> addr = isStackRow(name) ? std::vector<int>{} : addressKeys(model, name);
    const bool ok = !addr.empty() && !s.running;
    const bool fin = model == 12 && !name.startsWith(QLatin1Char('R'));
    const QString shown = name;
    std::vector<CalcAction> m;
    auto add = [&](const QString &t, std::vector<int> pre, bool valid, bool sep = false) {
        CalcAction a; a.title = t; a.separatorBefore = sep; a.enabled = ok && valid;
        if (a.enabled) { a.keys = pre; a.keys.insert(a.keys.end(), addr.begin(), addr.end()); }
        m.push_back(a);
    };
    add(QStringLiteral("RCL %1").arg(shown), {kRCL}, true);
    add(QStringLiteral("STO %1").arg(shown), {kSTO}, true);
    // register arithmetic: 15C STO and RCL with + - × ÷ (R0..R9, R.0..R.9, I); 12C STO only, R0..R4; 16C none
    const bool arith15 = model == 15;
    const bool arith12 = model == 12 && !fin && addr.size() == 1 && addr[0] != kDot
                         && (addr[0] == kDigit[0] || addr[0] == kDigit[1] || addr[0] == kDigit[2] || addr[0] == kDigit[3] || addr[0] == kDigit[4]);
    if (model != 16) {
        for (int i = 0; i < 4; ++i) add(QStringLiteral("STO%1 %2").arg(opName(i), shown), {kSTO, kOpKeys[i]}, arith15 || arith12, i == 0);
    }
    if (model == 15) {
        for (int i = 0; i < 4; ++i) add(QStringLiteral("RCL%1 %2").arg(opName(i), shown), {kRCL, kOpKeys[i]}, true, i == 0);
        add(QStringLiteral("x≷ %1").arg(shown), {kF, k15XSwap}, true, true);
    } else if (model == 16) {
        // the 16C exchanges X only with I (x≷I) or the register I points at (x≷(i))
        CalcAction a; a.title = QStringLiteral("x≷ I"); a.separatorBefore = true;
        a.enabled = ok && name == "I"; if (a.enabled) a.keys = {kF, k16XSwapI};
        m.push_back(a);
    }
    (void)kOps;
    return m;
}

void perform(Machine &m, const std::vector<int> &keys) {
    if (keys.empty()) return;
    m.releaseAll();
    m.powerOn();
    if (m.displayText().contains(QStringLiteral("rror"))) m.calc.tap(kBSP, 80, 220);
    for (int k : keys) m.calc.tap(k, 80, 220);
    m.calc.run(400);
}

}  // namespace CalcActions
}  // namespace vc
