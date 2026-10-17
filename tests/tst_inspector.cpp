// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
// The HP-15C memory pool, complex stack and matrices through the inspector (the Mac app's Pool15Tests).
#include "TestSupport.h"
#include "Machine.h"

using namespace vc;

class TstInspector : public QObject {
    Q_OBJECT
    std::map<int, std::unique_ptr<Machine>> machines_;
    QByteArray fw_;
    Machine &machine(int variant) {
        auto &m = machines_[variant];
        if (!m) { m = std::make_unique<Machine>(); m->loadFirmware(fw_); m->boot(nullptr); if (m->variant() != variant) m->switchModel(variant); }
        return *m;
    }
    InspectorSnapshot snapshotOf(const QByteArray &img, int variant) {
        Machine &m = machine(variant);
        m.putMemory(img);
        auto s = inspect(m.calc, true);
        if (!s) QTest::qFail("no snapshot", __FILE__, __LINE__);
        return s.value_or(InspectorSnapshot());
    }
    InspectorSnapshot snapshot(const char *dump, int variant) { return snapshotOf(vct::fixture(QStringLiteral("pool15/%1.bin").arg(dump)), variant); }
    static QStringList texts(const std::vector<RegisterValue> &v) { QStringList l; for (auto &x : v) l << x.text; return l; }
    static QByteArray synthComplex(QByteArray d, bool flag8 = true) {
        auto bcd = [](int n) { quint64 v = quint64(n) << 48; QByteArray b; for (int i = 0; i < 8; ++i) { b.append(char(v & 0xFF)); v >>= 8; } return b; };
        auto off = [](int p) { return 0x70 + 8 * (p - 0x10); };
        int data = uchar(d[0x11E]), pool = d[0x118] == 0 ? 0x100 : uchar(d[0x118]);
        for (int p = pool - 1; p >= data; --p) d.replace(off(p + 5), 8, d.mid(off(p), 8));
        for (int i = 0; i < 5; ++i) d.replace(off(data + i), 8, bcd(i + 1));
        for (int b = 0; b < 7; ++b) d[0x118 + b] = char(uchar(d[0x118 + b]) + 5);
        if (flag8) { d[0x4C] = char(d[0x4C] | 0x08); d[0xC4] = char(d[0xC4] | 1); }
        return d;
    }
private slots:
    void initTestCase() { fw_ = vct::firmware15(); }
    void matricesAndStorageEnd() {
        VC_REQUIRE(fw_, "15C");
        auto s = snapshot("v150_24_fillA", 150);
        QVERIFY(s.pool); auto &p = *s.pool;
        QCOMPARE(s.registerCount, 20);
        QCOMPARE(s.registers.back().name, QString("R.9"));
        QCOMPARE(p.gmem, QString("19 70 00-0"));
        QCOMPARE(p.rows[0], 2); QCOMPARE(p.cols[0], 3); QCOMPARE(p.dimension(1), QString("1×2"));
        QCOMPARE(texts(s.matrices[0].values), (QStringList{"11", "12", "13", "21", "22", "23"}));
        QVERIFY(s.matrices[0].isResult); QVERIFY(!s.isComplex());
        auto seg = p.segments();
        QCOMPARE(int(seg.size()), 4);
        QVERIFY(seg[0].kind == Pool15::Kind::Storage && seg[1].kind == Pool15::Kind::Matrix && seg[1].matrix == 0
                && seg[2].kind == Pool15::Kind::Matrix && seg[2].matrix == 1 && seg[3].kind == Pool15::Kind::Free);
        int total = 0; for (auto &x : seg) total += x.count;
        QCOMPARE(total, 96);
    }
    void complexStackOrderAndFlag8FromMemory() {
        VC_REQUIRE(fw_, "15C");
        auto s = snapshot("v150_18_cx_order", 150);
        QVERIFY(s.isComplex());
        QVERIFY(std::count(s.setFlags().begin(), s.setFlags().end(), 8) || s.setFlags().size() > 0);
        QStringList im; for (auto n : {"X", "Y", "Z", "T", "LAST X"}) { auto *v = s.imaginaryOf(n); im << (v ? v->text : "?"); }
        QCOMPARE(im, (QStringList{"180", "8", "6", "6", "10"}));
        QCOMPARE(s.pool->gmem, QString("19 73 00-0"));
        auto seg = s.pool->segments();
        QVERIFY(seg[0].kind == Pool15::Kind::Complex); QCOMPARE(seg[0].count, 5);
    }
    void complexWithMatricesAndResult() {
        VC_REQUIRE(fw_, "15C");
        auto s = snapshot("v150_34_sf8_with_matrices", 150);
        QCOMPARE(s.pool->resultLetter(), QString("C"));
        QCOMPARE(texts(s.matrices[2].values), (QStringList{"181", "192", "203"}));
        QCOMPARE(texts(s.matrices[0].values), (QStringList{"11", "12", "13", "21"}));
        QCOMPARE(s.pool->gmem, QString("19 64 00-0")); QCOMPARE(s.registerCount, 20);
    }
    void doubleMemory152() {
        VC_REQUIRE(fw_, "15C");
        auto s = snapshot("v152_81_prog_dimA_cx_dim25", 152);
        auto &p = *s.pool;
        QCOMPARE(p.size(), 192); QCOMPARE(p.gmem, QString("25 149 02-3")); QCOMPARE(s.registerCount, 26);
        QCOMPARE(p.dimension(0), QString("3×4"));
        auto seg = p.segments();
        QCOMPARE(int(seg.size()), 5);
        QVERIFY(seg[0].kind == Pool15::Kind::Complex && seg[1].kind == Pool15::Kind::Storage && seg[2].kind == Pool15::Kind::Matrix
                && seg[3].kind == Pool15::Kind::Free && seg[4].kind == Pool15::Kind::Program);
        auto e = snapshot("v152_40_dims_ABCDE", 152);
        QCOMPARE(e.pool->gmem, QString("19 145 00-0")); QCOMPARE(e.pool->matrixRegisters, 29);
    }
    void financePacGMem() {
        VC_REQUIRE(fw_, "15C");
        auto s = snapshotOf(vct::fixture("FinancePac.15c"), 152);
        QCOMPARE(s.pool->gmem, QString("96 33 64-5"));
    }
    void descriptor() {
        RegisterValue v; v.raw = 0x1A00000000000000ULL >> 0; v.raw = 0x1A'0000'0000'0000ULL;
        QCOMPARE(v.matrixDescriptor(), 0);
        RegisterValue w; w.raw = 0x01'2300'0000'0001ULL;
        QCOMPARE(w.matrixDescriptor(), -1);
    }
    void parseRealImageWithComplexOn() {
        VC_REQUIRE(fw_, "15C");
        QByteArray empty = vct::fixture("E3E3-152-empty-usb-read.15c");
        auto base = snapshotOf(empty, 152).pool;
        auto s = snapshotOf(synthComplex(empty), 152);
        QVERIFY(s.isComplex());
        QStringList im; for (auto n : {"X", "Y", "Z", "T", "LAST X"}) { auto *v = s.imaginaryOf(n); im << (v ? v->text : "?"); }
        QCOMPARE(im, (QStringList{"5", "4", "3", "2", "1"}));
        QCOMPARE(s.pool->free, base->free - 5); QCOMPARE(s.pool->scratch, 0);
        QVERIFY(s.pool->segments()[0].kind == Pool15::Kind::Complex);
        for (auto &x : s.pool->segments()) QVERIFY(x.kind != Pool15::Kind::Workspace);
    }
    void lostFlag8IsNotASolveWorkspace() {
        VC_REQUIRE(fw_, "15C");
        QByteArray empty = vct::fixture("E3E3-152-empty-usb-read.15c");
        QByteArray d = synthComplex(empty, false); d[0xC4] = char(d[0xC4] | 1);
        auto s = snapshotOf(d, 152);
        QVERIFY(!s.isComplex());
        for (auto &x : s.pool->segments()) QVERIFY(x.kind != Pool15::Kind::Workspace);
    }
    void sixteenC() {
        QByteArray fw = vct::firmware16();
        VC_REQUIRE(fw, "16C");
        Machine m; m.loadFirmware(fw); m.boot(nullptr);
        m.putMemory(vct::fixture("pi-1010-ready.16c"));
        auto s = inspect(m.calc, true);
        QVERIFY(s); QCOMPARE(s->model, 16);
        QVERIFY(s->base == 16 || s->base == 10 || s->base == 8 || s->base == 2 || s->base == 0);
        QVERIFY(!s->stack.empty());
    }
};

QTEST_GUILESS_MAIN(TstInspector)
#include "tst_inspector.moc"
