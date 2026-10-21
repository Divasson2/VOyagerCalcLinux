// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
//
// Machine.h - the calculator as the app sees it: boot, restore a saved state, keys held by a finger, the
// calculator's own model switch, memory images (port of CalculatorMachine.swift). Synchronous and
// single-threaded; EngineWorker runs it on the engine thread, tests call it directly. Throws vc::Error.
#pragma once
#include "Emulator.h"
#include "Inspector.h"
#include "Model.h"
#include <QByteArray>
#include <map>
#include <set>

namespace vc {

struct SavedState {
    QByteArray memory;       // 2 KB VoyagerSave image
    int variant = 0;         // 120 / 150 / 152 / 160: which ROM was running
    QString firmwareId;      // SHA-256 of the firmware file it belongs to
    QByteArray toJson() const;
    static bool fromJson(const QByteArray &json, SavedState *out);
};

class Machine {
public:
    static constexpr int keyON = 30, keyF = 31, keyG = 32, keyENTER = 25, keyBSP = 24;
    static constexpr int kMemoryStart = 0x100, kMemoryEnd = 0x7F0;      // the compared part of an image

    Emulator calc;

    void loadFirmware(const QByteArray &data);
    QString firmwareId() const { return firmwareId_; }
    const QByteArray &firmwareData() const { return firmware_; }
    int family() const { return family_; }
    const std::vector<int> &images() const { return images_; }
    bool canSwitchModel() const { return images_.size() > 1; }
    unsigned capabilities() const { return calc.capabilities(); }
    bool hasCapability(unsigned c) const { return capabilities() & c; }
    int model() const { return calc.model(); }
    int variant() const { return calc.variant(); }
    int simMs() const { return simMs_; }
    /// Why a memory image cannot be used with this firmware ("" when it can).
    QString memoryUnavailableReason() const;

    // time and keys
    void run(int ms);
    void press(int code);
    void release(int code);
    void release(const std::vector<int> &codes, int afterMs);
    void releaseAll();
    void tap(int code) { calc.tap(code); simMs_ += 720; }
    void pressShifted(int shift, int code);
    bool shiftPending(int shift);
    void chordWithON(const std::vector<int> &keys);
    void powerOff();
    void powerOn();
    bool isBlank() { return calc.display().isBlank(); }
    QString displayText() { return calc.displayText(); }

    /// Reset the chip and turn the calculator on; with a saved state of the same firmware put its memory back.
    /// Returns a note for the user when the saved state could not be used.
    QString boot(const SavedState *saved);
    void putMemory(const QByteArray &data);
    QByteArray memory(bool turnBackOn = true);
    QByteArray rawMemory();
    SavedState savedState(bool turnBackOn = true);
    void clearPrError();
    void switchVariant(int v);
    void switchModel(int v);                     // switch and check
    void perform(KeyChord c);
    QString memoryStatus();                      // g MEM while RCL is held (15C)
    /// 15C: DIM matrix m (0..4) to rows x cols and store raw (row-major BCD) in the memory, as the Matrix Editor's Apply. Throws Error.
    void writeMatrix15(int m, int rows, int cols, const std::vector<uint64_t> &raw);

    static QString variantName(int v);           // "12C", "15C", "15C (15.2)", "16C"
    static int modelMenuKey(int variant);        // key under each label of the multi-ROM model menu

    // trial loads of a memory image on a scratch machine (what Open / Read do before loading it)
    struct MemoryCheck {
        enum Kind { Fits, Rejected, Changed, Unreadable, WrongSize, NeedsMoreMemory } kind = Unreadable;
        QString display;     // Fits
        int bytes = 0;       // Changed: bytes; WrongSize: the size
        bool fits() const { return kind == Fits; }
        bool operator==(const MemoryCheck &o) const { return kind == o.kind && (kind != Changed && kind != WrongSize || bytes == o.bytes); }
    };
    static MemoryCheck checkMemory(const QByteArray &mem, const QByteArray &firmware, int variant);
    /// Structural 16C layout check on a scratch machine (see Emulator::precheckMemory); verdict 0 when it cannot be told.
    static Emulator::LayoutCheck layoutCheck(const QByteArray &mem, const QByteArray &firmware, int variant);
    static std::vector<int> identifyMemory(const QByteArray &mem, const QByteArray &firmware, const std::vector<int> &among);
    static bool needs152(const QByteArray &mem, const QByteArray &firmware, const std::vector<int> &among);

private:
    QByteArray firmware_;
    QString firmwareId_;
    int family_ = 0;
    std::vector<int> images_;
    int simMs_ = 0;
    int minimumHoldMs_ = 80;
    std::map<int, int> downSince_;
    std::set<int> pendingRelease_;
    std::vector<std::pair<int, int>> delayedRelease_;       // (at, code)
};

}  // namespace vc
