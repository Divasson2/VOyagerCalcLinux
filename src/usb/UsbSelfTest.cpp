// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
#include "UsbSelfTest.h"
#include "CalculatorLink.h"
#include "HidrawTransport.h"
#include "StepGuard.h"
#include "UsbLog.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSysInfo>
#include <QThread>
#include <cerrno>
#include <array>
#include <cstring>
#include <memory>
#include <mutex>
#include <fcntl.h>
#include <grp.h>
#include <poll.h>
#include <pwd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#if __has_include(<linux/hidraw.h>)
#include <linux/hidraw.h>
#define VC_HAVE_HIDRAW_H 1
#endif

namespace vc {
namespace {

/// A small sysfs file, read under a time limit (a sysfs read can block while the USB device is being reset). Empty when unreadable or stuck.
QString readLine(const QString &path) { QString t; guardedReadFile(path, 2000, &t); return t.trimmed(); }
QString err() { return QString::fromLocal8Bit(std::strerror(errno)); }

QString hexBytes(const uint8_t *p, size_t n) { return UsbLog::hex(p, n); }

/// Wait up to `ms` for input reports on fd; returns how many came, adds their bytes (length-byte framing) to `payload`.
int collect(int fd, int ms, QByteArray *payload, const std::function<void(const QString &)> &out, int showFirst = 4) {
    QElapsedTimer t; t.start();
    int count = 0;
    while (t.elapsed() < ms) {
        pollfd p{fd, POLLIN, 0};
        int r = ::poll(&p, 1, int(std::min<qint64>(250, ms - t.elapsed())));
        if (r <= 0) continue;
        uint8_t buf[256];
        ssize_t n = ::read(fd, buf, sizeof buf);
        if (n <= 0) { out(QStringLiteral("      read returned %1 (%2)").arg(n).arg(err())); break; }
        ++count;
        if (count <= showFirst) out(QStringLiteral("      report %1: %2 byte(s): %3").arg(count).arg(n).arg(hexBytes(buf, size_t(n))));
        const int len = buf[0];
        if (len > 0 && len < 64 && n >= len + 1) payload->append(reinterpret_cast<char *>(buf) + 1, len);
        if (payload->size() >= 2048) break;
    }
    return count;
}

/// A 65-byte hidraw write under a time limit.
StepOutcome guardedWrite(int fd, const uint8_t (&frame)[65]) {
    auto b = std::make_shared<std::array<uint8_t, 65>>();
    std::memcpy(b->data(), frame, 65);
    return runStep(QStringLiteral("write 65 bytes"), 6000, [fd, b] { return long(::write(fd, b->data(), b->size())); }, {}, false);
}

}  // namespace

int runUsbSelfTest(const std::function<void(const QString &)> &outRaw, const QString &sysfs, const QString &devRoot, const QString &usbSysfs, int readTimeoutMs) {
    // Every line is written (and flushed by the caller) the moment it happens; the helper threads of the guarded steps may print too, so one lock.
    std::mutex outLock;
    const std::function<void(const QString &)> out = [&](const QString &l) { std::lock_guard<std::mutex> g(outLock); outRaw(l); };
    UsbLog::setEcho([&](const QString &l) { out(QStringLiteral("      log: ") + l); });         // the steps of the program's own USB code, as they happen
    struct EchoOff { ~EchoOff() { UsbLog::setEcho({}); } } echoOff;
    out(QStringLiteral("VoyagerCalc USB self-test  (%1)").arg(QStringLiteral("version %1").arg(QStringLiteral(VOYAGERCALC_VERSION))));
    out(QStringLiteral("System: %1, kernel %2, %3").arg(QSysInfo::prettyProductName(), QSysInfo::kernelVersion(), QSysInfo::currentCpuArchitecture()));
    {
        QStringList groups;
        gid_t g[64]; int n = ::getgroups(64, g);
        for (int i = 0; i < n; ++i) if (group *gr = ::getgrgid(g[i])) groups << QString::fromLocal8Bit(gr->gr_name);
        passwd *pw = ::getpwuid(::getuid());
        out(QStringLiteral("User: %1 (uid %2), groups: %3").arg(pw ? QString::fromLocal8Bit(pw->pw_name) : QString(), QString::number(::getuid()), groups.join(QLatin1Char(' '))));
    }
    out(QString());

    // 1. USB bus: is the calculator visible to the computer at all?
    out(QStringLiteral("Every step is printed before it starts; a step that does not return within its limit is reported as STUCK."));
    out(QStringLiteral("Step 1: the USB bus (%1) ...").arg(usbSysfs));
    out(QStringLiteral("USB devices from Hewlett-Packard (vendor 03F0):"));
    bool usbSeen = false, usbAny = false;
    for (const QString &d : QDir(usbSysfs).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        const QString base = usbSysfs + QLatin1Char('/') + d;
        if (readLine(base + QStringLiteral("/idVendor")).toLower() != QLatin1String("03f0")) continue;
        usbAny = true;
        const QString pid = readLine(base + QStringLiteral("/idProduct")).toLower();
        if (pid == QLatin1String("1341")) usbSeen = true;
        out(QStringLiteral("  %1: 03F0:%2 \"%3\"%4").arg(d, pid.toUpper(), readLine(base + QStringLiteral("/product")),
                                                       pid == QLatin1String("1341") ? QStringLiteral("   <- the calculator in USB mode") : QString()));
    }
    if (!usbAny) out(QStringLiteral("  none. The calculator is not connected to this computer (in a virtual machine: connect the USB device to Linux, not to the host)."));
    out(QString());

    // 2. hidraw nodes
    out(QStringLiteral("Step 2: the hidraw devices (%1) ...").arg(sysfs));
    out(QStringLiteral("hidraw devices (%1):").arg(sysfs));
    QString calcNode;
    const QStringList nodes = QDir(sysfs).entryList({QStringLiteral("hidraw*")}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    if (nodes.isEmpty()) out(QStringLiteral("  none."));
    for (const QString &n : nodes) {
        QString hidId, name;
        QString ue;
        if (!guardedReadFile(sysfs + QLatin1Char('/') + n + QStringLiteral("/device/uevent"), 2000, &ue)) out(QStringLiteral("  %1: its uevent file cannot be read or did not answer within 2 s").arg(n));
        else
            for (const QString &l : ue.split(QLatin1Char('\n'))) {
                const QString s = l.trimmed();
                if (s.startsWith(QLatin1String("HID_ID="))) hidId = s.mid(7);
                if (s.startsWith(QLatin1String("HID_NAME="))) name = s.mid(9);
            }
        const QStringList parts = hidId.split(QLatin1Char(':'));
        const QString vid = parts.size() == 3 ? parts[1].right(4).toUpper() : QStringLiteral("????"), pid = parts.size() == 3 ? parts[2].right(4).toUpper() : QStringLiteral("????");
        const QString node = devRoot + QLatin1Char('/') + n;
        struct stat st {};
        QString perm = QStringLiteral("(no device file)");
        if (::stat(node.toLocal8Bit().constData(), &st) == 0) {
            passwd *pw = ::getpwuid(st.st_uid); group *gr = ::getgrgid(st.st_gid);
            const bool rw = ::access(node.toLocal8Bit().constData(), R_OK | W_OK) == 0;
            perm = QStringLiteral("mode %1 %2:%3, you %4").arg(st.st_mode & 07777, 4, 8, QLatin1Char('0')).arg(pw ? QString::fromLocal8Bit(pw->pw_name) : QString::number(st.st_uid),
                       gr ? QString::fromLocal8Bit(gr->gr_name) : QString::number(st.st_gid), rw ? QStringLiteral("CAN read and write") : QStringLiteral("can NOT read and write (permission denied)"));
        }
        const bool isCalc = vid == QLatin1String("03F0") && pid == QLatin1String("1341");
        if (isCalc && calcNode.isEmpty()) calcNode = n;
        out(QStringLiteral("  %1: %2:%3 \"%4\"%5").arg(n, vid, pid, name, isCalc ? QStringLiteral("   <- the calculator") : QString()));
        out(QStringLiteral("      %1").arg(perm));
        const QStringList h = nodeHolders(node);
        if (!h.isEmpty()) out(QStringLiteral("      already open in: %1  (close that program first)").arg(h.join(QStringLiteral(", "))));
    }
    out(QString());

    if (calcNode.isEmpty()) {
        if (usbSeen) out(QStringLiteral("RESULT: the USB bus sees 03F0:1341 but Linux made no hidraw device for it. The kernel HID driver did not take it (try unplugging it and doing f ENTER ON again)."));
        else out(QStringLiteral("RESULT: No calculator found – press f ENTER ON. Turn the calculator off, hold f and ENTER, press ON, then run this test again."));
        return 2;
    }
    const QString path = devRoot + QLatin1Char('/') + calcNode;

    bool openStuck = false;
#ifdef VC_HAVE_HIDRAW_H
    {   // what the device says about itself
        out(QStringLiteral("Step 3: open %1 to read the device's own description (limit 6 s) ...").arg(path));
        int e = 0;
        const int fd = guardedOpen(path, 6000, &e);
        if (fd == -2) { openStuck = true; out(QStringLiteral("  STUCK: open() of %1 did not return within 6 s (see the log lines above for the thread state).").arg(path)); }
        else if (fd < 0) out(QStringLiteral("  cannot open: %1").arg(QString::fromLocal8Bit(std::strerror(e))));
        else {
            hidraw_devinfo di {};
            if (::ioctl(fd, HIDIOCGRAWINFO, &di) == 0) out(QStringLiteral("Device info: bus %1, vendor %2, product %3").arg(di.bustype).arg(di.vendor & 0xFFFF, 4, 16, QLatin1Char('0')).arg(di.product & 0xFFFF, 4, 16, QLatin1Char('0')));
            int sz = 0;
            if (::ioctl(fd, HIDIOCGRDESCSIZE, &sz) == 0 && sz > 0 && sz <= HID_MAX_DESCRIPTOR_SIZE) {
                hidraw_report_descriptor rd {}; rd.size = unsigned(sz);
                if (::ioctl(fd, HIDIOCGRDESC, &rd) == 0) out(QStringLiteral("Report descriptor (%1 bytes): %2").arg(sz).arg(hexBytes(rd.value, size_t(sz))));
            }
            ::close(fd);
            out(QStringLiteral("  open and close worked."));
        }
    }
#endif

    // 3. the read, the way the program does it
    out(QStringLiteral("Step 4, Test A: read the memory the way VoyagerCalc does (open %1, send FE, wait %2 ms) ...").arg(path).arg(readTimeoutMs));
    UsbLog::write(QStringLiteral("selftest: test A on %1").arg(path));
    bool ok = false; int silent = 0;
    {
        HidrawTransport t; t.setPath(path);
        CalculatorLink link(&t);
        link.readTimeoutMs = readTimeoutMs;
        try {
            QByteArray d = link.readMemory();
            ok = d.size() == 2048;
            out(QStringLiteral("  OK: %1 bytes received (SHA-256 of the image %2)").arg(d.size()).arg(QString::fromLatin1(QCryptographicHash::hash(d, QCryptographicHash::Sha256).toHex().left(16))));
        } catch (const LinkError &e) {
            out(QStringLiteral("  FAILED: %1").arg(e.message()));
            if (e.kind == LinkError::Timeout && e.a == 0) silent = 1;
            if (e.kind == LinkError::Stuck) openStuck = true;
        } catch (const Error &e) { out(QStringLiteral("  FAILED: %1").arg(e.message())); }
    }
    if (ok) { out(QString()); out(QStringLiteral("RESULT: the calculator answers; reading works on this computer.")); return 0; }

    if (openStuck) {
        out(QString());
        out(QStringLiteral("RESULT: STUCK. Linux itself does not answer when the calculator's device is opened or written (the call never returns), so the program cannot read it. "
                           "This is not a permission problem and not the calculator's memory. Turn the calculator off, disconnect it from the virtual machine (in your virtualization software, e.g. its USB devices menu), "
                           "connect it to Linux again, hold f and ENTER, press ON and run this test again. Please send this whole output and the output of:  sudo dmesg | tail -40"));
        return 1;
    }
    // 4. other ways to ask, to see which one the calculator answers
    out(QString());
    out(QStringLiteral("Test B: raw: open, send 65 bytes [00 FE ...], collect replies for %1 ms").arg(readTimeoutMs));
    int oe = 0;
    int fd = guardedOpen(path, 6000, &oe);
    if (fd == -2) { out(QStringLiteral("  STUCK: open() did not return within 6 s")); out(QString()); out(QStringLiteral("RESULT: STUCK (see above). Please send this whole output and:  sudo dmesg | tail -40")); return 1; }
    if (fd < 0) { errno = oe; out(QStringLiteral("  cannot open %1: %2").arg(path, err())); out(QString()); out(QStringLiteral("RESULT: the device exists but cannot be opened: %1. If this says Permission denied, use Calculator → USB → Fix USB Permissions in the program.").arg(err())); return 1; }
    QByteArray payload;
    {
        uint8_t w[65] = {0}; w[1] = 0xFE;
        const StepOutcome wo = guardedWrite(fd, w);
        if (!wo.finished) { out(QStringLiteral("  STUCK: write() did not return within 6 s")); out(QString()); out(QStringLiteral("RESULT: STUCK (see above). Please send this whole output and:  sudo dmesg | tail -40")); return 1; }
        const long n = wo.value;
        out(QStringLiteral("  write returned %1%2").arg(n).arg(n < 0 ? QStringLiteral(" (%1)").arg(wo.why()) : QString()));
        int c = collect(fd, readTimeoutMs, &payload, out);
        out(QStringLiteral("  %1 report(s), %2 payload byte(s)").arg(c).arg(payload.size()));
        if (payload.size() >= 2048) { ::close(fd); out(QString()); out(QStringLiteral("RESULT: this way works (test B) although test A did not: please send this output, it shows the bug.")); return 1; }
    }
    out(QStringLiteral("Test C: send the same request again and wait 3 more seconds"));
    {
        uint8_t w[65] = {0}; w[1] = 0xFE;
        const StepOutcome wo = guardedWrite(fd, w);
        out(wo.finished ? QStringLiteral("  write returned %1").arg(wo.value) : QStringLiteral("  STUCK: write() did not return within 6 s"));
        QByteArray p2; int c = collect(fd, 3000, &p2, out);
        out(QStringLiteral("  %1 report(s), %2 payload byte(s)").arg(c).arg(p2.size()));
    }
#if defined(VC_HAVE_HIDRAW_H) && defined(HIDIOCGINPUT)
    out(QStringLiteral("Test D: ask for an input report with GET_REPORT (the way the Mac app polls)"));
    {
        uint8_t w[65] = {0}; w[1] = 0xFE;
        (void)guardedWrite(fd, w);
        QThread::msleep(200);
        auto b = std::make_shared<std::array<uint8_t, 65>>(); b->fill(0);
        const StepOutcome io = runStep(QStringLiteral("GET_REPORT ioctl"), 6000, [fd, b] { return long(::ioctl(fd, HIDIOCGINPUT(65), b->data())); }, {}, false);
        if (!io.finished) out(QStringLiteral("  STUCK: the ioctl did not return within 6 s"));
        else out(QStringLiteral("  ioctl returned %1%2").arg(io.value).arg(io.value < 0 ? QStringLiteral(" (%1)").arg(io.why()) : QStringLiteral(": ") + hexBytes(b->data(), size_t(io.value > 0 ? io.value : 0))));
    }
#endif
    ::close(fd);
    out(QString());
    if (silent) out(QStringLiteral("RESULT: the calculator was found and opened (permissions are fine) but sends NOTHING back. Make sure it is in USB mode right now (off, hold f and ENTER, press ON; the display stays blank) and that no other program has it open (see above). Please send this whole output."));
    else out(QStringLiteral("RESULT: reading failed (see test A). Please send this whole output."));
    return 1;
}

}  // namespace vc
