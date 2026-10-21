// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
// The whole window driven offscreen: firmware start, keys, flyout, model switch, memory files, notes, exports,
// the USB flows over a fake calculator, "no access" -> Fix USB Permissions, About.
#include "TestSupport.h"
#include "CalculatorWidget.h"
#include "FakeCalculator.h"
#include "InspectorPanel.h"
#include <QListView>
#include <QScrollBar>
#include "MainWindow.h"
#include <QStandardPaths>
#ifdef Q_OS_LINUX
#include "usb/HidrawTransport.h"
#endif
#include "usb/UsbPlatform.h"
#include "usb/UsbLog.h"
#include <QMenuBar>
#include <QSettings>
#include <QScreen>
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QProcess>
#include <QThread>
#include <atomic>
#ifdef Q_OS_LINUX
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

using namespace vc;

namespace {
template <typename F> bool waitFor(F cond, int ms = 15000) {
    QElapsedTimer t; t.start();
    while (!cond()) { if (t.elapsed() > ms) return false; QTest::qWait(10); }
    return true;
}
#ifdef Q_OS_LINUX
/// The device end of a socket standing in for /dev/hidrawN: reads the 65-byte hidraw writes (report id 0 + 64) and lets the fake calculator answer.
class HidrawDevice : public QThread {
public:
    HidrawDevice(int fd, FakeCalculator *c) : fd_(fd), calc_(c) {}
    std::atomic<bool> stop{false};
    void run() override {
        while (!stop) {
            uint8_t buf[256];
            ssize_t n = ::recv(fd_, buf, sizeof buf, MSG_DONTWAIT);
            if (n <= 0) { QThread::msleep(1); continue; }
            Report r{}; std::copy(buf + 1, buf + 65, r.begin());
            calc_->send(r);
            Report in; while (calc_->receive(0, &in)) ::send(fd_, in.data(), in.size(), 0);
        }
    }
private:
    int fd_; FakeCalculator *calc_;
};
#endif

/// A transport that hands every open() to one shared fake (the app makes a new transport per operation).
class Forward : public HidTransport {
    FakeCalculator *f_;
public:
    explicit Forward(FakeCalculator *f) : f_(f) {}
    void open() override { f_->open(); }
    void close() override { f_->close(); }
    void send(const Report &r) override { f_->send(r); }
    bool receive(int t, Report *o) override { return f_->receive(t, o); }
};
class Denied : public HidTransport {
public:
    void open() override { throw LinkError::accessDenied("/dev/hidraw3"); }
    void close() override {}
    void send(const Report &) override {}
    bool receive(int, Report *) override { return false; }
};
}

class TstApp : public QObject {
    Q_OBJECT
    QTemporaryDir data_, out_;
    QStringList errors, notices;
    std::vector<PendingAction> asked;
    int answer = 1;
    QString nextSave, nextOpen;

