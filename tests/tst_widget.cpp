// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
// The calculator face: key zones (gold above, cap, blue on the front), clicks, right-click latch, and the computer
// keyboard rules (Shift alone = f, Alt alone = g, Alt + key = g then key).
#include "TestSupport.h"
#include "CalculatorWidget.h"
#include "HardwareKeyRouter.h"
#include <QSignalSpy>

using namespace vc;

class TstWidget : public QObject {
    Q_OBJECT
private slots:
    void everyKeyIsClickableOnEveryModel() {
        for (int id : {12, 15, 16}) {
            CalculatorWidget w; w.setModelId(id); w.resize(900, 560); w.show();
            const Model &m = Model::with(id);
            int n = 0;
            for (int i = 0; i < 39; ++i) {
                const KeySpec &k = m.keys()[i];
                QRectF r = w.keyRect(k.code);
                QVERIFY2(!r.isEmpty(), qPrintable(QString("model %1 key %2").arg(id).arg(k.code)));
                KeyPart part;
                int hit = w.keyAt(r.center(), &part);
                QCOMPARE(hit, k.code);
                QCOMPARE(int(part), int(KeyPart::Face));
                ++n;
            }
            QCOMPARE(n, 39);
        }
    }
    void goldAndBlueZonesAreLegends() {
        CalculatorWidget w; w.setModelId(15); w.resize(900, 560); w.show();
        const Model &m = Model::hp15c();
        const KeySpec *sin = nullptr;
        for (int i = 0; i < 39; ++i) if (QString(m.keys()[i].face) == "SIN") sin = &m.keys()[i];
        QVERIFY(sin);
        KeyPart part;
        QCOMPARE(w.keyAt(w.zoneRect(sin->code, KeyPart::FLabel).center(), &part), sin->code);
        QCOMPARE(int(part), int(KeyPart::FLabel));
        QCOMPARE(w.keyAt(w.zoneRect(sin->code, KeyPart::GLabel).center(), &part), sin->code);
        QCOMPARE(int(part), int(KeyPart::GLabel));
        QCOMPARE(w.keyAt(QPointF(-5, -5)), -1);
    }
    void clickPressesAndReleases() {
        CalculatorWidget w; w.setModelId(15); w.resize(900, 560); w.show(); w.setKeysEnabled(true);
        QSignalSpy press(&w, &CalculatorWidget::keyPressed), release(&w, &CalculatorWidget::keyReleased), legend(&w, &CalculatorWidget::legendPressed), latch(&w, &CalculatorWidget::latchToggled);
        const KeySpec &k = Model::hp15c().keys()[0];
        QTest::mousePress(&w, Qt::LeftButton, {}, w.keyRect(k.code).center().toPoint());
        QCOMPARE(press.count(), 1); QCOMPARE(press[0][0].toInt(), k.code);
        QTest::mouseRelease(&w, Qt::LeftButton, {}, w.keyRect(k.code).center().toPoint());
        QCOMPARE(release.count(), 1);
        QTest::mousePress(&w, Qt::LeftButton, {}, w.zoneRect(k.code, KeyPart::FLabel).center().toPoint());
        QTest::mouseRelease(&w, Qt::LeftButton, {}, w.zoneRect(k.code, KeyPart::FLabel).center().toPoint());
        QCOMPARE(legend.count(), 1); QCOMPARE(legend[0][1].toInt(), 31);
        QTest::mouseClick(&w, Qt::RightButton, {}, w.keyRect(k.code).center().toPoint());
        QCOMPARE(latch.count(), 1);
    }
    void keysAreDisabledUntilTheCalculatorRuns() {
        CalculatorWidget w; w.setModelId(15); w.resize(900, 560); w.show(); w.setKeysEnabled(false);
        QSignalSpy press(&w, &CalculatorWidget::keyPressed);
        QTest::mouseClick(&w, Qt::LeftButton, {}, w.keyRect(Model::hp15c().keys()[0].code).center().toPoint());
        QCOMPARE(press.count(), 0);
    }
    void shiftAloneIsF_AltAloneIsG() {
        HardwareKeyRouter r; r.model = []() -> const Model & { return Model::hp15c(); };
        std::vector<QString> log;
        r.press = [&](int c) { log.push_back("P" + QString::number(c)); };
        r.release = [&](int c) { log.push_back("R" + QString::number(c)); };
        r.pressShifted = [&](int s, int c) { log.push_back("S" + QString::number(s) + "," + QString::number(c)); };
        r.modifier(HardwareKeyRouter::ShiftKey, true); r.modifier(HardwareKeyRouter::ShiftKey, false);
        QCOMPARE(log, (std::vector<QString>{"P31", "R31"}));
        log.clear();
        r.modifier(HardwareKeyRouter::AltKey, true); r.modifier(HardwareKeyRouter::AltKey, false);
        QCOMPARE(log, (std::vector<QString>{"P32", "R32"}));
        log.clear();
        // Shift held while typing a character is not f
        r.modifier(HardwareKeyRouter::ShiftKey, true);
        r.keyDown(1, {U'*', -1}, false); r.keyUp(1);
        r.modifier(HardwareKeyRouter::ShiftKey, false);
        QVERIFY(log.size() == 2 && log[0][0] == 'P' && log[1][0] == 'R');
        log.clear();
        // Alt + a key with a blue legend: g, then the key
        r.modifier(HardwareKeyRouter::AltKey, true);
        r.keyDown(2, {U'1', -1}, false);                       // 1 has a blue legend (→DEG? on 15C: "→P"...)
        QVERIFY(!log.empty());
        r.keyUp(2);
        r.modifier(HardwareKeyRouter::AltKey, false);
        QVERIFY(log[0].startsWith("S32,"));
    }
    void everyModelHasShortcutsForTheDigits() {
        for (int id : {12, 15, 16}) {
            const Model &m = Model::with(id);
            for (char c = '0'; c <= '9'; ++c) QVERIFY2(m.keyForShortcut({char32_t(c), -1}), qPrintable(QString("%1 %2").arg(id).arg(c)));
            QVERIFY(m.keyForShortcut({0, SK_Return}));
            QVERIFY(m.keyForShortcut({0, SK_Delete}));
        }
    }
    void lcdRendersTheDisplayText() {
        CalculatorWidget w; w.setModelId(15); w.resize(900, 560); w.show();
        DisplaySnapshot d; d.model = 15; d.text = "1.2345";
        for (auto &s : d.seg) s = 0x7F;
        w.setDisplay(d);
        QImage img = w.grab().toImage();
        QVERIFY(!img.isNull());
        QRect g = w.glassRect().toRect();
        QVERIFY(g.width() > 100);
        // dark segments on the glass
        int dark = 0;
        for (int y = g.top() + 5; y < g.bottom() - 5; y += 2) for (int x = g.left() + 5; x < g.right() - 5; x += 2) if (qGray(img.pixel(x, y)) < 60) ++dark;
        QVERIFY(dark > 50);
    }
};
QTEST_MAIN(TstWidget)
#include "tst_widget.moc"
