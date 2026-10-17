// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
// The calculator link over a HID-API backend (what Windows uses: SetupAPI + hid.dll), tested on every platform with a fake backend:
// finding 03F0:1341 among other HID devices, the 65-byte [00 FE ...] frame, reply reports with the report id byte, a lost first request, devices that
// cannot be opened, a call that never returns (time limits), the self-test output.
#include "TestSupport.h"
#include "FakeCalculator.h"
#include "usb/HidPort.h"
#include "usb/PortTransport.h"
#include "usb/StepGuard.h"
#include "usb/UsbLog.h"
#include "usb/UsbPlatform.h"
#include "usb/UsbSelfTest.h"
#include <QElapsedTimer>
#include <QThread>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>

using namespace vc;

namespace {

QByteArray pattern(int seed) {
    QByteArray b(2048, 0);
    for (int i = 0; i < 2048; ++i) b[i] = char(uchar(i * 31 + seed * 7 + (i >> 5)));
    return b;
}

/// A HID device as Windows delivers it: WriteFile takes outLen bytes (report id 0 + 64), ReadFile gives inLen bytes (report id 0 + 64).
class FakePort : public HidPort {
public:
    FakePort(FakeCalculator *calc, bool dropFirst, int inLen) : calc_(calc), dropFirst_(dropFirst), inLen_(inLen) {}
    std::atomic<int> goodWrites{0}, badWrites{0};
    void write(const uint8_t *data, size_t n, int) override {
        if (n != 65 || data[0] != 0) { ++badWrites; return; }          // a frame without the report id 0: the device would not answer
        ++goodWrites;
        if (dropFirst_) { dropFirst_ = false; return; }
        Report r{}; std::copy(data + 1, data + 65, r.begin());
        calc_->send(r);
        Report in;
        std::lock_guard<std::mutex> g(m_);
        while (calc_->receive(0, &in)) { std::vector<uint8_t> rep(size_t(inLen_), 0); std::copy(in.begin(), in.end(), rep.begin() + 1); q_.push_back(rep); }
        cv_.notify_all();
    }
    size_t read(uint8_t *buf, size_t cap, int timeoutMs) override {
        std::unique_lock<std::mutex> l(m_);
        if (!cv_.wait_for(l, std::chrono::milliseconds(timeoutMs), [&] { return !q_.empty(); })) return 0;
        auto rep = q_.front(); q_.pop_front();
        const size_t n = std::min(cap, rep.size());
        std::copy(rep.begin(), rep.begin() + long(n), buf);
        return n;
    }
    void close() override { ++closes; }
    std::atomic<int> closes{0};
private:
    FakeCalculator *calc_; bool dropFirst_; int inLen_;
    std::mutex m_; std::condition_variable cv_; std::deque<std::vector<uint8_t>> q_;
};

class FakeBackend : public HidBackend {
public:
    std::vector<HidDeviceInfo> devices;
    std::shared_ptr<FakePort> port;
    FakeCalculator *calc = nullptr;
    bool dropFirst = false;
    int denyOpen = 0;                         // that many open() calls fail with "access denied" first
    int openBlockMs = 0, enumBlockMs = 0;
    std::atomic<int> opens{0};
    std::vector<HidDeviceInfo> enumerate() override { if (enumBlockMs) QThread::msleep(ulong(enumBlockMs)); return devices; }
    std::unique_ptr<HidPort> open(const HidDeviceInfo &d) override {
        ++opens;
        if (openBlockMs) QThread::msleep(ulong(openBlockMs));
        if (denyOpen > 0) { --denyOpen; throw LinkError::accessDenied(QString(), QStringLiteral("another program is using ") + d.path); }
        auto *p = new FakePort(calc, dropFirst, d.inputReportBytes);
        return std::unique_ptr<HidPort>(p);
    }
};

HidDeviceInfo dev(const QString &path, int vid, int pid, const QString &name = QString()) {
    HidDeviceInfo d; d.path = path; d.vendor = vid; d.product = pid; d.name = name; d.inputReportBytes = 65; d.outputReportBytes = 65; return d;
}

}  // namespace

