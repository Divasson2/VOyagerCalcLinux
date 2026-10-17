// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
//
// StepGuard.h - run ONE call that might block inside the kernel (sysfs read, open() or write() of a hidraw node, close(), an ioctl) on its own
// short-lived thread and wait for it with a time limit. A call that does not return in time is abandoned (its thread finishes whenever the kernel
// lets it, and a late result is cleaned up) and the caller gets "timed out" plus what that thread is doing (state, kernel wait channel, syscall),
// so a stuck USB device becomes an error message and a line in the USB log instead of a frozen window.
#pragma once
#include <QString>
#include <functional>

namespace vc {

struct StepOutcome {
    bool finished = false;      // false: timed out (the call is still running somewhere)
    long value = 0;             // what the call returned
    int err = 0;                // errno right after it returned
    QString why() const;        // strerror(err)
    bool failed() const { return finished && value < 0; }
};

/// Calls fn() on a helper thread and waits up to timeoutMs. fn must own everything it touches (capture shared_ptrs, not references to locals) because
/// it may outlive this call. `late(value, err)` runs on the helper thread if fn returns after the wait was given up (e.g. close a descriptor that
/// open() finally returned). logSteps: write "begin" and "done" lines to the USB log (a timeout is always logged).
StepOutcome runStep(const QString &name, int timeoutMs, std::function<long()> fn, std::function<void(long, int)> late = {}, bool logSteps = true);

/// "state D (disk sleep), waiting in <wchan>, in syscall N" for a thread of this process (Linux /proc), or empty.
QString threadState(int tid);

/// Open a device node (O_RDWR|O_CLOEXEC) with a time limit. Returns the descriptor, -1 (errno in *err) or -2 on timeout.
int guardedOpen(const QString &path, int timeoutMs, int *err, const std::function<int(const char *)> &openFn = {});
/// Read a small text file (sysfs) with a time limit; false on timeout or when it cannot be read.
bool guardedReadFile(const QString &path, int timeoutMs, QString *text, bool *timedOut = nullptr);
/// Which processes have this node open ("pid 123 (name)"), for the diagnostics. Bounded in time.
QStringList nodeHolders(const QString &node, int timeoutMs = 3000);

}  // namespace vc
