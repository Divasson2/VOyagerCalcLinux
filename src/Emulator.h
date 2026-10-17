// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
//
// Emulator.h - thin C++ wrapper around the C emulator core (core/include/voyager_core.h); the
// counterpart of VoyagerCalculator.swift. Errors are thrown as vc::Error (message in plain words).
// Not thread-safe: one Emulator belongs to one thread at a time (the engine thread, see EngineWorker).
#pragma once
#include <QByteArray>
#include <QMetaType>
#include <QString>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>

extern "C" {
#include "voyager_core.h"
}

namespace vc {

/// Anything that went wrong in the engine, with a message fit to show to the user.
class Error : public std::runtime_error {
public:
    explicit Error(const QString &m) : std::runtime_error(m.toUtf8().constData()), message_(m) {}
    QString message() const { return message_; }
private:
    QString message_;
};

/// Everything the display widget needs to paint the LCD (copied out of the engine thread).
struct DisplaySnapshot {
    std::array<uint8_t, 10> seg{};
    std::array<bool, 10> dot{}, comma{};
    bool minus = false;
    uint64_t annBits = 0;
    QString text;
    int model = 0;          // 12, 15, 16 (what runs now), 0 = none
    int variant = 0;        // 120, 150, 152, 160
    bool operator==(const DisplaySnapshot &o) const {
        return seg == o.seg && dot == o.dot && comma == o.comma && minus == o.minus && annBits == o.annBits
            && model == o.model && variant == o.variant;
    }
    bool operator!=(const DisplaySnapshot &o) const { return !(*this == o); }
    bool isBlank() const { for (QChar c : text) if (c != QLatin1Char(' ')) return false; return true; }
};

class Emulator {
public:
    static constexpr int kMemorySize = VC_MEMORY_SIZE;      // 2048

    Emulator();
    ~Emulator();
    Emulator(const Emulator &) = delete;
    Emulator &operator=(const Emulator &) = delete;

    void loadFirmware(const QByteArray &image);              // throws
    void startWhole();                                       // throws; the calculator is off until ON
    void press(int code) { vc_press_key(c_, code); }
    void release(int code) { vc_release_key(c_, code); }
    void run(int ms);                                        // throws; simulated ms, 10 ms steps
    /// Press, hold, release and let the calculator react (like a finger).
    void tap(int code, int holdMs = 120, int afterMs = 600) { press(code); run(holdMs); release(code); run(afterMs); }
    DisplaySnapshot display();
    QString displayText();

    QByteArray saveMemory();                                 // throws, 2 KB
    void loadMemory(const QByteArray &mem);                  // throws

    int family() const { return vc_firmware_family(c_); }    // VC_MODEL_* / VC_FAMILY_*
    unsigned capabilities() const { return vc_capabilities(c_); }
    int model() const { return vc_model(c_); }
    int variant() const { return vc_model_variant(c_); }
    QString firmwareInfo() const;
    QString firmwareChecksum() const;                        // "C8C8" or empty
    std::vector<int> firmwareImages() const;
    bool hasFirmware() const { return loaded_; }
    vc_calc *handle() { return c_; }

    static int keyCode(int model, const char *name) { return vc_key_code(model, name); }

private:
    void check(int r);
    QString lastError() const;
    vc_calc *c_ = nullptr;
    bool loaded_ = false;
};

}  // namespace vc

Q_DECLARE_METATYPE(vc::DisplaySnapshot)
