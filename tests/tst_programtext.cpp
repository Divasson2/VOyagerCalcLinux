// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
// 0.7.8: program copy / paste - the step catalogues, reading program text in every accepted format (our clipboard
// text, "Export Listing as Text", HP-15C Simulator files, step + keycodes, key names only), the messages for bad lines,
// label clashes and renumbering, the memory-fit check, and the edit helpers. Pure logic: no firmware needed.
#include "TestSupport.h"
#include "ProgramText.h"
#include "ProgramEntry.h"
#include "ProgramDocument.h"
#include "Manz15C.h"

using namespace vc;

namespace {
QStringList keysOf(const std::vector<ProgStep> &s) { QStringList l; for (auto &x : s) l << (x.user ? x.keys + " u" : x.keys); return l; }
ProgStep st(const char *k, bool user = false) { ProgStep s; s.keys = QString::fromUtf8(k); s.user = user; return s; }
const char *kManz =
    "# ------------------------------------------------------------------------------\n"
    "# HEWLETT·PACKARD 15C Simulator program\n"
    "#T:Mother's Kitchen\n"
    "# ------------------------------------------------------------------------------\n\n"
    "   000 {             } \n"
    "   001 {    42 21 11 } f LBL A\n"
    "   002 {       44  0 } STO 0\n"
    "   003 {       44 .3 } STO .3\n"
    "   004 {    44 24  u } STO (i)\n"
    "   005 {    43 30  5 } g TEST x=y\n"
    "   006 {          48 } .\n"
    "   007 {           7 } 7\n"
    "   008 {       22 ,1 } GTO ,1\n"
    "   009 {    45 16 11 } RCL MATRIX A\n"
    "   010 {       43 32 } g RTN\n";
}

