// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
// 0.7.8 program copy / paste through the window (offscreen): paste from the clipboard (key names, our copied text),
// copy, cut, delete, the 20-level undo / redo, labels renumbered, the Paste Program Text dialog (errors, choices,
// letter labels, too big), comments travelling with their steps, the HP-16C. Screenshots when VC_SHOTS is set.
#include "TestSupport.h"
#include "InspectorPanel.h"
#include "MainWindow.h"
#include "PasteProgramDialog.h"
#include "ProgramEntry.h"
#include <QClipboard>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QTemporaryDir>

using namespace vc;

namespace {
template <typename F> bool waitFor(F cond, int ms = 20000) {
    QElapsedTimer t; t.start();
    while (!cond()) { if (t.elapsed() > ms) return false; QTest::qWait(10); }
    return true;
}
}

class TstProgramUi : public QObject {
    Q_OBJECT
    QTemporaryDir data_, out_;
    QStringList errors, notices, confirms;
    bool confirmAnswer = true;
    std::function<bool(PasteProgramDialog &)> dialogHook;
    int dialogs = 0;

    std::unique_ptr<MainWindow> window(const QByteArray &fw) {
        auto w = std::make_unique<MainWindow>();
        w->prompts.error = [this](const QString &m) { errors << m; };
        w->prompts.notice = [this](const QString &m) { notices << m; };
        w->prompts.confirm = [](const PendingAction &) { return 1; };
        w->prompts.confirmEdit = [this](const QString &t, const QString &) { confirms << t; return confirmAnswer; };
        w->prompts.pasteDialog = [this](PasteProgramDialog &d) { ++dialogs; return dialogHook ? dialogHook(d) : false; };
        w->resize(1100, 640);
        w->show();
        w->setShowInspector(true);
        w->startFirmware(fw, "test.bin", false);
        if (!waitFor([&] { return w->isRunning(); })) { QTest::qFail("did not boot", __FILE__, __LINE__); return w; }
        w->worker()->performAndWait([](Machine &m) { enterProgram(m, {}); });     // an empty program to start with
        return w;
    }
    static std::vector<ProgStep> program(MainWindow &w) {
        std::vector<ProgStep> out;
        w.worker()->performAndWait([&](Machine &m) { if (auto s = inspect(m.calc, true)) out = stepsFromListing(s->model, s->program); });
        return out;
    }
    static QStringList keys(MainWindow &w) {
        QStringList k;
        for (auto &s : program(w)) k << QString(s.keys).replace(',', ' ').simplified();
        return k;
    }
    /// Wait for an edit (or undo / redo) to finish.
    static bool settled(MainWindow &w) { QTest::qWait(30); return waitFor([&] { return !w.isEditingProgram(); }); }
    bool editDone(MainWindow &w, int undoBefore) {
        return waitFor([&] { return !w.isEditingProgram() && (w.programUndoDepth() != undoBefore || !errors.isEmpty()); });
    }
    void shot(QWidget *wid, const char *name) {
        const QByteArray dir = qgetenv("VC_SHOTS");
        if (dir.isEmpty()) return;
        wid->grab().save(QString::fromUtf8(dir) + QLatin1Char('/') + QLatin1String(name) + QStringLiteral(".png"));
    }

private slots:
    void initTestCase() {
        qputenv("VOYAGERCALC_DATA_DIR", data_.path().toUtf8());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, out_.path());
    }
    void init() { errors.clear(); notices.clear(); confirms.clear(); confirmAnswer = true; dialogHook = nullptr; dialogs = 0; }

    void pasteCopyCutDeleteUndo15C() {
        QByteArray fw = vct::firmware15(); VC_REQUIRE(fw, "HP-15C");
        auto w = window(fw);
        QVERIFY(w->canEditProgram());
        QVERIFY(keys(*w).isEmpty());
        // key names from the clipboard go straight in
        QApplication::clipboard()->setText("LBL A\nSTO 1\nRCL 1\nx²\nRTN\n");
        w->pasteProgram(0);
        QVERIFY(editDone(*w, 0));
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join('|')));
        QCOMPARE(dialogs, 0);
        QCOMPARE(keys(*w), QStringList({"42 21 11", "44 1", "45 1", "43 11", "43 32"}));
        QCOMPARE(w->programUndoDepth(), 1);
        QVERIFY(waitFor([&] { return w->inspector()->selectedSteps() == std::make_pair(1, 5); }));
        shot(w.get(), "qt-window-pasted-15c");

        // copy steps 2–3: our text
        QApplication::clipboard()->clear();
        w->copyProgramSteps(2, 3);
        QVERIFY(waitFor([&] { return QApplication::clipboard()->text().contains("STO 1"); }));
        const QString copied = QApplication::clipboard()->text();
        QVERIFY2(copied.startsWith("# VoyagerCalc program"), qPrintable(copied));
        QVERIFY(copied.contains("002-"));
        QVERIFY(!copied.contains("x²") && !copied.contains("LBL"));

        // delete 2–3, undo, redo, undo
        w->deleteProgramSteps(2, 3);
        QVERIFY(editDone(*w, 1));
        QCOMPARE(keys(*w), QStringList({"42 21 11", "43 11", "43 32"}));
        QCOMPARE(w->programUndoDepth(), 2);
        w->undoProgramEdit(); QVERIFY(settled(*w));
        QCOMPARE(keys(*w).size(), 5);
        QCOMPARE(w->programRedoDepth(), 1);
        w->redoProgramEdit(); QVERIFY(settled(*w));
        QCOMPARE(keys(*w).size(), 3);
        w->undoProgramEdit(); QVERIFY(settled(*w));
        QCOMPARE(keys(*w).size(), 5);
        QVERIFY(confirms.isEmpty());        // nothing changed in between: no question

        // paste the copied steps after the last step (our clipboard text)
        QApplication::clipboard()->setText(copied);
        const int depth = w->programUndoDepth();
        w->pasteProgram(5);
        QVERIFY(editDone(*w, depth));
        QCOMPARE(keys(*w), QStringList({"42 21 11", "44 1", "45 1", "43 11", "43 32", "44 1", "45 1"}));

        // cut step 1 to the clipboard
        w->cutProgramSteps(1, 1);
        QVERIFY(editDone(*w, depth + 1));
        QCOMPARE(keys(*w).size(), 6);
        QVERIFY(QApplication::clipboard()->text().contains("42 21 11") || QApplication::clipboard()->text().contains("42,21,11"));

        // a change on the calculator after the edit: undo asks first; "cancel" keeps everything
        w->worker()->performAndWait([](Machine &m) { m.calc.tap(Emulator::keyCode(15, "7"), 80, 220); m.calc.tap(Emulator::keyCode(15, "STO"), 80, 220); m.calc.tap(Emulator::keyCode(15, "5"), 80, 220); });
        confirmAnswer = false;
        w->undoProgramEdit(); QVERIFY(settled(*w));
        QCOMPARE(confirms.size(), 1);
        QCOMPARE(keys(*w).size(), 6);
        confirmAnswer = true;
        w->undoProgramEdit(); QVERIFY(settled(*w));
        QCOMPARE(keys(*w).size(), 7);
    }

    void undoKeepsTwentyLevels() {
        QByteArray fw = vct::firmware15(); VC_REQUIRE(fw, "HP-15C");
        auto w = window(fw);
        QApplication::clipboard()->setText("ENTER\n");
        for (int i = 0; i < 22; ++i) {
            const int d = w->programUndoDepth();
            w->pasteProgram(i);
            QVERIFY(waitFor([&] { return !w->isEditingProgram() && keys(*w).size() == i + 1; }));
            Q_UNUSED(d)
        }
        QCOMPARE(w->programUndoDepth(), MainWindow::kProgramUndoLevels);
        for (int i = 0; i < 25; ++i) { w->undoProgramEdit(); QVERIFY(settled(*w)); }
        QCOMPARE(keys(*w).size(), 2);       // 22 pastes, 20 undone
        QCOMPARE(w->programRedoDepth(), 20);
    }

    void labelsAreRenumbered() {
        QByteArray fw = vct::firmware15(); VC_REQUIRE(fw, "HP-15C");
        auto w = window(fw);
        // the same digit-label program twice: the second LBL 1 / GTO 1 become LBL 0 … automatically, with a notice
        const QString lib = "LBL 1\nENTER\nGTO 1\nRTN\n";
        QApplication::clipboard()->setText(lib);
        w->pasteProgram(0); QVERIFY(editDone(*w, 0));
        w->pasteProgram(4); QVERIFY(editDone(*w, 1));
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join('|')));
        QCOMPARE(dialogs, 0);
        QCOMPARE(notices.size(), 1);
        QVERIFY2(notices[0].contains("LBL 1"), qPrintable(notices[0]));
        const QStringList k = keys(*w);
        QCOMPARE(k.size(), 8);
        QCOMPARE(k[4].left(5), QStringLiteral("42 21"));
        QVERIFY(k[4] != k[0]);
        QCOMPARE(k[6].mid(3), k[4].mid(6));     // GTO follows its label

        // a letter label clash opens the dialog; it is renumbered only when asked
        QApplication::clipboard()->setText("LBL A\nRTN\n");
        w->pasteProgram(8); QVERIFY(editDone(*w, 2));
        QCOMPARE(dialogs, 0);                    // LBL A is free: straight in
        dialogHook = [this](PasteProgramDialog &d) {
            if (!d.plan().anyLetterClash()) return false;
            if (!d.canInsert()) return false;    // allowed without renumbering (duplicate letter labels are legal)
            shot(&d, "qt-paste-dialog-letter-clash");
            d.setRenumberLetters(true);
            return true;
        };
        w->pasteProgram(10); QVERIFY(editDone(*w, 3));
        QCOMPARE(dialogs, 1);
        const QStringList k2 = keys(*w);
        QCOMPARE(k2.size(), 12);
        QVERIFY(k2[10] != QStringLiteral("42 21 11") && k2[10] != k2[8]);
        QVERIFY(k2[10].startsWith("42 21"));
    }

    void dialogShowsErrorsAndChoices() {
        QByteArray fw = vct::firmware15(); VC_REQUIRE(fw, "HP-15C");
        auto w = window(fw);
        QStringList seen;
        dialogHook = [&](PasteProgramDialog &d) {
            seen << d.statusText();
            for (const auto &l : d.parse().lines) if (l.kind == ParsedLine::Error) seen << QStringLiteral("%1: %2").arg(l.line).arg(l.message);
            shot(&d, "qt-paste-dialog-errors");
            return d.canInsert();
        };
        QApplication::clipboard()->setText("LBL A\nSTO 1\nFROB 3\n42 99\nRTN\n");
        w->pasteProgram(0);
        QVERIFY(waitFor([&] { return dialogs == 1; }));
        QVERIFY(settled(*w));
        QVERIFY(keys(*w).isEmpty());
        QCOMPARE(w->programUndoDepth(), 0);
        const QString all = seen.join('\n');
        QVERIFY2(all.contains("3: ") && all.contains("FROB"), qPrintable(all));
        QVERIFY2(all.contains("4: "), qPrintable(all));

        // too big: Insert is off and nothing changes
        QString big; for (int i = 0; i < 600; ++i) big += "ENTER\n";
        dialogs = 0; seen.clear();
        dialogHook = [&](PasteProgramDialog &d) { seen << d.statusText(); return d.canInsert(); };
        w->pasteProgramText(0, big);
        QVERIFY(waitFor([&] { return dialogs == 1; }));
        QVERIFY(seen.join(' ').contains("byte") || seen.join(' ').contains("memory") || seen.join(' ').contains("fit"));
        QVERIFY(keys(*w).isEmpty());

        // a good text in the dialog, with a comment that travels with its step
        dialogs = 0;
        dialogHook = [&](PasteProgramDialog &d) {
            shot(&d, "qt-paste-dialog-ok");
            return d.canInsert() && d.steps().size() == 3;
        };
        w->pasteProgramText(0, "001-42,21,11  f LBL A\n002-   44  1  STO 1   ; radius\n003-   43 32  g RTN\n");
        QVERIFY(editDone(*w, 0));
        QCOMPARE(keys(*w).size(), 3);
        QCOMPARE(w->programDoc().comment(2), QStringLiteral("radius"));
        // insert a step at the top: the comment moves to step 3
        QApplication::clipboard()->setText("ENTER\n");
        w->pasteProgram(0); QVERIFY(editDone(*w, 1));
        QCOMPARE(w->programDoc().comment(3), QStringLiteral("radius"));
        QVERIFY(w->programDoc().comment(2).isEmpty());
        // undo puts the notes back too
        w->undoProgramEdit(); QVERIFY(settled(*w));
        QCOMPARE(w->programDoc().comment(2), QStringLiteral("radius"));
    }

    void insertFilesOneAfterTheOther() {
        QByteArray fw = vct::firmware15(); VC_REQUIRE(fw, "HP-15C");
        auto w = window(fw);
        QTemporaryDir dir;
        auto write = [&](const QString &name, const QByteArray &text) { QFile f(dir.filePath(name)); f.open(QIODevice::WriteOnly); f.write(text); return f.fileName(); };
        const QString a = write("a.txt", "LBL A\nLBL 1\nGTO 1\nRTN\n");
        const QString b = write("b.txt", "LBL B\nLBL 1\nGSB 1\nRTN\n");
        dialogHook = [&](PasteProgramDialog &d) { return d.canInsert() && d.plan().anyClash(); };
        w->insertProgramFiles(0, {a, b});
        QVERIFY(editDone(*w, 0));
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join('|')));
        const QStringList k = keys(*w);
        QCOMPARE(k.size(), 8);
        QCOMPARE(k[1], QStringLiteral("42 21 1"));
        QVERIFY(k[5] != k[1]);                    // b's LBL 1 renumbered
    }

    void hp16cPasteAndUndo() {
        for (const char *which : {"stock", "206"}) {
            QByteArray fw = QByteArray(which) == "stock" ? vct::firmware16() : vct::firmware206();
            if (fw.isEmpty()) continue;
            init();
            auto w = window(fw);
            if (QByteArray(which) == "206" && w->snapshot().variant != 160) {
                w->chooseModel(160);
                QVERIFY(waitFor([&] { return !w->isSwitching() && w->snapshot().variant == 160; }));
                w->worker()->performAndWait([](Machine &m) { enterProgram(m, {}); });
            }
            QCOMPARE(w->snapshot().model, 16);
            // key names (HEX, F as a digit, SL)
            QApplication::clipboard()->setText("LBL A\nHEX\nF\nSL\nSTO 1\nRTN\n");
            w->pasteProgram(0);
            QVERIFY(editDone(*w, 0));
            QVERIFY2(errors.isEmpty(), qPrintable(errors.join('|')));
            QCOMPARE(keys(*w).size(), 6);
            w->deleteProgramSteps(3, 4); QVERIFY(editDone(*w, 1));
            QCOMPARE(keys(*w).size(), 4);
            w->undoProgramEdit(); QVERIFY(settled(*w));
            QCOMPARE(keys(*w).size(), 6);
            // an ambiguous key name: the dialog offers the choices; nothing goes in until one is taken
            bool sawChoices = false;
            dialogHook = [&](PasteProgramDialog &d) {
                for (const auto &l : d.parse().lines)
                    if (l.kind == ParsedLine::Ambiguous) {
                        sawChoices = l.choices.size() >= 2 && !d.canInsert();
                        if (QByteArray(which) == "stock") shot(&d, "qt-paste-dialog-choices-16c");
                        d.choose(l.line, 0);
                        break;
                    }
                return d.canInsert();
            };
            const int depth = w->programUndoDepth();
            w->pasteProgramText(6, "g HEX\nRTN\n");
            QVERIFY(editDone(*w, depth));
            QVERIFY(sawChoices);
            QCOMPARE(keys(*w).size(), 8);
            shot(w.get(), QByteArray(which) == "stock" ? "qt-window-16c" : "qt-window-16c-206");
        }
    }
};

QTEST_MAIN(TstProgramUi)
#include "tst_programui.moc"
