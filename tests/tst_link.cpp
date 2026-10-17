// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
// The USB link against a fake calculator that parses the reports like the CE firmware's HID handler
// (the Mac app's CalculatorLinkTests, same vectors).
#include "TestSupport.h"
#include "Machine.h"
#include "usb/CalculatorLink.h"
#include "FakeCalculator.h"
#include <QElapsedTimer>
#include <QThread>
#include <set>

using namespace vc;

static QByteArray pattern(int seed) {
    QByteArray b(2048, 0);
    for (int i = 0; i < 2048; ++i) b[i] = char(uchar(i * 31 + seed * 7 + (i >> 5)));
    return b;
}
static QByteArray merged(const QByteArray &a, const QByteArray &b) { return a.left(0x70) + b.mid(0x70, 0x780) + a.mid(0x7F0); }
static CalculatorLink makeLink(FakeCalculator &f) { CalculatorLink l(&f); l.commandPauseMs = 0; l.packetPauseMs = 0; l.readTimeoutMs = 300; return l; }
static std::vector<int> cBlocks(const FakeCalculator &f) { std::vector<int> b; for (auto &r : f.sent) if (r[1] == 'C' && r[0] != 0xFE) b.push_back(r[2]); return b; }
template <typename F> static LinkError linkError(F fn) { try { fn(); } catch (const LinkError &e) { return e; } return LinkError::io("no error"); }

