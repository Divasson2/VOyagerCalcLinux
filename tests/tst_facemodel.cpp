// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
#include "FaceModel.h"
#include <QtTest>

using namespace vc;

class TstFaceModel : public QObject {
    Q_OBJECT
private slots:
    void everyKeyCentreHitsItself_data() {
        QTest::addColumn<int>("model");
        QTest::newRow("12C") << 12; QTest::newRow("15C") << 15; QTest::newRow("16C") << 16;
    }
    void everyKeyCentreHitsItself() {
        QFETCH(int, model);
        const FaceGeometry &g = model == 12 ? kFace12C : model == 15 ? kFace15C : kFace16C;
        FaceModel f(g);
        int n = 0;
        for (int c = 0; c < 40; ++c) {
            if (!g.hasKey[c]) { QVERIFY2(c == 35, "only the lower half of ENTER has no cap"); continue; }
            QCOMPARE(f.keyAt(g.key[c].x + g.key[c].w / 2, g.key[c].y + g.key[c].h / 2), c);
            ++n;
        }
        QCOMPARE(n, 39);
    }
    void hitAreasNeverOverlap_data() { everyKeyCentreHitsItself_data(); }
    void hitAreasNeverOverlap() {
        QFETCH(int, model);
        const FaceGeometry &g = model == 12 ? kFace12C : model == 15 ? kFace15C : kFace16C;
        FaceModel f(g);
        for (int a = 0; a < 40; ++a) for (int b = a + 1; b < 40; ++b) {
            if (!g.hasKey[a] || !g.hasKey[b]) continue;
            Rect4 r = f.hitRect(a), s = f.hitRect(b);
            double ox = std::min(r.x + r.w, s.x + s.w) - std::max(r.x, s.x);
            double oy = std::min(r.y + r.h, s.y + s.h) - std::max(r.y, s.y);
            QVERIFY2(!(ox > 1e-6 && oy > 1e-6), qPrintable(QString("%1 and %2 overlap").arg(a).arg(b)));
        }
    }
    void outsideTheKeysIsNoKey() {
        FaceModel f(kFace15C);
        QCOMPARE(f.keyAt(0.5, 0.01), -1);     // above the display
        QCOMPARE(f.keyAt(0.999, 0.999), -1);
    }
    void enterCoversTwoRows() {
        FaceModel f(kFace15C);
        const Rect4 &e = kFace15C.key[25];
        QVERIFY(e.h > kFace15C.key[24].h * 1.5);
        QCOMPARE(f.keyAt(e.x + e.w / 2, e.y + e.h * 0.9), 25);
    }
};

QTEST_APPLESS_MAIN(TstFaceModel)
#include "tst_facemodel.moc"