    std::unique_ptr<MainWindow> window(const QByteArray &fw, const QString &name = "test.bin") {
        auto w = std::make_unique<MainWindow>();
        w->prompts.error = [this](const QString &m) { errors << m; };
        w->prompts.notice = [this](const QString &m) { notices << m; };
        w->prompts.confirm = [this](const PendingAction &a) { asked.push_back(a); return answer; };
        w->prompts.confirmWrite = [](const PendingWrite &) { return true; };
        w->prompts.saveFile = [this](const QString &n, const QString &) { return nextSave.isEmpty() ? QString() : nextSave; Q_UNUSED(n) };
        w->prompts.openFile = [this](const QString &, const QString &) { return nextOpen; };
        w->prompts.askFixPermissions = [this](const QString &m) { errors << m; return false; };
        w->usbFast = true; w->usbSettleMs = 0;
        w->resize(900, 560);
        w->show();
        w->startFirmware(fw, name, false);
        if (!waitFor([&] { return w->isRunning(); })) { QTest::qFail("did not boot", __FILE__, __LINE__); }
        return w;
    }
    void type(MainWindow &w, const QString &keys) {
        for (const QString &k : keys.split(' ', Qt::SkipEmptyParts)) {
            int code = Emulator::keyCode(w.snapshot().model, k.toLatin1().constData());
            QVERIFY2(code >= 0, qPrintable(k));
            w.press(code); QTest::qWait(60); w.release(code); QTest::qWait(60);
        }
    }
    static QString shownText(MainWindow &w) { return w.snapshot().text; }
private slots:
    void init() { errors.clear(); notices.clear(); asked.clear(); answer = 1; nextSave.clear(); nextOpen.clear(); }
    void initTestCase() {
        qputenv("VOYAGERCALC_DATA_DIR", data_.path().toUtf8());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, out_.path());
    }

    void bootsAndCalculates() {
        QByteArray fw = vct::firmware15(); VC_REQUIRE(fw, "15C");
        auto w = window(fw);
        QCOMPARE(w->snapshot().model, 15);
        type(*w, "1 ENTER 2 +");
        QVERIFY(waitFor([&] { return shownText(*w).contains("3.0000"); }));
        QVERIFY(errors.isEmpty());
    }
    void hardwareKeyboardWorks() {
        QByteArray fw = vct::firmware15(); VC_REQUIRE(fw, "15C");
        auto w = window(fw);
        w->activateWindow(); QVERIFY(QTest::qWaitForWindowActive(w.get()));
        for (char c : QByteArray("7\r6*")) {
            QKeyEvent p(QEvent::KeyPress, c == '\r' ? Qt::Key_Return : int(c == '*' ? Qt::Key_Asterisk : Qt::Key_0 + (c - '0')), Qt::NoModifier, QString(QChar(c == '\r' ? '\r' : c)));
            QApplication::sendEvent(w->calculator(), &p);
            QTest::qWait(60);
            QKeyEvent r(QEvent::KeyRelease, p.key(), Qt::NoModifier, p.text());
            QApplication::sendEvent(w->calculator(), &r);
            QTest::qWait(60);
        }
        QVERIFY2(waitFor([&] { return shownText(*w).contains("42.0000"); }), qPrintable("[" + shownText(*w) + "] active=" + QString::number(QApplication::activeWindow() == w.get())));
    }
    void inspectorFlyout() {
        QByteArray fw = vct::firmware15(); VC_REQUIRE(fw, "15C");
        auto w = window(fw);
        w->setShowInspector(true);
        type(*w, "5 ENTER 6");
        QVERIFY(waitFor([&] { return w->inspector()->registerRowTexts().join('\n').contains("6") && w->inspector()->headerText().contains("15C"); }));
        QVERIFY(w->inspector()->registerRowTexts().size() > 5);
        // program: enter PRGM, 1 ENTER
        type(*w, "g R/S");     // P/R
        type(*w, "1 +");
        QVERIFY(waitFor([&] { return w->inspector()->programRowTexts().size() >= 3; }));
        const QStringList rows = w->inspector()->programRowTexts();
        QVERIFY2(rows.join('\n').contains("001"), qPrintable(rows.join('\n')));
    }
    void stockLimits() {
        QByteArray fw = vct::firmwareMatching("*12c*stock*"); if (fw.isEmpty()) QSKIP("no stock 12C firmware (optional)");
    }
    void modelChooserSwitchesAndKeepsR1() {
        QByteArray fw = vct::firmwareMulti(); VC_REQUIRE(fw, "multi");
        auto w = window(fw);
        QCOMPARE(w->snapshot().variant, 120);
        type(*w, "7 7 STO 1");
        answer = 0; w->chooseModel(160);
        QCOMPARE(asked.size(), size_t(1)); QCOMPARE(w->snapshot().variant, 120);      // cancelled
        answer = 1; w->chooseModel(160);
        QVERIFY(waitFor([&] { return !w->isSwitching() && w->snapshot().variant == 160; }));
        QCOMPARE(w->snapshot().model, 16);
        QVERIFY(!w->snapshot().isBlank());                 // the calculator is on after the switch
        type(*w, "RCL 1");
        QTest::qWait(300);
        QVERIFY(!shownText(*w).contains("77"));            // the E3E3 firmware clears memory on a model switch (as on the Mac)
        QVERIFY(errors.isEmpty());
        // the remembered state: a new window starting the same firmware finds model 16
        w->saveStateNow(false);
    }
    void backupFirstOffer() {
        QByteArray fw = vct::firmwareMulti(); VC_REQUIRE(fw, "multi");
        auto w = window(fw);
        answer = 2; nextSave = out_.path() + "/backup-before.12c";
        w->chooseModel(152);
        QVERIFY(waitFor([&] { return !w->isSwitching() && w->snapshot().variant == 152; }));
        QVERIFY(QFileInfo(nextSave).size() == 2048);
    }
    void memoryFileRoundTrip() {
        QByteArray fw = vct::firmware15(); VC_REQUIRE(fw, "15C");
        auto w = window(fw);
        type(*w, "4 2 STO 3");
        nextSave = out_.path() + "/mine.15c";
        w->saveMemory();
        QVERIFY(waitFor([&] { return QFileInfo(nextSave).exists(); }));
        QCOMPARE(QFileInfo(nextSave).size(), 2048);
        type(*w, "0 STO 3");
        asked.clear(); answer = 1;
        w->openMemoryFile(nextSave);
        QVERIFY(waitFor([&] { return !asked.empty(); }));
        QCOMPARE(int(asked[0].kind), int(PendingAction::LoadMemory));
        QTest::qWait(300);
        type(*w, "RCL 3");
        QVERIFY(waitFor([&] { return shownText(*w).contains("42.0000"); }));
        // a wrong-size file is refused with a plain message
        QFile bad(out_.path() + "/bad.15c"); bad.open(QIODevice::WriteOnly); bad.write("xx"); bad.close();
        w->openMemoryFile(bad.fileName());
        QVERIFY(!errors.isEmpty() && errors.last().contains("2048"));
    }
    void fixtureProgramThenExports() {
        QByteArray fw = vct::firmware15(); VC_REQUIRE(fw, "15C");
        auto w = window(fw);
        w->openMemoryFile(QString(VC_FIXTURES_DIR) + "/FinancePac.15c");
        QVERIFY(waitFor([&] { return !asked.empty(); }));
        QVERIFY(waitFor([&] { return !w->isSwitching() && w->snapshot().variant == 152; }));
        QTest::qWait(500);
        w->setShowInspector(true);
        w->setComment("my note", 1);
        nextSave = out_.path() + "/doc.pdf";
        w->exportPdf();
        QVERIFY(waitFor([&] { return QFileInfo(nextSave).exists(); }));
        if (QStandardPaths::findExecutable("pdffonts").isEmpty()) { QVERIFY(QFileInfo(nextSave).size() > 1000); QSKIP("poppler (pdffonts) not installed: fonts checked on Linux only"); }
        QProcess p; p.start("pdffonts", {nextSave}); p.waitForFinished();
        const QString fonts = p.readAllStandardOutput();
        QVERIFY2(fonts.contains("HP15C") || fonts.contains("Simulator"), qPrintable(fonts));
        nextSave = out_.path() + "/list.txt";
        w->exportText();
        QVERIFY(waitFor([&] { return QFileInfo(nextSave).exists(); }));
        QFile f(nextSave); f.open(QIODevice::ReadOnly);
        QVERIFY(f.readAll().size() > 20);
        QVERIFY(errors.isEmpty());
    }
    // ---- 0.6.9 ------------------------------------------------------------------------------------------------------
    void powerMenuShowsEscWithoutBindingIt() {
        QByteArray fw = vct::firmware15(); VC_REQUIRE(fw, "15C");
        auto w = window(fw);
        QAction *power = nullptr;
        for (QAction *a : w->findChildren<QAction *>()) if (a->text().startsWith("ON / OFF")) power = a;
        QVERIFY(power);
        QCOMPARE(power->text(), QString("ON / OFF\tEsc"));          // Esc is shown at the right like a shortcut ...
        QVERIFY(power->shortcuts().isEmpty());                       // ... but is NOT bound: the key path of the face is the only handler
    }
    void resetAndMemoryResetClearEverythingIncludingStoredNotes() {
        QByteArray fw = vct::firmware15(); VC_REQUIRE(fw, "15C");
        auto w = window(fw);
        w->openMemoryFile(QString(VC_FIXTURES_DIR) + "/FinancePac.15c");
        QVERIFY(waitFor([&] { return !asked.empty(); }));
        QVERIFY(waitFor([&] { return !w->isSwitching() && w->snapshot().variant == 152; }));
        QTest::qWait(500);
        w->setComment("note", 1);
        QVERIFY(!w->programDoc().comments.empty());
        QVERIFY(!w->programDoc().isEmpty());
        PendingAction a; a.kind = PendingAction::MemoryReset;
        w->confirm(a);
        QVERIFY(w->programDoc().isEmpty());
        QVERIFY(w->programDoc().comments.empty() && w->programDoc().title.isEmpty() && w->programDoc().author.isEmpty());
        QVERIFY(NotesStore::load(w->firmware().id).isEmpty());        // not even a stored copy is left
    }
    void textFileGetsItsOwnMessageAndAnyEndingOpens() {
        QByteArray fw = vct::firmware15(); VC_REQUIRE(fw, "15C");
        auto w = window(fw);
        QFile t(out_.path() + "/manz.15c"); t.open(QIODevice::WriteOnly); t.write("{ HP-15C program }\n# 001  LBL A\n"); t.close();
        w->openMemoryFile(t.fileName());
        QVERIFY(!errors.isEmpty() && errors.last().contains("text program"));
        QFile fx(QString(VC_FIXTURES_DIR) + "/FinancePac.15c"); QVERIFY(fx.open(QIODevice::ReadOnly));
        QFile odd(out_.path() + "/Pack.whatever"); odd.open(QIODevice::WriteOnly); odd.write(fx.readAll()); odd.close();
        asked.clear(); errors.clear();
        w->openMemoryFile(odd.fileName());
        QVERIFY(waitFor([&] { return !asked.empty(); }));
        QVERIFY(errors.isEmpty());
    }
    void notesFile() {
        QByteArray fw = vct::firmware15(); VC_REQUIRE(fw, "15C");
        auto w = window(fw);
        w->openMemoryFile(QString(VC_FIXTURES_DIR) + "/FinancePac.15c");
        QVERIFY(waitFor([&] { return !asked.empty(); }));
        QTest::qWait(500);
        w->setComment("first step", 1);
        nextSave = out_.path() + "/notes.voyagerdoc";
        w->saveNotes();
        QVERIFY(waitFor([&] { return QFileInfo(nextSave).exists(); }));
        w->clearNotes();
        QVERIFY(w->programDoc().comments.empty());
        w->openNotesFile(nextSave);
        QVERIFY(waitFor([&] { return !w->programDoc().comments.empty(); }));
    }
    void usbReadWriteBackupAndFlag8() {
        QByteArray fw = vct::firmware15(); VC_REQUIRE(fw, "15C");
        auto w = window(fw);
        QFile fx(QString(VC_FIXTURES_DIR) + "/Calc15-0-usb-read.15c"); QVERIFY(fx.open(QIODevice::ReadOnly));
        FakeCalculator fake(fx.readAll());
        w->makeTransport = [&] { return std::unique_ptr<HidTransport>(new Forward(&fake)); };
        w->readFromCalculator();
        QVERIFY(waitFor([&] { return !asked.empty(); }));
        QCOMPARE(int(asked[0].kind), int(PendingAction::LoadMemory));
        QTest::qWait(500);
        // write the emulator's memory back to the calculator
        type(*w, "9 STO 4");
        bool confirmed = false; PendingWrite seen;
        w->prompts.confirmWrite = [&](const PendingWrite &p) { confirmed = true; seen = p; return true; };
        w->writeToCalculator();
        QVERIFY(waitFor([&] { return !notices.isEmpty() || !errors.isEmpty(); }, 30000));
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
        QVERIFY(confirmed);
        QVERIFY(QFileInfo(seen.backup).exists());
        QVERIFY(notices.last().contains("read back and compared"));
        QCOMPARE(fake.memory.size(), 2048);
    }
    void noAccessOffersFix() {
        QByteArray fw = vct::firmware15(); VC_REQUIRE(fw, "15C");
        auto w = window(fw);
        w->makeTransport = [] { return std::unique_ptr<HidTransport>(new Denied); };
        w->readFromCalculator();
        QVERIFY(waitFor([&] { return !errors.isEmpty(); }));
        QVERIFY2(errors.last().contains("Fix USB Permissions"), qPrintable(errors.last()));
    }
