// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
// The HP-15C Matrix Editor (0.7.1): number cells ("1,5", "1.5", "2E-3", thousands separators), tab-separated ranges as
// Excel / LibreOffice / Google Sheets put them on the clipboard, Copy as TSV -> Paste round trip (exact), matrix
// write / read in a memory image with the DIM bookkeeping, and the whole flow in the window: open, edit, paste,
// Apply into the emulator's memory, the firmware computing with it, Write to Calculator sending it (fake calculator).
#include "TestSupport.h"
#include "FakeCalculator.h"
#include "MainWindow.h"
#include "Matrix15.h"
#include "MatrixEditor.h"
#include "KeyClick.h"
#include "CalculatorWidget.h"
#include <QLineEdit>
#include <QSettings>
#include <QTemporaryDir>
#include <random>

using namespace vc;
using namespace vc::matrix15;

namespace {
template <typename F> bool waitFor(F cond, int ms = 15000) {
    QElapsedTimer t; t.start();
    while (!cond()) { if (t.elapsed() > ms) return false; QTest::qWait(10); }
    return true;
}
class Forward : public HidTransport {
    FakeCalculator *f_;
public:
    explicit Forward(FakeCalculator *f) : f_(f) {}
    void open() override { f_->open(); }
    void close() override { f_->close(); }
    void send(const Report &r) override { f_->send(r); }
    bool receive(int t, Report *o) override { return f_->receive(t, o); }
};
uint64_t num(const QString &t) { uint64_t v = 0; bool ok = parseCell(t, &v); if (!ok) qWarning("not a number: %s", qPrintable(t)); return v; }
QByteArray dump(const char *name) {
    QFile f(QStringLiteral(VC_FIXTURES_DIR "/pool15/") + QLatin1String(name));
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}
}

