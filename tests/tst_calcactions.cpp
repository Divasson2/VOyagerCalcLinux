// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
// The inspector's context menus (0.7.4): GTO nnn and the register items key the same as a person on the calculator.
#include "TestSupport.h"
#include "CalcActions.h"
#include "Machine.h"

using namespace vc;

namespace {
constexpr int D[10] = {36, 26, 27, 28, 16, 17, 18, 6, 7, 8};
}

class TstCalcActions : public QObject {
    Q_OBJECT
    std::unique_ptr<Machine> boot(const QByteArray &fw, int variant = 0) {
        auto m = std::make_unique<Machine>(); m->loadFirmware(fw); m->boot(nullptr);
        if (variant && m->variant() != variant) m->switchModel(variant);
        m->powerOn(); return m;
    }
    static void keys(Machine &m, std::vector<int> k) { CalcActions::perform(m, k); }
    static void number(Machine &m, int n) { std::vector<int> k; for (QChar c : QString::number(n)) k.push_back(D[c.digitValue()]); keys(m, k); }
    static InspectorSnapshot snap(Machine &m) { auto s = inspect(m.calc, true); return s.value_or(InspectorSnapshot()); }
    static QString reg(const InspectorSnapshot &s, const QString &n) {
        for (auto &r : s.stack) if (r.name == n) return r.text.trimmed();
        for (auto &r : s.registers) if (r.name == n) return r.text.trimmed();
        return QStringLiteral("?");
    }
    static const CalcAction *item(const std::vector<CalcAction> &m, const QString &t) {
        for (auto &a : m) if (a.title == t) return &a;
        return nullptr;
    }
    static QString num(const QString &t) { QString s = t; s.remove(QLatin1Char(' ')); while (s.endsWith(QLatin1Char('0')) && s.contains(QLatin1Char('.'))) s.chop(1); if (s.endsWith(QLatin1Char('.'))) s.chop(1); return s; }
    /// state: X = x, R = r (stored in `name` with STO by keys), then the menu item; returns (X, Y, LAST X, register)
    QStringList run(const QByteArray &fw, int variant, const QString &name, const QString &title, int x, int r, bool viaMenu = true) {
        auto m = boot(fw, variant);
        auto s0 = snap(*m);
        auto addr = CalcActions::addressKeys(s0.model, name);
        if (addr.empty()) { QTest::qFail("no address", __FILE__, __LINE__); return {}; }
        number(*m, r); std::vector<int> k = {33}; k.insert(k.end(), addr.begin(), addr.end()); keys(*m, k);
        keys(*m, {25}); number(*m, 7); keys(*m, {25}); number(*m, x);     // Y = 7 (to see stack lift), X = x
        auto menu = CalcActions::registerMenu(snap(*m), name);
        const CalcAction *a = item(menu, title);
        if (!a || !a->enabled) { QTest::qFail(qPrintable("no item " + title), __FILE__, __LINE__); return {}; }
        keys(*m, a->keys);
        auto s = snap(*m);
        return {num(reg(s, "X")), num(reg(s, "Y")), num(reg(s, "Z")), num(reg(s, "LAST X")), num(reg(s, name))};
    }
    void check(const QByteArray &fw, int variant, const QString &name, const QString &title, std::vector<int> hpKeys, QStringList want) {
        QStringList got = run(fw, variant, name, title, 3, 12);
        QCOMPARE(got, want);
        // the same keys pressed by hand (the HP keystroke sequence from the owner's handbook) give the same calculator
        auto a = boot(fw, variant), b = boot(fw, variant);
        for (Machine *m : {a.get(), b.get()}) { number(*m, 5); keys(*m, {25}); number(*m, 4); }
        auto menu = CalcActions::registerMenu(snap(*a), name);
        const CalcAction *it = item(menu, title); QVERIFY(it && it->enabled);
        QCOMPARE(it->keys, hpKeys);
        keys(*a, it->keys); keys(*b, hpKeys);
        auto sa = snap(*a), sb = snap(*b);
        QVERIFY(sa.stack == sb.stack); QVERIFY(sa.registers == sb.registers);
        QCOMPARE(a->displayText(), b->displayText());
    }
    static std::vector<int> pr(Machine &m) { return m.model() == 12 ? std::vector<int>{31, 20} : std::vector<int>{32, 20}; }   // f P/R (12C), g P/R
    void programOf(Machine &m, int steps) {      // P/R, steps lines of digits (and ENTER between), P/R
        keys(m, pr(m));
        for (int i = 0; i < steps; ++i) keys(m, {D[(i + 1) % 10]});
        keys(m, pr(m));
    }
    void gtoCase(const QByteArray &fw, int variant, bool prgm, int line, QString prefix) {
        auto m = boot(fw, variant); programOf(*m, 6);
        if (prgm) keys(*m, pr(*m));
        auto s = snap(*m); QCOMPARE(s.programMode, prgm); QVERIFY(s.lines >= 6);    // 12C: 8 lines from the start
        CalcAction a = CalcActions::gto(s, line);
        QCOMPARE(a.title, QStringLiteral("GTO ") + prefix); QVERIFY(a.enabled);
        keys(*m, a.keys);
        auto t = snap(*m);
        QCOMPARE(t.line, line); QCOMPARE(t.lines, s.lines); QCOMPARE(t.programMode, prgm);    // positioned, nothing keyed in
        if (prgm) QVERIFY2(m->displayText().trimmed().startsWith(prefix), qPrintable(m->displayText()));
        QVERIFY(!CalcActions::gto(s, s.lines + 1).enabled);
    }
private slots:
    void gto15() { auto fw = vct::firmware15(); VC_REQUIRE(fw, "15C"); gtoCase(fw, 0, true, 4, "004"); gtoCase(fw, 0, false, 2, "002"); gtoCase(fw, 0, true, 0, "000"); }
    void gto16() { auto fw = vct::firmware16(); VC_REQUIRE(fw, "16C"); gtoCase(fw, 0, true, 5, "005"); gtoCase(fw, 0, false, 3, "003"); }
    void gto12() { auto fw = vct::firmwareMulti(); VC_REQUIRE(fw, "multi"); gtoCase(fw, 120, true, 4, "04"); gtoCase(fw, 120, false, 2, "02"); }
    void gtoKeys() {
        QCOMPARE(CalcActions::gtoKeys(15, false, 12, 20), (std::vector<int>{11, 5, 36, 26, 27})); QCOMPARE(CalcActions::gtoKeys(16, false, 12, 20), (std::vector<int>{11, 37, 36, 26, 27}));
        QCOMPARE(CalcActions::gtoKeys(12, false, 12, 20), (std::vector<int>{32, 22, 26, 27}));
        QCOMPARE(CalcActions::gtoKeys(12, true, 12, 20), (std::vector<int>{32, 22, 37, 26, 27}));
    }
    void registers15() {
        auto fw = vct::firmware15(); VC_REQUIRE(fw, "15C");
        // results: X, Y, Z, LAST X, the register (X = 3 over Y = 7, register = 12)
        check(fw, 0, "R5", "RCL R5", {34, 17}, {"12", "3", "7", "0", "12"});
        check(fw, 0, "R5", "STO R5", {33, 17}, {"3", "7", "12", "0", "3"});
        check(fw, 0, "R5", "STO+ R5", {33, 39, 17}, {"3", "7", "12", "0", "15"});
        check(fw, 0, "R5", "STO− R5", {33, 29, 17}, {"3", "7", "12", "0", "9"});
        check(fw, 0, "R5", "STO× R5", {33, 19, 17}, {"3", "7", "12", "0", "36"});
        check(fw, 0, "R5", "STO÷ R5", {33, 9, 17}, {"3", "7", "12", "0", "4"});
        check(fw, 0, "R5", "RCL+ R5", {34, 39, 17}, {"15", "7", "12", "0", "12"});
        check(fw, 0, "R5", "RCL− R5", {34, 29, 17}, {"-9", "7", "12", "0", "12"});
        check(fw, 0, "R5", "RCL× R5", {34, 19, 17}, {"36", "7", "12", "0", "12"});
        check(fw, 0, "R5", "RCL÷ R5", {34, 9, 17}, {"0.25", "7", "12", "0", "12"});
        check(fw, 0, "R5", "x≷ R5", {31, 16, 17}, {"12", "7", "12", "0", "3"});
        check(fw, 0, "R.3", "STO+ R.3", {33, 39, 37, 28}, {"3", "7", "12", "0", "15"});
        check(fw, 0, "R.3", "x≷ R.3", {31, 16, 37, 28}, {"12", "7", "12", "0", "3"});
        check(fw, 0, "I", "RCL I", {34, 14}, {"12", "3", "7", "0", "12"});
        check(fw, 0, "I", "STO I", {33, 14}, {"3", "7", "12", "0", "3"});
        check(fw, 0, "I", "STO+ I", {33, 39, 14}, {"3", "7", "12", "0", "15"});
        check(fw, 0, "I", "RCL× I", {34, 19, 14}, {"36", "7", "12", "0", "12"});
        check(fw, 0, "I", "x≷ I", {31, 16, 14}, {"12", "7", "12", "0", "3"});
    }
    void registers16() {
        auto fw = vct::firmware16(); VC_REQUIRE(fw, "16C");
        check(fw, 0, "R5", "RCL R5", {34, 17}, {"12", "3", "7", "0", "12"});
        check(fw, 0, "R5", "STO R5", {33, 17}, {"3", "7", "12", "0", "3"});
        check(fw, 0, "RB", "RCL RB", {34, 1}, {"12", "3", "7", "0", "12"});
        check(fw, 0, "R.A", "STO R.A", {33, 37, 0}, {"3", "7", "12", "0", "3"});
        check(fw, 0, "I", "RCL I", {34, 21}, {"12", "3", "7", "0", "12"});
        check(fw, 0, "I", "STO I", {33, 21}, {"3", "7", "12", "0", "3"});
        check(fw, 0, "I", "x≷ I", {31, 11}, {"12", "7", "12", "0", "3"});
    }
    void registers12() {
        auto fw = vct::firmwareMulti(); VC_REQUIRE(fw, "multi");
        check(fw, 120, "R3", "RCL R3", {34, 28}, {"12", "3", "7", "0", "12"});
        check(fw, 120, "R3", "STO+ R3", {33, 39, 28}, {"3", "7", "12", "0", "15"});
        check(fw, 120, "R3", "STO− R3", {33, 29, 28}, {"3", "7", "12", "0", "9"});
        check(fw, 120, "R3", "STO× R3", {33, 19, 28}, {"3", "7", "12", "0", "36"});
        check(fw, 120, "R3", "STO÷ R3", {33, 9, 28}, {"3", "7", "12", "0", "4"});
        check(fw, 120, "R.3", "STO R.3", {33, 37, 28}, {"3", "7", "12", "0", "3"});
        check(fw, 120, "PMT", "RCL PMT", {34, 3}, {"12", "3", "7", "0", "12"});
        check(fw, 120, "PMT", "STO PMT", {33, 3}, {"3", "7", "12", "0", "3"});
    }
    void menusPerModel() {
        InspectorSnapshot s; s.model = 15;
        for (auto &a : CalcActions::registerMenu(s, "LAST X")) QVERIFY(!a.enabled);
        for (auto &a : CalcActions::registerMenu(s, "X")) QVERIFY(!a.enabled);
        for (auto &a : CalcActions::registerMenu(s, "R25")) QVERIFY(!a.enabled);       // only through (i)
        QCOMPARE(int(CalcActions::registerMenu(s, "R1").size()), 11);
        s.running = true; for (auto &a : CalcActions::registerMenu(s, "R1")) QVERIFY(!a.enabled);
        s.running = false; s.model = 16;
        auto m16 = CalcActions::registerMenu(s, "R1");
        QCOMPARE(int(m16.size()), 3); QVERIFY(m16[0].enabled && m16[1].enabled && !m16[2].enabled);   // no register arithmetic on the 16C
        QVERIFY(CalcActions::registerMenu(s, "I")[2].enabled);
        s.model = 12;
        auto m12 = CalcActions::registerMenu(s, "R5");
        QCOMPARE(int(m12.size()), 6); QVERIFY(m12[1].enabled && !m12[2].enabled);     // 12C: STO arithmetic on R0..R4 only
        QVERIFY(CalcActions::registerMenu(s, "R4")[2].enabled);
        QVERIFY(!CalcActions::registerMenu(s, "PMT")[2].enabled);
        QVERIFY(!CalcActions::registerMenu(s, "R.1")[2].enabled);
    }
};

QTEST_GUILESS_MAIN(TstCalcActions)
#include "tst_calcactions.moc"
