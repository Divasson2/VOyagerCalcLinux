// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
// The all-segments self test (ON + x) with real firmware: the 12C, 15C and 16C light every segment of the same LCD glass,
// so the rendered display must be identical on all three models. Set VC_SHOT_DIR to keep the pictures (LCD and whole face).
#include "TestSupport.h"
#include "CalculatorWidget.h"
#include "Emulator.h"
#include "LcdPainter.h"
#include <QPainter>

using namespace vc;

class TstSelftest : public QObject {
    Q_OBJECT
    // boot, ON, OFF, then hold ON and press x; keep the frame with the most segments lit
    static DisplaySnapshot selfTest(const QByteArray &fw) {
        Emulator e; e.loadFirmware(fw); e.startWhole();
        e.tap(30, 150, 1500); e.tap(30, 150, 800);
        int mul = vc_key_code(15, "*");
        e.press(30); e.run(100); e.press(mul); e.run(200); e.release(30); e.run(100); e.release(mul);
        DisplaySnapshot best; int bestN = -1;
        for (int i = 0; i < 40; ++i) {
            e.run(100);
            DisplaySnapshot d = e.display();
            int n = qPopulationCount(quint64(d.annBits));
            for (auto s : d.seg) n += qPopulationCount(quint32(s));
            for (bool b : d.comma) n += b;
            for (bool b : d.dot) n += b;
            if (n > bestN) { bestN = n; best = d; }
        }
        return best;
    }
    static QImage lcd(const DisplaySnapshot &d) {
        QImage img(720, 180, QImage::Format_ARGB32); img.fill(QColor(0xE8, 0xE4, 0xD8));
        QPainter p(&img); p.setRenderHint(QPainter::Antialiasing); paintLcd(p, QRectF(10, 10, 700, 160), d); p.end();
        return img;
    }
private slots:
    void selfTestLooksTheSameOnEveryModel() {
        const QByteArray fw[3] = { vct::firmwareMatching(QStringLiteral("HP12c-multi*E6E6.bin")),   // boots as a 12C
                                    vct::firmware15(), vct::firmware16() };
        for (auto &f : fw) VC_REQUIRE(f, "12C/15C/16C");
        const QString dir = qEnvironmentVariable("VC_SHOT_DIR");
        QImage first;
        for (int i = 0; i < 3; ++i) {
            DisplaySnapshot d = selfTest(fw[i]);
            QVERIFY(d.annBits != 0);
            for (bool c : d.comma) QVERIFY(c);      // all ten digits have a comma on the glass
            QImage img = lcd(d);
            if (!dir.isEmpty()) {
                img.save(dir + QStringLiteral("/selftest-lcd-%1C.png").arg(d.model));
                CalculatorWidget w; w.setModelId(d.model); w.resize(900, 560); w.show(); w.setDisplay(d);
                w.grab().save(dir + QStringLiteral("/selftest-face-%1C.png").arg(d.model));
            }
            if (i == 0) first = img;
            else QVERIFY2(img == first, qPrintable(QStringLiteral("model %1 self test differs from the first model").arg(d.model)));
        }
    }
};

QTEST_MAIN(TstSelftest)
#include "tst_selftest.moc"