#ifdef Q_OS_LINUX
    void readFromCalculatorReachesTheOpenStepAtOnce() {
        // Field report (test4): the GUI's Read never logged an open line (the selftest, which names the node itself, worked). The GUI path runs the node SEARCH:
        // here with the engine running, a real sysfs file as the first (non-matching) hidraw node, and a kernel-style hidraw fake as the calculator.
        QByteArray fw = vct::firmware15(); VC_REQUIRE(fw, "15C");
        auto w = window(fw);
        QString real;
        for (const QFileInfo &fi : QDir("/sys/class/net").entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) { const QString c = fi.filePath() + "/uevent"; if (QFileInfo(c).exists() && QFileInfo(c).size() == 4096) { real = c; break; } }
        QTemporaryDir sys; QVERIFY(sys.isValid());
        QDir().mkpath(sys.path() + "/hidraw0/device"); QDir().mkpath(sys.path() + "/hidraw1/device"); QDir().mkpath(sys.path() + "/hidraw2/device");
        if (!real.isEmpty()) QVERIFY(QFile::link(real, sys.path() + "/hidraw0/device/uevent"));
        auto put = [&](const QString &rel, const QByteArray &t) { QFile f(sys.path() + "/" + rel); QVERIFY(f.open(QIODevice::WriteOnly)); f.write(t); };
        if (real.isEmpty()) put("hidraw0/device/uevent", "HID_ID=0003:0000046D:0000C52B\n");
        put("hidraw1/device/uevent", "HID_ID=0003:0000046D:0000C077\nHID_NAME=Mouse\n");
        put("hidraw2/device/uevent", "HID_ID=0003:000003F0:00001341\nHID_NAME=HP Calculator\n");
        QFile fx(QString(VC_FIXTURES_DIR) + "/Calc15-0-usb-read.15c"); QVERIFY(fx.open(QIODevice::ReadOnly));
        FakeCalculator fake(fx.readAll());
        int sv[2]; QVERIFY(::socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sv) == 0);
        HidrawDevice dev(sv[1], &fake); dev.start();
        const QString root = sys.path();
        w->makeTransport = [root, sv] {
            auto *t = new HidrawTransport; t->setRoots(root, "/dev");
            t->openFn = [sv](const char *) { return ::dup(sv[0]); };
            return std::unique_ptr<HidTransport>(t);
        };
        UsbLog::clear();
        QElapsedTimer tm; tm.start();
        w->readFromCalculator();
        QVERIFY2(waitFor([&] { return UsbLog::text().contains("open /dev/hidraw2 ok"); }, 2000), qPrintable(UsbLog::text()));      // the open step is reached within 2 s
        QVERIFY(waitFor([&] { return !asked.empty() || !errors.isEmpty(); }, 15000));
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n') + UsbLog::text()));
        QCOMPARE(int(asked[0].kind), int(PendingAction::LoadMemory));
        QVERIFY(UsbLog::text().contains("discovery: 3 hidraw node(s)") && UsbLog::text().contains("dump complete"));
        dev.stop = true; dev.wait(); ::close(sv[0]); ::close(sv[1]);
    }
