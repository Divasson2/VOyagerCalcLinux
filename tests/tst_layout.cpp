// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
//
// 0.7.6: the firmware catalogue and automatic 16C layout detection/conversion.
// Every memory image here is built in the test. No calculator dump and no ROM is read.
#include "TestSupport.h"
#include "Emulator.h"
#include "Inspector.h"
#include "InspectorPanel.h"
#include "Machine.h"
#include "MainWindow.h"
#include "PdfExport.h"
#include "ProgramDocument.h"
#include <QElapsedTimer>
#include <QFile>
#include <QMenuBar>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>

using namespace vc;

namespace {
template <typename F> bool waitFor(F cond, int ms = 20000) {
    QElapsedTimer t; t.start();
    while (!cond()) { if (t.elapsed() > ms) return false; QTest::qWait(10); }
    return true;
}
static const char kMagic[] = "\xAF\x54\x39\xFE\xD9\x67\xBC\x12";
QByteArray blankMemory() {
    QByteArray m(Emulator::kMemorySize, 0);
    m[1] = 8;
    for (int i = 0; i < 8; ++i) m[0x7F0 + i] = kMagic[i];
    return m;
}
void setLines(QByteArray &m, int lines) {
    const int blocks = lines == 0 ? 0 : (lines - 1) / 7;
    const int pos = lines == 0 ? 0 : (lines - 1) % 7 + 1;
    m[0x7E0] = char(0xFC - blocks);
    m[0x7E1] = char(pos & 15);
    m[0x7E8] = m[0x7E0];
    m[0x7E9] = m[0x7E1];
}
bool hasLayout(const std::vector<Emulator::Memory16Layout> &v, Emulator::Memory16Layout l) {
    return std::find(v.begin(), v.end(), l) != v.end();
}
}