class TstMatrix : public QObject {
    Q_OBJECT
    QTemporaryDir data_, out_;
    QStringList errors, notices;
private slots:
    void keyClickSoundIsADeepShortThock() {
        auto s = KeyClick::samples();
        QVERIFY(s.size() > 1000 && s.size() < KeyClick::kRate / 4);          // under 250 ms
        int peak = 0; for (auto v : s) peak = std::max(peak, std::abs(int(v)));
        QVERIFY(peak > 3000 && peak < 32767);
        QVERIFY(std::abs(int(s.back())) < 200);                               // fades out, no pop
        int zc = 0; for (size_t i = 1; i < s.size(); ++i) zc += (s[i - 1] < 0) != (s[i] < 0);
        QVERIFY(double(zc) / 2 / (double(s.size()) / KeyClick::kRate) < 400); // deep: low fundamental
        QByteArray w = KeyClick::wav();
        QVERIFY(w.startsWith("RIFF")); QCOMPARE(w.mid(8, 4), QByteArray("WAVE"));
        QCOMPARE(w.size(), int(44 + 2 * s.size()));
        const int before = KeyClick::playCount(); KeyClick::play(); QCOMPARE(KeyClick::playCount(), before + 1);
    }
    void legendClicksDefaultAndLcdLift() {
        CalculatorWidget c;
        QVERIFY(c.legendClicks());
        c.setLegendClicks(false); QVERIFY(!c.legendClicks());
        QCOMPARE(CalculatorWidget::lcdLift(15), QColor(0x66, 0x66, 0x66));
        QCOMPARE(CalculatorWidget::lcdLift(16), QColor(0x66, 0x66, 0x66));
        QVERIFY(!CalculatorWidget::lcdLift(12).isValid());          // the 12C keeps its original LCD
    }
    void initTestCase() {
        qputenv("VOYAGERCALC_DATA_DIR", data_.path().toUtf8());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, out_.path());
    }
    void init() { errors.clear(); notices.clear(); }

    void cellsAcceptCommaPointAndExponents() {
        QCOMPARE(num("1,5"), num("1.5"));
        QCOMPARE(num("1.5"), uint64_t(0x01500000000000ull));
        QCOMPARE(num("2E-3"), uint64_t(0x02000000000997ull));
        QCOMPARE(num("2e-3"), num("0,002"));
        QCOMPARE(num("-1,25E+2"), num("-125"));
        QCOMPARE(num("1.234,5"), num("1234.5"));         // European thousands + decimal comma
        QCOMPARE(num("1,234.5"), num("1234.5"));         // English
        QCOMPARE(num("1,234,567"), num("1234567"));
        QCOMPARE(num("1 234,5"), num("1234.5"));
        QCOMPARE(num(QStringLiteral("\u22123")), num("-3"));   // typographic minus
        QCOMPARE(num(""), uint64_t(0));
        QCOMPARE(num("  7  "), num("7"));
        uint64_t v; QString why;
        QVERIFY(!parseCell("abc", &v, &why)); QVERIFY(!why.isEmpty());
        QVERIFY(!parseCell("3+4i", &v, &why)); QVERIFY(why.contains("complex"));
        QVERIFY(!parseCell("1e100", &v, &why)); QVERIFY(why.contains("large"));
        QVERIFY(!parseCell("1,2,3", &v));
        QVERIFY(!parseCell("1.2.3", &v));
        QCOMPARE(cellText(num("1,5")), QStringLiteral("1.5"));
        QCOMPARE(cellText(num("2E-3")), QStringLiteral("0.002"));
        QCOMPARE(cellText(num("1.23456789012")), QStringLiteral("1.23456789"));  // 10 digits, rounded
    }
    void tablesFromSpreadsheets() {
        // Excel (Windows): \r\n and a trailing line break
        Table t = parseTable("1\t2\t3\r\n4,5\t5\t6\r\n");
        QVERIFY(t.ok()); QCOMPARE(t.rows, 2); QCOMPARE(t.cols, 3); QCOMPARE(t.cell(1, 0), QStringLiteral("4,5")); QVERIFY(!t.ragged);
        // LibreOffice / Google Sheets: \n, no trailing break, a quoted cell
        t = parseTable("\"1.5\"\t2E-3\n-7\t8");
        QVERIFY(t.ok()); QCOMPARE(t.rows, 2); QCOMPARE(t.cols, 2); QCOMPARE(t.cell(0, 0), QStringLiteral("1.5"));
        QVERIFY(badCells(t).isEmpty());
        // a column
        t = parseTable("1\n2\n3\n");
        QCOMPARE(t.rows, 3); QCOMPARE(t.cols, 1);
        // ragged: missing cells become empty (= 0) and the table says so
        t = parseTable("1\t2\t3\n4\n");
        QCOMPARE(t.cols, 3); QVERIFY(t.ragged); QCOMPARE(t.cell(1, 2), QString());
        // a trailing tab on every line is not a column
        t = parseTable("1\t2\t\n3\t4\t\n");
        QCOMPARE(t.cols, 2);
        // semicolons (CSV from a European spreadsheet)
        t = parseTable("1,5;2\n3;4");
        QCOMPARE(t.rows, 2); QCOMPARE(t.cols, 2); QCOMPARE(t.cell(0, 0), QStringLiteral("1,5"));
        // bad cells and errors
        t = parseTable("1\tx\n3\t4");
        QCOMPARE(badCells(t).size(), 1); QVERIFY(badCells(t).first().startsWith("1,2"));
        QVERIFY(!parseTable("").ok());
        QVERIFY(!parseTable("\n\n").ok());
        QString big; for (int i = 0; i < 100; ++i) big += "1\n";
        QVERIFY(!parseTable(big).ok());
    }
    void copyThenPasteIsExact() {
        std::mt19937 rng(42);
        for (int round = 0; round < 200; ++round) {
            int r = 1 + int(rng() % 8), c = 1 + int(rng() % 8);
            std::vector<uint64_t> raw;
            for (int i = 0; i < r * c; ++i) {
                uint64_t v = rng() % 2 ? 9 : 0; v = v << 4 | (1 + rng() % 9);
                for (int k = 0; k < 9; ++k) v = v << 4 | (rng() % 10);
                int e = int(rng() % 199) - 99, ee = e < 0 ? 1000 + e : e;
                v = v << 12 | uint64_t(ee / 100) << 8 | uint64_t(ee / 10 % 10) << 4 | uint64_t(ee % 10);
                if (rng() % 10 == 0) v = 0;
                raw.push_back(v);
            }
            Table t = parseTable(toTsv(r, c, raw));
            QVERIFY(t.ok()); QCOMPARE(t.rows, r); QCOMPARE(t.cols, c);
            for (int i = 0; i < r * c; ++i) QCOMPARE(num(t.cells[i]), raw[size_t(i)]);
        }
        // the inspector's own "Copy Matrix as TSV" text is the same text
        MatrixValue mv; mv.rows = 1; mv.cols = 2;
        RegisterValue a, b; a.text = cellText(num("1.5")); b.text = cellText(num("-2e-7"));
        mv.values = {a, b};
        QCOMPARE(mv.tsv(), toTsv(1, 2, {num("1.5"), num("-2e-7")}));
    }
    void writeAndReadInAnImage() {
        QByteArray img = dump("v150_00_baseline.bin");
        if (img.isEmpty()) QSKIP("no pool dumps");
        auto p0 = pool(img, 150); QVERIFY(p0); QCOMPARE(p0->free, 78);
        QCOMPARE(room(img, 150, 0), 78);
        std::vector<uint64_t> v = {num("1"), num("2,5"), num("-3E-4"), num("4"), num("5"), num("6")};
        QVERIFY(write(&img, 150, 2, 3, 2, v).isEmpty());
        Stored s; QVERIFY(read(img, 150, 2, &s));
        QCOMPARE(s.rows, 3); QCOMPARE(s.cols, 2); QCOMPARE(s.raw, v);
        QCOMPARE(pool(img, 150)->free, 72);
        auto after = poolAfter(img, 150, 0, 4, 4);
        QVERIFY(after); QCOMPARE(after->free, 72 - 16); QCOMPARE(pool(img, 150)->free, 72);   // preview only
        // too big: refused with Error 10 wording, image unchanged
        QByteArray before = img;
        std::vector<uint64_t> lots(81, num("1"));
        QString err = write(&img, 150, 0, 9, 9, lots);
        QVERIFY(err.contains("Error 10")); QCOMPARE(img, before);
        // wrong value count
        QVERIFY(!write(&img, 150, 0, 2, 2, {num("1")}).isEmpty());
        // 0 x 0 frees it
        QVERIFY(write(&img, 150, 2, 0, 0, {}).isEmpty());
        QCOMPARE(pool(img, 150)->free, 78);
    }

    // ---- the window --------------------------------------------------------------------------------------------
    void editorInTheWindowAndUsbWrite() {
        QByteArray fw = vct::firmware15(); VC_REQUIRE(fw, "15C");
        MainWindow w;
        w.prompts.error = [this](const QString &m) { errors << m; };
        w.prompts.notice = [this](const QString &m) { notices << m; };
        w.prompts.confirm = [](const PendingAction &) { return 1; };
        w.prompts.confirmWrite = [](const PendingWrite &) { return true; };
        QString clip = "2\t0\t1\n1,5\t3\t0\n0\t1\t4\n";     // a 3 x 3 range, decimal comma
        QString copied;
        int previews = 0;
        w.prompts.matrixHooks = [&](MatrixEditor::Hooks &h) {
            h.clipboardText = [&] { return clip; };
            h.confirmPaste = [&](const Table &, const QStringList &) { ++previews; return true; };
            h.copyText = [&](const QString &t) { copied = t; };
        };
        w.usbFast = true; w.usbSettleMs = 0;
        w.resize(900, 560); w.show();
        w.startFirmware(fw, "t.bin", false);
        QVERIFY(waitFor([&] { return w.isRunning(); }));
        QTest::qWait(300);
        w.openMatrixEditor();
        MatrixEditor *ed = w.matrixEditor();
        QVERIFY(ed); QVERIFY(ed->isVisible());
        QCOMPARE(ed->matrix(), 0);
        QCOMPARE(ed->rows() * ed->cols(), 0);
        QVERIFY(ed->available() > 9);
        // A: typed 2 x 2, Enter / Tab navigation
        ed->setSize(2, 2);
        ed->grid()->setCurrentCell(0, 0);
        ed->activateWindow(); ed->grid()->setFocus();
        auto focus = [&]() -> QWidget * {      // the open cell editor (offscreen windows never get the focus), else the grid
            const QRect cur = ed->grid()->visualRect(ed->grid()->currentIndex());
            for (QLineEdit *e : ed->grid()->findChildren<QLineEdit *>()) if (!e->isHidden() && e->geometry().contains(cur.center())) return e;
            return ed->grid();
        };
        auto typeCell = [&](const char *t, Qt::Key after) { QWidget *f = focus(); QString s = QString::fromLatin1(t);
            if (f == ed->grid()) { QTest::keyClicks(f, s.left(1)); s = s.mid(1); }   // a key on the grid opens the editor; after Tab one is already open
            QTest::keyClicks(focus(), s); QTest::keyClick(focus(), after); };
        typeCell("1", Qt::Key_Tab);
        QCOMPARE(ed->grid()->currentColumn(), 1);
        typeCell("2", Qt::Key_Return);
        QCOMPARE(ed->grid()->currentRow(), 1);
        typeCell("4", Qt::Key_Return);
        QCOMPARE(ed->grid()->currentRow(), 0); QCOMPARE(ed->grid()->currentColumn(), 0);   // wrapped to the next column... then the first
        ed->setCell(1, 0, "3");
        QCOMPARE(ed->cell(0, 0), QStringLiteral("1")); QCOMPARE(ed->cell(0, 1), QStringLiteral("2")); QCOMPARE(ed->cell(1, 1), QStringLiteral("4"));
        QVERIFY(ed->applyNow());
        QVERIFY(waitFor([&] { return ed->status().contains("is now in the emulator's memory"); }));
        // B: pasted 3 x 3
        ed->setMatrix(1);
        ed->pasteMatrix();
        QCOMPARE(previews, 1);
        QCOMPARE(ed->rows(), 3); QCOMPARE(ed->cols(), 3);
        QCOMPARE(ed->cell(1, 0), QStringLiteral("1,5"));
        ed->copyTsv();
        QCOMPARE(copied, QStringLiteral("2\t0\t1\n1.5\t3\t0\n0\t1\t4"));
        QVERIFY(ed->applyNow());
        QVERIFY(waitFor([&] { return ed->status().contains("Matrix B (3 × 3)"); }));
        // a matrix that does not fit is refused before anything is written
        ed->setMatrix(2);
        ed->setSize(9, 9);
        QVERIFY(!ed->fits());
        for (int r = 0; r < 9; ++r) for (int c = 0; c < 9; ++c) ed->setCell(r, c, "1");
        QVERIFY(!ed->applyNow()); QVERIFY(ed->status().contains("Error 10"));
        // bad cell refused
        ed->setSize(1, 2); ed->setCell(0, 0, "1"); ed->setCell(0, 1, "x");
        QVERIFY(!ed->applyNow()); QVERIFY(ed->status().contains("column 2"));
        // reopening A from memory shows what was applied
        ed->setMatrix(0);
        QCOMPARE(ed->rows(), 2); QCOMPARE(ed->cell(1, 0), QStringLiteral("3"));
        // the firmware computes with them: RCL MATRIX B, f MATRIX 9 (det) = 25.5
        auto key = [&](const char *k) { int c = Emulator::keyCode(15, k); QVERIFY2(c >= 0, k); w.press(c); QTest::qWait(80); w.release(c); QTest::qWait(120); };
        for (const char *k : {"RCL", "CHS", "e^x", "f", "CHS", "9"}) key(k);
        QVERIFY(waitFor([&] { return w.snapshot().text.contains("25.5"); }, 5000));
        // Write to Calculator sends the memory with both matrices
        QFile fx(QString(VC_FIXTURES_DIR) + "/Calc15-0-usb-read.15c"); QVERIFY(fx.open(QIODevice::ReadOnly));
        FakeCalculator fake(fx.readAll());
        w.makeTransport = [&] { return std::unique_ptr<HidTransport>(new Forward(&fake)); };
        w.writeToCalculator();
        QVERIFY(waitFor([&] { return !notices.isEmpty() || !errors.isEmpty(); }, 30000));
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
        Stored a, b;
        QVERIFY(read(fake.memory, 150, 0, &a)); QVERIFY(read(fake.memory, 150, 1, &b));
        // det leaves its LU decomposition in the RESULT matrix (A), as on the real calculator: A is 3 x 3 now
        QCOMPARE(b.rows, 3); QCOMPARE(b.cols, 3);
        QCOMPARE(b.raw, (std::vector<uint64_t>{num("2"), num("0"), num("1"), num("1.5"), num("3"), num("0"), num("0"), num("1"), num("4")}));
        QCOMPARE(a.rows, 3);
        // switching away from the 15C disables the editor (no switch here: just the menu state)
        QVERIFY(w.menu("Calculator"));
    }
};

QTEST_MAIN(TstMatrix)
#include "tst_matrix.moc"