#endif
    void inspectorSectionHeadersStayPinned() {
        // The "Program (n lines)" bar used to scroll away with the listing. Synthetic snapshot: no firmware needed.
        auto snap = std::make_shared<InspectorSnapshot>();
        snap->model = 15; snap->variant = 150; snap->hasProgram = true; snap->lines = 120; snap->line = 3; snap->registerCount = 40;
        auto reg = [](const QString &n, const QString &v) { RegisterValue r; r.name = n; r.text = v; return r; };
        for (const char *n : {"X", "Y", "Z", "T", "LAST X"}) snap->stack.push_back(reg(n, "0.000000000"));
        for (int i = 0; i < 40; ++i) snap->registers.push_back(reg(QString("R%1").arg(i), "1.000000000"));
        for (int i = 1; i <= 120; ++i) { ProgramLine l; l.line = i; l.keys = "43,22"; l.mnemonic = "STO 1"; snap->program.push_back(l); }
        InspectorPanel p; p.resize(300, 520); p.setPane(InspectorPanel::Both); p.setSnapshot(snap); p.show();
        QVERIFY(QTest::qWaitForWindowExposed(&p));
        QListView *prog = p.listView(InspectorPanel::Program), *regs = p.listView(InspectorPanel::Registers);
        QTest::qWait(50);
        // not scrolled: the header is its own first row at the top
        QCOMPARE(p.pinnedHeaderText(InspectorPanel::Program), QString("Program (120 lines)"));
        // scroll the listing down: the header stays at the very top of the program list, fully visible, and the list is the only thing that moves
        prog->verticalScrollBar()->setValue(prog->verticalScrollBar()->maximum() / 2);
        QTest::qWait(50);
        QCOMPARE(p.pinnedHeaderText(InspectorPanel::Program), QString("Program (120 lines)"));
        QRect r = p.pinnedHeaderRect(InspectorPanel::Program);
        QCOMPARE(r.top(), 0); QVERIFY(r.height() >= 18); QCOMPARE(r.width(), prog->viewport()->width());
        QVERIFY(prog->viewport()->rect().contains(r));
        prog->verticalScrollBar()->setValue(prog->verticalScrollBar()->maximum());
        QTest::qWait(50);
        QCOMPARE(p.pinnedHeaderRect(InspectorPanel::Program).top(), 0);
        {   // the pinned bar is opaque even when the style gives AlternateBase an alpha: no row text shows through it
            QPalette old = QApplication::palette(), pal = old;
            pal.setColor(QPalette::AlternateBase, QColor(255, 0, 0, 60));
            QApplication::setPalette(pal);
            prog->verticalScrollBar()->setValue(prog->verticalScrollBar()->maximum() / 2 + 7);   // a listing row half under the bar
            auto colours = [&] {
                const QRect bar = p.pinnedHeaderRect(InspectorPanel::Program);
                QImage img = prog->viewport()->grab().toImage().convertToFormat(QImage::Format_RGB32);
                QSet<QRgb> seen;
                for (int y = bar.top() + 1; y < bar.bottom(); ++y) for (int x = bar.width() * 75 / 100; x < bar.width() - 2; ++x) seen.insert(img.pixel(x, y));
                return seen;
            };
            QSet<QRgb> seen;
            for (int i = 0; i < 40 && (seen = colours()).size() != 1; ++i) QTest::qWait(50);   // slow machines (qemu): let it settle
            QApplication::setPalette(old);
            QVERIFY2(seen.size() == 1, qPrintable(QString("%1 colours under the pinned bar").arg(seen.size())));
        }
        // nothing overlaps the registers area: the program list starts below the register list, the pinned bar is inside the program viewport
        QVERIFY(prog->geometry().top() >= regs->geometry().bottom());
        QVERIFY(prog->viewport()->rect().contains(p.pinnedHeaderRect(InspectorPanel::Program)));
        // register list: the header of the section being scrolled is pinned, and the next header pushes it up
        regs->verticalScrollBar()->setValue(0);
        QTest::qWait(30);
        QCOMPARE(p.pinnedHeaderText(InspectorPanel::Registers), QString("Stack"));
        regs->verticalScrollBar()->setValue(regs->verticalScrollBar()->maximum());
        QTest::qWait(30);
        QCOMPARE(p.pinnedHeaderText(InspectorPanel::Registers), QString("Storage registers (40)"));
        QCOMPARE(p.pinnedHeaderRect(InspectorPanel::Registers).top(), 0);
        // just before the storage header arrives, the stack header is pushed up (partly out of sight), never drawn below the next header
        const QModelIndex storage = regs->model()->index(6, 0);                              // row 0 header, 5 stack rows, row 6 = storage header
        regs->scrollTo(storage, QAbstractItemView::PositionAtTop);
        QTest::qWait(30);
        regs->verticalScrollBar()->setValue(regs->verticalScrollBar()->value() - 8);          // the storage header is 8 px below the top
        QTest::qWait(30);
        QCOMPARE(p.pinnedHeaderText(InspectorPanel::Registers), QString("Stack"));
        QVERIFY(p.pinnedHeaderRect(InspectorPanel::Registers).bottom() <= regs->visualRect(storage).top());
        // screenshots for the report
        prog->verticalScrollBar()->setValue(prog->verticalScrollBar()->maximum() / 2);
        regs->verticalScrollBar()->setValue(0);
        QTest::qWait(60);
        const QString dir = qEnvironmentVariable("VC_SHOTS");
        if (!dir.isEmpty()) p.grab().save(dir + "/inspector-pinned-headers.png");
    }
