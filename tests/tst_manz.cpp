// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
// 0.7.2: Torsten Manz HP-15C Simulator program files (.15c / .txt): reading, writing, keys to press, keying a program into
// the emulated 15C, and the File menu items. The round trip over the simulator's 100 example programs runs when
// MANZ_PROGRAMS points at their folder (they are not part of this repository); keying in needs VOYAGERCALC_FIRMWARE_DIR.
#include "TestSupport.h"
#include "Manz15C.h"
#include "ProgramEntry15.h"
#include "Machine.h"
#include <QDirIterator>
#include <QRegularExpression>

using namespace vc;

namespace {
const char *kSample =
    "# ------------------------------------------------------------------------------\n"
    "# HEWLETT·PACKARD 15C Simulator program\n"
    "# Created with version 4.6.00\n"
    "# ------------------------------------------------------------------------------\n"
    "#T:Mother's Kitchen\n#D:First line\n#D:Second line\n#L-1:Can area\n#R0:Radius\n#R13:Point three\n#RI:Index\n#F8:Complex\n"
    "# ------------------------------------------------------------------------------\n\n"
    "   000 {             } \n"
    "   001 {    42 21 11 } f LBL A\n"
    "   002 {       44  0 } STO 0\n"
    "   003 {       44 .3 } STO .3\n"
    "   004 {    44 24  u } STO (i)\n"
    "   005 {    43 30  5 } g TEST x=y\n"
    "   006 {          48 } .\n"
    "   007 {           7 } 7\n"
    "   008 {       22 ,1 } GTO ,1\n"
    "   009 {    45 16 11 } RCL MATRIX A\n"
    "   010 {       43 32 } g RTN\n\n"
    "# ------------------------------------------------------------------------------\n";
using T = HP15CToken;
Manz15CProgram parsed(const QString &s, Manz15CProgram::ReadError *e = nullptr) {
    auto p = Manz15CProgram::parse(s, e); return p ? *p : Manz15CProgram();
}
QByteArray utf16(const QString &s) { QByteArray d; d.append(reinterpret_cast<const char *>(s.utf16()), s.size() * 2); return d; }
QStringList manzFiles() {
    QStringList out;
    const QString dir = qEnvironmentVariable("MANZ_PROGRAMS");
    if (dir.isEmpty()) return out;
    QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) { QString f = it.next(); const QString x = QFileInfo(f).suffix().toLower(); if (x == "15c" || x == "txt") out << f; }
    out.sort();
    return out;
}
QByteArray readAll(const QString &p) { QFile f(p); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); }
}

