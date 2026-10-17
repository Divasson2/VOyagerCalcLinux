// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
#include "UsbPermissions.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryFile>

namespace vc {

/// share/usb-access.sh next to the program (installed layout <app>/bin/voyagercalc, <app>/share/usb-access.sh); empty in a developer build.
QString accessScriptPath() {
    const QString p = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("../share/usb-access.sh"));
    return QFileInfo::exists(p) ? QDir::cleanPath(p) : QString();
}

QString udevRule() {
    return QStringLiteral("# VoyagerCalc: HP 12C / 15C / 16C CE in USB memory mode (f-ENTER-ON), USB 03F0:1341.\n"
                          "# Access for the logged-in desktop user (uaccess) and for the plugdev group.\n"
                          "KERNEL==\"hidraw*\", ATTRS{idVendor}==\"03f0\", ATTRS{idProduct}==\"1341\", MODE=\"0660\", GROUP=\"plugdev\", TAG+=\"uaccess\"\n");
}

QString fixScript() {
    QString rule = udevRule();
    rule.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QStringLiteral(
        "#!/bin/sh\n"
        "# VoyagerCalc: let the logged-in user use the calculator's USB connection. Run as root (pkexec).\n"
        "set -e\n"
        "RULES_DIR=\"${VC_RULES_DIR:-/etc/udev/rules.d}\"\n"
        "USER_NAME=\"$1\"\n"
        "DEV=\"$2\"\n"
        "[ -n \"$USER_NAME\" ] || { [ -n \"$PKEXEC_UID\" ] && USER_NAME=\"$(id -nu \"$PKEXEC_UID\")\"; }\n"
        "mkdir -p \"$RULES_DIR\"\n"
        "cat > \"$RULES_DIR/99-voyagercalc.rules\" <<'RULE_EOF'\n"
        "%1"
        "RULE_EOF\n"
        "chmod 0644 \"$RULES_DIR/99-voyagercalc.rules\"\n"
        "if [ -z \"$VC_SKIP_SYSTEM\" ]; then\n"
        "  getent group plugdev >/dev/null || groupadd plugdev || true\n"
        "  if [ -n \"$USER_NAME\" ] && ! id -nG \"$USER_NAME\" | tr ' ' '\n' | grep -qx plugdev; then usermod -aG plugdev \"$USER_NAME\" || true; fi\n"
        "  udevadm control --reload-rules || true\n"
        "  udevadm trigger --subsystem-match=hidraw --action=change || true\n"
        "  # a calculator that is already connected: give access now, no reconnect needed\n"
        "  if [ -n \"$DEV\" ] && [ -e \"$DEV\" ]; then\n"
        "    chgrp plugdev \"$DEV\" 2>/dev/null || true; chmod 0660 \"$DEV\" || true\n"
        "    if command -v setfacl >/dev/null 2>&1 && [ -n \"$USER_NAME\" ]; then setfacl -m \"u:$USER_NAME:rw\" \"$DEV\" || true; fi\n"
        "  fi\n"
        "fi\n"
        "echo done\n").arg(udevRule());
}

QString manualCommand() {
    const QString script = accessScriptPath();
    if (!script.isEmpty()) return QStringLiteral("sh '%1'").arg(script);
    return QStringLiteral("sudo sh -c 'printf \"%s\\n\" '\"'\"'KERNEL==\"hidraw*\", ATTRS{idVendor}==\"03f0\", ATTRS{idProduct}==\"1341\", MODE=\"0660\", GROUP=\"plugdev\", TAG+=\"uaccess\"'\"'\"' "
                          "> /etc/udev/rules.d/99-voyagercalc.rules && udevadm control --reload-rules && udevadm trigger --subsystem-match=hidraw --action=change'");
}

FixResult fixPermissions(const QString &device) {
    FixResult r;
    // The installed package has a script that tries pkexec (only with a running polkit agent, with a time limit), then sudo,
    // then su, in a terminal window if needed, and always ends with a clear result line (also logged).
    const QString script = accessScriptPath();
    if (!script.isEmpty()) {
        QProcess p;
        p.setProcessChannelMode(QProcess::MergedChannels);
        p.start(QStringLiteral("/bin/sh"), {script, QStringLiteral("--gui")});
        if (!p.waitForStarted(5000)) { r.message = QStringLiteral("Could not start the permission helper."); return r; }
        if (!p.waitForFinished(11 * 60 * 1000)) { p.kill(); r.message = QStringLiteral("The permission helper did not finish."); return r; }
        const QString out = QString::fromUtf8(p.readAll()).trimmed();
        r.ok = p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
        r.noPkexec = !r.ok && p.exitCode() == 3;
        if (!r.ok) {
            const QStringList lines = out.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            r.message = lines.isEmpty() ? QStringLiteral("USB permissions were not installed.") : lines.mid(qMax(0, lines.size() - 3)).join(QLatin1Char('\n'));
        }
        return r;
    }
    // developer build: pkexec directly, with a time limit
    const QString pk = QStandardPaths::findExecutable(QStringLiteral("pkexec"));
    if (pk.isEmpty()) {
        r.noPkexec = true;
        r.message = QStringLiteral("This computer has no graphical password prompt (pkexec). Open a terminal and run the command shown.");
        return r;
    }
    const QString user = qEnvironmentVariable("USER", qEnvironmentVariable("LOGNAME"));
    QProcess p;
    p.start(pk, {QStringLiteral("/bin/sh"), QStringLiteral("-c"), fixScript(), QStringLiteral("voyagercalc-fix"), user, device});
    if (!p.waitForFinished(120000)) { p.kill(); r.noPkexec = true; r.message = QStringLiteral("No password window answered."); return r; }
    r.ok = p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
    if (!r.ok) r.message = p.exitCode() == 126 ? QStringLiteral("The password prompt was cancelled; nothing was changed.")
                                               : QStringLiteral("The permission fix did not work (code %1).").arg(p.exitCode());
    return r;
}

}  // namespace vc
