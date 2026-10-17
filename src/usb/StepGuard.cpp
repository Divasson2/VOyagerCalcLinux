// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
#include "StepGuard.h"
#include "UsbLog.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <atomic>
#include <cerrno>
#include <condition_variable>
#include <cstring>
#ifndef _WIN32
#include <fcntl.h>
#endif
#include <memory>
#include <mutex>
#include <thread>
#ifndef _WIN32
#include <unistd.h>
#endif
#ifdef __linux__
#include <sys/syscall.h>
#endif

namespace vc {

QString StepOutcome::why() const { return QString::fromLocal8Bit(std::strerror(err)); }

namespace {
struct State {
    std::mutex m;
    std::condition_variable cv;
    bool done = false, abandoned = false;
    long value = 0; int err = 0;
    std::atomic<int> tid{0};
};
int thisTid() {
#ifdef __linux__
    return int(::syscall(SYS_gettid));
#else
    return 0;
#endif
}
QString slurp(const QString &path) { QFile f(path); return f.open(QIODevice::ReadOnly) ? QString::fromLocal8Bit(f.readAll()).trimmed() : QString(); }
}  // namespace

QString threadState(int tid) {
    if (tid <= 0) return QString();
    const QString base = QStringLiteral("/proc/self/task/%1/").arg(tid);
    const QString stat = slurp(base + QStringLiteral("stat"));
    if (stat.isEmpty()) return QString();
    const int close = stat.lastIndexOf(QLatin1Char(')'));
    const QString st = close >= 0 && close + 2 < stat.size() ? QString(stat.at(close + 2)) : QStringLiteral("?");
    const QString names = QStringLiteral("R running|S sleeping|D uninterruptible sleep (stuck in the kernel)|T stopped|Z zombie");
    QString desc = st;
    for (const QString &n : names.split(QLatin1Char('|'))) if (n.startsWith(st + QLatin1Char(' '))) desc = n;
    QString s = QStringLiteral("thread state %1").arg(desc);
    const QString wchan = slurp(base + QStringLiteral("wchan"));
    if (!wchan.isEmpty() && wchan != QLatin1String("0")) s += QStringLiteral(", waiting in kernel function %1").arg(wchan);
    const QString sc = slurp(base + QStringLiteral("syscall"));
    if (!sc.isEmpty()) s += QStringLiteral(", syscall %1").arg(sc.section(QLatin1Char(' '), 0, 0));
    return s;
}

StepOutcome runStep(const QString &name, int timeoutMs, std::function<long()> fn, std::function<void(long, int)> late, bool logSteps) {
    auto st = std::make_shared<State>();
    if (logSteps) UsbLog::write(QStringLiteral("step: %1 (limit %2 ms)").arg(name).arg(timeoutMs));
    QElapsedTimer timer; timer.start();
    try {
        std::thread([st, fn = std::move(fn), late = std::move(late), name, timer] {
            st->tid = thisTid();
            long v = fn();
            const int e = errno;
            std::unique_lock<std::mutex> lock(st->m);
            if (st->abandoned) {
                lock.unlock();
                UsbLog::write(QStringLiteral("step: %1 finally returned %2 after %3 ms (too late, the program had given up)").arg(name).arg(v).arg(timer.elapsed()));
                if (late) late(v, e);
                return;
            }
            st->value = v; st->err = e; st->done = true;
            st->cv.notify_all();
        }).detach();
    } catch (const std::exception &e) {
        UsbLog::write(QStringLiteral("step: %1: could not start a helper thread (%2)").arg(name, QString::fromLocal8Bit(e.what())));
        StepOutcome o; o.finished = true; o.value = -1; o.err = EAGAIN; return o;
    }
    std::unique_lock<std::mutex> lock(st->m);
    st->cv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] { return st->done; });
    StepOutcome o;
    if (st->done) {
        o.finished = true; o.value = st->value; o.err = st->err;
        lock.unlock();
        if (logSteps) UsbLog::write(o.value < 0 ? QStringLiteral("step: %1 FAILED after %2 ms: %3").arg(name).arg(timer.elapsed()).arg(o.why())
                                                : QStringLiteral("step: %1 ok after %2 ms (result %3)").arg(name).arg(timer.elapsed()).arg(o.value));
        return o;
    }
    st->abandoned = true;
    const int tid = st->tid;
    lock.unlock();
    UsbLog::write(QStringLiteral("step: %1 TIMED OUT after %2 ms - the call does not return. %3").arg(name).arg(timer.elapsed()).arg(threadState(tid)));
    return o;
}

#ifndef _WIN32
int guardedOpen(const QString &path, int timeoutMs, int *err, const std::function<int(const char *)> &openFn) {
    const QByteArray p = path.toLocal8Bit();
    auto fn = openFn ? openFn : std::function<int(const char *)>([](const char *s) { return ::open(s, O_RDWR | O_CLOEXEC); });
    StepOutcome o = runStep(QStringLiteral("open %1").arg(path), timeoutMs, [fn, p] { return long(fn(p.constData())); },
                            [path](long v, int) { if (v >= 0) { ::close(int(v)); UsbLog::write(QStringLiteral("step: closed the late descriptor of %1").arg(path)); } });
    if (!o.finished) { if (err) *err = ETIMEDOUT; return -2; }
    if (o.value < 0) { if (err) *err = o.err; return -1; }
    return int(o.value);
}

#endif

bool guardedReadFile(const QString &path, int timeoutMs, QString *text, bool *timedOut) {
    auto out = std::make_shared<QString>();
    auto ok = std::make_shared<bool>(false);
    StepOutcome o = runStep(QStringLiteral("read %1").arg(path), timeoutMs, [path, out, ok] {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly)) { *out = QString::fromUtf8(f.readAll()); *ok = true; }
        return 0L;
    }, {}, false);
    if (timedOut) *timedOut = !o.finished;
    if (!o.finished || !*ok) return false;
    if (text) *text = *out;
    return true;
}

QStringList nodeHolders(const QString &node, int timeoutMs) {
    auto out = std::make_shared<QStringList>();
    StepOutcome o = runStep(QStringLiteral("who has %1 open").arg(node), timeoutMs, [node, out] {
        const QDir proc(QStringLiteral("/proc"));
        for (const QString &pid : proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            bool ok = false; pid.toInt(&ok);
            if (!ok) continue;
            const QDir fd(QStringLiteral("/proc/%1/fd").arg(pid));
            for (const QString &n : fd.entryList(QDir::NoDotAndDotDot | QDir::System | QDir::Files | QDir::Dirs))
                if (QFileInfo(fd.filePath(n)).symLinkTarget() == node) { *out << QStringLiteral("pid %1 (%2)").arg(pid, slurp(QStringLiteral("/proc/%1/comm").arg(pid))); break; }
        }
        return 0L;
    }, {}, false);
    return o.finished ? *out : QStringList();
}

}  // namespace vc
