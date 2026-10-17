// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
// The documentation PDF (QPdfWriter): embedded fonts, pages, header, text, Letter and A4; the text export.
#include "TestSupport.h"
#include "Inspector.h"
#include "Machine.h"
#include "PdfExport.h"
#include "ProgramAnalysis.h"
#include "ProgramDocument.h"
#include <QProcess>
#include <QTemporaryDir>

using namespace vc;

class TstPdf : public QObject {
    Q_OBJECT
    QTemporaryDir dir_;
    static QString run(const QString &prog, const QStringList &args) {
        QProcess p; p.start(prog, args); p.waitForFinished(30000); return QString::fromUtf8(p.readAllStandardOutput());
    }
    ProgramDocumentContent content(Machine &m, const QString &memfile, ProgramDocument d = {}) {
        QFile f(QString(VC_FIXTURES_DIR) + "/" + memfile); f.open(QIODevice::ReadOnly);
        m.putMemory(f.readAll());
        auto s = inspect(m.calc, true);
        d.model = ProgramDocumentInfo::modelNameFor(m.variant(), m.model());
        d.prefill(ProgramAnalysis(s->program, s->model));
        ProgramDocumentInfo info; info.modelName = d.model; info.firmware = "test.bin"; info.memoryFile = memfile; info.model = s->model;
        return ProgramDocumentContent(d, info, s->program, s->pool, s->setFlags());
    }
private slots:
    void financePacPdf_data() { QTest::addColumn<int>("paper"); QTest::newRow("A4") << int(Paper::A4); QTest::newRow("Letter") << int(Paper::Letter); }
    void financePacPdf() {
        QFETCH(int, paper);
        auto fw = vct::firmware15(); VC_REQUIRE(fw, "15C");
        Machine m; m.loadFirmware(fw); m.boot(nullptr);
        m.switchModel(152);
        ProgramDocument d;
        auto c = content(m, "FinancePac.15c", d);
        QByteArray pdf = renderProgramDocumentPdf(c, Paper(paper), PdfFonts::fromResources());
        QVERIFY(pdf.startsWith("%PDF"));
        const QString path = dir_.path() + QString("/f%1.pdf").arg(paper);
        if (qEnvironmentVariableIsSet("VC_SHOTS")) QFile::remove(qEnvironmentVariable("VC_SHOTS") + QString("/doc-%1.pdf").arg(paper));
        QFile f(path); f.open(QIODevice::WriteOnly); f.write(pdf); f.close();
        if (qEnvironmentVariableIsSet("VC_SHOTS")) QFile::copy(path, qEnvironmentVariable("VC_SHOTS") + QString("/doc-%1.pdf").arg(paper));
        const QString info = run("pdfinfo", {path});
        QRegularExpression re("Pages:\\s+(\\d+)");
        int pages = re.match(info).captured(1).toInt();
        QVERIFY2(pages >= 3 && pages <= 14, qPrintable(info));
        QVERIFY2(info.contains(paper == int(Paper::A4) ? "595" : "612"), qPrintable(info));
        const QString fonts = run("pdffonts", {path});
        QVERIFY2(fonts.contains("HP15C") || fonts.contains("Simulator"), qPrintable(fonts));
        QVERIFY2(fonts.contains("HP15CSimulatorDisplay") && fonts.contains("HP15CSimulatorKeys"), qPrintable(fonts));
        for (const QString &l : fonts.split('\n')) if (l.contains("HP15CSimulator")) QVERIFY2(l.contains(QRegularExpression("\\s+yes\\s+(yes|no)\\s+yes")), qPrintable(l));
        const QString text = run("pdftotext", {"-layout", path, "-"});
        QVERIFY(text.contains("Page 1 of " + QString::number(pages)));
        QVERIFY(text.contains(QString("Page %1 of %1").arg(pages)));
        QVERIFY(text.contains("HP-15C"));
        QVERIFY(!text.contains("©"));
    }
    void plainTextExport() {
        auto fw = vct::firmware15(); VC_REQUIRE(fw, "15C");
        Machine m; m.loadFirmware(fw); m.boot(nullptr); m.switchModel(152);
        auto c = content(m, "FinancePac.15c");
        const QString t = c.plainText();
        QVERIFY(t.size() > 200);
        QVERIFY(t.contains("001"));
    }
    void noFirmwareInPdf() {
        auto fw = vct::firmware15(); VC_REQUIRE(fw, "15C");
        Machine m; m.loadFirmware(fw); m.boot(nullptr); m.switchModel(152);
        auto c = content(m, "FinancePac.15c");
        QByteArray pdf = renderProgramDocumentPdf(c, Paper::A4, PdfFonts::fromResources());
        QVERIFY(pdf.size() < 800 * 1024);
        QVERIFY(!pdf.contains(fw.left(64)));
    }
};
QTEST_MAIN(TstPdf)
#include "tst_pdf.moc"
