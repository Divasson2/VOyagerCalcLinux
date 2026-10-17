// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
// Program analysis and documentation: the same expectations as the Mac app's ProgramDocTests.
#include "TestSupport.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include "Machine.h"
#include "ProgramAnalysis.h"
#include "ProgramDocument.h"
#include "MemoryFileNames.h"
#include "Model.h"

using namespace vc;

class TstAnalysis : public QObject {
    Q_OBJECT
    static std::vector<ProgramLine> financePac(QByteArray fw) {
        Machine m; m.loadFirmware(fw); m.boot(nullptr);
        if (m.variant() != 152) m.switchModel(152);
        m.putMemory(vct::fixture("FinancePac.15c"));
        auto s = inspect(m.calc, true);
        return s ? s->program : std::vector<ProgramLine>{};
    }
    static std::vector<ProgramLine> lines(const QStringList &keys) {
        std::vector<ProgramLine> v;
        for (int i = 0; i < keys.size(); ++i) { ProgramLine l; l.line = i + 1; l.keys = keys[i]; l.mnemonic = keys[i]; v.push_back(l); }
        return v;
    }
private slots:
    void financePacLabelsRegistersFlags() {
        QByteArray fw = vct::firmware15(); if (fw.isEmpty()) fw = vct::firmwareMulti();
        VC_REQUIRE(fw, "15C");
        auto p = financePac(fw);
        QCOMPARE(int(p.size()), 407);
        QCOMPARE(p[0].fullMnemonic(), QString("f LBL 7"));
        ProgramAnalysis a(p, 15);
        auto lab = [&](const char *n) { return a.label(QString::fromLatin1(n)); };
        QVERIFY(lab("A")); QCOMPARE(lab("A")->defined, (std::vector<int>{19}));
        QCOMPARE(lab("7")->defined, (std::vector<int>{1}));
        QCOMPARE(lab("1")->defined, (std::vector<int>{91}));
        QCOMPARE(std::vector<int>(lab("1")->called.begin(), lab("1")->called.begin() + 4), (std::vector<int>{22, 64, 74, 85}));
        QCOMPARE(lab("1")->callKinds, QStringList{"GSB"});
        QCOMPARE(lab("3")->called, (std::vector<int>{52})); QCOMPARE(lab("3")->callKinds, QStringList{"SOLVE"});
        QCOMPARE(lab(".2")->defined, (std::vector<int>{140})); QCOMPARE(lab(".2")->called, (std::vector<int>{144}));
        QVERIFY(lab(".0")->defined.empty());
        QVERIFY(std::count(lab(".0")->called.begin(), lab(".0")->called.end(), 35));
        bool undef = false; for (auto &l : a.undefinedLabels()) undef |= l.name == ".0";
        QVERIFY(undef);
        QStringList names; for (auto &l : a.labels) names << l.name;
        QVERIFY(names.indexOf("9") < names.indexOf(".1"));
        QVERIFY(names.indexOf(".9") < names.indexOf("A"));
        auto reg = [&](const char *n) { return a.reg(QString::fromLatin1(n)); };
        QVERIFY(reg("I"));
        QCOMPARE(std::vector<int>(reg("I")->steps.begin(), reg("I")->steps.begin() + 3), (std::vector<int>{138, 141, 143}));
        QCOMPARE(reg("I")->operations.mid(0, 3), (QStringList{"STO", "(i)", "DSE"}));
        QCOMPARE(reg("(i)")->operations, QStringList{"RCL +"});
        QVERIFY(reg("R.6") && reg("R.6")->operations.contains("STO"));
        QVERIFY(reg("R3")->operations.contains("RCL +"));
        QVERIFY(reg("R.5")->operations.contains("STO +"));
        QStringList fl; for (auto &f : a.flags) fl << f.name;
        QCOMPARE(fl, (QStringList{"0", "1"}));
        QCOMPARE(a.flag("1")->operations, (QStringList{"CF", "SF", "F?"}));
        QCOMPARE(std::vector<int>(a.flag("1")->steps.begin(), a.flag("1")->steps.begin() + 3), (std::vector<int>{51, 92, 119}));
        ProgramDocument d; d.prefill(a);
        QCOMPARE(d.labels.size(), qsizetype(a.labels.size()));
        QCOMPARE(d.registers.size(), qsizetype(a.registers.size()));
    }

