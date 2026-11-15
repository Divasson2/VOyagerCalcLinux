// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
// 0.7.8: program copy / paste on the emulated calculators (needs VOYAGERCALC_FIRMWARE_DIR): every step of the HP-15C and
// HP-16C catalogues can be keyed in through the firmware; paste-then-export gives the pasted text back; edits keep the
// registers; a program that does not fit is refused with the memory unchanged; HP-16C stock and 206-slot layouts.
#include "TestSupport.h"
#include "ProgramEntry.h"
#include "ProgramText.h"
#include "Machine.h"

using namespace vc;

namespace {
QStringList keysOf(const std::vector<ProgStep> &s) { QStringList l; for (auto &x : s) l << (x.user ? x.keys + " u" : x.keys); return l; }
std::vector<ProgStep> listing(Machine &m) { auto s = inspect(m.calc, true); return s ? stepsFromListing(m.model(), s->program) : std::vector<ProgStep>{}; }
QByteArray compared(Machine &m) { return m.memory(true).mid(Machine::kMemoryStart, Machine::kMemoryEnd - Machine::kMemoryStart); }
std::vector<ProgStep> parsed(const QString &t, int model) {
    ProgramParse p = parseProgramText(t, model);
    if (!p.ok()) qWarning("parse: %s", qPrintable(p.summary()));
    return p.steps;
}
const char *k15 =
    "# VoyagerCalc program · HP-15C · 9 steps (001–009)\n"
    "001-42,21,11    f LBL A\n"
    "002-44 .3       STO .3          ; keep\n"
    "003-44 11 u     STO A u\n"
    "004-42,21, 1    f LBL 1\n"
    "005-45,16,11    RCL MATRIX A\n"
    "006-22 .1       GTO .1\n"
    "007-43,30, 5    g TEST 5\n"
    "008-32 1        GSB 1\n"
    "009-43 32       g RTN\n";
const char *k16 =
    "# VoyagerCalc program · HP-16C · 9 steps (001–009)\n"
    "001-43,22, A    g LBL A\n"
    "002-42 44       f WSIZE\n"
    "003-45 .d       RCL .d          ; keep\n"
    "004-43,22, 1    g LBL 1\n"
    "005-A           A\n"
    "006-42 22       f x≷I\n"
    "007-21 1        GSB 1\n"
    "008-43 49       g x=y?\n"
    "009-43 21       g RTN\n";
}