class TstLayout : public QObject {
    Q_OBJECT
    QTemporaryDir data_, out_;
private slots:
    void initTestCase() {
        qputenv("VOYAGERCALC_DATA_DIR", data_.path().toUtf8());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, out_.path());
    }

    void catalogueHasThirteenConfirmedRows() {
        const auto rows = Emulator::knownFirmwareCatalogue();
        QCOMPARE(rows.size(), 13u);
        QSet<QString> codes;
        const Emulator::KnownFirmware *eaea = nullptr, *e9 = nullptr, *b3 = nullptr, *e3 = nullptr, *c8 = nullptr, *e8 = nullptr;
        for (const auto &r : rows) {
            QVERIFY(!r.displayName.isEmpty());
            QVERIFY(!r.notes.isEmpty());
            QVERIFY(!r.switchMethod.isEmpty());
            QVERIFY(!codes.contains(r.code));
            codes.insert(r.code);
            if (r.variants.size() > 1) { QCOMPARE(r.switchMenu, 4); QCOMPARE(r.switchKeys.size(), r.variants.size()); }
            else { QCOMPARE(r.switchMenu, 0); QVERIFY(r.switchKeys.empty()); }
            if (r.code == "EAEA") eaea = &r;
            if (r.code == "E9E9") e9 = &r;
            if (r.code == "B3B3") b3 = &r;
            if (r.code == "E3E3") e3 = &r;
            if (r.code == "C8C8") c8 = &r;
            if (r.code == "E8E8") e8 = &r;
        }
        QVERIFY(eaea && e9 && b3 && e3 && c8 && e8);
        QCOMPARE(eaea->layout, Emulator::Memory16Layout::Layout206);
        QVERIFY(eaea->perModeSlots);
        QCOMPARE(eaea->displayName, QString("HP-16C Enhanced EAEA"));
        QVERIFY(!eaea->displayName.contains(QStringLiteral("TEST")));
        QVERIFY(eaea->notes.contains(QStringLiteral("automatically")));
        QVERIFY(eaea->notes.contains(QStringLiteral("Undo Load")));
        QVERIFY(!eaea->notes.contains(QStringLiteral("explicit conversion")));
        QVERIFY(eaea->sources.contains(QStringLiteral("HP16C-CE-enhanced-152-EAEA-guide.pdf")));
        QVERIFY(eaea->sources.contains(QStringLiteral("HP16C-EAEA-HARDWARE-NOTES.md")));
        QCOMPARE(eaea->keyFor(152), 3);
        QCOMPARE(eaea->keyFor(160), 1);
        QCOMPARE(e9->layout, Emulator::Memory16Layout::Layout176);
        QCOMPARE(e9->keyFor(160), 1);
        QCOMPARE(b3->layout, Emulator::Memory16Layout::LegacyB3);
        QVERIFY(b3->notes.contains("alias"));
        QCOMPARE(e3->layout, Emulator::Memory16Layout::EarlyE3E3);
        QCOMPARE(c8->variants, (std::vector<int>{150, 152, 160}));
        QCOMPARE(c8->layout, Emulator::Memory16Layout::Standard);
        QCOMPARE(e8->keyFor(160), 5);
        int keeping = 0;
        for (const auto &r : rows) if (r.perModeSlots) { ++keeping; QVERIFY(r.code == "E9E9" || r.code == "EAEA"); }
        QCOMPARE(keeping, 2);
        QCOMPARE(Emulator::memory16Slots(Emulator::Memory16Layout::Layout206), 206);
        QCOMPARE(Emulator::memory16Slots(Emulator::Memory16Layout::Layout176), 176);
        QCOMPARE(Emulator::memory16Slots(Emulator::Memory16Layout::Standard), 29);
    }

    void automaticImportFollowsTheThreeLayouts() {
        QByteArray empty = blankMemory();
        const auto emptyAnalysis = Emulator::analyzeMemory16(empty);
        QVERIFY(emptyAnalysis.magicValid);
        QVERIFY(emptyAnalysis.pointersValid);
        QVERIFY(emptyAnalysis.upperWindowEmpty);
        QVERIFY(!hasLayout(emptyAnalysis.candidates, Emulator::Memory16Layout::EarlyE3E3));
        for (auto target : {Emulator::Memory16Layout::Standard, Emulator::Memory16Layout::Layout176, Emulator::Memory16Layout::Layout206}) {
            const auto p = Emulator::prepareMemory16(empty, target);
            QVERIFY(p.ambiguous);
            QVERIFY(!p.converted);
            QCOMPARE(p.data, empty);
            QVERIFY(p.note.contains("current") || p.note.contains("selected"));
        }

        QByteArray wide = blankMemory();
        setLines(wide, 7);
        wide[0x6E0] = 0x0B;
        wide[0x6E3] = 0x22;
        const auto grown = Emulator::prepareMemory16(wide, Emulator::Memory16Layout::Layout206);
        QVERIFY(grown.converted);
        QVERIFY(!grown.ambiguous);
        QVERIFY(grown.note.contains("program placement"));
        QCOMPARE(grown.data.size(), 2048);
        QCOMPARE(grown.data.left(0x168), wide.left(0x168));
        QCOMPARE(grown.data.mid(0x7D8), wide.mid(0x7D8));
        QCOMPARE(grown.data.mid(0x7F0, 8), QByteArray(kMagic, 8));
        QCOMPARE(int(uchar(grown.data[0x7D0])), 0x0B);
        const auto back = Emulator::convertPool(grown.data, Emulator::Memory16Layout::Layout206, Emulator::Memory16Layout::Layout176);
        QCOMPARE(back, wide);

        QByteArray sparse = blankMemory();
        setLines(sparse, 7);
        sparse[0x7D0] = 0x0B;
        const auto analysis = Emulator::analyzeMemory16(sparse);
        QVERIFY(hasLayout(analysis.programHeads, Emulator::Memory16Layout::Standard));
        QVERIFY(hasLayout(analysis.programHeads, Emulator::Memory16Layout::Layout206));
        const auto shrunk = Emulator::prepareMemory16(sparse, Emulator::Memory16Layout::Layout176);
        QVERIFY(shrunk.converted);
        QVERIFY(shrunk.ambiguous);
        QVERIFY(shrunk.note.contains("identical bytes"));
        QVERIFY(shrunk.note.contains("compatible raw layouts"));
        const auto same206 = Emulator::prepareMemory16(sparse, Emulator::Memory16Layout::Layout206);
        QVERIFY(same206.ambiguous);
        QVERIFY(!same206.converted);
        QCOMPARE(same206.data, sparse);

        QByteArray low = blankMemory();
        low[0x168] = 0x42;
        const auto kept = Emulator::prepareMemory16(low, Emulator::Memory16Layout::Layout206);
        QVERIFY(kept.ambiguous);
        QVERIFY(!kept.converted);
        QCOMPARE(kept.data, low);
        QVERIFY(kept.note.contains("current"));
        QCOMPARE(Emulator::convertPool(low, Emulator::Memory16Layout::Layout206, Emulator::Memory16Layout::Layout176), low);

        QByteArray early = blankMemory();
        const auto historical = Emulator::prepareMemory16(early, Emulator::Memory16Layout::Layout206, Emulator::Memory16Layout::EarlyE3E3);
        QVERIFY(!historical.converted);
        QCOMPARE(historical.data, early);
        QVERIFY(historical.note.contains("Historical"));

        QByteArray lossy = blankMemory();
        setLines(lossy, 90);
        lossy[0x168 + (176 - 13) * 8] = 1;
        lossy[0x168 + (206 - 13) * 8] = 0x0B;
        QVERIFY_THROWS_EXCEPTION(Error, Emulator::prepareMemory16(lossy, Emulator::Memory16Layout::Layout176, Emulator::Memory16Layout::Layout206));
        QCOMPARE(lossy[0x168 + (176 - 13) * 8], char(1));
    }

    void chipWindowIgnoresFdThroughFf() {
        QByteArray m = blankMemory();
        m[0x7D8] = char(0xAB);
        m[0x7E1] = char(0xA0);
        const auto a = Emulator::analyzeMemory16(m);
        QVERIFY(a.upperWindowEmpty);
        QVERIFY(a.gapEmpty);
        QVERIFY(a.stockPoolEmpty);
        for (int pos : {0x6E8, 0x6EF, 0x6F0, 0x7D7}) {
            QByteArray changed = m;
            changed[pos] = 1;
            const auto b = Emulator::analyzeMemory16(changed);
            QVERIFY2(!b.upperWindowEmpty, qPrintable(QString::number(pos, 16)));
            QCOMPARE(b.gapEmpty, pos >= 0x6F0);
            QCOMPARE(b.stockPoolEmpty, pos < 0x6F0);
        }
    }

    void inspectorAndPdfFollow206() {
        auto snap = std::make_shared<InspectorSnapshot>();
        snap->model = 16; snap->variant = 160; snap->base = 10; snap->wordSize = 16; snap->hasProgram = true; snap->lines = 0; snap->registerCount = 721;
        snap->firmwareName = QStringLiteral("HP-16C Enhanced EAEA · CE EAEA");
        Pool16 pool; pool.layout = Emulator::Memory16Layout::Layout206; pool.wordSize = 16; pool.lines = 0; pool.registers = 721;
        QCOMPARE(pool.slotCount(), 206);
        QCOMPARE(pool.floor(), 0x2F);
        QCOMPARE(pool.remap(), 0x0F);
        QCOMPARE(pool.dataBytes(), 1442);
        QVERIFY(pool.range().contains("2F"));
        QVERIFY(pool.summary().contains("721"));
        snap->pool16 = pool;
        RegisterValue r; r.name = "R720"; r.text = "12345"; r.decimal = "12345"; r.isInteger = true;
        snap->registers.push_back(r);
        ProgramLine line; line.line = 1; line.keys = "43,22, A"; line.mnemonic = "LBL A";
        snap->program.push_back(line);
        InspectorPanel panel; panel.resize(360, 640); panel.setPane(InspectorPanel::Both); panel.setSnapshot(snap); panel.show();
        QVERIFY(QTest::qWaitForWindowExposed(&panel));
        const QString rows = panel.registerRowTexts().join('\n');
        QVERIFY2(rows.contains("206"), qPrintable(rows));
        QVERIFY(rows.contains("R720"));
        QVERIFY(panel.headerText().contains("EAEA"));
        QVERIFY(panel.programRowTexts().join('\n').contains("001"));

        ProgramDocumentInfo info; info.modelName = "HP-16C"; info.firmware = "EAEA"; info.model = 16;
        ProgramDocumentContent content(ProgramDocument(), info, snap->program, std::nullopt, {}, snap->pool16);
        const QString joined = [&] { QString s; for (auto &row : content.memoryRows()) s += row.second + '\n'; return s; }();
        QVERIFY(joined.contains("206"));
        QVERIFY(joined.contains("2F"));
        QVERIFY(content.plainText().contains("MEMORY CONFIGURATION"));
        QVERIFY(content.plainText().contains("R720") || content.plainText().contains("206"));
        const QByteArray pdf = renderProgramDocumentPdf(content, Paper::A4, PdfFonts::fromResources());
        QVERIFY(pdf.startsWith("%PDF"));
        if (QStandardPaths::findExecutable("pdftotext").isEmpty()) return;
        const QString path = out_.path() + "/pool206.pdf";
        QFile f(path); QVERIFY(f.open(QIODevice::WriteOnly)); f.write(pdf); f.close();
        auto extract = [&](const QStringList &args) {
            QProcess p; p.start("pdftotext", args); p.waitForFinished(20000);
            return QString::fromUtf8(p.readAllStandardOutput());
        };
        // -layout can split a word when glyph advances do not match the viewer's; the
        // plain extraction keeps the text-showing order. Either form is acceptable.
        const QString laidOut = extract({QStringLiteral("-layout"), path, QStringLiteral("-")});
        const QString plain = extract({path, QStringLiteral("-")});
        const QString combined = laidOut + QLatin1Char('\n') + plain;
        // Poppler on arm64 extracts "fi" as U+FB01. NFKC splits that ligature before the letter check.
        const QString letters = combined.normalized(QString::NormalizationForm_KC).toLower().remove(QRegularExpression(QStringLiteral("[^a-z0-9]")));
        QVERIFY2(letters.contains(QStringLiteral("memoryconfiguration")), qPrintable(combined.left(2500)));
        QVERIFY2(combined.contains(QStringLiteral("206")) || letters.contains(QStringLiteral("206")), qPrintable(combined.left(2500)));
    }

    void loadedFirmwareUsesTheCatalogueAndUndo() {
        const QByteArray fw = vct::firmware16();
        VC_REQUIRE(fw, "B7B7");
        Machine machine;
        machine.loadFirmware(fw);
        machine.boot(nullptr);
        const auto known = machine.calc.knownFirmware();
        QVERIFY(known);
        QCOMPARE(known->code, QString("B7B7"));
        QCOMPARE(known->layout, Emulator::Memory16Layout::Layout176);
        QCOMPARE(machine.calc.firmwareMemory16Layout(), Emulator::Memory16Layout::Layout176);
        QVERIFY(machine.calc.firmwareIdentification().contains("B7B7"));
        QVERIFY(!machine.keepsMemoryOnModelSwitch());
        machine.calc.setMemory16Layout(Emulator::Memory16Layout::Layout206);
        QCOMPARE(machine.calc.memory16Layout(), Emulator::Memory16Layout::Layout206);
        QCOMPARE(machine.calc.firmwareMemory16Layout(), Emulator::Memory16Layout::Layout176);
        auto overridden = inspect(machine.calc, false, 8);
        QVERIFY(overridden && overridden->pool16);
        QCOMPARE(overridden->pool16->slotCount(), 206);
        if (overridden->wordSize == 16 && overridden->lines == 0) QCOMPARE(overridden->registerCount, 721);
        machine.calc.setMemory16Layout(Emulator::Memory16Layout::Automatic);
        auto restored = inspect(machine.calc, false, 1);
        QVERIFY(restored && restored->pool16 && restored->pool16->slotCount() == 176);

        QStringList errors;
        std::vector<PendingAction> asked;
        auto window = std::make_unique<MainWindow>();
        window->prompts.error = [&](const QString &m) { errors << m; };
        window->prompts.confirm = [&](const PendingAction &a) { asked.push_back(a); return 1; };
        window->prompts.notice = [](const QString &) {};
        window->prompts.confirmWrite = [](const PendingWrite &) { return false; };
        window->usbFast = true; window->usbSettleMs = 0;
        window->resize(980, 640);
        window->show();
        window->startFirmware(fw, "b7b7.bin", false);
        QVERIFY(waitFor([&] { return window->isRunning() && window->snapshot().variant == 160; }));
        bool sawInspector = false, sawUsb = false;
        for (QAction *a : window->menuBar()->actions()) if (a->menu() && a->text().contains("Model"))
            for (QAction *item : a->menu()->actions()) if (item->menu()) {
                if (item->text().contains("inspector layout")) sawInspector = true;
                if (item->text().contains("USB 16C")) sawUsb = true;
            }
        QVERIFY(sawInspector);
        QVERIFY(sawUsb);
        window->worker()->performAndWait([&](Machine &m) { m.tap(Emulator::keyCode(16, "4")); m.tap(Emulator::keyCode(16, "2")); m.tap(Emulator::keyCode(16, "STO")); m.tap(Emulator::keyCode(16, "0")); });
        QByteArray before;
        window->worker()->performAndWait([&](Machine &m) { before = m.memory(true); });
        window->worker()->performAndWait([&](Machine &m) { m.tap(Emulator::keyCode(16, "0")); m.tap(Emulator::keyCode(16, "STO")); m.tap(Emulator::keyCode(16, "0")); });
        window->offerMemory(before, "synthetic-before.16c");
        QVERIFY(waitFor([&] { return window->canUndoMemoryImport() || !errors.isEmpty(); }, 60000));
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
        QVERIFY(!asked.empty());
        QCOMPARE(int(asked.front().kind), int(PendingAction::LoadMemory));
        QVERIFY(window->memoryImportStatus().contains("Undo") || window->memoryImportStatus().contains("layout"));
        window->undoMemoryImport();
        QVERIFY(waitFor([&] { return !window->canUndoMemoryImport(); }));
        QVERIFY(window->memoryImportStatus().contains("restored"));
        QVERIFY(errors.isEmpty());
    }
};

QTEST_MAIN(TstLayout)
#include "tst_layout.moc"