    void millerRabin16C() {
        QByteArray fw = vct::firmware16();
        VC_REQUIRE(fw, "16C");
        Machine m; m.loadFirmware(fw); m.boot(nullptr);
        m.putMemory(vct::fixture("miller-rabin-ext-ready.16c"));
        auto s = inspect(m.calc, true);
        QVERIFY(s && s->program.size() > 50);
        ProgramAnalysis a(s->program, 16);
        for (auto &l : s->program) {
            if (l.mnemonic.startsWith("LBL ")) {
                auto *lb = a.label(l.mnemonic.mid(4));
                QVERIFY2(lb && std::count(lb->defined.begin(), lb->defined.end(), l.line), qPrintable(l.mnemonic));
            }
            if (l.mnemonic.startsWith("GSB ") || l.mnemonic.startsWith("GTO ")) {
                QString n = l.mnemonic.mid(4); if (n == "I") n = "I (indirect)";
                auto *lb = a.label(n);
                QVERIFY2(lb && std::count(lb->called.begin(), lb->called.end(), l.line), qPrintable(l.mnemonic));
            }
        }
        QVERIFY(!a.labels.empty() && !a.registers.empty());
    }

    void screenView() {
        ProgramLine l; l.line = 1; l.keys = "42,21,11"; l.mnemonic = "LBL A"; l.prefix = "f";
        QCOMPARE(l.screenView(15), QString("001-42,21,11"));
        ProgramLine b; b.line = 2; b.keys = "15"; b.mnemonic = "1/x";
        QCOMPARE(b.screenView(15), QString("002-    15"));
        ProgramLine c; c.line = 5; c.keys = "44 .6"; c.mnemonic = "STO .6";
        QCOMPARE(c.screenView(15), QString("005-  44 .6"));
        ProgramLine d; d.line = 7; d.keys = "43,22, A"; d.mnemonic = "LBL A"; d.prefix = "g";
        QCOMPARE(d.screenView(16), QString("007-43,22, A"));
    }

    void commentsFollowInsertsAndDeletes() {
        QStringList pool{"1", "2", "3", "40", "30", "20", "10", "36", "44 1", "45 2", "43 32", "34", "33"};
        quint64 seed = 7;
        QStringList base;
        for (int i = 0; i < 60; ++i) { seed = seed * 6364136223846793005ULL + 1442695040888963407ULL; base << pool[int((seed >> 33) % pool.size())]; }
        base[20] = "40"; base[29] = "44 1"; base[44] = "45 2";
        ProgramDocument d;
        d.setComment("start", 1, lines(base));
        d.setComment("add here", 21, lines(base));
        d.setComment("store", 45, lines(base));
        d.setComment("gone soon", 30, lines(base));
        QCOMPARE(d.comment(21), QString("add here"));
        QStringList changed = base;
        changed.insert(10, "26"); changed.insert(11, "27"); changed.insert(12, "28");
        changed.removeAt(32);
        auto r = d.rematch(lines(changed));
        QCOMPARE(d.comment(1), QString("start"));
        QCOMPARE(d.comment(24), QString("add here"));
        QCOMPARE(d.comment(47), QString("store"));
        auto o = d.orphanedComments();
        QCOMPARE(int(o.size()), 1); QCOMPARE(o[0].text, QString("gone soon"));
        QCOMPARE(r.kept, 1); QCOMPARE(r.moved, 2); QCOMPARE(r.orphaned, 1);
        auto back = ProgramDocument::decode(d.encoded());
        QVERIFY(back && *back == d);
        QCOMPARE(QFileInfo(ProgramDocument::sidecarPath("/x/FinancePac.15c")).fileName(), QString("FinancePac.15c.voyagerdoc"));
    }

    void documentJsonSchemaMatchesMac() {
        ProgramDocument d; d.title = "T"; d.author = "A"; d.model = "15C";
        d.labels["A"] = "x"; d.registers["I"] = {"idx", "loop"}; d.flags["1"] = "f";
        d.comments.push_back({3, "c", "42", {"1"}, {"2"}, false});
        auto doc = QJsonDocument::fromJson(d.encoded()).object();
        QCOMPARE(doc["format"].toString(), QString("voyagerdoc")); QCOMPARE(doc["version"].toInt(), 1);
        for (auto k : {"title", "author", "description", "model", "labels", "registers", "flags", "comments"}) QVERIFY2(doc.contains(k), k);
        auto c = doc["comments"].toArray()[0].toObject();
        for (auto k : {"step", "text", "keys", "before", "after", "orphaned"}) QVERIFY2(c.contains(k), k);
        auto r = doc["registers"].toObject()["I"].toObject();
        QVERIFY(r.contains("name") && r.contains("meaning"));
        QVERIFY(!ProgramDocument::decode("{\"format\":\"other\"}").has_value());
    }
    // ---- 0.6.9 -------------------------------------------------------------------------------------------------------

