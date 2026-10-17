// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
//
// EngineWorker.h - runs the Machine on its own thread in real time (10 ms ticks, or as fast as possible in
// turbo), like CalculatorEngine.swift on the Mac. The UI thread talks to it only through queued calls:
// the slots below, post() and submit(); results come back as signals or through the submit() callback.
#pragma once
#include "Machine.h"
#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <functional>
#include <memory>

Q_DECLARE_METATYPE(std::shared_ptr<const vc::InspectorSnapshot>)
Q_DECLARE_METATYPE(vc::SavedState)

namespace vc {

/// What the UI needs to know about the firmware that is running.
struct FirmwareDetails {
    QString id, info, checksum;
    int family = 0;
    unsigned capabilities = 0;
    std::vector<int> images;
    bool stock() const { return family == VC_FAMILY_STOCK; }
};

class EngineWorker : public QObject {
    Q_OBJECT
public:
    explicit EngineWorker(QObject *parent = nullptr);
    using Job = std::function<void(Machine &)>;

    /// Run `fn` on the engine thread (queued; returns at once).
    void post(std::function<void()> fn);
    /// Run `job` on the engine thread; then `done(error)` runs on `context`'s thread ("" = success).
    void submit(QObject *context, Job job, std::function<void(const QString &error)> done);
    /// Run `job` on the engine thread and wait for it. Never call it from the engine thread.
    bool performAndWait(Job job, QString *error = nullptr);

public slots:
    /// Load firmware and boot (restoring `saved` when it belongs to this firmware). Emits booted().
    void start(const QByteArray &firmware, bool hasSaved, const vc::SavedState &saved);
    void pressKey(int code);
    void releaseKey(int code);
    void pressShifted(int shift, int code);
    void releaseAll();
    void releaseLater(const QVector<int> &codes, int afterMs);
    void setTurbo(bool on) { turbo_ = on; }
    void setInspector(bool on);
    /// Run n ms of simulated time at once (tests, screenshots).
    void runFast(int ms);
    void stop();

signals:
    void booted(bool ok, const QString &message, const QString &note, const vc::FirmwareDetails &details);
    void displayChanged(const vc::DisplaySnapshot &snapshot);
    void inspectorChanged(std::shared_ptr<const vc::InspectorSnapshot> snapshot);
    void fault(const QString &message);

private slots:
    void tick();

private:
    void step(int ms);
    void publish(bool force = false);
    void inspectNow(bool force);
    Machine machine_;
    QTimer *timer_ = nullptr;
    QElapsedTimer clock_, inspectClock_;
    qint64 simNs_ = 0;
    DisplaySnapshot last_;
    std::optional<InspectorSnapshot> lastInspection_;
    bool running_ = false, turbo_ = false, inspector_ = false;
};

}  // namespace vc

Q_DECLARE_METATYPE(vc::FirmwareDetails)
