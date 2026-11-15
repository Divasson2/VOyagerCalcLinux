// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
//
// ProgramEntry.cpp - see ProgramEntry.h.
#include "ProgramEntry.h"
#include "Manz15C.h"
#include <algorithm>

namespace vc {

namespace {
constexpr int kAnnUser = 4, kAnnPrgm = 24;       // LCD annunciator bits (ModelData.h)
constexpr int keyF = 31, keyG = 32, keyRS = 20, keyRDN = 22, keyBSP = 24, keyRCL = 34, keyDot = 37;

QString stepNo(int n) { return QStringLiteral("%1").arg(n, 3, 10, QLatin1Char('0')); }

/// 16C: keycode -> face key. Digits 0–9 are their keys, A–F the top-left six keys, two-digit codes are row/column.
std::optional<int> key16(const QString &tok) {
    if (tok.size() == 1 && tok[0] >= u'A' && tok[0] <= u'F') return tok[0].unicode() - 'A';
    bool ok = false; const int code = tok.toInt(&ok);
    if (!ok) return std::nullopt;
    if (tok.size() == 1) return HP15CKeyEntry::keyIndex(code);      // the digit keys are in the same places
    const int row = code / 10, col = code % 10;
    if (row < 1 || row > 4) return std::nullopt;
    return (row - 1) * 10 + (col == 0 ? 9 : col - 1);
}
}  // namespace

std::vector<int> stepKeys(int model, const ProgStep &s) {
    const StepCatalog *cat = StepCatalog::forModel(model);
    if (!cat || !cat->find(s.keys)) return {};
    if (model == 15) {
        auto t = HP15CKeycodes::parseDisplay(s.keys);
        if (!t) return {};
        HP15CTokens tok = *t;
        if (s.user) tok.push_back(HP15CToken::user());
        auto k = HP15CKeyEntry::keys(HP15CStep(tok));
        return k ? *k : std::vector<int>{};
    }
    std::vector<int> out;
    for (const QString &tk : cat->tokenKey(s.keys).split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        if (tk.startsWith(QLatin1Char('.'))) {
            auto d = key16(tk.mid(1));
            if (!d) return {};
            out.push_back(keyDot); out.push_back(*d);
            continue;
        }
        auto k = key16(tk);
        if (!k) return {};
        out.push_back(*k);
    }
    return out;
}

std::vector<ProgStep> deleteSteps(const std::vector<ProgStep> &program, int first, int last) {
    std::vector<ProgStep> out;
    for (int i = 0; i < int(program.size()); ++i) if (i + 1 < first || i + 1 > last) out.push_back(program[size_t(i)]);
    return out;
}

std::vector<ProgStep> insertSteps(const std::vector<ProgStep> &program, int after, const std::vector<ProgStep> &incoming) {
    const int at = std::clamp(after, 0, int(program.size()));
    std::vector<ProgStep> out(program.begin(), program.begin() + at);
    out.insert(out.end(), incoming.begin(), incoming.end());
    out.insert(out.end(), program.begin() + at, program.end());
    return out;
}

ProgramEntryReport enterProgram(Machine &m, const std::vector<ProgStep> &steps, const std::function<void(int, int)> &progress) {
    const int model = m.model();
    const StepCatalog *cat = StepCatalog::forModel(model);
    if (!cat) throw Error(QStringLiteral("Programs can only be pasted on the HP-15C and HP-16C. Switch the model first."));
    std::vector<std::vector<int>> keys;
    for (size_t i = 0; i < steps.size(); ++i) {
        auto k = stepKeys(model, steps[i]);
        if (k.empty()) throw Error(QStringLiteral("The %1 cannot take step %2 {%3}.").arg(cat->modelName(), stepNo(int(i) + 1), cat->displayKeys(steps[i])));
        keys.push_back(k);
    }
    m.powerOn();
    auto s0 = inspect(m.calc, true, 4096);
    if (!s0) throw Error(QStringLiteral("The calculator's memory cannot be read."));
    if (s0->running) throw Error(QStringLiteral("The calculator is running a program. Stop it (R/S) and try again."));
    const FitReport fit = checkFit(*s0, steps);
    if (!fit.fits) throw Error(fit.message);

    const QByteArray before = m.memory(true);
    auto lit = [&](int bit) { return m.calc.display().annBits >> bit & 1; };
    auto key = [&](int k) { m.calc.tap(k, 80, 220); };
    auto lines = [&] { auto s = inspect(m.calc, false, 0); return s ? s->lines : 0; };
    try {
        if (m.displayText().contains(QStringLiteral("rror"))) key(keyBSP);
        if (!lit(kAnnPrgm)) { key(keyG); key(keyRS); }                 // g P/R
        if (!lit(kAnnPrgm))
            throw Error(QStringLiteral("The calculator did not go into program mode (g P/R). Clear any message on its display and try again."));
        key(keyF); key(keyRDN);                                        // f CLEAR PRGM (program only)
        if (lines() != 0) throw Error(QStringLiteral("The calculator did not clear its program (f CLEAR PRGM)."));
        const bool userAtStart = model == 15 && lit(kAnnUser);
        bool user = userAtStart;
        auto setUser = [&](bool on) { if (model == 15 && user != on) { key(keyF); key(keyRCL); user = on; } };
        int n = 0;
        for (size_t i = 0; i < steps.size(); ++i) {
            setUser(steps[i].user);
            for (int k : keys[i]) key(k);
            const QString text = m.displayText();
            const int now = lines();
            if (text.contains(QStringLiteral("rror"))) {
                key(keyBSP);
                if (text.contains(QLatin1Char('4')))
                    throw Error(QStringLiteral("The calculator ran out of memory (Error 4) at step %1 {%2}. The calculator's memory is as before.")
                                    .arg(stepNo(int(i) + 1), cat->displayKeys(steps[i])));
                throw Error(QStringLiteral("The calculator did not take step %1 {%2} (display: “%3”). The calculator's memory is as before.")
                                .arg(stepNo(int(i) + 1), cat->displayKeys(steps[i]), text.trimmed()));
            }
            if (now != n + 1)
                throw Error(QStringLiteral("The calculator did not take step %1 {%2} (display: “%3”). The calculator's memory is as before.")
                                .arg(stepNo(int(i) + 1), cat->displayKeys(steps[i]), text.trimmed()));
            n = now;
            if (progress && (i % 25 == 0)) progress(int(i), int(steps.size()));
        }
        setUser(userAtStart);
        key(keyG); key(keyRS);                                         // back to run mode
        ProgramEntryReport r; r.steps = int(steps.size());
        auto s = inspect(m.calc, true);
        const std::vector<ProgStep> got = stepsFromListing(model, s ? s->program : std::vector<ProgramLine>{});
        for (size_t i = 0; i < steps.size(); ++i) {
            const ProgStep g = i < got.size() ? got[i] : ProgStep();
            if (cat->tokenKey(g.keys) != cat->tokenKey(steps[i].keys) || g.user != steps[i].user)
                r.mismatches.push_back({int(i) + 1, cat->displayKeys(steps[i]), cat->displayKeys(g)});
        }
        if (got.size() != steps.size() || !r.mismatches.empty()) {
            QString first = r.mismatches.empty() ? QString() : QStringLiteral(" (step %1: {%2} became {%3})").arg(stepNo(r.mismatches[0].step), r.mismatches[0].file, r.mismatches[0].calculator);
            throw Error(QStringLiteral("The program in the calculator does not match what was keyed in%1. The calculator's memory is as before.").arg(first));
        }
        if (progress) progress(int(steps.size()), int(steps.size()));
        return r;
    } catch (...) {
        try { m.putMemory(before); } catch (...) {}
        throw;
    }
}

}  // namespace vc
