// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
//
// UsbSelfTestPort.cpp - the USB self-test for platforms with a HID API (Windows): lists every HID device with VID:PID, name and report lengths, finds the
// calculator (03F0:1341, only after f ENTER ON), and tries the real read the way the program does it. Portable: tested on Linux with a fake backend.
#include "UsbSelfTest.h"
#include "CalculatorLink.h"
#include "PortTransport.h"
#include "StepGuard.h"
#include "UsbLog.h"
#include <QCryptographicHash>
#include <QSysInfo>
#include <mutex>

namespace vc {

int runHidSelfTest(const std::function<void(const QString &)> &outRaw, std::shared_ptr<HidBackend> backend, int readTimeoutMs) {
    std::mutex outLock;
    const std::function<void(const QString &)> out = [&](const QString &l) { std::lock_guard<std::mutex> g(outLock); outRaw(l); };
    UsbLog::setEcho([&](const QString &l) { out(QStringLiteral("      log: ") + l); });
    struct EchoOff { ~EchoOff() { UsbLog::setEcho({}); } } echoOff;
    out(QStringLiteral("VoyagerCalc USB self-test  (version %1)").arg(QStringLiteral(VOYAGERCALC_VERSION)));
    out(QStringLiteral("System: %1, kernel %2, %3").arg(QSysInfo::prettyProductName(), QSysInfo::kernelVersion(), QSysInfo::currentCpuArchitecture()));
    out(QStringLiteral("Every step is printed before it starts; a step that does not return within its limit is reported as STUCK."));
    out(QString());
    out(QStringLiteral("Step 1: the HID devices Windows knows about ..."));
    std::vector<HidDeviceInfo> list;
    {
        auto res = std::make_shared<std::vector<HidDeviceInfo>>();
        auto err = std::make_shared<QString>();
        StepOutcome o = runStep(QStringLiteral("list the HID devices"), 5000, [backend, res, err] {
            try { *res = backend->enumerate(); } catch (const Error &e) { *err = e.message(); return -1L; }
            return 0L;
        }, {}, false);
        if (!o.finished) { out(QStringLiteral("  STUCK: listing the devices did not return within 5 s.")); out(QString()); out(QStringLiteral("RESULT: STUCK. Windows does not answer when the USB devices are listed. Unplug the calculator, plug it in again and run this test again.")); return 1; }
        if (o.value < 0) { out(QStringLiteral("  FAILED: %1").arg(*err)); return 1; }
        list = *res;
    }
    bool seen = false;
    for (const HidDeviceInfo &d : list) {
        seen = seen || d.isCalculator();
        out(QStringLiteral("  %1:%2 \"%3\"  reports in/out %4/%5 bytes%6").arg(QString::number(d.vendor, 16).rightJustified(4, QLatin1Char('0')).toUpper(),
              QString::number(d.product, 16).rightJustified(4, QLatin1Char('0')).toUpper(), d.name).arg(d.inputReportBytes).arg(d.outputReportBytes)
              .arg(d.isCalculator() ? QStringLiteral("   <- the calculator") : QString()));
        out(QStringLiteral("      %1").arg(d.path));
    }
    if (list.empty()) out(QStringLiteral("  none."));
    out(QString());
    if (!seen) {
        out(QStringLiteral("RESULT: No calculator found – press f ENTER ON. Connect the calculator by USB, turn it off, hold f and ENTER, press ON (the display stays blank), then run this test again. "
                           "In a virtual machine (Parallels): connect the calculator to Windows, not to the Mac (Devices → USB & Bluetooth)."));
        return 2;
    }
    out(QStringLiteral("Step 2, Test A: read the memory the way VoyagerCalc does (send 00 FE, wait %1 ms) ...").arg(readTimeoutMs));
    bool ok = false, silent = false, stuck = false;
    {
        PortTransport t(backend);
        CalculatorLink link(&t);
        link.readTimeoutMs = readTimeoutMs;
        try {
            QByteArray d = link.readMemory();
            ok = d.size() == 2048;
            out(QStringLiteral("  OK: %1 bytes received (SHA-256 of the image %2)").arg(d.size()).arg(QString::fromLatin1(QCryptographicHash::hash(d, QCryptographicHash::Sha256).toHex().left(16))));
        } catch (const LinkError &e) {
            out(QStringLiteral("  FAILED: %1").arg(e.message()));
            silent = e.kind == LinkError::Timeout && e.a == 0;
            stuck = e.kind == LinkError::Stuck;
        } catch (const Error &e) { out(QStringLiteral("  FAILED: %1").arg(e.message())); }
    }
    out(QString());
    if (ok) { out(QStringLiteral("RESULT: the calculator answers; reading works on this computer.")); return 0; }
    if (stuck) out(QStringLiteral("RESULT: STUCK. Windows does not answer when the calculator is opened or written. Unplug it, plug it in again, do f ENTER ON and run this test again. Please send this whole output."));
    else if (silent) out(QStringLiteral("RESULT: the calculator was found and opened but sends NOTHING back. Make sure it is in USB mode right now (off, hold f and ENTER, press ON; the display stays blank) and that no other program has it open. Please send this whole output."));
    else out(QStringLiteral("RESULT: reading failed (see test A). Please send this whole output."));
    return 1;
}

}  // namespace vc

#ifdef _WIN32
#include "HidPort.h"
namespace vc { int runUsbSelfTest(const std::function<void(const QString &)> &out) { return runHidSelfTest(out, platformHidBackend()); } }
#endif
