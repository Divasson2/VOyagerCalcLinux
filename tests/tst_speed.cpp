// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
// Speed of the engine thread: the calculator always runs in real time (10 ms of calculator time per 10 ms of wall
// time; the Turbo mode was removed in 0.7.1). Measured idle (the firmware sleeps) and busy (a 300-pass count-down
// program at the firmware's default 4 MHz clock). Needs a 15C firmware; prints the ratios.
#include "TestSupport.h"
#include "EngineWorker.h"
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QThread>

using namespace vc;

class TstSpeed : public QObject {
    Q_OBJECT
    QThread thread_;
    EngineWorker *w_ = nullptr;
    int simMs() { int ms = 0; w_->performAndWait([&](Machine &m) { ms = m.simMs(); }); return ms; }
    double ratio(int wallMs) {
        int s0 = simMs(); QElapsedTimer t; t.start();
        QTest::qWait(wallMs);
        int s1 = simMs();
        return double(s1 - s0) / double(t.elapsed());
    }
private slots:
    void initTestCase() {
        QByteArray fw = vct::firmware15();
        VC_REQUIRE(fw, "15C");
        qRegisterMetaType<vc::FirmwareDetails>();
        w_ = new EngineWorker; w_->moveToThread(&thread_); thread_.start();
        QSignalSpy booted(w_, &EngineWorker::booted);
        QMetaObject::invokeMethod(w_, [this, fw] { w_->start(fw, false, SavedState()); });
        QVERIFY(booted.wait(10000));
        QVERIFY(booted.first().at(0).toBool());
        w_->performAndWait([](Machine &m) { if (m.isBlank()) m.tap(30); });      // boot normally leaves it on
    }
    void cleanupTestCase() {
        if (w_) { w_->post([w = w_] { w->stop(); w->deleteLater(); }); thread_.quit(); thread_.wait(); }
    }
    // LBL A, 1, -, x=0?, RTN, GTO A; then 300 GSB A. Returns the wall time until the program has stopped.
    qint64 runCountdown() {
        w_->performAndWait([](Machine &m) {
            for (int k : {24, 32, 20, 31, 10, 0, 26, 29, 32, 19, 32, 21, 11, 0, 32, 20, 28, 36, 36}) m.tap(k);
        });
        QElapsedTimer t; t.start();
        w_->post([w = w_] { w->pressKey(21); w->releaseLater({21}, 150); });
        QTest::qWait(60);
        w_->post([w = w_] { w->pressKey(0); w->releaseLater({0}, 150); });
        QString text;
        while (t.elapsed() < 30000) {
            QTest::qWait(20);
            w_->performAndWait([&](Machine &m) { text = m.calc.displayText(); });
            if (t.elapsed() > 120 && text.trimmed().startsWith(QLatin1String("0.0000"))) break;
        }
        return t.elapsed() - 60;
    }
    void alwaysRealTime() {
        // 0.7.1: there is no Turbo any more; idle and busy both run at 1x
        double idle = ratio(1500);
        qint64 busy = runCountdown();
        qInfo("normal: idle %.2fx real time; 300-pass program %lld ms", idle, busy);
        QVERIFY(idle > 0.85 && idle < 1.15);
        // a real 15C CE at 4 MHz takes about 5.4 s for this loop (0.7.1 test9 took 4.2 s: ~30 % fast, VC_IPC_REAL fixes it)
        QVERIFY2(busy > 4800 && busy < 6500, qPrintable(QString::number(busy)));
        normalMs_ = busy;
    }
    void busyIsRealTimeToo() {
        // while a program runs the calculator still gets 10 ms of calculator time per 10 ms of wall time
        w_->performAndWait([](Machine &m) { for (int k : {28, 36, 36}) m.tap(k); });      // 300 again
        int s0 = simMs(); QElapsedTimer t; t.start();
        w_->post([w = w_] { w->pressKey(21); w->releaseLater({21}, 150); });
        QTest::qWait(60);
        w_->post([w = w_] { w->pressKey(0); w->releaseLater({0}, 150); });
        QTest::qWait(1500);
        double r = double(simMs() - s0) / double(t.elapsed());
        qInfo("busy: %.2fx real time", r);
        QVERIFY(r > 0.85 && r < 1.15);
        QTest::qWait(3500);                   // let the program end
    }
private:
    qint64 normalMs_ = 0;
};

QTEST_MAIN(TstSpeed)
#include "tst_speed.moc"
