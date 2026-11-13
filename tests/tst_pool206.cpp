// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
//
// 0.7.7: the pool206 firmware profiles C9C9 (HP-15C CE 3-ROM) and ECEC (HP-12C Multi) next to EAEA.
// Firmware comes from VOYAGERCALC_FIRMWARE_DIR and the hardware-test memory files (with their
// verify_readback.py) from VOYAGERCALC_HWTEST_DIR; neither is in the repository, and the
// firmware-dependent cases skip without them.
#include "TestSupport.h"
#include "Emulator.h"
#include "Inspector.h"
#include "Machine.h"
#include "MainWindow.h"
#include <QElapsedTimer>
#include <QProcess>
#include <QSettings>
#include <QTemporaryDir>

using namespace vc;

namespace {
template <typename F> bool waitFor(F cond, int ms = 30000) {
    QElapsedTimer t; t.start();
    while (!cond()) { if (t.elapsed() > ms) return false; QTest::qWait(10); }
    return true;
}
struct Fw { const char *code; const char *glob; std::vector<int> variants; int boot; std::map<int, int> keys; };
const Fw kFirmware[] = {
    {"C9C9", "HP15c-3ROM-pool206*C9C9*.bin", {150, 152, 160}, 150, {{150, 1}, {152, 3}, {160, 5}}},
    {"ECEC", "HP12c-Multi-pool206*ECEC*.bin", {120, 152, 160}, 120, {{120, 1}, {152, 3}, {160, 5}}},
    {"EAEA", "HP16c-enhanced-152-pool206-EAEA*.bin", {152, 160}, 160, {{152, 3}, {160, 1}}},
};
const Fw *fwFor(const QString &code) { for (auto &f : kFirmware) if (code == f.code) return &f; return nullptr; }
QString hwtestDir() { return qEnvironmentVariable("VOYAGERCALC_HWTEST_DIR"); }
QByteArray readFile(const QString &p) { QFile f(p); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); }
int variantOf(const QString &file) {
    if (file.endsWith(".16c")) return 160;
    if (file.endsWith(".12c")) return 120;
    return file.startsWith("15c150") ? 150 : 152;
}
// verify_readback.py TEST-NAME FILE → (passed, output)
std::pair<bool, QString> verify(const QString &name, const QByteArray &image, const QString &tmpDir) {
    const QString path = tmpDir + "/" + name + "-readback.bin";
    QFile f(path); if (!f.open(QIODevice::WriteOnly)) return {false, "cannot write " + path};
    f.write(image); f.close();
    QProcess p;
    p.start("python3", {hwtestDir() + "/verify_readback.py", name, path});
    if (!p.waitForFinished(60000)) return {false, "verify_readback.py did not finish"};
    const QString out = QString::fromUtf8(p.readAllStandardOutput() + p.readAllStandardError()).trimmed();
    return {p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0 && out.startsWith("PASS"), out};
}
}

