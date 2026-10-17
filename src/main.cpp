// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
//
// main.cpp - VoyagerCalc for Linux. Normal start: open the window; it loads the remembered firmware.
// Developer options for offscreen checks: --firmware FILE, --press KEYS (names, e.g. "1 2 ENTER"), --screenshot OUT.png,
// --width N, --inspector, --model-menu.
#include "CalculatorWidget.h"
#include "InspectorPanel.h"
#include "MainWindow.h"
#include "usb/HidrawTransport.h"
#include "usb/UsbPermissions.h"
#include "usb/UsbLog.h"
#include "usb/UsbSelfTest.h"
#include <QApplication>
#include <QCoreApplication>
#include <QIcon>
#include <cstdio>
#include <cstring>
#include <QCommandLineParser>
#include <QFile>
#include <QTimer>

int main(int argc, char **argv) {
    // install.sh: allow USB access (one graphical password prompt), no window
    if (argc > 1 && std::strcmp(argv[1], "--fix-usb-permissions") == 0) {
        QCoreApplication core(argc, argv);
        vc::FixResult r = vc::fixPermissions(vc::HidrawTransport::findDevice());
        if (r.ok) { std::puts("USB access for the calculator is set up."); return 0; }
        if (r.noPkexec) {
            std::printf("USB permissions were NOT installed. Paste this one line in a terminal (it asks for the password there):\n\n%s\n", vc::manualCommand().toUtf8().constData());
            return 3;
        }
        std::printf("%s\n", r.message.toUtf8().constData());
        return 2;
    }
    // voyagercalc --usb-selftest: what USB devices there are, whether the calculator answers, and why not (no window, plain text)
    if (argc > 1 && std::strcmp(argv[1], "--usb-selftest") == 0) {
        QCoreApplication core(argc, argv);
        const int rc = vc::runUsbSelfTest([](const QString &l) { std::puts(l.toUtf8().constData()); std::fflush(stdout); });
        std::printf("\n(The USB log is also in %s)\n", vc::UsbLog::filePath().toUtf8().constData());
        return rc;
    }
    if (argc > 1 && std::strcmp(argv[1], "--print-udev-rule") == 0) { std::fputs(vc::udevRule().toUtf8().constData(), stdout); return 0; }
    if (argc > 1 && std::strcmp(argv[1], "--version") == 0) { std::printf("VoyagerCalc %s\n", VOYAGERCALC_VERSION); return 0; }
    QApplication app(argc, argv);
    Q_INIT_RESOURCE(voyagercalc);
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/voyagercalc.png")));
    app.setOrganizationName(QStringLiteral("VoyagerCalc"));
    app.setApplicationName(QStringLiteral("VoyagerCalc"));
    app.setApplicationDisplayName(QStringLiteral("VoyagerCalc"));
    app.setDesktopFileName(QStringLiteral("voyagercalc"));
    qRegisterMetaType<vc::DisplaySnapshot>();
    qRegisterMetaType<vc::SavedState>();

    QCommandLineParser cli;
    cli.addHelpOption();
    cli.addPositionalArgument(QStringLiteral("file"), QStringLiteral("a firmware (.bin) or memory file to open"), QStringLiteral("[file]"));
    QCommandLineOption fw(QStringLiteral("firmware"), QStringLiteral("firmware file to run"), QStringLiteral("file"));
    QCommandLineOption press(QStringLiteral("press"), QStringLiteral("keys to press after the start, by name"), QStringLiteral("keys"));
    QCommandLineOption shot(QStringLiteral("screenshot"), QStringLiteral("save a picture of the window and quit"), QStringLiteral("png"));
    QCommandLineOption width(QStringLiteral("width"), QStringLiteral("window width"), QStringLiteral("pixels"));
    QCommandLineOption insp(QStringLiteral("inspector"), QStringLiteral("show the Registers & Program flyout"));
    QCommandLineOption mem(QStringLiteral("memory"), QStringLiteral("memory file to load (with --firmware)"), QStringLiteral("file"));
    cli.addOptions({fw, press, shot, width, insp, mem});
    cli.process(app);

    vc::MainWindow w;
    if (cli.isSet(width)) w.resize(cli.value(width).toInt(), int(cli.value(width).toInt() * 0.62));
    if (cli.isSet(insp)) w.setShowInspector(true);
    w.show();
    if (cli.isSet(fw)) w.openFirmwareFile(cli.value(fw));
    else if (!cli.positionalArguments().isEmpty()) {
        // a firmware dropped on the program icon, else a memory file once the firmware runs
        const QString p = cli.positionalArguments().first();
        QTimer::singleShot(0, &w, [&w, p] { w.launch(); if (p.endsWith(QStringLiteral(".bin"), Qt::CaseInsensitive)) w.openFirmwareFile(p); });
    } else QTimer::singleShot(0, &w, [&w] { w.launch(); });
    if (cli.isSet(shot)) {
        // wait for the boot, press the keys, let the display settle, save the picture
        auto *poll = new QTimer(&w);
        auto state = std::make_shared<int>(0);
        QObject::connect(poll, &QTimer::timeout, &w, [&, poll, state] {
            if (*state == 0 && w.isRunning()) {
                *state = 1;
                if (cli.isSet(mem)) { QFile f(cli.value(mem)); if (f.open(QIODevice::ReadOnly)) w.worker()->submit(&w, [d = f.readAll()](vc::Machine &m) { m.putMemory(d); }, [](const QString &) {}); }
                const QStringList keys = cli.value(press).split(' ', Qt::SkipEmptyParts);
                for (const QString &k : keys) {
                    int code = vc::Emulator::keyCode(w.snapshot().model, k.toLatin1().constData());
                    if (code < 0) continue;
                    w.worker()->post([ww = w.worker(), code] { ww->pressKey(code); ww->runFast(150); ww->releaseKey(code); ww->runFast(150); });
                }
                QTimer::singleShot(1500, &w, [&, poll] {
                    w.grab().save(cli.value(shot));
                    app.quit();
                });
            }
        });
        poll->start(100);
        QTimer::singleShot(60000, &app, &QCoreApplication::quit);
    }
    return app.exec();
}
