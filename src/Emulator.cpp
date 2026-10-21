// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
#include "Emulator.h"

namespace vc {

Emulator::Emulator() : c_(vc_create()) { vc_set_clock_ipc(c_, VC_IPC_REAL); }   // real 15C CE speed (0.7.1 test10)
Emulator::~Emulator() { vc_destroy(c_); }

QString Emulator::lastError() const {
    const char *e = vc_last_error(c_);
    return e && *e ? QString::fromUtf8(e) : QStringLiteral("unknown error");
}

void Emulator::check(int r) {
    if (r >= 0) return;
    throw Error(lastError());
}

void Emulator::loadFirmware(const QByteArray &image) {
    check(vc_load_firmware(c_, reinterpret_cast<const uint8_t *>(image.constData()), size_t(image.size())));
    loaded_ = true;
}

void Emulator::startWhole() { check(vc_start(c_, VC_ENGINE_WHOLE, 0)); }

void Emulator::run(int ms) { check(vc_run_ms(c_, uint32_t(ms < 10 ? 10 : ms))); }

DisplaySnapshot Emulator::display() {
    DisplaySnapshot s;
    vc_display d{};
    if (vc_get_display_cells(c_, &d) != VC_OK) return s;
    for (int i = 0; i < 10; ++i) { s.seg[i] = d.seg[i]; s.dot[i] = d.dot[i] != 0; s.comma[i] = d.comma[i] != 0; }
    s.minus = d.minus != 0;
    s.annBits = d.ann_bits;
    char t[48] = {0};
    vc_display_text(&d, t, sizeof t);
    s.text = QString::fromLatin1(t);
    s.model = vc_model(c_);
    s.variant = vc_model_variant(c_);
    return s;
}

QString Emulator::displayText() { return display().text; }

QByteArray Emulator::saveMemory() {
    QByteArray out(kMemorySize, 0);
    check(vc_save_memory(c_, reinterpret_cast<uint8_t *>(out.data())));
    return out;
}

void Emulator::loadMemory(const QByteArray &mem) {
    if (mem.size() != kMemorySize) throw Error(QStringLiteral("A calculator memory is 2048 bytes, not %1.").arg(mem.size()));
    check(vc_load_memory(c_, reinterpret_cast<const uint8_t *>(mem.constData())));
}

Emulator::LayoutCheck Emulator::precheckMemory(const QByteArray &mem) const {
    LayoutCheck r;
    if (mem.size() != kMemorySize) return r;
    vc_memcheck k;
    if (vc_memory_precheck(c_, reinterpret_cast<const uint8_t *>(mem.constData()), &k) != VC_OK) return r;
    r.verdict = k.verdict;
    r.message = QString::fromUtf8(k.message);
    return r;
}

QByteArray Emulator::convertE3E3(const QByteArray &mem) {
    if (mem.size() != kMemorySize) return QByteArray();
    QByteArray out = mem;
    return vc_memory_convert_e3e3(reinterpret_cast<uint8_t *>(out.data())) == 1 ? out : QByteArray();
}

QString Emulator::firmwareInfo() const {
    const char *i = vc_firmware_info(c_);
    return i ? QString::fromUtf8(i) : QString();
}

QString Emulator::firmwareChecksum() const {
    int v = vc_firmware_checksum(c_);
    return v >= 0 ? QStringLiteral("%1%1").arg(v, 2, 16, QLatin1Char('0')).toUpper() : QString();
}

std::vector<int> Emulator::firmwareImages() const {
    int v[8] = {0};
    int n = vc_firmware_images(c_, v, 8);
    std::vector<int> out;
    for (int i = 0; i < n && i < 8; ++i) out.push_back(v[i]);
    return out;
}

}  // namespace vc