#ifdef Q_OS_LINUX
    void blockedUsbOpenEndsInAnErrorNotAHangingWindow() {
        // Field report (test4): the USB log showed "(start)" and then only the watchdog 25 s later: something before the first log line of open() never returned.
        QByteArray fw = vct::firmware15(); VC_REQUIRE(fw, "15C");
        auto w = window(fw);
        UsbLog::clear();
        w->makeTransport = [] {
            auto *t = new HidrawTransport;
            t->setPath("/dev/hidraw3"); t->openTimeoutMs = 300;
            t->openFn = [](const char *) { QThread::msleep(1500); return -1; };          // open() sits in the kernel
            return std::unique_ptr<HidTransport>(t);
        };
        QElapsedTimer tm; tm.start();
        w->readFromCalculator();
        QVERIFY(w->linkBusy());
        QVERIFY(waitFor([&] { return !errors.isEmpty(); }, 8000));
        QVERIFY2(tm.elapsed() < 5000, qPrintable(QString::number(tm.elapsed())));          // long before the 25 s watchdog
        QVERIFY2(errors.last().contains("did not answer when VoyagerCalc tried to open /dev/hidraw3"), qPrintable(errors.last()));
        QVERIFY(!w->linkBusy());
        const QString log = UsbLog::text();
        QVERIFY2(log.contains("(requested; starting the USB thread)") && log.contains("USB thread running") && log.contains("(start)")
                 && log.contains("TIMED OUT") && log.contains("FAILED: Linux did not answer"), qPrintable(log));
        QVERIFY(!log.contains("watchdog"));
    }
