// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
// The Linux side of USB: hidraw framing over a socket pair standing in for /dev/hidrawN, device discovery in a
// fake sysfs, the permission script (run into a temporary folder, never touching the system) and the backup file.
#include "TestSupport.h"
#include "FakeCalculator.h"
#include "usb/HidrawTransport.h"
#include "usb/StepGuard.h"
#include "usb/UsbLog.h"
#include "usb/UsbSelfTest.h"
#include <fcntl.h>
#include <sys/stat.h>
#include "usb/UsbPermissions.h"
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTemporaryDir>
#include <atomic>
#include <QElapsedTimer>
#include <sys/socket.h>
#include <unistd.h>

using namespace vc;

/// The device end of the socket: a thread that reads hidraw writes (65 bytes: report id 0 + 64) and answers.
class DeviceThread : public QThread {
public:
    DeviceThread(int fd, FakeCalculator *calc) : fd_(fd), calc_(calc) {}
    std::atomic<bool> stop{false};
    std::vector<size_t> writeSizes;
    void run() override {
        while (!stop) {
            uint8_t buf[256];
            ssize_t n = ::recv(fd_, buf, sizeof buf, MSG_DONTWAIT);
            if (n <= 0) { QThread::msleep(1); continue; }
            writeSizes.push_back(size_t(n));
            Report r{};
            std::copy(buf + 1, buf + 65, r.begin());       // drop the leading report id
            calc_->send(r);
            Report in;
            while (calc_->receive(0, &in)) ::send(fd_, in.data(), in.size(), 0);
        }
    }
private:
    int fd_; FakeCalculator *calc_;
};


/// A hidraw node as the Linux kernel behaves for a device WITHOUT report ids: write(fd, 65 bytes) must start with the report id 0, which the kernel
/// strips (the device sees 64 bytes). Any other first byte is taken as a report id the device does not have: the kernel sends 63 bytes to a
/// report that does not exist and the device never answers. Replies arrive as plain 64-byte reads (no id byte), or 65 bytes when `replyWithId`.
class KernelHidrawThread : public QThread {
public:
    KernelHidrawThread(int fd, FakeCalculator *calc, bool dropFirstRequest = false) : fd_(fd), calc_(calc), dropFirst_(dropFirstRequest) {}
    std::atomic<bool> stop{false};
    bool replyWithId = false;
    std::atomic<int> goodWrites{0}, badWrites{0};
    void run() override {
        while (!stop) {
            uint8_t buf[256];
            ssize_t n = ::recv(fd_, buf, sizeof buf, MSG_DONTWAIT);
            if (n <= 0) { QThread::msleep(1); continue; }
            if (n != 65 || buf[0] != 0) { ++badWrites; continue; }          // not report id 0 + 64 bytes: the device does not answer
            ++goodWrites;
            if (dropFirst_) { dropFirst_ = false; continue; }                // a lost first request
            Report r{}; std::copy(buf + 1, buf + 65, r.begin());
            calc_->send(r);
            Report in;
            while (calc_->receive(0, &in)) {
                if (replyWithId) { uint8_t o[65] = {0}; std::copy(in.begin(), in.end(), o + 1); ::send(fd_, o, sizeof o, 0); }
                else ::send(fd_, in.data(), in.size(), 0);
            }
        }
    }
private:
    int fd_; FakeCalculator *calc_; bool dropFirst_;
};

static QByteArray pattern(int seed) {
    QByteArray b(2048, 0);
    for (int i = 0; i < 2048; ++i) b[i] = char(uchar(i * 31 + seed * 7 + (i >> 5)));
    return b;
}