class TstProgramEdit : public QObject {
    Q_OBJECT
    void everyStep(Machine &m, int chunk) {
        const StepCatalog *cat = StepCatalog::forModel(m.model());
        std::vector<ProgStep> all;
        for (const auto &e : cat->entries()) {
            ProgStep s; s.keys = e.keys; all.push_back(s);
            if (e.userCapable) { s.user = true; all.push_back(s); }
        }
        QStringList failures;
        for (size_t i = 0; i < all.size(); i += size_t(chunk)) {
            std::vector<ProgStep> part(all.begin() + long(i), all.begin() + long(std::min(all.size(), i + size_t(chunk))));
            try { enterProgram(m, part); }
            catch (const Error &e) { failures << e.message(); }
            if (failures.size() > 5) break;
        }
        qInfo("%s: %d steps keyed in through the firmware", qPrintable(cat->modelName()), int(all.size()));
        QCOMPARE(failures, QStringList());
    }
private slots:
    void everyHP15CStepCanBeKeyedIn() {
        QByteArray fw = vct::firmware15(); VC_REQUIRE(fw, "HP-15C");
        Machine m; m.loadFirmware(fw); m.boot(nullptr);
        if (m.variant() != 152) m.switchVariant(152);
        everyStep(m, 150);
    }
    void everyHP16CStepCanBeKeyedIn() {
        QByteArray fw = vct::firmware16(); VC_REQUIRE(fw, "HP-16C");
        Machine m; m.loadFirmware(fw); m.boot(nullptr);
        everyStep(m, 120);
    }
    void pasteThenExportMatches_data() {
        QTest::addColumn<QString>("which");
        QTest::newRow("15C") << "15";
        QTest::newRow("16C stock") << "16";
        QTest::newRow("16C 206 slots") << "206";
    }
    /// Paste our clipboard text into an empty calculator, copy it back: the same text. Then paste the middle steps after
    /// the last one and check the listing is the expected splice.
    void pasteThenExportMatches() {
        QFETCH(QString, which);
        QByteArray fw = which == "15" ? vct::firmware15() : which == "16" ? vct::firmware16() : vct::firmware206();
        VC_REQUIRE(fw, "this");
        Machine m; m.loadFirmware(fw); m.boot(nullptr);
        if (which == "15" && m.variant() != 150) m.switchVariant(150);
        if (which == "206" && m.variant() != 160) m.switchVariant(160);
        const int model = which == "15" ? 15 : 16;
        QCOMPARE(m.model(), model);
        const QString text = QString::fromUtf8(model == 15 ? k15 : k16);
        std::vector<ProgStep> steps = parsed(text, model);
        QCOMPARE(int(steps.size()), 9);
        ProgramEntryReport r = enterProgram(m, steps);
        QCOMPARE(r.steps, 9);
        std::vector<ProgStep> back = listing(m);
        QCOMPARE(keysOf(back), keysOf(steps));
        for (size_t i = 0; i < back.size(); ++i) back[i].comment = steps[i].comment;
        QCOMPARE(programFragmentText(model, back, 1), text);
        // copy steps 4–8, paste after step 9, and again at the top
        std::vector<ProgStep> section(back.begin() + 3, back.begin() + 8);
        const QString clip = programFragmentText(model, section, 4);
        std::vector<ProgStep> pasted = parsed(clip, model);
        QCOMPARE(keysOf(pasted), keysOf(section));
        std::vector<ProgStep> want = insertSteps(insertSteps(back, 9, pasted), 0, pasted);
        enterProgram(m, want);
        QCOMPARE(keysOf(listing(m)), keysOf(want));
        // cut steps 1–5 (the copy at the top)
        want = deleteSteps(want, 1, 5);
        enterProgram(m, want);
        QCOMPARE(keysOf(listing(m)), keysOf(want));
        QVERIFY(!inspect(m.calc, false, 0)->programMode);
    }
    void registersAreKept() {
        QByteArray fw = vct::firmware15(); VC_REQUIRE(fw, "HP-15C");
        Machine m; m.loadFirmware(fw); m.boot(nullptr);
        if (m.variant() != 150) m.switchVariant(150);
        // 7 STO 3, 8 STO .2
        for (int k : {6, 33, 28, 7, 33, 37, 27}) m.calc.tap(k, 80, 220);
        auto before = inspect(m.calc, false);
        enterProgram(m, parsed(QString::fromUtf8(k15), 15));
        auto after = inspect(m.calc, false);
        QCOMPARE(after->registers[3].text, before->registers[3].text);
        QCOMPARE(after->registers[12].text, before->registers[12].text);
        QCOMPARE(after->stack[0].text, before->stack[0].text);
    }
    void tooBigIsRefusedAndMemoryKept() {
        QByteArray fw = vct::firmware15(); VC_REQUIRE(fw, "HP-15C");
        Machine m; m.loadFirmware(fw); m.boot(nullptr);
        if (m.variant() != 150) m.switchVariant(150);
        enterProgram(m, parsed(QString::fromUtf8(k15), 15));
        const QByteArray before = compared(m);
        std::vector<ProgStep> big{600, ProgStep()}; for (auto &s : big) s.keys = "1";
        QVERIFY_THROWS_EXCEPTION(Error, enterProgram(m, big));
        QCOMPARE(compared(m), before);
        // a step the 15C cannot take is refused before anything is keyed
        std::vector<ProgStep> bad(1); bad[0].keys = "43,22, A";
        QVERIFY_THROWS_EXCEPTION(Error, enterProgram(m, bad));
        QCOMPARE(compared(m), before);
    }
    void hp16cRegistersFollowTheFormula() {
        QByteArray fw = vct::firmware16(); VC_REQUIRE(fw, "HP-16C");
        Machine m; m.loadFirmware(fw); m.boot(nullptr);
        for (int lines : {0, 7, 8, 50}) {
            std::vector<ProgStep> p{size_t(lines), ProgStep()}; for (auto &s : p) s.keys = "1";
            enterProgram(m, p);
            auto s = inspect(m.calc, false, 0);
            QVERIFY(s && s->pool16);
            QCOMPARE(s->registerCount, registers16(s->pool16->slotCount(), lines, s->wordSize));
        }
    }
};

QTEST_GUILESS_MAIN(TstProgramEdit)
#include "tst_programedit.moc"