    static std::vector<ProgramLine> lines12(const QStringList &mn) {
        std::vector<ProgramLine> v;
        for (int i = 0; i < mn.size(); ++i) { ProgramLine l; l.line = i + 1; l.keys = "x"; l.mnemonic = mn[i]; if (mn[i].startsWith("GTO")) l.prefix = "g"; v.push_back(l); }
        return v;
    }
    static ProgramDocumentContent content12(const QStringList &mn) {
        ProgramDocumentInfo info; info.modelName = "HP-12C"; info.model = 12; info.memoryFile = "t.bin12c"; info.date = QDate(2026, 1, 1);
        return ProgramDocumentContent(ProgramDocument(), info, lines12(mn));
    }

    void twelveCTargetZeroIsAlwaysDefined() {
        ProgramAnalysis a(lines12({"GTO 00", "1", "GTO 03", "GTO 09"}), 12);
        auto lab = [&](const QString &n) { return a.label(n); };
        QVERIFY(lab("00")); QCOMPARE(lab("00")->defined, std::vector<int>{0}); QCOMPARE(lab("00")->called, std::vector<int>{1});
        QCOMPARE(lab("03")->defined, std::vector<int>{3});
        QVERIFY(lab("09")->defined.empty());
        auto empty = content12(QStringList(8, "GTO 00"));
        auto rows = empty.labelRows();
        QCOMPARE(int(rows.size()), 1); QCOMPARE(rows[0].defined, QString("00 (start)"));
        QVERIFY(!empty.plainText().contains("not defined"));
    }

    void twelveCTextExportAlwaysHasBranchTargets() {
        const QString t = content12({"GTO 00", "1", "GTO 03"}).plainText();
        QVERIFY2(t.contains("\nBRANCH TARGETS\n"), qPrintable(t));
        QVERIFY(t.contains("line 00") && t.contains("00 (start)"));
        QVERIFY(!t.contains("\nLABELS\n"));
        const QString none = content12({"1", "2", "+"}).plainText();
        QVERIFY2(none.contains("\nBRANCH TARGETS\n"), qPrintable(none));
        QVERIFY(none.contains("No GTO in this program."));
        QVERIFY(t.indexOf("BRANCH TARGETS") < t.indexOf("LISTING"));
    }

    void memoryFileEndings() {
        using namespace MemoryFileNames;
        QCOMPARE(defaultExtension(12), QString("bin12c")); QCOMPARE(defaultExtension(15), QString("bin15c")); QCOMPARE(defaultExtension(16), QString("bin16c"));
        QCOMPARE(Model::hp15c().fileExtension(), QString("bin15c"));
        QCOMPARE(modelHint("BIN16C"), 16); QCOMPARE(modelHint("12c"), 12); QCOMPARE(modelHint("15cmem"), 15); QCOMPARE(modelHint("txt"), 0);
        const QString x = QFileInfo("/x").absoluteFilePath();         // "/x" on Linux, "C:/x" on Windows (a path without a drive letter gets one)
        auto side = sidecarCandidates("/x/Pack.bin15c");
        QCOMPARE(side.first(), QString("/x/Pack.bin15c.voyagerdoc"));
        QVERIFY(side.contains(x + "/Pack.15c.voyagerdoc") && side.contains(x + "/Pack.15cmem.voyagerdoc") && side.contains(x + "/Pack.voyagerdoc"));
        QVERIFY(sidecarCandidates("/x/Pack.dat").size() == 2);
    }

    void memoryFileContentNotEnding() {
        using namespace MemoryFileNames;
        QByteArray image(2048, '\0'); for (int i = 0; i < image.size(); ++i) image[i] = char(i * 37 + 11);
        QVERIFY(problem(image, "anything.xyz").isEmpty());            // any ending, right size and binary: a memory file
        QByteArray text = "{ 15C program }\n# step 1\nLBL A\nRTN\n"; text.append(QByteArray(2048 - text.size(), ' '));
        const QString msg = problem(text, "Manz.15c");
        QVERIFY2(msg.contains("text program"), qPrintable(msg));   // even exactly 2048 bytes of text is never taken for a memory
        QVERIFY(problem(QByteArray(100, 'x'), "n").contains("exactly 2048 bytes"));
        QVERIFY(problem(QByteArray(100, '\1'), "n").contains("exactly 2048 bytes"));
    }
};

QTEST_GUILESS_MAIN(TstAnalysis)
#include "tst_analysis.moc"