class TstUsb : public QObject {
    Q_OBJECT
private slots:
    void hidrawFramingEndToEnd() {
        int sv[2];
        QVERIFY(::socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sv) == 0);
        FakeCalculator calc(pattern(0));
        DeviceThread dev(sv[1], &calc); dev.start();
        {
            HidrawTransport t(sv[0]);
            CalculatorLink l(&t); l.commandPauseMs = 0; l.packetPauseMs = 0; l.readTimeoutMs = 1000;
            QCOMPARE(l.readMemory(), pattern(0));
            QByteArray back = l.writeMemory(pattern(4));
            QCOMPARE(back.mid(0x70, 0x780), pattern(4).mid(0x70, 0x780));
        }
        dev.stop = true; dev.wait();
        QVERIFY(!dev.writeSizes.empty());
        for (size_t n : dev.writeSizes) QCOMPARE(n, size_t(65));      // hidraw: report id byte + 64
        ::close(sv[1]);
    }
    void receiveTimesOut() {
        int sv[2];
        QVERIFY(::socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sv) == 0);
        HidrawTransport t(sv[0]);
        Report r;
        QVERIFY(!t.receive(20, &r));
        ::close(sv[1]);
    }
    void discovery() {
        QTemporaryDir d; QVERIFY(d.isValid());
        auto mk = [&](const QString &name, const QString &hidId) {
            QDir().mkpath(d.path() + "/" + name + "/device");
            QFile f(d.path() + "/" + name + "/device/uevent");
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(("DRIVER=hid-generic\nHID_ID=" + hidId + "\nHID_NAME=x\n").toUtf8());
        };
        QCOMPARE(HidrawTransport::findDevice(d.path()), QString());
        mk("hidraw0", "0003:0000046D:0000C52B");
        QCOMPARE(HidrawTransport::findDevice(d.path()), QString());
        mk("hidraw3", "0003:000003F0:00001341");
        QCOMPARE(HidrawTransport::findDevice(d.path()), QString("/dev/hidraw3"));
        QCOMPARE(HidrawTransport::findDevice(d.path() + "/nowhere"), QString());
    }
    void udevRuleText() {
        QString r = udevRule();
        QVERIFY(r.contains("ATTRS{idVendor}==\"03f0\"")); QVERIFY(r.contains("ATTRS{idProduct}==\"1341\""));
        QVERIFY(r.contains("TAG+=\"uaccess\"")); QVERIFY(r.contains("GROUP=\"plugdev\"")); QVERIFY(r.contains("MODE=\"0660\""));
        QVERIFY(manualCommand().contains("99-voyagercalc.rules"));
    }
    void fixScriptWorksAsShDashC() {          // exactly how pkexec runs it: sh -c SCRIPT name user device
        QTemporaryDir d; QVERIFY(d.isValid());
        QProcess p;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("VC_RULES_DIR", d.path() + "/r"); env.insert("VC_SKIP_SYSTEM", "1");
        p.setProcessEnvironment(env);
        p.start("/bin/sh", {"-c", fixScript(), "voyagercalc-fix", "tester", ""});
        QVERIFY(p.waitForFinished(10000));
        QCOMPARE(p.exitCode(), 0);
        QFile rule(d.path() + "/r/99-voyagercalc.rules");
        QVERIFY(rule.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(rule.readAll()), udevRule());
    }
    void fixScriptInstallsTheRule() {
        QTemporaryDir d; QVERIFY(d.isValid());
        QString script = d.path() + "/fix.sh";
        { QFile f(script); QVERIFY(f.open(QIODevice::WriteOnly)); f.write(fixScript().toUtf8()); }
        QProcess syn; syn.start("/bin/sh", {"-n", script}); QVERIFY(syn.waitForFinished()); QCOMPARE(syn.exitCode(), 0);
        QProcess p;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("VC_RULES_DIR", d.path() + "/rules.d"); env.insert("VC_SKIP_SYSTEM", "1");
        p.setProcessEnvironment(env);
        p.start("/bin/sh", {script, "tester", ""});
        QVERIFY(p.waitForFinished(10000));
        QCOMPARE(p.exitCode(), 0);
        QFile rule(d.path() + "/rules.d/99-voyagercalc.rules");
        QVERIFY(rule.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(rule.readAll()), udevRule());
        QCOMPARE(int(QFileInfo(rule).permissions() & (QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther)),
                 int(QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther));
    }
    // ---- 0.7.0 test4: the read that did not read on the real calculator ---------------------------------------------------------------------

    void readThroughKernelStyleHidraw() {
        for (bool withId : {false, true}) {
            int sv[2];
            QVERIFY(::socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sv) == 0);
            FakeCalculator calc(pattern(1));
            KernelHidrawThread dev(sv[1], &calc); dev.replyWithId = withId; dev.start();
            HidrawTransport t(sv[0]);
            CalculatorLink l(&t); l.commandPauseMs = 0; l.readTimeoutMs = 1000;
            QCOMPARE(l.readMemory(), pattern(1));
            dev.stop = true; dev.wait();
            QCOMPARE(int(dev.badWrites), 0);                 // every write was report id 0 + the 64-byte report
            QVERIFY(int(dev.goodWrites) >= 1);
            ::close(sv[1]);
        }
    }
    void aWriteWithoutTheReportIdByteIsNeverAnswered() {      // why the 65th byte matters: 64 bytes starting with FE = "report id 0xFE"
        int sv[2];
        QVERIFY(::socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sv) == 0);
        FakeCalculator calc(pattern(1));
        KernelHidrawThread dev(sv[1], &calc); dev.start();
        uint8_t w[64] = {0}; w[0] = 0xFE;
        QCOMPARE(::send(sv[0], w, sizeof w, 0), ssize_t(64));
        HidrawTransport t(sv[0]);
        Report r;
        QVERIFY(!t.receive(150, &r));
        dev.stop = true; dev.wait();
        QCOMPARE(int(dev.badWrites), 1); QCOMPARE(int(dev.goodWrites), 0);
        ::close(sv[1]);
    }
    void aLostFirstRequestIsSentAgain() {
        int sv[2];
        QVERIFY(::socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sv) == 0);
        FakeCalculator calc(pattern(2));
        KernelHidrawThread dev(sv[1], &calc, /*dropFirstRequest*/ true); dev.start();
        HidrawTransport t(sv[0]);
        CalculatorLink l(&t); l.commandPauseMs = 0; l.readTimeoutMs = 300; l.retryAfterSilence = 20;
        QCOMPARE(l.readMemory(), pattern(2));
        QCOMPARE(int(dev.goodWrites), 2);                    // the request was sent twice
        dev.stop = true; dev.wait();
        ::close(sv[1]);
    }
    void silenceStaysAnErrorWithTheCountAndTheHints() {
        FakeCalculator calc(pattern(3)); calc.silent = true;
        CalculatorLink l(&calc); l.commandPauseMs = 0; l.readTimeoutMs = 60; l.retryAfterSilence = 10;
        try { l.readMemory(); QFAIL("a silent calculator must fail"); }
        catch (const LinkError &e) {
            QCOMPARE(int(e.kind), int(LinkError::Timeout)); QCOMPARE(e.a, 0);
            QVERIFY(e.message().contains("0 of 2048 bytes")); QVERIFY(e.message().contains("Show USB Log")); QVERIFY(e.message().contains("--usb-selftest"));
        }
        int requests = 0; for (auto &r : calc.sent) if (r[0] == 0xFE) ++requests;
        QCOMPARE(requests, 2);                               // once more after the silence, not more
        calc.sent.clear(); l.retryAfterSilence = 0;
        try { l.readMemory(); QFAIL("fail"); } catch (const LinkError &) {}
        QCOMPARE(int(calc.sent.size()), 1);
    }
    void messagesAreSpecific() {
        QVERIFY(LinkError::noDevice().message().contains("No calculator found – press f ENTER ON"));
        const QString denied = LinkError::accessDenied("/dev/hidraw3").message();
        QVERIFY(denied.contains("Permission denied opening /dev/hidraw3 – click Fix USB Permissions"));
        QVERIFY(LinkError::hung().message().contains("25 seconds"));
        QVERIFY(!LinkError::hung().message().contains("0 of 2048"));       // the watchdog is not a silent calculator
    }
    void descriptorIsOpenedLikeVoyagerInterface() {
        QTemporaryDir d; QVERIFY(d.isValid());
        const QString fifo = d.path() + "/hidraw9";
        QVERIFY(::mkfifo(fifo.toLocal8Bit().constData(), 0600) == 0);       // a node that opens read-write and has nothing to read
        HidrawTransport t; t.setPath(fifo);
        t.open();                                                           // must not throw or block (plain O_RDWR, flush by poll)
        QCOMPARE(t.devicePath(), fifo);
        Report r;
        QVERIFY(!t.receive(20, &r));
        t.close();
    }
    void usbLogRecordsEveryReport() {
        UsbLog::clear();
        int sv[2];
        QVERIFY(::socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sv) == 0);
        FakeCalculator calc(pattern(1));
        KernelHidrawThread dev(sv[1], &calc); dev.start();
        HidrawTransport t(sv[0]);
        CalculatorLink l(&t); l.commandPauseMs = 0; l.readTimeoutMs = 1000;
        l.readMemory();
        dev.stop = true; dev.wait(); ::close(sv[1]);
        const QString log = UsbLog::text();
        QVERIFY2(log.contains("send 65 byte(s): 00 FE +63x00"), qPrintable(log.left(600)));       // the 65-byte frame: id 0, FE, zeros
        QVERIFY(log.contains("recv 64 byte(s): 3F "));                                           // a 63-byte report: length byte 0x3F
        QVERIFY(log.contains("dump complete: ") && log.contains("2048 bytes"));
        QCOMPARE(UsbLog::hex(reinterpret_cast<const uint8_t *>("\x01\x02\0\0"), 4), QString("01 02 +2x00"));
    }
    void selfTestFindsTheCalculatorAndSaysWhyItFails() {
        QTemporaryDir d; QVERIFY(d.isValid());
        auto put = [&](const QString &rel, const QString &text) { QDir().mkpath(QFileInfo(d.path() + "/" + rel).absolutePath()); QFile f(d.path() + "/" + rel); QVERIFY(f.open(QIODevice::WriteOnly)); f.write(text.toUtf8()); };
        // nothing connected
        QStringList out0; auto cap0 = [&](const QString &l) { out0 << l; };
        QCOMPARE(runUsbSelfTest(cap0, d.path() + "/sys", d.path() + "/dev", d.path() + "/usb", 100), 2);
        QVERIFY(out0.join("\n").contains("press f ENTER ON"));
        // a mouse, and the calculator on hidraw1 whose node cannot be read (a plain empty file stands in for the device)
        put("sys/hidraw0/device/uevent", "HID_ID=0003:0000046D:0000C52B\nHID_NAME=Mouse\n");
        put("sys/hidraw1/device/uevent", "HID_ID=0003:000003F0:00001341\nHID_NAME=HP Calculator\n");
        put("usb/1-2/idVendor", "03f0\n"); put("usb/1-2/idProduct", "1341\n"); put("usb/1-2/product", "HP-15C\n");
        put("dev/hidraw0", ""); put("dev/hidraw1", "");
        QStringList out; auto cap = [&](const QString &l) { out << l; };
        QCOMPARE(runUsbSelfTest(cap, d.path() + "/sys", d.path() + "/dev", d.path() + "/usb", 100), 1);
        const QString t = out.join("\n");
        QVERIFY2(t.contains("hidraw0: 046D:C52B \"Mouse\""), qPrintable(t));
        QVERIFY2(t.contains("hidraw1: 03F0:1341 \"HP Calculator\"") && t.contains("<- the calculator"), qPrintable(t));
        QVERIFY(t.contains("03F0:1341 \"HP-15C\"   <- the calculator in USB mode"));
        QVERIFY(t.contains("CAN read and write"));
        QVERIFY(t.contains("Test A:") && t.contains("Test B:") && t.contains("RESULT:"));
    }

    // ---- 0.7.0 test5: the hang before the device is opened (the USB log showed only "(start)" and the watchdog) ----------------------------
    void aBlockedOpenIsGivenUpAndNamed() {
        UsbLog::clear();
        std::atomic<int> lateFd{-1};
        HidrawTransport t; t.setPath("/dev/hidraw7");
        t.openTimeoutMs = 300;
        t.openFn = [&](const char *) { QThread::msleep(900); int p[2]; if (::pipe(p) != 0) return -1; ::close(p[1]); lateFd = p[0]; return p[0]; };   // an open() that sits in the kernel
        CalculatorLink l(&t);
        QElapsedTimer tm; tm.start();
        try { l.readMemory(); QFAIL("a blocked open() must end in an error"); }
        catch (const LinkError &e) {
            QCOMPARE(int(e.kind), int(LinkError::Stuck));
            QVERIFY2(e.message().contains("open /dev/hidraw7") && e.message().contains("stuck") && e.message().contains("virtual machine"), qPrintable(e.message()));
        }
        QVERIFY2(tm.elapsed() < 1500, qPrintable(QString::number(tm.elapsed())));       // returned at the limit, not when open() came back
        const QString log = UsbLog::text();
        QVERIFY2(log.contains("readMemory: looking for and opening") && log.contains("open: begin") && log.contains("open: candidate /dev/hidraw7")
                 && log.contains("step: open /dev/hidraw7") && log.contains("TIMED OUT"), qPrintable(log));
        QVERIFY2(log.contains("thread state"), qPrintable(log));                     // what the stuck thread is doing
        // when open() finally returns, its descriptor is closed again (no leak, nothing left open on the calculator)
        for (int i = 0; i < 60 && lateFd < 0; ++i) QThread::msleep(50);
        QVERIFY(lateFd >= 0);
        for (int i = 0; i < 40 && ::fcntl(lateFd, F_GETFD) != -1; ++i) QThread::msleep(50);
        QVERIFY(lateFd >= 0 && ::fcntl(lateFd, F_GETFD) == -1);
    }
    void aBlockedSysfsScanIsGivenUp() {
        // /sys/class/hidraw/hidrawN/device/uevent that does not answer: a FIFO nobody writes to blocks open() exactly like that
        UsbLog::clear();
        QTemporaryDir d; QVERIFY(d.isValid());
        QDir().mkpath(d.path() + "/sys/hidraw0/device");
        const QString fifo = d.path() + "/sys/hidraw0/device/uevent";
        QVERIFY(::mkfifo(fifo.toLocal8Bit().constData(), 0600) == 0);
        HidrawTransport t; t.setRoots(d.path() + "/sys", d.path() + "/dev");
        t.discoverTimeoutMs = 300;
        CalculatorLink l(&t);
        QElapsedTimer tm; tm.start();
        try { l.readMemory(); QFAIL("a blocked scan must end in an error"); }
        catch (const LinkError &e) { QCOMPARE(int(e.kind), int(LinkError::Stuck)); QVERIFY2(e.message().contains("look for the calculator"), qPrintable(e.message())); }
        QVERIFY(tm.elapsed() < 1500);
        QVERIFY2(UsbLog::text().contains("step: discovery in") && UsbLog::text().contains("TIMED OUT"), qPrintable(UsbLog::text()));
        int w = ::open(fifo.toLocal8Bit().constData(), O_WRONLY | O_NONBLOCK); if (w >= 0) ::close(w);      // let the abandoned thread end
    }
    void discoveryNamesEveryCandidateAndSkipsOnesThatCannotBeOpened() {
        UsbLog::clear();
        QTemporaryDir d; QVERIFY(d.isValid());
        auto put = [&](const QString &rel, const QString &text) { QDir().mkpath(QFileInfo(d.path() + "/" + rel).absolutePath()); QFile f(d.path() + "/" + rel); QVERIFY(f.open(QIODevice::WriteOnly)); f.write(text.toUtf8()); };
        put("sys/hidraw0/device/uevent", "HID_ID=0003:0000046D:0000C52B\nHID_NAME=Mouse\n");
        put("sys/hidraw1/device/uevent", "HID_ID=0003:000003F0:00001341\nHID_NAME=HP Calculator\n");
        put("sys/hidraw2/device/uevent", "HID_ID=0003:000003F0:00001341\nHID_NAME=HP Calculator\n");
        QCOMPARE(HidrawTransport::findDevices(d.path() + "/sys"), QStringList({"hidraw1", "hidraw2"}));
        const QByteArray quiet = (d.path() + "/quiet").toLocal8Bit();                 // a node that opens read-write and has nothing to read
        QVERIFY(::mkfifo(quiet.constData(), 0600) == 0);
        auto openQuiet = [quiet] { return ::open(quiet.constData(), O_RDWR | O_CLOEXEC); };
        // hidraw1 is denied, hidraw2 opens: the program must go on to hidraw2
        HidrawTransport t; t.setRoots(d.path() + "/sys", "/dev");
        t.openFn = [=](const char *p) { if (QByteArray(p).endsWith("hidraw1")) { errno = EACCES; return -1; } return openQuiet(); };
        t.open();
        QCOMPARE(t.devicePath(), QString("/dev/hidraw2"));
        t.close();
        const QString log = UsbLog::text();
        QVERIFY2(log.contains("discovery: 3 hidraw node(s)") && log.contains("hidraw0: HID_ID 0003:0000046D:0000C52B \"Mouse\"  (not the calculator)")
                 && log.contains("hidraw1: HID_ID 0003:000003F0:00001341 \"HP Calculator\"  <- the calculator") && log.contains("open /dev/hidraw1 failed: Permission denied (skipped)")
                 && log.contains("open: candidate /dev/hidraw2") && log.contains("open /dev/hidraw2 ok"), qPrintable(log));
        // every candidate denied: the permission message wins; first one stuck, second fine: opens the second
        HidrawTransport t2; t2.setRoots(d.path() + "/sys", "/dev");
        t2.openFn = [](const char *) { errno = EACCES; return -1; };
        try { t2.open(); QFAIL("denied"); } catch (const LinkError &e) { QCOMPARE(int(e.kind), int(LinkError::AccessDenied)); }
        HidrawTransport t3; t3.setRoots(d.path() + "/sys", "/dev"); t3.openTimeoutMs = 200;
        t3.openFn = [=](const char *p) { if (QByteArray(p).endsWith("hidraw1")) { QThread::msleep(600); return -1; } return openQuiet(); };
        t3.open();
        QCOMPARE(t3.devicePath(), QString("/dev/hidraw2"));
        t3.close();
    }
    void discoveryDoesNotSpinOnARealSysfsFile() {
        // The test4 hang: sysfs files report size 4096, so "while (!file.atEnd()) readLine()" never ends at the true end of the text. A fake sysfs of
        // plain files cannot show that; a symlink to a REAL sysfs uevent file (size 4096) as the first, non-matching node does.
        QString real;
        for (const QString &c : {QString("/sys/class/net/lo/uevent"), QString("/sys/class/mem/null/uevent"), QString("/sys/kernel/uevent_seqnum")})
            if (QFileInfo(c).exists() && QFileInfo(c).size() == 4096) { real = c; break; }
        if (real.isEmpty()) for (const QFileInfo &fi : QDir("/sys/class/net").entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) { const QString c = fi.filePath() + "/uevent"; if (QFileInfo(c).exists() && QFileInfo(c).size() == 4096) { real = c; break; } }
        if (real.isEmpty()) QSKIP("no sysfs uevent file on this machine");
        QTemporaryDir d; QVERIFY(d.isValid());
        QDir().mkpath(d.path() + "/sys/hidraw0/device"); QDir().mkpath(d.path() + "/sys/hidraw1/device");
        QVERIFY(QFile::link(real, d.path() + "/sys/hidraw0/device/uevent"));          // first node: a real sysfs file that is NOT the calculator
        QFile f(d.path() + "/sys/hidraw1/device/uevent"); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("HID_ID=0003:000003F0:00001341\nHID_NAME=HP Calculator\n"); f.close();
        QElapsedTimer tm; tm.start();
        QCOMPARE(HidrawTransport::findDevices(d.path() + "/sys"), QStringList({"hidraw1"}));
        QVERIFY2(tm.elapsed() < 1000, qPrintable(QString::number(tm.elapsed())));
        QCOMPARE(HidrawTransport::findDevice(d.path() + "/sys"), QString("/dev/hidraw1"));
    }
    void aBlockedWriteIsGivenUp() {
        int sv[2];
        QVERIFY(::socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sv) == 0);
        HidrawTransport t(sv[0]);
        t.writeTimeoutMs = 300;
        t.writeFn = [](int, const void *, size_t) -> long { QThread::msleep(800); return 65; };
        QElapsedTimer tm; tm.start();
        try { t.send(CalculatorLink::dumpReport()); QFAIL("blocked write"); }
        catch (const LinkError &e) { QCOMPARE(int(e.kind), int(LinkError::Stuck)); QVERIFY(e.message().contains("send the request")); }
        QVERIFY(tm.elapsed() < 1500);
        ::close(sv[0]); ::close(sv[1]);
    }
    void theWatchdogMessageNamesTheLastStep() {
        const QString m = LinkError::hung("step: open /dev/hidraw0 (limit 6000 ms)").message();
        QVERIFY2(m.contains("last step: step: open /dev/hidraw0"), qPrintable(m));
        QVERIFY(!LinkError::hung().message().contains("last step"));
        const QString s = LinkError::stuck("open /dev/hidraw0", 6000).message();
        QVERIFY(s.contains("did not answer when VoyagerCalc tried to open /dev/hidraw0") && s.contains("6 seconds") && s.contains("--usb-selftest"));
    }
    void runStepReportsTheThreadState() {
        UsbLog::clear();
        StepOutcome o = runStep("sleeper", 100, [] { QThread::msleep(400); return 0L; });
        QVERIFY(!o.finished);
        QVERIFY2(UsbLog::text().contains("step: sleeper TIMED OUT") && UsbLog::text().contains("thread state S"), qPrintable(UsbLog::text()));
        StepOutcome ok = runStep("quick", 1000, [] { errno = 0; return 7L; });
        QVERIFY(ok.finished && ok.value == 7);
        StepOutcome bad = runStep("failing", 1000, [] { errno = ENOENT; return -1L; });
        QVERIFY(bad.failed() && bad.err == ENOENT);
        QThread::msleep(500);                                                       // let the abandoned thread end
        QVERIFY(UsbLog::text().contains("finally returned"));
    }
    void selfTestPrintsProgressAtOnceAndDoesNotHangOnABlockedSysfsFile() {
        QTemporaryDir d; QVERIFY(d.isValid());
        QDir().mkpath(d.path() + "/sys/hidraw0/device");
        const QString fifo = d.path() + "/sys/hidraw0/device/uevent";
        QVERIFY(::mkfifo(fifo.toLocal8Bit().constData(), 0600) == 0);
        QStringList out; QList<qint64> at; QElapsedTimer tm; tm.start();
        auto cap = [&](const QString &l) { out << l; at << tm.elapsed(); };
        const int rc = int(runUsbSelfTest(cap, d.path() + "/sys", d.path() + "/dev", d.path() + "/usb", 100));
        const QString t = out.join("\n");
        QVERIFY2(rc == 2 || rc == 1, qPrintable(t));
        QVERIFY2(t.contains("Step 2: the hidraw devices") && t.contains("its uevent file cannot be read or did not answer"), qPrintable(t));
        const int i = out.indexOf(QRegularExpression("^Step 2:.*"));
        QVERIFY(i >= 0 && at[i] < 800);                                              // the line was handed over BEFORE the blocked read ended (2 s)
        QVERIFY(tm.elapsed() >= 1900 && tm.elapsed() < 6000);                         // and the whole test did end
        int w = ::open(fifo.toLocal8Bit().constData(), O_WRONLY | O_NONBLOCK); if (w >= 0) ::close(w);
    }
    void backupFile() {
        QTemporaryDir d; QVERIFY(d.isValid());
        qputenv("VOYAGERCALC_BACKUP_DIR", d.path().toUtf8());
        QString err;
        QString path = saveBackup(pattern(2), 152, &err);
        QVERIFY2(!path.isEmpty(), qPrintable(err));
        QVERIFY(path.startsWith(d.path()));
        QVERIFY(path.endsWith(".bin15c"));
        QFile f(path); QVERIFY(f.open(QIODevice::ReadOnly)); QCOMPARE(f.readAll(), pattern(2));
        QString p16 = saveBackup(pattern(3), 160, &err);
        QVERIFY(p16.endsWith(".bin16c"));
        qunsetenv("VOYAGERCALC_BACKUP_DIR");
    }
};

QTEST_GUILESS_MAIN(TstUsb)
#include "tst_usb.moc"
