// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
#include "Machine.h"
#include "Matrix15.h"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>

namespace vc {

QByteArray SavedState::toJson() const {
    QJsonObject o;
    o[QStringLiteral("memory")] = QString::fromLatin1(memory.toBase64());   // as Swift's Codable Data
    o[QStringLiteral("variant")] = variant;
    o[QStringLiteral("firmwareID")] = firmwareId;
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

bool SavedState::fromJson(const QByteArray &json, SavedState *out) {
    QJsonObject o = QJsonDocument::fromJson(json).object();
    if (!o.contains(QStringLiteral("memory"))) return false;
    out->memory = QByteArray::fromBase64(o[QStringLiteral("memory")].toString().toLatin1());
    out->variant = o[QStringLiteral("variant")].toInt();
    out->firmwareId = o[QStringLiteral("firmwareID")].toString();
    return out->memory.size() == Emulator::kMemorySize;
}

void Machine::loadFirmware(const QByteArray &data) {
    calc.loadFirmware(data);
    family_ = calc.family();
    firmwareId_ = QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
    firmware_ = data;
    images_ = calc.firmwareImages();
}

QString Machine::memoryUnavailableReason() const {
    return hasCapability(VC_CAP_MEMORY) ? QString()
        : QStringLiteral("HP's original firmware keeps its memory in a different layout than the CE firmware, so memory files are not available with it.");
}

void Machine::run(int ms) {
    int left = ms;
    while (left > 0) {
        int step = std::min(left, 10);
        calc.run(step);
        simMs_ += step; left -= step;
        if (!delayedRelease_.empty()) {
            std::vector<int> due;
            for (auto it = delayedRelease_.begin(); it != delayedRelease_.end();)
                if (it->first <= simMs_) { due.push_back(it->second); it = delayedRelease_.erase(it); } else ++it;
            for (int c : due) release(c);
        }
        if (!pendingRelease_.empty()) {
            for (auto it = pendingRelease_.begin(); it != pendingRelease_.end();) {
                if (simMs_ - downSince_[*it] >= minimumHoldMs_) { calc.release(*it); downSince_.erase(*it); it = pendingRelease_.erase(it); }
                else ++it;
            }
        }
    }
}

void Machine::press(int code) {
    if (downSince_.count(code) && !pendingRelease_.count(code)) return;
    pendingRelease_.erase(code);
    downSince_[code] = simMs_;
    calc.press(code);
}

void Machine::release(int code) {
    auto it = downSince_.find(code);
    if (it == downSince_.end()) return;
    if (simMs_ - it->second >= minimumHoldMs_) { calc.release(code); downSince_.erase(it); }
    else pendingRelease_.insert(code);
}

void Machine::release(const std::vector<int> &codes, int afterMs) {
    for (int k : codes) delayedRelease_.push_back({simMs_ + std::max(0, afterMs), k});
}

void Machine::releaseAll() {
    for (auto &kv : downSince_) calc.release(kv.first);
    downSince_.clear(); pendingRelease_.clear(); delayedRelease_.clear();
}

bool Machine::shiftPending(int shift) {
    const Model &m = Model::with(calc.model());
    const char *want = shift == keyF ? "f" : "g";
    uint64_t bits = calc.display().annBits;
    for (const Annunciator &a : m.annunciators())
        if (QString::fromLatin1(a.label) == QLatin1String(want) && a.bit >= 0 && a.bit < 64 && (bits >> a.bit & 1)) return true;
    return false;
}

void Machine::pressShifted(int shift, int code) {
    if (!shiftPending(shift)) {
        press(shift); run(std::max(minimumHoldMs_, 100));
        release(shift); run(150);
    }
    press(code);
}

void Machine::chordWithON(const std::vector<int> &keys) {
    for (int k : keys) calc.press(k);
    run(100);
    calc.press(keyON); run(300);
    calc.release(keyON); run(200);
    for (int k : keys) calc.release(k);
    run(800);
}

void Machine::powerOff() {
    for (int i = 0; i < 3 && !isBlank(); ++i) { tap(keyON); run(500); }
}

void Machine::powerOn() {
    for (int i = 0; i < 2 && isBlank(); ++i) { tap(keyON); run(1000); }
}

QString Machine::boot(const SavedState *saved) {
    calc.startWhole();
    simMs_ = 0; downSince_.clear(); pendingRelease_.clear(); delayedRelease_.clear();
    tap(keyON); run(1500);
    QString note;
    if (saved && hasCapability(VC_CAP_MEMORY)) {
        if (saved->firmwareId != firmwareId_) {
            note = QStringLiteral("The saved calculator memory belongs to another firmware; starting with a cleared memory.");
        } else {
            if (saved->variant != variant()) switchVariant(saved->variant);
            if (saved->variant == variant()) { putMemory(saved->memory); return QString(); }
            note = QStringLiteral("Could not switch the firmware back to the model the memory was saved from.");
        }
    }
    clearPrError();
    return note;
}

void Machine::putMemory(const QByteArray &data) {
    QString why = memoryUnavailableReason();
    if (!why.isEmpty()) throw Error(why);
    powerOn();
    calc.loadMemory(data);
    powerOff();
    powerOn();
}

QByteArray Machine::memory(bool turnBackOn) {
    QString why = memoryUnavailableReason();
    if (!why.isEmpty()) throw Error(why);
    bool wasOn = !isBlank();
    powerOff();
    QByteArray d = calc.saveMemory();
    if (wasOn && turnBackOn) powerOn();
    return d;
}

void Machine::writeMatrix15(int m, int rows, int cols, const std::vector<uint64_t> &raw) {
    if (model() != 15) throw Error(QStringLiteral("The Matrix Editor needs the HP-15C (the calculator runs another model now)."));
    QByteArray mem = memory(true);
    const QString err = matrix15::write(&mem, variant(), m, rows, cols, raw);
    if (!err.isEmpty()) throw Error(err);
    putMemory(mem);
}

QByteArray Machine::rawMemory() {
    QString why = memoryUnavailableReason();
    if (!why.isEmpty()) throw Error(why);
    return calc.saveMemory();
}

SavedState Machine::savedState(bool turnBackOn) {
    SavedState s;
    s.memory = memory(turnBackOn); s.variant = variant(); s.firmwareId = firmwareId_;
    return s;
}

void Machine::clearPrError() {
    if (displayText().contains(QStringLiteral("Pr Error"))) tap(keyBSP);
}

int Machine::modelMenuKey(int v) {
    switch (v) { case 120: case 150: return 1; case 152: return 3; case 160: return 5; default: return -1; }
}

void Machine::switchVariant(int v) {
    int k = modelMenuKey(v);
    if (images_.size() <= 1 || std::find(images_.begin(), images_.end(), v) == images_.end() || k < 0) return;
    powerOff();
    chordWithON({keyG, keyENTER});
    tap(16);                    // "4": model menu
    tap(k); run(2000);
    clearPrError();
    powerOn();
}

QString Machine::variantName(int v) {
    switch (v) {
    case 120: return QStringLiteral("12C");
    case 150: return QStringLiteral("15C");
    case 152: return QStringLiteral("15C (15.2)");
    case 160: return QStringLiteral("16C");
    default: return QString::number(v);
    }
}

void Machine::switchModel(int v) {
    if (std::find(images_.begin(), images_.end(), v) == images_.end() || images_.size() <= 1)
        throw Error(QStringLiteral("This firmware has no %1 image.").arg(variantName(v)));
    releaseAll();
    switchVariant(v);
    if (variant() != v)
        throw Error(QStringLiteral("The calculator did not switch to the %1; it is running as the %2.").arg(variantName(v), variantName(variant())));
}

void Machine::perform(KeyChord c) {
    releaseAll();
    powerOff();
    chordWithON(chordInfo(c).keys);
}

QString Machine::memoryStatus() {
    const int rcl = 34;
    tap(keyG);
    calc.press(rcl); calc.run(400);
    QString t = displayText();
    calc.release(rcl); calc.run(800);
    return t;
}

// --- memory checks -----------------------------------------------------------------------------------

Machine::MemoryCheck Machine::checkMemory(const QByteArray &mem, const QByteArray &firmware, int variant) {
    MemoryCheck r;
    if (mem.size() != Emulator::kMemorySize) { r.kind = MemoryCheck::WrongSize; r.bytes = int(mem.size()); return r; }
    try {
        Machine m;
        m.loadFirmware(firmware);
        m.boot(nullptr);
        if (m.variant() != variant) m.switchModel(variant);
        m.powerOff();
        m.calc.loadMemory(mem);
        QString shown;
        for (int i = 0; i < 3; ++i) {
            m.tap(keyON); m.run(1500);
            QString t = m.displayText();
            if (t.contains(QStringLiteral("Error")) && t.contains(QStringLiteral("Pr"))) { r.kind = MemoryCheck::Rejected; return r; }
            bool blank = true; for (QChar c : t) if (c != QLatin1Char(' ')) blank = false;
            if (!blank) { shown = t; break; }
            auto ins = inspect(m.calc, false);
            if (ins && ins->running) { shown = QStringLiteral("running"); break; }
        }
        if (shown.isEmpty()) { r.kind = MemoryCheck::Unreadable; return r; }
        m.powerOff();
        QByteArray after = m.calc.saveMemory();
        int changed = 0;
        for (int i = kMemoryStart; i < kMemoryEnd; ++i) if (mem[i] != after[i]) ++changed;
        if (changed > 0) { r.kind = MemoryCheck::Changed; r.bytes = changed; return r; }
        if ((variant == 150 || variant == 152) && shown != QLatin1String("running")) {
            m.powerOn();
            if (m.memoryStatus().contains(QStringLiteral("Error 10"))) { r.kind = MemoryCheck::NeedsMoreMemory; return r; }
        }
        r.kind = MemoryCheck::Fits; r.display = shown.trimmed();
        return r;
    } catch (const std::exception &) {
        r.kind = MemoryCheck::Unreadable;
        return r;
    }
}

Emulator::LayoutCheck Machine::layoutCheck(const QByteArray &mem, const QByteArray &firmware, int variant) {
    try {
        Machine m;
        m.loadFirmware(firmware);
        m.boot(nullptr);
        if (m.variant() != variant) m.switchModel(variant);
        return m.calc.precheckMemory(mem);
    } catch (const std::exception &) {
        return Emulator::LayoutCheck();
    }
}

std::vector<int> Machine::identifyMemory(const QByteArray &mem, const QByteArray &firmware, const std::vector<int> &among) {
    std::vector<int> out;
    for (int v : among) if (checkMemory(mem, firmware, v).fits()) out.push_back(v);
    return out;
}

bool Machine::needs152(const QByteArray &mem, const QByteArray &firmware, const std::vector<int> &among) {
    auto has = [&](int v) { return std::find(among.begin(), among.end(), v) != among.end(); };
    return has(150) && has(152) && checkMemory(mem, firmware, 150).kind == MemoryCheck::NeedsMoreMemory;
}

}  // namespace vc
