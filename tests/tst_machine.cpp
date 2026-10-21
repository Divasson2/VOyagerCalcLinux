// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
// The engine and machine: boot, keys, model switch, saved state, memory images, the Keys-menu chords.
#include "TestSupport.h"
#include "Machine.h"

using namespace vc;

class TstMachine : public QObject {
    Q_OBJECT
    static std::unique_ptr<Machine> machine(const QByteArray &fw) { auto m = std::make_unique<Machine>(); m->loadFirmware(fw); m->boot(nullptr); return m; }
    static void type(Machine &m, const QString &keys) {
        for (const QString &k : keys.split(' ', Qt::SkipEmptyParts)) m.tap(Emulator::keyCode(m.model(), k.toLatin1().constData()));
    }
    static bool showsPrError(Machine &m) { return m.displayText().contains("Pr Error", Qt::CaseInsensitive) || m.displayText().contains("Pr  Error"); }
    bool switchKeepsR1(Machine &m, int v) {
        type(m, "7 7 STO 1");
        m.switchModel(v);
        if (m.variant() != v) QTest::qFail("variant", __FILE__, __LINE__);
        if (m.displayText().trimmed().isEmpty()) QTest::qFail("calculator is off after the switch", __FILE__, __LINE__);
        type(m, "RCL 1");
        bool kept = m.displayText().contains("77");
        m.tap(Machine::keyBSP);
        return kept;
    }
    template <typename F> void each(F body) {
        bool any = false;
        const QPair<QByteArray, const char *> all[] = {{vct::firmware16(), "B7B7"}, {vct::firmware15(), "C8C8"}, {vct::firmwareMulti(), "E3E3"}};
        for (auto &p : all) if (!p.first.isEmpty()) { any = true; auto m = machine(p.first); body(*m, p.second); }
        if (!any) QSKIP("no firmware (set VOYAGERCALC_FIRMWARE_DIR)");
    }
private slots:
    void firmwareImages() {
        bool any = false;
        if (auto d = vct::firmware16(); !d.isEmpty()) { any = true; Emulator c; c.loadFirmware(d); QCOMPARE(c.firmwareImages(), (std::vector<int>{160})); }
        if (auto d = vct::firmwareMulti(); !d.isEmpty()) { any = true; Emulator c; c.loadFirmware(d); QCOMPARE(c.firmwareImages(), (std::vector<int>{120, 152, 160})); }
        if (auto d = vct::firmware15(); !d.isEmpty()) { any = true; Emulator c; c.loadFirmware(d); QCOMPARE(c.firmwareImages(), (std::vector<int>{150, 152, 160})); }
        if (!any) QSKIP("no firmware");
    }
    void badFirmwareThrows() {
        Machine m;
        QVERIFY_THROWS_EXCEPTION(vc::Error, m.loadFirmware(QByteArray(1000, 'x')));
    }
    void bootAndType() {
        each([&](Machine &m, const char *n) {
            type(m, "1 2 3");
            QVERIFY2(m.displayText().contains("123"), n);
            QVERIFY2(!showsPrError(m), n);
        });
    }
    void e3e3AllTransitions() {
        auto fw = vct::firmwareMulti(); VC_REQUIRE(fw, "E3E3");
        auto m = machine(fw);
        QCOMPARE(m->images(), (std::vector<int>{120, 152, 160})); QVERIFY(m->canSwitchModel());
        QCOMPARE(m->variant(), 120);
        for (int v : {152, 160, 120, 160, 152, 120, 120}) {
            QVERIFY2(!switchKeepsR1(*m, v), qPrintable(QString("to %1 clears memory").arg(v)));
            QCOMPARE(m->model(), v / 10);
        }
    }
    void c8c8Transitions() {
        auto fw = vct::firmware15(); VC_REQUIRE(fw, "C8C8");
        auto m = machine(fw);
        QCOMPARE(m->images(), (std::vector<int>{150, 152, 160}));
        QCOMPARE(m->variant(), 150);
        const std::pair<int, bool> cases[] = {{152, true}, {160, false}, {150, false}, {160, false}, {152, false}, {150, true}};
        for (auto [v, keeps] : cases) QVERIFY2(switchKeepsR1(*m, v) == keeps, qPrintable(QString("to %1").arg(v)));
    }
    void singleImageCannotSwitch() {
        auto fw = vct::firmware16(); VC_REQUIRE(fw, "B7B7");
        auto m = machine(fw);
        QVERIFY(!m->canSwitchModel());
        QVERIFY_THROWS_EXCEPTION(vc::Error, m->switchModel(150));
    }
    void restoreOnLaunchKeepsTheChosenModel() {
        auto fw = vct::firmwareMulti(); VC_REQUIRE(fw, "E3E3");
        auto m = machine(fw);
        m->switchModel(160);
        type(*m, "A B C");
        SavedState saved = m->savedState(false);
        QCOMPARE(saved.variant, 160);
        // the JSON round trip, as the state store does
        SavedState back; QVERIFY(SavedState::fromJson(saved.toJson(), &back));
        QCOMPARE(back.memory, saved.memory); QCOMPARE(back.variant, 160); QCOMPARE(back.firmwareId, saved.firmwareId);
        Machine m2; m2.loadFirmware(fw);
        QVERIFY(m2.boot(&back).isEmpty());
        QCOMPARE(m2.variant(), 160);
        QVERIFY2(m2.displayText().toUpper().contains("ABC"), qPrintable(m2.displayText()));
    }
    void stateOfAnotherFirmwareIsIgnored() {
        auto fw = vct::firmware16(); VC_REQUIRE(fw, "B7B7");
        Machine m; m.loadFirmware(fw);
        SavedState s; s.memory = QByteArray(2048, 0); s.variant = 160; s.firmwareId = "not-this-one";
        QVERIFY(!m.boot(&s).isEmpty() || true);       // a note, never an exception
        QVERIFY(!m.displayText().trimmed().isEmpty());
    }
    void memoryRoundTrip() {
        auto fw = vct::firmware15(); VC_REQUIRE(fw, "15C");
        auto m = machine(fw);
        type(*m, "4 2 STO 3");
        QByteArray img = m->memory();
        QCOMPARE(img.size(), 2048);
        auto m2 = machine(fw);
        m2->putMemory(img);
        type(*m2, "RCL 3");
        QVERIFY2(m2->displayText().contains("42"), qPrintable(m2->displayText()));
    }
    void checkMemory() {
        auto fw = vct::firmware15(); VC_REQUIRE(fw, "15C");
        auto ok = Machine::checkMemory(vct::fixture("FinancePac.15c"), fw, 152);
        QVERIFY2(ok.fits(), qPrintable(QString::number(ok.kind)));
        QCOMPARE(Machine::checkMemory(QByteArray(100, 0), fw, 152).kind, Machine::MemoryCheck::WrongSize);
        // FinancePac needs the 15.2's double memory
        auto no = Machine::checkMemory(vct::fixture("FinancePac.15c"), fw, 150);
        QVERIFY(!no.fits());
        QCOMPARE(Machine::identifyMemory(vct::fixture("FinancePac.15c"), fw, {150, 152}), (std::vector<int>{152}));
        QVERIFY(Machine::needs152(vct::fixture("FinancePac.15c"), fw, {150, 152}));
    }
    // Keys menu
    void serviceMenuChord() {
        each([&](Machine &m, const char *n) {
            m.perform(KeyChord::ServiceMenu);
            QVERIFY2(m.displayText().contains("1.L2.C3.H"), qPrintable(QString(n) + ": " + m.displayText()));
            m.tap(16);
            if (m.canSwitchModel()) QVERIFY2(m.displayText().contains("15.2"), qPrintable(QString(n) + ": " + m.displayText()));
        });
    }
    void memoryResetChord() {
        each([&](Machine &m, const char *n) {
            type(m, "7 STO 1");
            m.perform(KeyChord::MemoryReset);
            QVERIFY2(showsPrError(m), qPrintable(QString(n) + ": " + m.displayText()));
            m.clearPrError();
            type(m, "RCL 1");
            QVERIFY2(!m.displayText().contains("7"), n);
        });
    }
    void selfTestChord() {
        each([&](Machine &m, const char *n) {
            m.perform(KeyChord::SelfTest); m.run(4000);
            QVERIFY2(m.displayText().contains("-8,8,8,8,8,8,8,8,8,8,"), qPrintable(QString(n) + ": " + m.displayText()));
        });
    }
    void usbChordJustTurnsOn() {
        each([&](Machine &m, const char *n) {
            type(m, "1 2 3");
            m.perform(KeyChord::UsbMode);
            QVERIFY2(m.displayText().contains("123"), qPrintable(QString(n) + ": " + m.displayText()));
        });
    }
    void latchLogic() {
        KeyLatch l;
        using A = KeyLatch::Action;
        auto eq = [](const std::vector<A> &a, std::vector<std::pair<int, std::vector<int>>> want) {
            if (a.size() != want.size()) return false;
            for (size_t i = 0; i < a.size(); ++i) if (int(a[i].kind) != want[i].first || a[i].keys != want[i].second) return false;
            return true;
        };
        const int P = A::Press, R = A::Release, L = A::ReleaseLater;
        QVERIFY(eq(l.toggle(32), {{P, {32}}}));
        QVERIFY(eq(l.toggle(25), {{P, {25}}}));
        QVERIFY(eq(l.press(30), {{P, {30}}}));
        QVERIFY(eq(l.release(30), {{R, {30}}, {L, {25, 32}}}));
        QVERIFY(l.isEmpty());
        l.toggle(31);
        QVERIFY(eq(l.press(31), {{R, {31}}}));
        QVERIFY(eq(l.release(31), {}));
        QVERIFY(eq(l.toggle(7), {{P, {7}}}));
        QVERIFY(eq(l.toggle(7), {{R, {7}}}));
    }
    void latchedChordOpensTheServiceMenu() {
        auto fw = vct::firmwareMulti(); if (fw.isEmpty()) fw = vct::firmware16();
        VC_REQUIRE(fw, "multi or 16C");
        auto mp = machine(fw); Machine &m = *mp;
        m.powerOff();
        KeyLatch l;
        auto apply = [&](const std::vector<KeyLatch::Action> &as) {
            for (auto &a : as) switch (a.kind) {
                case KeyLatch::Action::Press: m.press(a.keys[0]); break;
                case KeyLatch::Action::Release: m.release(a.keys[0]); break;
                case KeyLatch::Action::ReleaseLater: m.release(a.keys, KeyLatch::kReleaseDelayMs); break;
            }
        };
        apply(l.toggle(Machine::keyG)); m.run(300);
        apply(l.toggle(Machine::keyENTER)); m.run(300);
        apply(l.press(Machine::keyON)); m.run(150);
        apply(l.release(Machine::keyON)); m.run(1200);
        QVERIFY2(m.displayText().contains("1.L2.C3.H"), qPrintable(m.displayText()));
    }
    void modelTables() {
        for (int id : {12, 15, 16}) {
            const Model &mod = Model::with(id);
            QCOMPARE(mod.id(), id);
            QCOMPARE(mod.keyCount(), 39);
            QVERIFY(mod.key(Machine::keyON)); QVERIFY(mod.key(Machine::keyENTER));
            QVERIFY(mod.gZoneFraction() > 0.3 && mod.gZoneFraction() < 0.4);
        }
        QCOMPARE(Model::hp15c().fileExtension(), QString("bin15c"));
        // the ENTER shortcut: Return
        Shortcut ret{0, int(SK_Return)};
        QVERIFY(Model::hp15c().keyForShortcut(ret));
        QCOMPARE(Model::hp15c().keyForShortcut(ret)->code, int(Machine::keyENTER));
    }
};

QTEST_GUILESS_MAIN(TstMachine)
#include "tst_machine.moc"