class TstProgramText : public QObject {
    Q_OBJECT
private slots:
    void catalogues() {
        auto *c15 = StepCatalog::forModel(15), *c16 = StepCatalog::forModel(16);
        QVERIFY(c15 && c16 && !StepCatalog::forModel(12));
        QVERIFY(c15->entries().size() > 600);
        QVERIFY(c16->entries().size() > 230);
        // the same step written in the usual ways
        QCOMPARE(c15->find("42,21,11")->mnemonic, QStringLiteral("f LBL A"));
        QCOMPARE(c15->find("42 21 11"), c15->find("42,21,11"));
        QCOMPARE(c15->find("44 .3"), c15->find("44 ,3"));
        QCOMPARE(c15->find("45 48 3")->mnemonic, QStringLiteral("RCL .3"));
        QCOMPARE(c15->find("43,30, 0")->keys, QStringLiteral("43,30, 0"));
        QCOMPARE(c15->find("43 30 0"), c15->find("43,30, 0"));
        bool user = false;
        QVERIFY(c15->find("44 24 u", &user) && user);
        QVERIFY(!c15->find("44 1 u"));                     // u only for STO / RCL A–E and (i)
        QVERIFY(!c15->find("42,21,19"));
        QCOMPARE(c16->find("43,22, b")->mnemonic, QStringLiteral("g LBL B"));
        QCOMPARE(c16->find("43 22 B"), c16->find("43,22, b"));
        QCOMPARE(c16->find("45 .d")->mnemonic, QStringLiteral("RCL .d"));
        QVERIFY(!c16->find("42,36, 8"));                   // WINDOW 8 does not exist
        QVERIFY(!c16->find("42 21 11"));                   // a 15C step
        // bytes: one, two for the 15C's two-byte steps and USER STO / RCL
        QCOMPARE(c15->bytes(st("42,21,11")), 1);
        QCOMPARE(c15->bytes(st("22 .1")), 2);
        QCOMPARE(c15->bytes(st("44 11", true)), 2);
        QCOMPARE(c16->bytes(st("43,22, A")), 1);
    }
    void keyNames() {
        auto *c15 = StepCatalog::forModel(15), *c16 = StepCatalog::forModel(16);
        auto one = [](const StepCatalog *c, const char *n) -> QString { auto v = c->named(QString::fromUtf8(n)); return v.size() == 1 ? v[0]->keys : QStringLiteral("<%1>").arg(v.size()); };
        QCOMPARE(one(c15, "f LBL A"), QStringLiteral("42,21,11"));
        QCOMPARE(one(c15, "LBL A"), QStringLiteral("42,21,11"));
        QCOMPARE(one(c15, "lbl a"), QStringLiteral("42,21,11"));
        QCOMPARE(one(c15, "x<>y"), QStringLiteral("34"));
        QCOMPARE(one(c15, "x≷y"), QStringLiteral("34"));
        QCOMPARE(one(c15, "STO 1"), QStringLiteral("44 1"));
        QCOMPARE(one(c15, "STO1"), QStringLiteral("44 1"));
        QCOMPARE(one(c15, "RCL (i)"), QStringLiteral("45 24"));
        QCOMPARE(one(c15, "sqrt"), QStringLiteral("11"));
        QCOMPARE(one(c15, "√x"), QStringLiteral("11"));
        QCOMPARE(one(c15, "g x≤y"), QStringLiteral("43 10"));
        QCOMPARE(one(c15, "x<=y?"), QStringLiteral("43 10"));
        QCOMPARE(one(c15, "x=y"), QStringLiteral("43,30, 5"));
        QCOMPARE(one(c15, "g TEST 5"), QStringLiteral("43,30, 5"));
        QCOMPARE(one(c15, "TEST x≠0"), QStringLiteral("43,30, 0"));
        QCOMPARE(one(c15, "+/-"), QStringLiteral("16"));
        QCOMPARE(one(c15, "RDN"), QStringLiteral("33"));
        QCOMPARE(one(c15, "R↓"), QStringLiteral("33"));
        QCOMPARE(one(c15, "y^x"), QStringLiteral("14"));
        QCOMPARE(one(c15, "GSB .2"), QStringLiteral("32 .2"));
        QCOMPARE(one(c15, "XEQ .2"), QStringLiteral("32 .2"));
        QCOMPARE(one(c15, "f SOLVE A"), QStringLiteral("42,10,11"));
        QCOMPARE(one(c15, "RCL MATRIX A"), QStringLiteral("45,16,11"));
        bool user = false;
        auto u = c15->named("STO A u", &user);
        QVERIFY(user && u.size() == 1 && u[0]->keys == QStringLiteral("44 11"));
        QCOMPARE(one(c16, "LBL A"), QStringLiteral("43,22, A"));
        QCOMPARE(one(c16, "SL"), QStringLiteral("42 A"));
        QCOMPARE(one(c16, "x≷I"), QStringLiteral("42 22"));
        QCOMPARE(one(c16, "x<>(i)"), QStringLiteral("42 21"));
        QCOMPARE(one(c16, "WSIZE"), QStringLiteral("42 44"));
        QCOMPARE(one(c16, "HEX"), QStringLiteral("23"));
        QCOMPARE(one(c16, "DBL*"), QStringLiteral("43 20"));
        QCOMPARE(one(c16, "B"), QStringLiteral("b"));      // the digit, not B? (bit test)
        QCOMPARE(one(c16, "B?"), QStringLiteral("42 6"));
        QCOMPARE(one(c16, "x=y"), QStringLiteral("43 49"));
        QCOMPARE(one(c16, "x=y?"), QStringLiteral("43 49"));
        QCOMPARE(c16->named("g HEX").size(), size_t(2));   // the shift fits neither: both HEX steps are offered
        QVERIFY(c15->named("WSIZE").empty());
    }
    void ourClipboardTextRoundTrip() {
        for (int model : {15, 16}) {
            std::vector<ProgStep> steps = model == 15
                ? std::vector<ProgStep>{st("42,21,11"), st("44 .3"), st("44 11", true), st("43,30, 5"), st("22 .1"), st("7"), st("43 32")}
                : std::vector<ProgStep>{st("43,22, A"), st("42 44"), st("45 .d"), st("A"), st("43 49"), st("21 b"), st("43 21")};
            steps[1].comment = QStringLiteral("radius; in cm");
            steps[3].comment = QStringLiteral("check");
            const QString text = programFragmentText(model, steps, 12, QStringLiteral("Demo"));
            QVERIFY(text.startsWith(QStringLiteral("# VoyagerCalc program · HP-1%1C · 7 steps (012–018)").arg(model == 15 ? 5 : 6)));
            ProgramParse p = parseProgramText(text, model);
            QVERIFY2(p.ok(), qPrintable(p.summary()));
            QCOMPARE(p.detectedModel, model);
            QCOMPARE(keysOf(p.steps), keysOf(steps));
            QCOMPARE(p.steps[1].comment, QStringLiteral("radius; in cm"));
            QCOMPARE(p.steps[3].comment, QStringLiteral("check"));
            QCOMPARE(p.title, QStringLiteral("Demo"));
            // the other model refuses it as a whole
            ProgramParse q = parseProgramText(text, model == 15 ? 16 : 15);
            QVERIFY(!q.ok());
            QVERIFY(q.modelError.contains(QStringLiteral("HP-1%1C program").arg(model == 15 ? 5 : 6)));
        }
    }
    void exportListingText() {
        for (int model : {15, 16}) {
            std::vector<ProgramLine> lines;
            const QStringList keys = model == 15 ? QStringList{"42,21,11", "44 .3", "45,16,11", "43 32"} : QStringList{"43,22, A", "42 44", "42 22", "43 21"};
            for (int i = 0; i < keys.size(); ++i) {
                ProgramLine l; l.line = i + 1; l.keys = keys[i];
                const auto *e = StepCatalog::forModel(model)->find(keys[i]);
                QVERIFY(e);
                const QString mn = e->mnemonic;
                if (mn.size() > 2 && mn[1] == ' ') { l.prefix = mn.left(1); l.mnemonic = mn.mid(2); } else l.mnemonic = mn;
                lines.push_back(l);
            }
            ProgramDocument doc; doc.title = QStringLiteral("Area");
            doc.setComment(QStringLiteral("start here"), 1, lines);
            ProgramDocumentInfo info; info.model = model; info.modelName = model == 15 ? QStringLiteral("HP-15C (15.2)") : QStringLiteral("HP-16C");
            ProgramDocumentContent c(doc, info, lines);
            const QString text = c.plainText();
            ProgramParse p = parseProgramText(text, model);
            QVERIFY2(p.ok(), qPrintable(p.summary() + "\n" + text));
            QCOMPARE(p.detectedModel, model);
            QCOMPARE(keysOf(p.steps), keys);
            QCOMPARE(p.steps[0].comment, QStringLiteral("start here"));
            QCOMPARE(p.title, QStringLiteral("Area"));
        }
    }
    void manzFile() {
        ProgramParse p = parseProgramText(QString::fromUtf8(kManz), 15);
        QVERIFY2(p.ok(), qPrintable(p.summary()));
        QCOMPARE(p.detectedModel, 15);
        QCOMPARE(p.title, QStringLiteral("Mother's Kitchen"));
        QCOMPARE(keysOf(p.steps), (QStringList{"42,21,11", "44 0", "44 .3", "44 24 u", "43,30, 5", "48", "7", "22 .1", "45,16,11", "43 32"}));
        // the same file as UTF-16 (how the simulator writes it)
        auto m = Manz15CProgram::parse(QString::fromUtf8(kManz));
        QVERIFY(m);
        ProgramParse q = parseProgramText(decodeProgramFile(m->fileData()), 15);
        QVERIFY(q.ok());
        QCOMPARE(keysOf(q.steps), keysOf(p.steps));
        // and back to simulator text
        const QString again = programManzText(p.steps, p.title);
        auto m2 = Manz15CProgram::parse(again);
        QVERIFY(m2 && m2->steps == m->steps);
        QCOMPARE(parseProgramText(QString::fromUtf8(kManz), 16).modelError.isEmpty(), false);
    }
    void stepAndKeycodeListings() {
        // forum / handbook style, with and without commas, 2- or 3-digit step numbers
        ProgramParse a = parseProgramText(QStringLiteral("001- 42,21,11\n002-    44  1\n003- 43 32\n"), 15);
        QVERIFY(a.ok()); QCOMPARE(keysOf(a.steps), (QStringList{"42,21,11", "44 1", "43 32"}));
        ProgramParse b = parseProgramText(QStringLiteral("01 42 21 12 LBL B\n02 45 2   RCL 2\n03 20  ×\n04 43 32  RTN\n"), 15);
        QVERIFY2(b.ok(), qPrintable(b.summary())); QCOMPARE(keysOf(b.steps), (QStringList{"42,21,12", "45 2", "20", "43 32"}));
        ProgramParse c = parseProgramText(QStringLiteral("42 21 11\n44 1\n43 32\n"), 15);        // keycodes only
        QVERIFY(c.ok()); QCOMPARE(keysOf(c.steps), (QStringList{"42,21,11", "44 1", "43 32"}));
        ProgramParse d = parseProgramText(QStringLiteral("000 {          }\n001 { 43 22 A } g LBL A\n002 { 42 44 } f WSIZE\n003 { 43 21 } g RTN\n"), 16);
        QVERIFY(d.ok()); QCOMPARE(keysOf(d.steps), (QStringList{"43,22, A", "42 44", "43 21"}));
    }
    void keyNameListings() {
        ProgramParse a = parseProgramText(QStringLiteral("LBL A\nSTO 1   ; radius\nx<>y\n2\n*\ng RTN\n"), 15);
        QVERIFY2(a.ok(), qPrintable(a.summary()));
        QCOMPARE(keysOf(a.steps), (QStringList{"42,21,11", "44 1", "34", "2", "20", "43 32"}));
        QCOMPARE(a.steps[1].comment, QStringLiteral("radius"));
        ProgramParse b = parseProgramText(QStringLiteral("001 LBL A\n002 RCL 1\n003 2\n004 ×\n005 RTN\n"), 16);
        QVERIFY2(b.ok(), qPrintable(b.summary()));
        QCOMPARE(keysOf(b.steps), (QStringList{"43,22, A", "45 1", "2", "20", "43 21"}));
        ProgramParse c = parseProgramText(QStringLiteral("1 LBL B\n2 44 1\n3 RTN\n"), 16);  // numbered: "44 1" is keycodes here
        QVERIFY(c.ok()); QCOMPARE(keysOf(c.steps), (QStringList{"43,22, b", "44 1", "43 21"}));
        ProgramParse d = parseProgramText(QStringLiteral("LBL A    start\nSL       shift\n"), 16);   // two spaces: the rest is a comment
        QVERIFY(d.ok()); QCOMPARE(d.steps[0].comment, QStringLiteral("start")); QCOMPARE(d.steps[1].comment, QStringLiteral("shift"));
    }
    void ambiguityAndChoices() {
        ProgramParse p = parseProgramText(QStringLiteral("LBL A\ng HEX\nRTN\n"), 16);
        QVERIFY(!p.ok());
        QCOMPARE(p.ambiguous, 1);
        const ParsedLine &l = p.lines[1];
        QCOMPARE(int(l.kind), int(ParsedLine::Ambiguous));
        QCOMPARE(int(l.choices.size()), 2);
        QVERIFY(l.message.contains(QStringLiteral("choose")));
        ProgramParse q = parseProgramText(QStringLiteral("LBL A\ng HEX\nRTN\n"), 16, {{2, l.choices[1].keys}});
        QVERIFY(q.ok());
        QCOMPARE(q.steps[1].keys, l.choices[1].keys);
    }
    void errorsNameTheLine() {
        ProgramParse p = parseProgramText(QStringLiteral("001-42,21,11\n002-42,21,19\n003-FOO\nSL\nWSIZE\n43 32\n"), 15);
        QCOMPARE(p.errors, 4);
        QVERIFY(!p.ok());
        QCOMPARE(int(p.lines[1].kind), int(ParsedLine::Error));
        QVERIFY2(p.lines[1].message.contains(QStringLiteral("not HP-15C keycodes")), qPrintable(p.lines[1].message));
        QVERIFY2(p.lines[2].message.contains(QStringLiteral("Unknown key name “FOO”")), qPrintable(p.lines[2].message));
        QVERIFY2(p.lines[3].message.contains(QStringLiteral("HP-16C function")), qPrintable(p.lines[3].message));
        QVERIFY2(p.lines[4].message.contains(QStringLiteral("HP-16C function")), qPrintable(p.lines[4].message));
        QCOMPARE(p.steps.size(), size_t(2));
        ProgramParse q = parseProgramText(QStringLiteral("43,22, A\n"), 15);
        QVERIFY2(q.lines[0].message.contains(QStringLiteral("HP-16C step")), qPrintable(q.lines[0].message));
        QVERIFY(parseProgramText(QString(), 15).steps.empty());
        QVERIFY(!parseProgramText(QString(), 15).ok());
    }
    void stepsFromTheListing() {
        ProgramLine a; a.line = 1; a.keys = "44 11"; a.bytes = QByteArray::fromHex("bf4a");
        ProgramLine b; b.line = 2; b.keys = "44 11"; b.bytes = QByteArray::fromHex("4a");
        auto s = stepsFromListing(15, {a, b}, {{2, QStringLiteral("x")}});
        QVERIFY(s[0].user && !s[1].user);
        QCOMPARE(s[1].comment, QStringLiteral("x"));
    }
    void labels() {
        const std::vector<ProgStep> existing{st("42,21, 1"), st("42,21,11"), st("32 1"), st("43 32")};
        const std::vector<ProgStep> incoming{st("42,21, 1"), st("42,21,11"), st("32 1"), st("22 11"), st("42,10, 1"), st("43 32")};
        const std::vector<int> blocks(incoming.size(), 0);
        RenumberPlan p = planRenumber(15, existing, incoming, blocks, false);
        QVERIFY(p.anyClash() && p.anyLetterClash());
        QCOMPARE(p.blocks[0].map.value("1"), QStringLiteral("0"));
        QVERIFY(!p.blocks[0].map.contains("A"));
        auto out = applyRenumber(15, incoming, blocks, p);
        QCOMPARE(keysOf(out), (QStringList{"42,21, 0", "42,21,11", "32 0", "22 11", "42,10, 0", "43 32"}));
        QVERIFY(p.describe().join("\n").contains(QStringLiteral("LBL 1 is already used: renumbered to LBL 0")));
        QVERIFY(p.describe().join("\n").contains(QStringLiteral("LBL A is already used and was not renumbered")));
        RenumberPlan q = planRenumber(15, existing, incoming, blocks, true);
        QCOMPARE(q.blocks[0].map.value("A"), QStringLiteral("B"));
        QCOMPARE(keysOf(applyRenumber(15, incoming, blocks, q)), (QStringList{"42,21, 0", "42,21,12", "32 0", "22 12", "42,10, 0", "43 32"}));
        // two pasted programs that both use LBL 1, nothing in the calculator yet (building a library)
        const std::vector<ProgStep> two{st("43,22, 1"), st("21 1"), st("43 21"), st("43,22, 1"), st("22 1"), st("43 21")};
        RenumberPlan r = planRenumber(16, {}, two, {0, 0, 0, 1, 1, 1}, false);
        QVERIFY(r.blocks[0].map.isEmpty());
        QCOMPARE(r.blocks[1].map.value("1"), QStringLiteral("0"));
        QCOMPARE(keysOf(applyRenumber(16, two, {0, 0, 0, 1, 1, 1}, r)), (QStringList{"43,22, 1", "21 1", "43 21", "43,22, 0", "22 0", "43 21"}));
        // GTO I is not a label
        QVERIFY(labelUse(15, {st("22 25")}).referenced.isEmpty());
        // blocks from file headers
        ProgramParse pp = parseProgramText(QStringLiteral("# file: a.txt\nLBL 1\nRTN\n# file: b.txt\nLBL 1\nRTN\n"), 16);
        QVERIFY(pp.ok());
        QCOMPARE(pp.stepBlocks, (std::vector<int>{0, 0, 1, 1}));
    }
    void memoryFit() {
        InspectorSnapshot s; s.model = 15; s.variant = 150;
        Pool15 p; p.free = 2; p.programRegisters = 1; s.pool = p;
        std::vector<ProgStep> prog(21, st("1"));
        QVERIFY(checkFit(s, prog).fits);
        prog.push_back(st("2"));
        FitReport f = checkFit(s, prog);
        QVERIFY(!f.fits);
        QCOMPARE(f.need, 22); QCOMPARE(f.available, 21);
        QVERIFY(f.message.contains(QStringLiteral("15C (15.2)")));
        std::vector<ProgStep> two(11, st("44 11", true));
        QCOMPARE(checkFit(s, two).need, 22);
        // 16C: registers are given up as the program grows; the ones that hold a value are named
        QCOMPARE(registers16(29, 0, 16), 101);
        QCOMPARE(registers16(29, 16, 16), 91);
        QCOMPARE(registers16(29, 203, 16), 0);
        InspectorSnapshot t; t.model = 16; t.wordSize = 16;
        Pool16 q; q.layout = Emulator::Memory16Layout::Standard; q.wordSize = 16; q.lines = 0; q.registers = 101; t.pool16 = q;
        for (int i = 0; i < 101; ++i) { RegisterValue r; r.name = QStringLiteral("R%1").arg(i); r.isInteger = true; r.word = (i == 95 || i == 3) ? 7 : 0; t.registers.push_back(r); }
        FitReport g = checkFit(t, std::vector<ProgStep>(30, st("1")));
        QVERIFY(g.fits);
        QCOMPARE(g.registersAfter, 84);
        QCOMPARE(g.lostRegisters, QStringList{"R95"});
        QVERIFY(!checkFit(t, std::vector<ProgStep>(204, st("1"))).fits);
    }
    void editHelpers() {
        std::vector<ProgStep> p{st("1"), st("2"), st("3"), st("4")};
        QCOMPARE(keysOf(deleteSteps(p, 2, 3)), (QStringList{"1", "4"}));
        QCOMPARE(keysOf(insertSteps(p, 0, {st("9")})), (QStringList{"9", "1", "2", "3", "4"}));
        QCOMPARE(keysOf(insertSteps(p, 4, {st("9")})), (QStringList{"1", "2", "3", "4", "9"}));
        QCOMPARE(keysOf(insertSteps(p, 2, {st("8"), st("9")})), (QStringList{"1", "2", "8", "9", "3", "4"}));
        QCOMPARE(stepKeys(16, st("43,22, A")), (std::vector<int>{32, 11, 0}));
        QCOMPARE(stepKeys(16, st("45 .d")), (std::vector<int>{34, 37, 3}));
        QCOMPARE(stepKeys(15, st("44 11", true)), (std::vector<int>{33, 0}));
        QVERIFY(stepKeys(15, st("43,22, A")).empty());
    }
};

QTEST_GUILESS_MAIN(TstProgramText)
#include "tst_programtext.moc"