class TstLink : public QObject {
    Q_OBJECT
private slots:
    void dumpCommandFraming() {
        Report r = CalculatorLink::dumpReport();
        QCOMPARE(int(r[0]), 0xFE);
        for (size_t i = 1; i < r.size(); ++i) QCOMPARE(int(r[i]), 0);
        QCOMPARE(CalculatorLink::vendorId, 0x03F0); QCOMPARE(CalculatorLink::productId, 0x1341);
    }
    void writeFraming() {
        QByteArray mem = pattern(1);
        auto reports = CalculatorLink::writeReports(mem);
        QCOMPARE(int(reports.size()), 64);
        for (int i = 0; i < 64; ++i) {
            auto &r = reports[i];
            QCOMPARE(int(r[0]), 34); QCOMPARE(int(r[1]), int('C')); QCOMPARE(int(r[2]), i);
            QCOMPARE(QByteArray(reinterpret_cast<const char *>(&r[3]), 32), mem.mid(i * 32, 32));
            for (size_t k = 35; k < r.size(); ++k) QCOMPARE(int(r[k]), 0);
        }
        QCOMPARE(linkError([] { CalculatorLink::writeReports(QByteArray(2047, 0)); }).kind, LinkError::WrongSize);
    }
    void reassembly() {
        QByteArray d;
        Report a{}; a[0] = 3; a[1] = 1; a[2] = 2; a[3] = 3; for (int i = 4; i < 64; ++i) a[i] = 9;
        QVERIFY(!CalculatorLink::append(a, &d)); QCOMPARE(d, QByteArray("\x01\x02\x03", 3));
        Report z{}; QVERIFY(!CalculatorLink::append(z, &d));
        Report bad{}; bad[0] = 64;
        QCOMPARE(linkError([&] { CalculatorLink::append(bad, &d); }).kind, LinkError::BadReport);
        QByteArray full(2040, 0);
        Report nine{}; nine[0] = 9;
        QCOMPARE(linkError([&] { CalculatorLink::append(nine, &full); }).kind, LinkError::BadReport);
        Report eight{}; eight[0] = 8;
        QVERIFY(CalculatorLink::append(eight, &full));
    }
    void readInChunks() {
        for (int chunk : {63, 32, 17, 1}) {
            FakeCalculator f(pattern(chunk)); f.chunk = chunk;
            auto l = makeLink(f);
            QCOMPARE(l.readMemory(), pattern(chunk));
            QCOMPARE(int(f.sent.size()), 1); QCOMPARE(f.sent[0], CalculatorLink::dumpReport());
            QCOMPARE(f.opens, 1); QCOMPARE(f.closes, 1);
        }
    }
    void noDevice() {
        FakeCalculator f(pattern(0)); f.connected = false;
        auto l = makeLink(f);
        QCOMPARE(linkError([&] { l.readMemory(); }).kind, LinkError::NoDevice);
        QCOMPARE(linkError([&] { l.writeMemory(pattern(1)); }).kind, LinkError::NoDevice);
        QVERIFY(LinkError::noDevice().message().contains("Connect the calculator by USB, turn it off, hold f and ENTER and press ON."));
    }
    void timeoutsDoNotHang() {
        FakeCalculator f(pattern(0)); f.silent = true;
        auto l = makeLink(f);
        QElapsedTimer t; t.start();
        auto e = linkError([&] { l.readMemory(); });
        QCOMPARE(e.kind, LinkError::Timeout); QCOMPARE(e.a, 0);
        QVERIFY(t.elapsed() < 1500);
        FakeCalculator g(pattern(0)); g.stopAfter = 1000;
        auto l2 = makeLink(g);
        QCOMPARE(linkError([&] { l2.readMemory(); }).kind, LinkError::Timeout);
        QCOMPARE(g.closes, 1);
    }
    void badReport() {
        FakeCalculator f(pattern(0)); f.corruptLength = true;
        auto l = makeLink(f);
        auto e = linkError([&] { l.readMemory(); });
        QCOMPARE(e.kind, LinkError::BadReport); QCOMPARE(e.a, 64);
    }
    void writeVerifies() {
        FakeCalculator f(pattern(0));
        auto l = makeLink(f);
        QByteArray back = l.writeMemory(pattern(5));
        QCOMPARE(back, merged(pattern(0), pattern(5)));
        QCOMPARE(f.memory, merged(pattern(0), pattern(5)));
        std::vector<int> want; for (int i = 3; i <= 63; ++i) want.push_back(i);
        QCOMPARE(cBlocks(f), want);
        QCOMPARE(f.sent.front(), CalculatorLink::dumpReport()); QCOMPARE(f.sent.back(), CalculatorLink::dumpReport());
    }
    void writeKeepsTheCalculatorsCPUState() {
        FakeCalculator f(pattern(0)); f.stopsOnCPUStateChange = true;
        auto l = makeLink(f);
        QCOMPARE(l.writeMemory(pattern(8)).mid(0x70, 0x780), pattern(8).mid(0x70, 0x780));
        QVERIFY(f.connected);
        FakeCalculator g(pattern(0)); g.stopsOnCPUStateChange = true;
        QVERIFY(linkError([&] { for (auto &r : CalculatorLink::writeReports(pattern(8))) g.send(r); }).kind == LinkError::Io);
        QVERIFY(!g.connected);
    }
    void writeKeepsTheProgramTop() {
        QByteArray empty = vct::fixture("E3E3-152-empty-usb-read.15c");
        QCOMPARE(empty.size(), 2048);
        for (const char *name : {"FinancePac.15c", "pi-1010-ready.16c"}) {
            QByteArray img = vct::fixture(name);
            QVERIFY(img.mid(0x7E0, 16) != QByteArray(16, 0));
            FakeCalculator f(empty); f.stopsOnCPUStateChange = true;
            auto l = makeLink(f);
            QByteArray back = l.writeMemory(img);
            QCOMPARE(back.mid(0x70, 0x780), img.mid(0x70, 0x780));
            QCOMPARE(back.mid(0x7F0), empty.mid(0x7F0));
        }
        FakeCalculator g(empty); g.ignoreBlocks = {63};
        auto l = makeLink(g);
        auto e = linkError([&] { l.writeMemory(vct::fixture("FinancePac.15c")); });
        QCOMPARE(e.kind, LinkError::Mismatch); QVERIFY(e.b >= 0x7E0);
    }
    void financePacOnE3E3ListsFromLine1() {
        QByteArray fw = vct::firmwareMulti(); VC_REQUIRE(fw, "E3E3");
        QByteArray empty = vct::fixture("E3E3-152-empty-usb-read.15c");
        FakeCalculator f(empty);
        auto l = makeLink(f);
        QByteArray back = l.writeMemory(vct::fixture("FinancePac.15c"));
        Machine m; m.loadFirmware(fw); m.boot(nullptr); m.switchModel(152); m.putMemory(back);
        auto s = inspect(m.calc, true);
        QVERIFY(s); QCOMPARE(int(s->program.size()), 407);
        QCOMPARE(s->program.front().fullMnemonic(), QString("f LBL 7"));
    }
    void writeRetriesAfterADroppedBlock() {
        FakeCalculator f(pattern(0)); f.ignoreOnceBlocks = {17};
        auto l = makeLink(f);
        QCOMPARE(l.writeMemory(pattern(6)), merged(pattern(0), pattern(6)));
        QCOMPARE(f.writesSeen, 2);
        QCOMPARE(int(cBlocks(f).size()), 61 + 1);
    }
    void writeMismatchIsReported() {
        FakeCalculator f(pattern(0)); f.ignoreBlocks = {3};
        auto l = makeLink(f);
        int count = 0, first = -1;
        for (int i = 0x70; i < 128; ++i) if (pattern(0)[i] != pattern(7)[i]) { if (first < 0) first = i; ++count; }
        auto e = linkError([&] { l.writeMemory(pattern(7)); });
        QCOMPARE(e.kind, LinkError::Mismatch); QCOMPARE(e.a, count); QCOMPARE(e.b, first);
        QCOMPARE(f.writesSeen, 3);
    }
    void writeCarriesFlag8() {
        QByteArray empty = vct::fixture("E3E3-152-empty-usb-read.15c"), cx = vct::fixture("pool15/v152_81_prog_dimA_cx_dim25.bin");
        QVERIFY(CalculatorLink::flag8(cx)); QVERIFY(!CalculatorLink::flag8(empty));
        FakeCalculator f(empty); f.stopsOnCPUStateChange = true;
        auto l = makeLink(f);
        QByteArray back = l.writeMemory(cx, 1);
        QVERIFY(f.connected);
        QCOMPARE(back.mid(0x70, 0x780), cx.mid(0x70, 0x780));
        QCOMPARE(uchar(back[0x4C]) & 0x08, 0x08);
        QByteArray exp = empty; exp[0x4C] = char(exp[0x4C] | 0x08);
        QCOMPARE(back.left(0x70), exp.left(0x70));
        auto b = cBlocks(f);
        QVERIFY(std::count(b.begin(), b.end(), 2)); QVERIFY(!std::count(b.begin(), b.end(), 0)); QVERIFY(!std::count(b.begin(), b.end(), 1));
        FakeCalculator g(empty);
        auto l2 = makeLink(g);
        QVERIFY(!CalculatorLink::flag8(l2.writeMemory(cx)));
        FakeCalculator h(cx); h.stopsOnCPUStateChange = true;
        auto l3 = makeLink(h);
        QByteArray off = l3.writeMemory(empty, 0);
        QCOMPARE(uchar(off[0x4C]) & 0x08, 0); QCOMPARE(uchar(off[0xC4]) & 1, 0);
    }
    void flag8RoundTripThroughTheFirmware() {
        QByteArray fw = vct::firmwareMulti(); VC_REQUIRE(fw, "E3E3");
        QByteArray empty = vct::fixture("E3E3-152-empty-usb-read.15c");
        Machine m; m.loadFirmware(fw); m.boot(nullptr); m.switchModel(152);
        m.tap(32); m.tap(16); m.tap(7);                      // g SF 8
        QByteArray image = m.memory();
        QVERIFY(CalculatorLink::flag8(image));
        for (bool fix : {true, false}) {
            FakeCalculator f(empty);
            auto l = makeLink(f);
            QByteArray back = l.writeMemory(image, fix ? 1 : -1);
            Machine c; c.loadFirmware(fw); c.boot(nullptr); c.switchModel(152); c.putMemory(back);
            auto s = inspect(c.calc, true);
            QVERIFY(s && s->pool);
            QCOMPARE(s->isComplex(), fix);
            QCOMPARE(s->pool->scratch, 0);
            auto seg = s->pool->segments();
            for (auto &x : seg) QVERIFY(x.kind != Pool15::Kind::Workspace);
            QVERIFY(seg[0].kind == (fix ? Pool15::Kind::Complex : Pool15::Kind::Free));
            QCOMPARE(seg[0].count, 5);
        }
    }
    void memoryChecks() {
        QByteArray fw16 = vct::firmware16(), fw15 = vct::firmware15();
        if (fw16.isEmpty() || fw15.isEmpty()) QSKIP("needs the 15C and 16C firmware");
        Machine m; m.loadFirmware(fw16); m.boot(nullptr); m.putMemory(vct::fixture("miller-rabin-ext-ready.16c"));
        QByteArray mem16 = m.memory();
        QVERIFY(Machine::checkMemory(mem16, fw16, 160).fits());
        QCOMPARE(Machine::checkMemory(QByteArray(100, 0), fw16, 160).kind, Machine::MemoryCheck::WrongSize);
        QVERIFY(!Machine::checkMemory(QByteArray(2048, 0), fw16, 160).fits());
        Machine m15; m15.loadFirmware(fw15); m15.boot(nullptr);
        QByteArray mem15 = m15.memory();
        QCOMPARE(Machine::checkMemory(mem15, fw16, 160).kind, Machine::MemoryCheck::Rejected);
        QVERIFY(!Machine::checkMemory(mem16, fw15, 150).fits());
        QCOMPARE(Machine::identifyMemory(mem15, fw15, {150, 152, 160}), (std::vector<int>{150, 152}));
        QCOMPARE(Machine::identifyMemory(mem16, fw15, {150, 152, 160}), (std::vector<int>{160}));
    }
    void financePacNeeds152() {
        QByteArray fw = vct::firmware15(); VC_REQUIRE(fw, "C8C8");
        QByteArray fp = vct::fixture("FinancePac.15c"), small = vct::fixture("Calc15-0-usb-read.15c");
        QCOMPARE(Machine::checkMemory(fp, fw, 150).kind, Machine::MemoryCheck::NeedsMoreMemory);
        QCOMPARE(Machine::identifyMemory(fp, fw, {150, 152, 160}), (std::vector<int>{152}));
        QVERIFY(Machine::checkMemory(small, fw, 150).fits()); QVERIFY(Machine::checkMemory(small, fw, 152).fits());
        QVERIFY(Machine::needs152(fp, fw, {150, 152, 160})); QVERIFY(!Machine::needs152(small, fw, {150, 152, 160}));
        QVERIFY(!Machine::needs152(fp, fw, {152}));
    }
    void known16CFiles() {
        QByteArray fw16 = vct::firmware16(), fw15 = vct::firmware15();
        VC_REQUIRE(fw16, "16C");
        for (const char *f : {"pi-1010-ready.16c", "fill-regs-jf-ws64-ready.16c", "e-2881-programE-ready.16c", "miller-rabin-ext-ready.16c"}) {
            QByteArray d = vct::fixture(f);
            QVERIFY2(Machine::checkMemory(d, fw16, 160).fits(), f);
            if (!fw15.isEmpty()) QVERIFY2(!Machine::checkMemory(d, fw15, 150).fits(), f);
        }
    }
    void onAndOffImagesFitEveryModel() {
        struct Case { QByteArray fw; std::vector<int> variants; };
        std::vector<Case> cases{{vct::firmwareMulti(), {120, 152, 160}}, {vct::firmware15(), {150, 152}}};
        bool any = false;
        for (auto &c : cases) {
            if (c.fw.isEmpty()) continue;
            for (int v : c.variants) {
                any = true;
                Machine m; m.loadFirmware(c.fw); m.boot(nullptr);
                if (m.variant() != v) m.switchModel(v);
                for (const char *k : {"4", "2", "STO", "1"}) m.tap(Emulator::keyCode(m.model(), k));
                m.run(300);
                QByteArray on = m.rawMemory(), off = m.memory(false);
                QVERIFY2(Machine::checkMemory(on, c.fw, v).fits(), qPrintable(QString("%1 on").arg(v)));
                QVERIFY2(Machine::checkMemory(off, c.fw, v).fits(), qPrintable(QString("%1 off").arg(v)));
            }
        }
        if (!any) QSKIP("no firmware");
    }
};

QTEST_GUILESS_MAIN(TstLink)
#include "tst_link.moc"