class TstPool206 : public QObject {
    Q_OBJECT
    QTemporaryDir data_, out_, tmp_;
private slots:
    void initTestCase() {
        qputenv("VOYAGERCALC_DATA_DIR", data_.path().toUtf8());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, out_.path());
    }

    void catalogueHasPool206Wrappers() {
        std::map<QString, Emulator::KnownFirmware> rows;
        for (auto &k : Emulator::knownFirmwareCatalogue()) rows[k.code] = k;
        for (const Fw &f : kFirmware) {
            QVERIFY2(rows.count(f.code), f.code);
            const auto &k = rows[f.code];
            QCOMPARE(k.layout, Emulator::Memory16Layout::Layout206);
            QVERIFY(k.perModeSlots);
            QCOMPARE(k.switchMenu, 4);
            QCOMPARE(k.variants, f.variants);
            for (auto [v, key] : f.keys) QCOMPARE(k.keyFor(v), key);
            QVERIFY(!k.displayName.contains("TEST"));
        }
        QVERIFY(rows["C9C9"].keyFor(120) < 0 && rows["ECEC"].keyFor(150) < 0);
        QVERIFY(rows["C9C9"].switchMethod.contains("B/C = 15") && rows["C9C9"].switchMethod.contains("D/E = 15.2") && rows["C9C9"].switchMethod.contains("CHS/7 = 16"));
        QVERIFY(rows["ECEC"].switchMethod.contains("i/PV = 12") && rows["ECEC"].switchMethod.contains("PMT/FV = 15.2"));
    }

    void profileModelSwitchAndImport_data() {
        QTest::addColumn<QString>("code");
        for (const Fw &f : kFirmware) QTest::newRow(f.code) << QString(f.code);
    }
    // Machine level: the profile, the Model-menu keys for every mode, then every hwtest file of that
    // firmware through the 16C import preparation (unchanged, 206) and a load + readback verify.
    void profileModelSwitchAndImport() {
        QFETCH(QString, code);
        const Fw &f = *fwFor(code);
        const QByteArray fw = vct::firmwareMatching(f.glob);
        if (fw.isEmpty()) QSKIP("firmware not in VOYAGERCALC_FIRMWARE_DIR");
        if (hwtestDir().isEmpty()) QSKIP("set VOYAGERCALC_HWTEST_DIR");
        Machine m;
        m.loadFirmware(fw);
        m.boot(nullptr);
        const auto known = m.calc.knownFirmware();
        QVERIFY(known);
        QCOMPARE(known->code, code);
        QVERIFY(!m.calc.firmwareIdentification().contains("Unknown"));
        QCOMPARE(m.calc.firmwareMemory16Layout(), Emulator::Memory16Layout::Layout206);
        QVERIFY(m.keepsMemoryOnModelSwitch());
        QCOMPARE(m.images(), f.variants);
        QCOMPARE(m.variant(), f.boot);
        // every mode, both directions, with the catalogue keys
        for (int v : f.variants) if (v != f.boot) { m.switchModel(v); QCOMPARE(m.variant(), v); m.switchModel(f.boot); QCOMPARE(m.variant(), f.boot); }
        m.switchModel(160);
        QCOMPARE(m.calc.memory16Layout(), Emulator::Memory16Layout::Layout206);
        auto snap = inspect(m.calc, false, 8);
        QVERIFY(snap && snap->pool16);
        QCOMPARE(snap->pool16->slotCount(), 206);

        const QDir dir(hwtestDir() + "/" + code);
        const QStringList files = dir.entryList({"*.16c", "*.15c", "*.12c"}, QDir::Files, QDir::Name);
        QVERIFY2(!files.isEmpty(), qPrintable(dir.path()));
        int n16 = 0;
        for (const QString &file : files) {
            const QByteArray data = readFile(dir.filePath(file));
            QCOMPARE(data.size(), Emulator::kMemorySize);
            const int v = variantOf(file);
            if (m.variant() != v) m.switchModel(v);
            QCOMPARE(m.variant(), v);
            QByteArray img = data;
            if (v == 160) {
                ++n16;
                const auto prepared = Emulator::prepareMemory16(data, m.calc.firmwareMemory16Layout());   // throws on a conversion error
                QVERIFY2(!prepared.converted, qPrintable(file + ": " + prepared.note));
                QCOMPARE(prepared.data, data);
                QCOMPARE(prepared.source, Emulator::Memory16Layout::Layout206);
                img = prepared.data;
            }
            QVERIFY2(Machine::checkMemory(img, fw, v).fits(), qPrintable(file));
            m.putMemory(img);
            const QByteArray back = m.memory(true);
            const auto [ok, out] = verify(QFileInfo(file).completeBaseName(), back, tmp_.path());
            QVERIFY2(ok, qPrintable(code + "/" + file + ": " + out));
            qInfo("%s/%s: %s", qPrintable(code), qPrintable(file), qPrintable(out.left(90)));
        }
        QCOMPARE(n16, 4);
    }

    void mainWindowImportPath_data() { profileModelSwitchAndImport_data(); }
    // The app's own Open path (MainWindow::offerMemory → confirm → load), with the Model menu switch.
    void mainWindowImportPath() {
        QFETCH(QString, code);
        const Fw &f = *fwFor(code);
        const QByteArray fw = vct::firmwareMatching(f.glob);
        if (fw.isEmpty()) QSKIP("firmware not in VOYAGERCALC_FIRMWARE_DIR");
        if (hwtestDir().isEmpty()) QSKIP("set VOYAGERCALC_HWTEST_DIR");
        QStringList errors;
        std::vector<PendingAction> asked;
        auto window = std::make_unique<MainWindow>();
        window->prompts.error = [&](const QString &msg) { errors << msg; };
        window->prompts.confirm = [&](const PendingAction &a) { asked.push_back(a); return 1; };
        window->prompts.notice = [](const QString &) {};
        window->prompts.confirmWrite = [](const PendingWrite &) { return false; };
        window->resize(980, 640);
        window->show();
        window->startFirmware(fw, code + ".bin", false);
        QVERIFY(waitFor([&] { return window->isRunning() && window->snapshot().variant == f.boot; }));
        if (f.boot != 160) {
            window->chooseModel(160);                                    // Model menu → 16C
            QVERIFY(waitFor([&] { return window->snapshot().variant == 160; }, 60000));
        }
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
        const QDir dir(hwtestDir() + "/" + code);
        for (const QString &file : dir.entryList({"*.16c"}, QDir::Files, QDir::Name)) {
            const QByteArray data = readFile(dir.filePath(file));
            const size_t before = asked.size();
            window->offerMemory(data, file);
            QVERIFY(waitFor([&] { return asked.size() > before || !errors.isEmpty(); }, 60000));
            QVERIFY2(errors.isEmpty(), qPrintable(file + ": " + errors.join('\n')));
            QCOMPARE(int(asked.back().kind), int(PendingAction::LoadMemory));
            QCOMPARE(asked.back().data, data);                           // loaded unchanged, as 206
            QVERIFY2(!asked.back().source.contains("converted"), qPrintable(asked.back().source));
            QVERIFY2(window->memoryImportStatus().contains("206"), qPrintable(window->memoryImportStatus()));
            QByteArray back;
            window->worker()->performAndWait([&](Machine &m) { back = m.memory(true); });
            const auto [ok, out] = verify(QFileInfo(file).completeBaseName(), back, tmp_.path());
            QVERIFY2(ok, qPrintable(code + "/" + file + ": " + out));
        }
        // and back to the boot mode with the Model menu
        if (f.boot != 160) {
            window->chooseModel(f.boot);
            QVERIFY(waitFor([&] { return window->snapshot().variant == f.boot; }, 60000));
        }
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
    }

    // 0.7.7 (José, 13 Nov): only stock and 206 slots. A retired intermediate build (B7B7) is never read as another
    // layout: memory loading is refused, and a layout choice does not make it readable.
    void retiredBuildIsUnsupportedNotAnotherLayout() {
        const QByteArray fw = vct::firmwareRetired16();
        VC_REQUIRE(fw, "B7B7");
        if (hwtestDir().isEmpty()) QSKIP("set VOYAGERCALC_HWTEST_DIR");
        const QByteArray file = readFile(hwtestDir() + "/C9C9/16c-mixed350-ws64.16c");
        QCOMPARE(file.size(), Emulator::kMemorySize);
        QStringList errors;
        std::vector<PendingAction> asked;
        auto window = std::make_unique<MainWindow>();
        window->prompts.error = [&](const QString &msg) { errors << msg; };
        window->prompts.confirm = [&](const PendingAction &a) { asked.push_back(a); return 0; };   // ask only, do not load
        window->prompts.notice = [](const QString &) {};
        window->prompts.confirmWrite = [](const PendingWrite &) { return false; };
        window->show();
        window->startFirmware(fw, "b7b7.bin", false);
        QVERIFY(waitFor([&] { return window->isRunning() && window->snapshot().variant == 160; }));
        QCOMPARE(window->memory16Target(), int(VC_MEM16_UNSUPPORTED));
        window->offerMemory(file, "mixed350.16c");
        QVERIFY(waitFor([&] { return !errors.isEmpty() || !asked.empty(); }, 60000));
        QVERIFY2(asked.empty() && !errors.isEmpty() && errors.front().contains("neither stock nor 206"), qPrintable(errors.join('\n')));
        QVERIFY(!errors.front().contains("176"));
        errors.clear();
        window->chooseInspectorLayout(VC_MEM16_LAYOUT206);
        QCOMPARE(window->memory16Target(), int(VC_MEM16_UNSUPPORTED));
        window->offerMemory(file, "mixed350.16c");
        QVERIFY(waitFor([&] { return !errors.isEmpty() || !asked.empty(); }, 60000));
        QVERIFY(asked.empty() && !errors.isEmpty());
        window->chooseInspectorLayout(VC_MEM16_NONE);
    }

    // An unknown extended ROM (a patched EAEA copy, so no catalogue row): the 206 floor constant is read from the
    // ROM, so it is 206 with no question. A patched retired build is undetermined: nothing is guessed, the user
    // chooses stock or 206, and that choice is the import target.
    void unknownExtendedFirmwareIsDetectedOrAsks() {
        auto patched = [](QByteArray fw) {
            const char orig = fw[0x1BFEE];
            for (int delta = 1; delta < 256; ++delta) {
                fw[0x1BFEE] = char(orig + delta);
                Emulator e; e.loadFirmware(fw);
                if (!e.knownFirmware()) return fw;
            }
            return QByteArray();
        };
        const QByteArray ea = vct::firmware206();
        const QByteArray e9 = vct::firmwareEnhanced16();
        if (!ea.isEmpty()) {
            Emulator e; e.loadFirmware(patched(ea));
            QVERIFY(!e.knownFirmware());
            QCOMPARE(e.firmwareMemory16Layout(), Emulator::Memory16Layout::Layout206);
        }
        VC_REQUIRE(e9, "E9E9");
        if (hwtestDir().isEmpty()) QSKIP("set VOYAGERCALC_HWTEST_DIR");
        const QByteArray unknown = patched(e9);
        QVERIFY(!unknown.isEmpty());
        { Emulator e; e.loadFirmware(unknown); QCOMPARE(e.firmwareMemory16Layout(), Emulator::Memory16Layout::Undetermined); }
        const QByteArray file = readFile(hwtestDir() + "/EAEA/16c-data721-ws16.16c");
        QStringList errors;
        std::vector<PendingAction> asked;
        auto window = std::make_unique<MainWindow>();
        window->prompts.error = [&](const QString &msg) { errors << msg; };
        window->prompts.confirm = [&](const PendingAction &a) { asked.push_back(a); return 0; };
        window->prompts.notice = [](const QString &) {};
        window->prompts.confirmWrite = [](const PendingWrite &) { return false; };
        window->show();
        window->startFirmware(unknown, "unknown-ext.bin", false);
        QVERIFY(waitFor([&] { return window->isRunning() && window->snapshot().variant == 160; }));
        QCOMPARE(window->memory16Target(), int(VC_MEM16_UNDETERMINED));
        window->offerMemory(file, "data721.16c");
        QVERIFY(waitFor([&] { return !errors.isEmpty() || !asked.empty(); }, 60000));
        QVERIFY2(asked.empty() && !errors.isEmpty() && errors.front().contains("not guessed"), qPrintable(errors.join('\n')));
        errors.clear();
        window->chooseInspectorLayout(VC_MEM16_LAYOUT206);
        QCOMPARE(window->memory16Target(), int(VC_MEM16_LAYOUT206));
        window->offerMemory(file, "data721.16c");
        QVERIFY(waitFor([&] { return !errors.isEmpty() || !asked.empty(); }, 60000));
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
        QCOMPARE(asked.back().data, file);
        QVERIFY2(window->memoryImportStatus().contains("as chosen"), qPrintable(window->memoryImportStatus()));
    }

    // Stock 16C firmware and a stock memory file: still loads as stock (29 slots), unchanged.
    void stockFirmwareStillLoadsStockFiles() {
        const QByteArray fw = vct::firmware16();
        VC_REQUIRE(fw, "stock 16C");
        const QByteArray file = vct::fixture("pi-1010-ready.16c");
        QCOMPARE(file.size(), Emulator::kMemorySize);
        { Emulator e; e.loadFirmware(fw); QCOMPARE(e.firmwareMemory16Layout(), Emulator::Memory16Layout::Standard); }
        const auto prepared = Emulator::prepareMemory16(file, Emulator::Memory16Layout::Standard);
        QVERIFY(!prepared.converted);
        QCOMPARE(prepared.data, file);
        QStringList errors;
        std::vector<PendingAction> asked;
        auto window = std::make_unique<MainWindow>();
        window->prompts.error = [&](const QString &msg) { errors << msg; };
        window->prompts.confirm = [&](const PendingAction &a) { asked.push_back(a); return 1; };
        window->prompts.notice = [](const QString &) {};
        window->prompts.confirmWrite = [](const PendingWrite &) { return false; };
        window->show();
        window->startFirmware(fw, "stock16c.bin", false);
        QVERIFY(waitFor([&] { return window->isRunning() && window->snapshot().variant == 160; }));
        QCOMPARE(window->memory16Target(), int(VC_MEM16_STD));
        window->offerMemory(file, "pi-1010-ready.16c");
        QVERIFY(waitFor([&] { return !errors.isEmpty() || !asked.empty(); }, 60000));
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
        QCOMPARE(int(asked.back().kind), int(PendingAction::LoadMemory));
        QCOMPARE(asked.back().data, file);
        QVERIFY(waitFor([&] { return window->snapshot().variant == 160 && window->canUndoMemoryImport(); }, 60000));
        QByteArray back;
        window->worker()->performAndWait([&](Machine &m) { back = m.memory(true); });
        QCOMPARE(back.mid(0x6F0, 0x7D8 - 0x6F0), file.mid(0x6F0, 0x7D8 - 0x6F0));     // stock pool as loaded
        auto snap = std::make_shared<std::optional<InspectorSnapshot>>();
        window->worker()->performAndWait([&](Machine &m) { *snap = inspect(m.calc, false, 4); });
        QVERIFY(*snap && (*snap)->pool16);
        QCOMPARE((*snap)->pool16->slotCount(), 29);
    }
};

QTEST_MAIN(TstPool206)
#include "tst_pool206.moc"
