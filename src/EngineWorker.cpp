// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
#include "EngineWorker.h"

namespace vc {

EngineWorker::EngineWorker(QObject *parent) : QObject(parent) {
    qRegisterMetaType<vc::DisplaySnapshot>();
    qRegisterMetaType<vc::SavedState>();
    qRegisterMetaType<vc::FirmwareDetails>();
    qRegisterMetaType<std::shared_ptr<const vc::InspectorSnapshot>>();
}

void EngineWorker::post(std::function<void()> fn) { QMetaObject::invokeMethod(this, std::move(fn), Qt::QueuedConnection); }

void EngineWorker::submit(QObject *context, Job job, std::function<void(const QString &)> done) {
    post([this, context, job = std::move(job), done = std::move(done)] {
        QString err;
        try { job(machine_); publish(true); }
        catch (const Error &e) { err = e.message(); }
        catch (const std::exception &e) { err = QString::fromUtf8(e.what()); }
        if (err.isEmpty() && inspector_) inspectNow(true);
        QMetaObject::invokeMethod(context, [done, err] { done(err); }, Qt::QueuedConnection);
    });
}

bool EngineWorker::performAndWait(Job job, QString *error) {
    QString err;
    QMetaObject::invokeMethod(this, [&] {
        try { job(machine_); }
        catch (const Error &e) { err = e.message(); }
        catch (const std::exception &e) { err = QString::fromUtf8(e.what()); }
    }, Qt::BlockingQueuedConnection);
    if (error) *error = err;
    return err.isEmpty();
}

void EngineWorker::start(const QByteArray &firmware, bool hasSaved, const SavedState &saved) {
    FirmwareDetails d;
    QString note;
    try {
        machine_.loadFirmware(firmware);
        d.id = machine_.firmwareId(); d.info = machine_.calc.firmwareInfo(); d.checksum = machine_.calc.firmwareChecksum();
        d.family = machine_.family(); d.capabilities = machine_.capabilities(); d.images = machine_.images();
        note = machine_.boot(hasSaved ? &saved : nullptr);
    } catch (const Error &e) {
        emit booted(false, e.message(), QString(), d);
        return;
    } catch (const std::exception &e) {
        emit booted(false, QString::fromUtf8(e.what()), QString(), d);
        return;
    }
    publish(true);
    emit booted(true, QString(), note, d);
    running_ = true;
    clock_.start(); inspectClock_.start();
    simNs_ = 0;
    if (!timer_) {
        timer_ = new QTimer(this);          // lives on the engine thread
        timer_->setInterval(10);
        timer_->setTimerType(Qt::PreciseTimer);
        connect(timer_, &QTimer::timeout, this, &EngineWorker::tick);
    }
    timer_->start();
}

void EngineWorker::step(int ms) {
    try { machine_.run(ms); }
    catch (const Error &e) { running_ = false; if (timer_) timer_->stop(); emit fault(e.message()); }
}

void EngineWorker::tick() {
    if (!running_) return;
    qint64 ns = clock_.nsecsElapsed();
    clock_.restart();
    // always real time (0.7.1: Turbo removed): 10 ms of calculator time per 10 ms of wall time
    simNs_ += ns;
    int steps = 0;
    while (running_ && simNs_ >= 10'000'000 && steps < 10) { step(10); simNs_ -= 10'000'000; ++steps; }
    if (simNs_ > 100'000'000) simNs_ = 0;          // fell behind (a stall): no catch-up race
    if (running_) publish();
}

void EngineWorker::publish(bool force) {
    DisplaySnapshot s = machine_.calc.display();
    if (force || s != last_) { last_ = s; emit displayChanged(s); }
    if (inspector_) inspectNow(false);
}

void EngineWorker::inspectNow(bool force) {
    if (!force && inspectClock_.elapsed() < 100) return;
    inspectClock_.restart();
    auto s = inspect(machine_.calc, true);
    bool same = s.has_value() == lastInspection_.has_value() && (!s || *s == *lastInspection_);
    if (force || !same) {
        lastInspection_ = s;
        emit inspectorChanged(s ? std::make_shared<const InspectorSnapshot>(*s) : nullptr);
    }
}

void EngineWorker::setInspector(bool on) {
    inspector_ = on;
    lastInspection_.reset();
    if (running_ && on) inspectNow(true);
}

void EngineWorker::pressKey(int code) { if (running_) machine_.press(code); }
void EngineWorker::releaseKey(int code) { if (running_) machine_.release(code); }
void EngineWorker::releaseAll() { machine_.releaseAll(); }
void EngineWorker::releaseLater(const QVector<int> &codes, int afterMs) { machine_.release(std::vector<int>(codes.begin(), codes.end()), afterMs); }

void EngineWorker::pressShifted(int shift, int code) {
    if (!running_) return;
    try { machine_.pressShifted(shift, code); }
    catch (const Error &e) { running_ = false; if (timer_) timer_->stop(); emit fault(e.message()); }
}

void EngineWorker::runFast(int ms) {
    for (int left = ms; left > 0 && running_; left -= 10) step(10);
    publish(true);
}

void EngineWorker::stop() { running_ = false; if (timer_) timer_->stop(); }

}  // namespace vc