class TstManz : public QObject {
    Q_OBJECT
private slots:
    void parsesStepsAndNotes() {
        Manz15CProgram::ReadError e;
        auto p = Manz15CProgram::parse(QString::fromUtf8(kSample), &e);
        QVERIFY2(p, qPrintable(e.message()));
        QCOMPARE(p->title, QStringLiteral("Mother's Kitchen"));
        QCOMPARE(p->description, QStringLiteral("First line\nSecond line"));
        QCOMPARE(int(p->steps.size()), 10);
        QVERIFY((p->steps[0].tokens == HP15CTokens{T::key(42), T::key(21), T::key(11)}));
        QVERIFY((p->steps[2].tokens == HP15CTokens{T::key(44), T::dot(3)}));
        QVERIFY((p->steps[3].tokens == HP15CTokens{T::key(44), T::key(24), T::user()}));
        QVERIFY((p->steps[7].tokens == HP15CTokens{T::key(22), T::dot(1)}));
        QCOMPARE(*p->steps[3].mnemonic(), QStringLiteral("STO (i)"));
        QCOMPARE(*p->steps[4].mnemonic(), QString::fromUtf8("g TEST x=y"));
        QStringList keys; for (auto &n : p->labelNotes) keys << n.key;
        QCOMPARE(keys, (QStringList{"L-1", "R0", "R13", "RI", "F8"}));
    }
    void notesGoToTheProgramDocument() {
        auto p = parsed(QString::fromUtf8(kSample));
        ProgramDocument d; p.apply(d);
        QCOMPARE(d.title, QStringLiteral("Mother's Kitchen"));
        QCOMPARE(d.labels["A"], QStringLiteral("Can area"));
        QCOMPARE(d.registers["R0"].meaning, QStringLiteral("Radius"));
        QCOMPARE(d.registers["R.3"].meaning, QStringLiteral("Point three"));
        QCOMPARE(d.registers["I"].meaning, QStringLiteral("Index"));
        QCOMPARE(d.flags["8"], QStringLiteral("Complex"));
        QStringList keys; for (auto &n : Manz15CProgram::notes(d)) keys << n.key;
        QCOMPARE(keys, (QStringList{"L-1", "R0", "R13", "RI", "F8"}));
        d.labels["C"] = "c"; d.labels[".2"] = "p2"; d.labels["3"] = "three";
        keys.clear(); for (auto &n : Manz15CProgram::notes(d)) keys << n.key;
        QCOMPARE(keys, (QStringList{"L-1", "L-3", "L3", "L12", "R0", "R13", "RI", "F8"}));
    }
    void exportLayout() {
        auto p = parsed(QString::fromUtf8(kSample));
        const QByteArray data = p.fileData();
        QCOMPARE(data.left(4), QByteArray("\xFF\xFE\x23\x00", 4));
        const QString text = *Manz15CProgram::decodeText(data);
        const QStringList lines = text.split(QStringLiteral("\r\n"));
        const QString rule = QStringLiteral("# ") + QString(78, '-');
        QCOMPARE(lines[0], rule);
        QCOMPARE(lines[1], QString::fromUtf8("# HEWLETT·PACKARD 15C Simulator program"));
        QVERIFY(lines.contains("   000 {             } "));
        QVERIFY(lines.contains("   001 {    42 21 11 } f LBL A"));
        QVERIFY(lines.contains("   002 {       44  0 } STO 0"));
        QVERIFY(lines.contains("   003 {       44 .3 } STO .3"));
        QVERIFY(lines.contains("   004 {    44 24  u } STO (i)"));
        QVERIFY(lines.contains("   006 {          48 } ."));
        QCOMPARE(lines.last(), rule);
        QVERIFY(!text.endsWith("\r\n"));
        QVERIFY(!text.contains(QRegularExpression("[^\r]\n")));
        auto again = Manz15CProgram::read(data);
        QVERIFY(again);
        QVERIFY(*again == p);
    }
    void encodings() {
        const QString body = QString::fromUtf8("{ 42 21 11 }\n{ 2 }\n#T:Café\n");
        QCOMPARE(Manz15CProgram::read(body.toUtf8())->title, QString::fromUtf8("Café"));
        QCOMPARE(int(Manz15CProgram::read(QByteArray("\xEF\xBB\xBF") + body.toUtf8())->steps.size()), 2);
        QByteArray cp = body.toLatin1();                                 // é is the same byte in cp1252
        QCOMPARE(Manz15CProgram::read(cp)->title, QString::fromUtf8("Café"));
        QCOMPARE(Manz15CProgram::read(QByteArray("\x80{1}\n", 5))->steps.size(), size_t(1));   // cp1252 € in a comment-free line
        auto u = Manz15CProgram::read(QByteArray("\xFF\xFE", 2) + utf16(body));
        QVERIFY(u);
        QCOMPARE(u->steps[0].keycodeText(), QStringLiteral("42 21 11"));
        QCOMPARE(u->steps[1].keycodeText(), QStringLiteral(" 2"));
    }
    void oldPointKeycodeAndBareBraces() {
        auto p = parsed("{45 48 3}\n{48}\n{1}");
        QCOMPARE(int(p.steps.size()), 3);
        QVERIFY((p.steps[0].tokens == HP15CTokens{T::key(45), T::dot(3)}));
        QVERIFY((p.steps[1].tokens == HP15CTokens{T::key(48)}));
        QVERIFY((p.steps[2].tokens == HP15CTokens{T::key(1)}));
    }
    void errorsNameTheLine() {
        Manz15CProgram::ReadError e;
        QVERIFY(!Manz15CProgram::parse("{ 42 21 11 }\n{ 42 21 99 }", &e));
        QVERIFY((e == Manz15CProgram::ReadError{Manz15CProgram::ReadError::UnknownStep, 2, "42 21 99"}));
        QVERIFY(!Manz15CProgram::parse("{ 2 }\nhello", &e));
        QVERIFY((e == Manz15CProgram::ReadError{Manz15CProgram::ReadError::InvalidLine, 2, "hello"}));
        QVERIFY(!Manz15CProgram::parse("{ 22 16 }"));
        QVERIFY(!Manz15CProgram::parse("{ 43 31 }"));
        QVERIFY(!Manz15CProgram::parse("{ 44 0 u }"));
        QStringList big; for (int i = 0; i < 1000; ++i) big << "{ 1 }";
        QVERIFY(!Manz15CProgram::parse(big.join('\n'), &e));
        QCOMPARE(e.kind, Manz15CProgram::ReadError::TooManySteps);
        QCOMPARE(e.line, 1000);
        QVERIFY(!Manz15CProgram::read(QByteArray("\xFF\xFE\x41", 3)));   // odd UTF-16
    }
    void keysToPress() {
        auto keys = [](const char *s) { return HP15CKeyEntry::keys(HP15CStep(*HP15CKeycodes::parseManz(s))); };
        QCOMPARE(*keys("42 21 11"), (std::vector<int>{31, 10, 0}));
        QCOMPARE(*keys("44 .3"), (std::vector<int>{33, 37, 28}));
        QCOMPARE(*keys("45 16 11"), (std::vector<int>{34, 31, 5, 0}));
        QCOMPARE(*keys("44 36"), (std::vector<int>{33, 31, 25}));
        QCOMPARE(*keys("43 30 0"), (std::vector<int>{32, 29, 36}));
        QCOMPARE(*keys("44 24 u"), (std::vector<int>{33, 13}));
        QCOMPARE(*keys("49"), (std::vector<int>{38}));
        QCOMPARE(*keys("40"), (std::vector<int>{39}));
        QCOMPARE(*keys("9"), (std::vector<int>{8}));
    }
    void listingKeysFromTheCalculator() {
        auto s = Manz15CProgram::stepsFromListing({"42,21,11", "44 .3", "45,10, .5", "43,30, 0", "6", "44 24"});
        QVERIFY(s);
        QStringList k; for (auto &x : *s) k << x.keycodeText();
        QCOMPARE(k, (QStringList{"42 21 11", "44 .3", "45 10 .5", "43 30  0", " 6", "44 24"}));
        std::vector<HP15CStep> imported{HP15CStep({T::key(44), T::key(24), T::user()})};
        auto s2 = Manz15CProgram::stepsFromListing({"44 24"}, &imported);
        QVERIFY(s2 && *s2 == imported);
    }
    /// Every example program of the simulator: read, write, read again; keycodes and step layout must match the original file.
    void roundTripOfSimulatorExamples() {
        const QStringList files = manzFiles();
        if (files.isEmpty()) QSKIP("MANZ_PROGRAMS not set");
        QStringList failures;
        for (const QString &f : files) {
            const QString name = QFileInfo(f).fileName();
            const QByteArray data = readAll(f);
            Manz15CProgram::ReadError e;
            auto p = Manz15CProgram::read(data, &e);
            if (!p) { failures << name + ": " + e.message(); continue; }
            const QString text = *Manz15CProgram::decodeText(data);
            QStringList original, origLines;
            for (const QString &raw : text.split(QRegularExpression("[\r\n]+"), Qt::SkipEmptyParts)) {
                const QString l = raw.trimmed();
                if (raw.contains('{') && !raw.startsWith('#')) origLines << raw.left(raw.indexOf('}')).replace(',', '.');
                const int o = l.indexOf('{'), c = l.lastIndexOf('}');
                if (l.startsWith('#') || o < 0 || c < 0) continue;
                QString k = l.mid(o + 1, c - o - 1).split(' ', Qt::SkipEmptyParts).join(' ').replace(',', '.');
                if (!k.isEmpty()) original << k;
            }
            const QByteArray out = p->fileData();
            auto again = Manz15CProgram::read(out);
            QStringList problems;
            if (!again) { failures << name + ": written file unreadable"; continue; }
            QStringList written; for (auto &s : again->steps) written << s.keycodeText().split(' ', Qt::SkipEmptyParts).join(' ');
            if (written != original) problems << "keycodes differ";
            if (!(*again == *p)) problems << "re-read differs";
            for (size_t i = 0; i < p->steps.size(); ++i) if (!HP15CKeyEntry::keys(p->steps[i])) { problems << QStringLiteral("no keys for step %1").arg(i + 1); break; }
            QStringList newLines;
            for (const QString &l : Manz15CProgram::decodeText(out)->split("\r\n")) if (l.contains('{') && !l.startsWith('#')) newLines << l.left(l.indexOf('}'));
            if (origLines != newLines) problems << "step layout differs";
            if (!out.startsWith(QByteArray("\xFF\xFE#\x00", 4))) problems << "no UTF-16LE BOM";
            if (!problems.isEmpty()) failures << name + ": " + problems.join(", ");
        }
        qInfo("Manz round trip: %d/%d passed", int(files.size() - failures.size()), int(files.size()));
        for (auto &f : failures) qInfo("  FAIL %s", qPrintable(f));
        QVERIFY(files.size() > 0);
        QCOMPARE(failures, QStringList());
    }
    /// Key sample programs into the real 15C firmware; the calculator's listing must equal the file, and exporting
    /// from the listing gives the same steps back.
    void keyedIntoTheEmulatedCalculator() {
        QByteArray fw = vct::firmware15();
        VC_REQUIRE(fw, "HP-15C");
        Machine m; m.loadFirmware(fw); m.boot(nullptr);
        if (m.variant() != 152) m.switchVariant(152);                 // the most memory, so the bigger examples fit
        auto p = parsed(QString::fromUtf8(kSample));
        // the inline sample: GTO .1 needs LBL .1 only at run time, keying works
        ProgramEntryReport r = enterProgram15(m, p.steps);
        QCOMPARE(r.steps, 10);
        QVERIFY(r.mismatches.empty());
        auto s = inspect(m.calc, true);
        QVERIFY(s && s->hasProgram);
        std::vector<QString> keys; for (auto &l : s->program) keys.push_back(l.keys);
        auto back = Manz15CProgram::stepsFromListing(keys, &p.steps);
        QVERIFY(back && *back == p.steps);
        auto plain = Manz15CProgram::stepsFromListing(keys);
        QVERIFY(plain);
        QCOMPARE((*plain)[3].keycodeText(), QStringLiteral("44 24"));   // the u is not in the listing
        QVERIFY(!s->programMode);

        const QStringList files = manzFiles();
        if (files.isEmpty()) return;
        int keyed = 0, tooBig = 0; QStringList failures;
        const int limit = qEnvironmentVariableIntValue("MANZ_KEY_ALL") ? int(files.size()) : 12;
        for (int i = 0; i < files.size() && keyed + tooBig < limit; ++i) {
            auto q = Manz15CProgram::read(readAll(files[i]));
            if (!q) continue;
            try {
                auto rr = enterProgram15(m, q->steps);
                if (!rr.mismatches.empty()) failures << QFileInfo(files[i]).fileName() + QStringLiteral(": %1 mismatches, first %2").arg(rr.mismatches.size()).arg(rr.mismatches[0].step);
                ++keyed;
            } catch (const Error &e) {
                if (e.message().contains("room for at most") || e.message().contains("Error 4")) { ++tooBig; continue; }
                failures << QFileInfo(files[i]).fileName() + ": " + e.message();
            }
        }
        qInfo("Keyed into the emulated 15C: %d programs, %d too big for the memory, %d failures", keyed, tooBig, int(failures.size()));
        for (auto &f : failures) qInfo("  FAIL %s", qPrintable(f));
        QCOMPARE(failures, QStringList());
    }
    void tooBigIsRefusedAndMemoryKept() {
        QByteArray fw = vct::firmware15();
        VC_REQUIRE(fw, "HP-15C");
        Machine m; m.loadFirmware(fw); m.boot(nullptr);
        if (m.variant() != 150) m.switchVariant(150);
        const QByteArray before = m.memory(true);
        std::vector<HP15CStep> steps(600, HP15CStep({T::key(1)}));
        QVERIFY_THROWS_EXCEPTION(Error, enterProgram15(m, steps));
        QCOMPARE(m.memory(true).mid(Machine::kMemoryStart, Machine::kMemoryEnd - Machine::kMemoryStart),
                 before.mid(Machine::kMemoryStart, Machine::kMemoryEnd - Machine::kMemoryStart));
    }
};

QTEST_GUILESS_MAIN(TstManz)
#include "tst_manz.moc"