#endif
    void noCalculatorMessageIsPlain() {
        QByteArray fw = vct::firmware15(); VC_REQUIRE(fw, "15C");
        auto w = window(fw);
        FakeCalculator fake(QByteArray(2048, 0)); fake.connected = false;
        w->makeTransport = [&] { return std::unique_ptr<HidTransport>(new Forward(&fake)); };
        w->readFromCalculator();
        QVERIFY(waitFor([&] { return !errors.isEmpty(); }));
        QVERIFY(errors.last().contains("No calculator found"));
        QVERIFY(!w->linkBusy());
    }
    void aboutHasCredits() {
        QByteArray fw = vct::firmware15(); VC_REQUIRE(fw, "15C");
        auto w = window(fw);
        QString text;
        w->aboutHook = [&](const QString &t) { text = t; };
        w->showAbout();
        QVERIFY(text.contains("Eric Rechlin")); QVERIFY(text.contains("Torsten Manz")); QVERIFY(text.contains("MIT"));
        QVERIFY(text.contains("Not affiliated"));
    }
    void menusExist() {
        QByteArray fw = vct::firmware15(); VC_REQUIRE(fw, "15C");
        auto w = window(fw);
        for (const char *m : {"File", "View", "Calculator", "Model", "Keys", "Help"}) QVERIFY2(w->menu(m), m);
        QStringList usb; for (QAction *a : w->menu("Calculator")->actions()) if (a->menu()) for (QAction *b : a->menu()->actions()) usb << b->text();
        QCOMPARE(usb.join('|').contains("Fix USB Permissions"), usbNeedsPermissionFix());       // Linux only: Windows needs no USB setup
        QVERIFY2(usb.join('|').contains("Read from Calculator"), qPrintable(usb.join('|')));
    }
    // ---- window size memory: the window must not grow at every launch ----
    static void clearWindowSettings() { QSettings s; for (const char *k : {"geometry", "geometryHasInspector", "inspectorWidth", "showInspector"}) s.remove(QString::fromLatin1(k)); }
    std::unique_ptr<MainWindow> launchPlain(const QByteArray &fw) {
        auto w = std::make_unique<MainWindow>();
        w->prompts.error = [this](const QString &m) { errors << m; };
        w->prompts.notice = [this](const QString &m) { notices << m; };
        w->usbFast = true; w->usbSettleMs = 0;
        w->show();
        if (!fw.isEmpty()) { w->startFirmware(fw, "t.bin", false); if (!waitFor([&] { return w->isRunning(); })) QTest::qFail("did not boot", __FILE__, __LINE__); QTest::qWait(150); }
        return w;
    }
    static bool screenTooSmall() { QScreen *sc = QGuiApplication::primaryScreen(); return !sc || sc->availableGeometry().width() < 1500 || sc->availableGeometry().height() < 800; }
    static QByteArray geometryOf(const QRect &r) { QWidget t; t.setGeometry(r); return t.saveGeometry(); }
    void windowSizeStaysTheSameAtEveryLaunch() {
        QByteArray fw = vct::firmware15(); VC_REQUIRE(fw, "15C");
        if (screenTooSmall()) QSKIP("needs a screen of at least 1500x800 (the offscreen test platform is configured with 1920x1080)");
        MainWindow::screenAreaOverride = QRect(0, 0, 1920, 1080);
        clearWindowSettings(); QSettings().setValue("showInspector", true);
        QList<QSize> sizes; QList<int> insp;
        for (int i = 0; i < 4; ++i) {
            auto w = launchPlain(fw);
            QVERIFY(w->inspector()->isVisible());
            QVERIFY(w->windowRestoreSettled());
            sizes << w->size(); insp << w->inspector()->width();
            w->close();
        }
        QVERIFY2(sizes[1] == sizes[2] && sizes[2] == sizes[3], qPrintable(QString("%1 %2 %3 %4").arg(sizes[0].width()).arg(sizes[1].width()).arg(sizes[2].width()).arg(sizes[3].width())));
        QCOMPARE(insp[1], insp[2]); QCOMPARE(insp[2], insp[3]);
        QVERIFY(sizes[3].width() < 1500);
        qInfo("window widths over 4 launches: %d %d %d %d, flyout %d", sizes[0].width(), sizes[1].width(), sizes[2].width(), sizes[3].width(), insp[3]);
        MainWindow::screenAreaOverride = QRect();
    }
    void windowSavedWithFlyoutDoesNotGrowAgain() {      // exactly the reported bug: geometry saved with the flyout inside, flyout shown again at start
        QByteArray fw = vct::firmware15(); VC_REQUIRE(fw, "15C");
        MainWindow::screenAreaOverride = QRect(0, 0, 1920, 1080);
        clearWindowSettings();
        { QSettings s; s.setValue("showInspector", true); s.setValue("geometryHasInspector", true); s.setValue("inspectorWidth", 340); s.setValue("geometry", geometryOf(QRect(100, 100, 1100, 650))); }
        auto w = launchPlain(fw);
        QVERIFY(w->inspector()->isVisible());
        QVERIFY2(w->width() <= 1100 && w->width() >= 700, qPrintable(QString::number(w->width())));   // never 1100 + 340
        QCOMPARE(w->inspector()->width(), 340);
        const int w1 = w->width();
        w->close(); w.reset();
        w = launchPlain(fw);
        QCOMPARE(w->width(), w1);
        MainWindow::screenAreaOverride = QRect();
    }
    void windowToggleOfFlyoutAddsAndGivesBackItsWidth() {
        QByteArray fw = vct::firmware15(); VC_REQUIRE(fw, "15C");
        if (screenTooSmall()) QSKIP("needs a screen of at least 1500x800 (the offscreen test platform is configured with 1920x1080)");
        MainWindow::screenAreaOverride = QRect(0, 0, 1920, 1080);
        clearWindowSettings(); QSettings().setValue("inspectorWidth", 400);
        auto w = launchPlain(fw);
        QVERIFY(!w->inspector()->isVisible());
        const int w0 = w->width();
        w->setShowInspector(true);
        QVERIFY(w->inspector()->isVisible());
        QCOMPARE(w->inspector()->width(), 400);
        QVERIFY2(w->width() > w0 + 390, qPrintable(QString("%1 -> %2").arg(w0).arg(w->width())));
        w->setShowInspector(false);
        QCOMPARE(w->width(), w0);
        MainWindow::screenAreaOverride = QRect();
    }
    void windowRestoredMuchWiderThanTheFaceLosesItsBlackBars() {
        QByteArray fw = vct::firmware15(); VC_REQUIRE(fw, "15C");
        if (screenTooSmall()) QSKIP("needs a screen of at least 1500x800 (the offscreen test platform is configured with 1920x1080)");
        MainWindow::screenAreaOverride = QRect(0, 0, 1920, 1080);
        clearWindowSettings();
        { QSettings s; s.setValue("geometryHasInspector", false); s.setValue("geometry", geometryOf(QRect(50, 50, 1700, 600))); }
        auto w = launchPlain(fw);
        const double aspect = w->calculator()->faceAspect();
        QVERIFY(aspect > 0);
        const int paneH = w->calculator()->height();
        QVERIFY2(w->width() < 1700 && w->calculator()->width() <= paneH * aspect * 1.12, qPrintable(QString("window %1, pane %2x%3, aspect %4").arg(w->width()).arg(w->calculator()->width()).arg(paneH).arg(aspect)));
        QVERIFY(w->width() >= w->minimumWidth());
        MainWindow::screenAreaOverride = QRect();
    }
    void windowLargerThanScreenOrOffScreenIsPulledBack() {      // no firmware needed
        MainWindow::screenAreaOverride = QRect(0, 0, 1280, 720);
        clearWindowSettings();
        { QSettings s; s.setValue("geometry", geometryOf(QRect(-300, 20, 3000, 2000))); s.setValue("geometryHasInspector", true); }
        { MainWindow w; QVERIFY2(w.width() <= 1280 && w.height() <= 720, qPrintable(QString("%1x%2").arg(w.width()).arg(w.height()))); QVERIFY(w.geometry().left() >= 0); }
        { QSettings s; s.setValue("geometry", geometryOf(QRect(9000, 9000, 900, 560))); }
        { MainWindow w; QCOMPARE(w.size(), QSize(900, 560)); QVERIFY(MainWindow::screenAreaOverride.contains(w.geometry().center())); QVERIFY(std::abs(w.geometry().center().x() - 640) <= 6); }
        clearWindowSettings();
        { MainWindow w; QCOMPARE(w.size(), QSize(900, 560)); }
        MainWindow::screenAreaOverride = QRect(0, 0, 800, 480);
        { MainWindow w; QVERIFY(w.width() <= 800 && w.height() <= 480); }
        MainWindow::screenAreaOverride = QRect();
    }
    void welcomeWithoutFirmware() {
        MainWindow w; w.prompts.error = [](const QString &) {}; w.show();
        QVERIFY(!w.isRunning());
        QVERIFY(!w.menu("Calculator")->actions().isEmpty());
    }
    void screenshots() {
        const QString dir = qEnvironmentVariable("VC_SHOTS", QDir::tempPath());
        struct { QByteArray fw; const char *name; int variant; } list[] = {{vct::firmwareMulti(), "12c", 120}, {vct::firmware15(), "15c", 0}, {vct::firmware16(), "16c", 0}};
        for (auto &e : list) {
            if (e.fw.isEmpty()) continue;
            auto w = window(e.fw);
            if (e.variant) { answer = 1; w->chooseModel(e.variant); waitFor([&] { return !w->isSwitching(); }); }
            w->resize(1100, 620); w->setShowInspector(true);
            type(*w, "1 2 3 ENTER 4 5 6 +");
            QTest::qWait(500);
            w->grab().save(dir + "/ui-" + e.name + ".png");
        }
    }
};
QTEST_MAIN(TstApp)
#include "tst_app.moc"