class TstPortUsb : public QObject {
    Q_OBJECT
    std::shared_ptr<FakeBackend> backendFor(FakeCalculator *calc) {
        auto b = std::make_shared<FakeBackend>();
        b->calc = calc;
        b->devices = {dev("\\\\?\\hid#vid_046d&pid_c52b#a", 0x046D, 0xC52B, "USB Receiver"), dev("\\\\?\\hid#vid_03f0&pid_1341#b", 0x03F0, 0x1341, "HP Calculator"),
                      dev("\\\\?\\hid#vid_8087&pid_0033#c", 0x8087, 0x0033)};
        return b;
    }
private slots:
    void readsTheMemoryOverTheHidApi() {
        UsbLog::clear();
        FakeCalculator calc(pattern(1));
        auto b = backendFor(&calc);
        PortTransport t(b);
        CalculatorLink l(&t); l.commandPauseMs = 0; l.readTimeoutMs = 1500;
        QCOMPARE(l.readMemory(), pattern(1));
        const QString log = UsbLog::text();
        QVERIFY2(log.contains("046D:C52B \"USB Receiver\"") && log.contains("03F0:1341 \"HP Calculator\"  <- the calculator"), qPrintable(log));
        QVERIFY2(log.contains("open: candidate \\\\?\\hid#vid_03f0&pid_1341#b") && log.contains("send 65 byte(s): 00 FE +63x00") && log.contains("dump complete"), qPrintable(log));
        QCOMPARE(b->opens.load(), 1);                          // only the calculator was opened, not the receiver or the other devices
    }
    void writesFrameWithReportIdAndPadsToTheDeclaredLength() {
        // 12C/15C/16C all declare 65-byte reports; a device that declared more gets the frame padded with zeros
        FakeCalculator calc(pattern(2));
        auto b = backendFor(&calc);
        QByteArray seen;
        class Spy : public HidPort {
        public:
            QByteArray *out; Spy(QByteArray *o) : out(o) {}
            void write(const uint8_t *d, size_t n, int) override { *out = QByteArray(reinterpret_cast<const char *>(d), int(n)); }
            size_t read(uint8_t *, size_t, int t) override { QThread::msleep(ulong(std::min(t, 20))); return 0; }
            void close() override {}
        };
        struct Be : HidBackend {
            QByteArray *out;
            std::vector<HidDeviceInfo> enumerate() override { HidDeviceInfo d = dev("p", 0x03F0, 0x1341); d.outputReportBytes = 70; d.inputReportBytes = 70; return {d}; }
            std::unique_ptr<HidPort> open(const HidDeviceInfo &) override { return std::unique_ptr<HidPort>(new Spy(out)); }
        };
        auto be = std::make_shared<Be>(); be->out = &seen;
        PortTransport t(be);
        t.open();
        t.send(CalculatorLink::dumpReport());
        QCOMPARE(seen.size(), 70);
        QCOMPARE(uchar(seen[0]), uchar(0));                    // report id 0
        QCOMPARE(uchar(seen[1]), uchar(0xFE));                 // the command
        QVERIFY(seen.mid(2) == QByteArray(68, 0));
        t.close();
    }
    void aFrameWithoutTheReportIdIsNeverAnswered() {
        FakeCalculator calc(pattern(3));
        FakePort p(&calc, false, 65);
        uint8_t bad[64] = {0xFE};
        p.write(bad, 64, 100);
        uint8_t buf[65];
        QCOMPARE(int(p.read(buf, 65, 100)), 0);
        QCOMPARE(p.badWrites.load(), 1);
    }
    void aLostFirstRequestIsSentAgain() {
        UsbLog::clear();
        FakeCalculator calc(pattern(4));
        auto b = backendFor(&calc); b->dropFirst = true;
        PortTransport t(b);
        CalculatorLink l(&t); l.commandPauseMs = 0; l.readTimeoutMs = 400; l.retryAfterSilence = 50;
        QCOMPARE(l.readMemory(), pattern(4));
        QVERIFY(UsbLog::text().contains("dump request 2"));
    }
    void writesAndVerifiesOverTheHidApi() {
        FakeCalculator calc(pattern(5));
        auto b = backendFor(&calc);
        PortTransport t(b);
        CalculatorLink l(&t); l.commandPauseMs = 0; l.packetPauseMs = 0; l.readTimeoutMs = 1500;
        QByteArray target = pattern(6);
        QByteArray back = l.writeMemory(target);
        QCOMPARE(back.mid(0x70, 0x780), target.mid(0x70, 0x780));
    }
    void noCalculatorAmongTheDevices() {
        FakeCalculator calc(pattern(1));
        auto b = backendFor(&calc);
        b->devices.erase(b->devices.begin() + 1);
        PortTransport t(b);
        try { t.open(); QFAIL("no calculator"); } catch (const LinkError &e) { QCOMPARE(int(e.kind), int(LinkError::NoDevice)); QVERIFY(e.message().contains("press f ENTER ON")); }
    }
    void accessDeniedNamesTheOtherProgram() {
        FakeCalculator calc(pattern(1));
        auto b = backendFor(&calc); b->denyOpen = 1;
        PortTransport t(b);
        try { t.open(); QFAIL("denied"); } catch (const LinkError &e) { QCOMPARE(int(e.kind), int(LinkError::AccessDenied)); QVERIFY(e.message().contains("another program")); }
        t.open();                                              // the next try works (the other program closed it)
        t.close();
    }
    void aSecondCalculatorNodeIsTriedWhenTheFirstCannotBeOpened() {
        FakeCalculator calc(pattern(1));
        auto b = backendFor(&calc);
        b->devices.push_back(dev("second", 0x03F0, 0x1341, "HP Calculator"));
        b->denyOpen = 1;
        PortTransport t(b);
        t.open();
        QCOMPARE(t.devicePath(), QString("second"));
        t.close();
    }
    void aBlockedOpenIsGivenUp() {
        UsbLog::clear();
        FakeCalculator calc(pattern(1));
        auto b = backendFor(&calc); b->openBlockMs = 900;
        PortTransport t(b); t.openTimeoutMs = 300;
        QElapsedTimer tm; tm.start();
        try { CalculatorLink l(&t); l.readMemory(); QFAIL("blocked"); }
        catch (const LinkError &e) { QCOMPARE(int(e.kind), int(LinkError::Stuck)); QVERIFY2(e.message().contains("Windows") || e.message().contains("Linux"), qPrintable(e.message())); }
        QVERIFY(tm.elapsed() < 1500);
        QVERIFY(UsbLog::text().contains("TIMED OUT"));
    }
    void aBlockedDeviceListIsGivenUp() {
        FakeCalculator calc(pattern(1));
        auto b = backendFor(&calc); b->enumBlockMs = 900;
        PortTransport t(b); t.enumerateTimeoutMs = 300;
        QElapsedTimer tm; tm.start();
        try { t.open(); QFAIL("blocked"); } catch (const LinkError &e) { QCOMPARE(int(e.kind), int(LinkError::Stuck)); }
        QVERIFY(tm.elapsed() < 1500);
    }
    void silenceStaysAnErrorWithTheHints() {
        FakeCalculator calc(pattern(1)); calc.silent = true;
        auto b = backendFor(&calc);
        PortTransport t(b);
        CalculatorLink l(&t); l.commandPauseMs = 0; l.readTimeoutMs = 200; l.retryAfterSilence = 20;
        try { l.readMemory(); QFAIL("silent"); } catch (const LinkError &e) { QCOMPARE(int(e.kind), int(LinkError::Timeout)); QCOMPARE(e.a, 0); QVERIFY(e.message().contains("0 of 2048")); }
    }
    void selfTestListsDevicesAndReads() {
        FakeCalculator calc(pattern(1));
        auto b = backendFor(&calc);
        QStringList out; auto cap = [&](const QString &l) { out << l; };
        QCOMPARE(runHidSelfTest(cap, b, 1500), 0);
        const QString t = out.join("\n");
        QVERIFY2(t.contains("Step 1:") && t.contains("046D:C52B \"USB Receiver\"") && t.contains("03F0:1341 \"HP Calculator\"") && t.contains("<- the calculator")
                 && t.contains("OK: 2048 bytes received") && t.contains("RESULT: the calculator answers"), qPrintable(t));
        // not connected
        auto none = std::make_shared<FakeBackend>(); none->devices = {dev("x", 0x046D, 0xC52B, "Mouse")};
        QStringList out2; auto cap2 = [&](const QString &l) { out2 << l; };
        QCOMPARE(runHidSelfTest(cap2, none, 100), 2);
        QVERIFY(out2.join("\n").contains("No calculator found – press f ENTER ON"));
        // silent calculator
        FakeCalculator quiet(pattern(1)); quiet.silent = true;
        auto qb = backendFor(&quiet);
        QStringList out3; auto cap3 = [&](const QString &l) { out3 << l; };
        QCOMPARE(runHidSelfTest(cap3, qb, 150), 1);
        QVERIFY2(out3.join("\n").contains("sends NOTHING back"), qPrintable(out3.join("\n")));
    }
    void platformPieces() {
#ifdef _WIN32
        QVERIFY(!usbNeedsPermissionFix());
        QVERIFY(LinkError::noDevice().message().contains("f ENTER ON"));
        QVERIFY(LinkError::timeout(0).message().contains("USB Test"));
#else
        QVERIFY(usbNeedsPermissionFix());
#endif
        QVERIFY(makeDefaultTransport() != nullptr);
    }
};

QTEST_GUILESS_MAIN(TstPortUsb)
#include "tst_portusb.moc"
