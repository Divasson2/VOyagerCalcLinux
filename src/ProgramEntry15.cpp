// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
//
// ProgramEntry15.cpp - see ProgramEntry15.h.
#include "ProgramEntry15.h"

namespace vc {

namespace {
constexpr int kAnnUser = 4, kAnnPrgm = 24;       // LCD annunciator bits (ModelData.h)

QString stepNo(int n) { return QStringLiteral("%1").arg(n, 3, 10, QLatin1Char('0')); }

struct Entry {
    Machine &m;
    bool lit(int bit) { return m.calc.display().annBits >> bit & 1; }
    void key(int k) { m.calc.tap(k, 80, 220); }
    int lines() { auto s = inspect(m.calc, false, 0); return s ? s->lines : 0; }

    ProgramEntryReport enter(const std::vector<HP15CStep> &steps, const std::vector<std::vector<int>> &keys) {
        using namespace HP15CKeyEntry;
        if (m.displayText().contains(QStringLiteral("rror"))) key(keyBSP);
        if (!lit(kAnnPrgm)) { key(keyG); key(keyRS); }                   // g P/R
        if (!lit(kAnnPrgm))
            throw Error(QStringLiteral("The calculator did not go into program mode (g P/R). Clear any message on its display and try again."));
        key(keyF); key(keyRDN);                                          // f CLEAR PRGM
        const bool userAtStart = lit(kAnnUser);
        bool user = userAtStart;
        auto setUser = [&](bool on) { if (user != on) { key(keyF); key(keyRCL); user = on; } };
        int n = lines();
        for (size_t i = 0; i < steps.size(); ++i) {
            setUser(steps[i].isUser());
            for (int k : keys[i]) key(k);
            const QString text = m.displayText();
            const int now = lines();
            if (text.contains(QStringLiteral("rror"))) {
                key(keyBSP);
                if (text.contains(QLatin1Char('4')))
                    throw Error(QStringLiteral("The calculator ran out of memory (Error 4) at step %1 {%2}. Free registers or matrices (DIM (i), MATRIX 0) or use the 15C (15.2) mode, then import again. The calculator's memory is as before the import.")
                                    .arg(stepNo(int(i) + 1), steps[i].keycodeText()));
                throw Error(QStringLiteral("The calculator did not take step %1 {%2} (display: “%3”). The calculator's memory is as before the import.")
                                .arg(stepNo(int(i) + 1), steps[i].keycodeText(), text.trimmed()));
            }
            if (now != n + 1)
                throw Error(QStringLiteral("The calculator did not take step %1 {%2} (display: “%3”). The calculator's memory is as before the import.")
                                .arg(stepNo(int(i) + 1), steps[i].keycodeText(), text.trimmed()));
            n = now;
        }
        setUser(userAtStart);
        key(keyG); key(keyRS);                                           // back to run mode
        ProgramEntryReport r; r.steps = int(steps.size());
        auto s = inspect(m.calc, true);
        const std::vector<ProgramLine> listing = s ? s->program : std::vector<ProgramLine>{};
        for (size_t i = 0; i < steps.size(); ++i) {
            const QString got = i < listing.size() ? listing[i].keys : QString();
            if (HP15CKeycodes::parseDisplay(got) != std::optional<HP15CTokens>(steps[i].plainTokens()))
                r.mismatches.push_back({int(i) + 1, steps[i].keycodeText(), got});
        }
        return r;
    }
};
}  // namespace

ProgramEntryReport enterProgram15(Machine &m, const std::vector<HP15CStep> &steps) {
    if (m.model() != 15)
        throw Error(QStringLiteral("Program files from the HP-15C Simulator can only be imported into the HP-15C. Switch the model to the 15C first."));
    std::vector<std::vector<int>> keys;
    for (size_t i = 0; i < steps.size(); ++i) {
        auto k = HP15CKeyEntry::keys(steps[i]);
        if (!k) throw Error(QStringLiteral("The calculator cannot take step %1 {%2}.").arg(stepNo(int(i) + 1), steps[i].keycodeText()));
        keys.push_back(*k);
    }
    m.powerOn();
    if (auto s = inspect(m.calc, false, 0)) {
        if (s->running) throw Error(QStringLiteral("The calculator is running a program. Stop it (R/S) and import again."));
        if (s->pool) {
            const int bytes = (s->pool->free + s->pool->programRegisters) * 7;
            if (int(steps.size()) > bytes)
                throw Error(QStringLiteral("The program has %1 steps, but the calculator has room for at most %2 (free memory, 7 bytes per register). Free registers or matrices (DIM (i), MATRIX 0) or use the 15C (15.2) mode, then import again.")
                                .arg(steps.size()).arg(bytes));
        }
    }
    const QByteArray before = m.memory(true);
    try {
        return Entry{m}.enter(steps, keys);
    } catch (...) {
        try { m.putMemory(before); } catch (...) {}
        throw;
    }
}

}  // namespace vc
