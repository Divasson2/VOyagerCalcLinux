// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
#include "Emulator.h"
#include <QStringList>
#include <algorithm>

namespace vc {

Emulator::Emulator() : c_(vc_create()) {
    vc_set_clock_ipc(c_, VC_IPC_REAL);                          // real 15C CE speed at 4 / 12 MHz (0.7.1 test10)
    vc_set_unthrottled_48mhz(c_, VC_UNTHROTTLED_48MHZ_US);      // 0.7.7: 48 MHz runs at full host speed
}
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

QString Emulator::layoutTitle(Memory16Layout l) {
    switch (l) {
    case Memory16Layout::Automatic: return QStringLiteral("Automatic (running firmware)");
    case Memory16Layout::Standard: return QStringLiteral("Stock 16C: 29 slots (203 bytes)");
    case Memory16Layout::Layout206: return QStringLiteral("Extended 16C: 206 slots (EAEA / C9C9 / ECEC, up to R720 at WS16)");
    case Memory16Layout::Unsupported: return QStringLiteral("Unsupported 16C pool (memory loading and inspector off)");
    case Memory16Layout::Undetermined: return QStringLiteral("Extended 16C, layout not confirmed (choose stock or 206 slots)");
    default: return QStringLiteral("an unknown layout");
    }
}
int Emulator::memory16Slots(Memory16Layout l) {
    return l == Memory16Layout::Layout206 ? 206 : l == Memory16Layout::Standard ? 29 : 0;
}

int Emulator::KnownFirmware::keyFor(int variant) const {
    auto it = switchKeys.find(variant);
    return it == switchKeys.end() ? -1 : it->second;
}
QString Emulator::KnownFirmware::modeNames() const {
    QStringList names;
    for (int v : variants) {
        switch (v) {
        case 120: names << QStringLiteral("12C"); break;
        case 150: names << QStringLiteral("15C"); break;
        case 152: names << QStringLiteral("15C (15.2)"); break;
        case 160: names << QStringLiteral("16C"); break;
        default: names << QString::number(v); break;
        }
    }
    return names.join(QStringLiteral(" / "));
}

static Emulator::KnownFirmware knownFrom(const vc_firmware_profile &p) {
    Emulator::KnownFirmware k;
    k.checksum = p.checksum;
    k.code = QStringLiteral("%1%1").arg(p.checksum, 2, 16, QChar(u'0')).toUpper();
    k.displayName = QString::fromUtf8(p.display_name);
    const int modes[][2] = {{120, VC_FW_12C}, {150, VC_FW_150}, {152, VC_FW_152}, {160, VC_FW_16C}};
    for (auto &m : modes) if (p.modes & unsigned(m[1])) k.variants.push_back(m[0]);
    k.layout = Emulator::Memory16Layout(p.mem16_layout);
    k.perModeSlots = p.per_mode_slots != 0;
    k.switchMenu = p.switch_menu;
    const int keys[][2] = {{120, p.key120}, {150, p.key150}, {152, p.key152}, {160, p.key160}};
    for (auto &m : keys) if (m[1] >= 0) k.switchKeys[m[0]] = m[1];
    k.switchMethod = QString::fromUtf8(p.switch_method);
    k.notes = QString::fromUtf8(p.notes);
    k.sources = QString::fromUtf8(p.sources);
    return k;
}

std::optional<Emulator::KnownFirmware> Emulator::knownFirmware() const {
    const vc_firmware_profile *p = vc_firmware_known_profile(c_);
    if (!p) return std::nullopt;
    return knownFrom(*p);
}
std::vector<Emulator::KnownFirmware> Emulator::knownFirmwareCatalogue() {
    std::vector<KnownFirmware> rows;
    const int n = vc_firmware_profile_count();
    rows.reserve(size_t(n));
    for (int i = 0; i < n; ++i) if (const vc_firmware_profile *p = vc_firmware_profile_at(i)) rows.push_back(knownFrom(*p));
    return rows;
}
QString Emulator::firmwareIdentification() const {
    if (auto p = knownFirmware()) return p->displayName + QStringLiteral(" · CE ") + p->code;
    const Memory16Layout layout = firmwareMemory16Layout();
    const QString text = layout == Memory16Layout::Automatic ? QStringLiteral("not applicable (no supported 16C ROM)") : layoutTitle(layout);
    return QStringLiteral("Unknown firmware, layout detected as ") + text;
}
QString Emulator::firmwareProfileDetails() const {
    if (auto p = knownFirmware())
        return QStringLiteral("Modes: %1. 16C: %2. Per-mode memory slots: %3.\n%4\n%5")
            .arg(p->modeNames(), layoutTitle(p->layout), p->perModeSlots ? QStringLiteral("yes") : QStringLiteral("no"), p->switchMethod, p->notes);
    const QString code = firmwareChecksum();
    return QStringLiteral("No confirmed catalogue entry for CE %1. ROM-based layout detection is used; an inspector override is available. Mode-memory retention is not assumed.")
        .arg(code.isEmpty() ? QStringLiteral("unknown") : code);
}
Emulator::Memory16Layout Emulator::memory16Layout() const { return Memory16Layout(vc_mem16_layout(c_)); }
Emulator::Memory16Layout Emulator::firmwareMemory16Layout() const { return Memory16Layout(vc_firmware_mem16(c_)); }
void Emulator::setMemory16Layout(Memory16Layout layout) { check(vc_set_mem16_layout(c_, int(layout))); }

static std::vector<Emulator::Memory16Layout> layoutsIn(unsigned mask) {
    std::vector<Emulator::Memory16Layout> out;
    for (auto l : {Emulator::Memory16Layout::Standard, Emulator::Memory16Layout::Layout206})
        if (mask & (1u << int(l))) out.push_back(l);
    return out;
}
Emulator::Memory16Analysis Emulator::analyzeMemory16(const QByteArray &data) {
    Memory16Analysis a;
    if (data.size() != kMemorySize) return a;
    vc_mem16_analysis o{};
    if (vc_memory_analyze16(reinterpret_cast<const uint8_t *>(data.constData()), &o) != VC_OK) return a;
    a.magicValid = o.magic_valid != 0;
    a.pointersValid = o.pointers_valid != 0;
    a.lines = o.lines;
    a.upperWindowEmpty = o.upper_empty != 0;
    a.gapEmpty = o.gap_empty != 0;
    a.stockPoolEmpty = o.stock_pool_empty != 0;
    a.candidates = layoutsIn(o.candidates);
    a.programHeads = layoutsIn(o.program_heads);
    a.evidence = layoutsIn(o.primary_candidates);
    return a;
}
QByteArray Emulator::convertPool(const QByteArray &mem, Memory16Layout from, Memory16Layout to) {
    if (mem.size() != kMemorySize) return {};
    QByteArray out = mem;
    return vc_memory_convert_pool(reinterpret_cast<uint8_t *>(out.data()), int(from), int(to)) == 1 ? out : QByteArray();
}
Emulator::Memory16Import Emulator::prepareMemory16(const QByteArray &data, Memory16Layout target, std::optional<Memory16Layout> knownSource) {
    auto unchanged = [&](const QString &text, bool ambiguous) {
        Memory16Import r;
        r.data = data; r.source = target; r.converted = false; r.ambiguous = ambiguous; r.note = text;
        return r;
    };
    const Memory16Layout supported[] = {Memory16Layout::Standard, Memory16Layout::Layout206};
    auto supportedLayout = [&](Memory16Layout l) { return std::find(std::begin(supported), std::end(supported), l) != std::end(supported); };
    if (!supportedLayout(target) || data.size() != kMemorySize)
        return unchanged(QStringLiteral("No automatic 16C layout conversion."), true);
    if (knownSource && !supportedLayout(*knownSource))
        return unchanged(QStringLiteral("The source layout is neither stock nor 206 slots; using current firmware unchanged."), true);
    const Memory16Analysis a = analyzeMemory16(data);
    std::vector<Memory16Layout> candidates = (knownSource && supportedLayout(*knownSource)) ? std::vector<Memory16Layout>{*knownSource} : a.evidence;
    if (candidates.empty())
        return unchanged(QStringLiteral("No conclusive 16C layout evidence; checking the file on the selected firmware."), true);
    auto contains = [&](Memory16Layout l) { return std::find(candidates.begin(), candidates.end(), l) != candidates.end(); };
    if (candidates.size() > 1 && contains(target))
        return unchanged(QStringLiteral("Ambiguous raw memory layout; using current %1-slot firmware layout unchanged.").arg(memory16Slots(target)), true);
    if (candidates.size() == 1 && candidates.front() == target) {
        const QString why = knownSource ? QStringLiteral("connected self-test") : QStringLiteral("structural evidence");
        return unchanged(QStringLiteral("Memory layout matches current %1-slot firmware (%2).").arg(memory16Slots(target)).arg(why), false);
    }
    std::vector<QByteArray> conversions;
    for (Memory16Layout from : candidates) {
        const QByteArray converted = convertPool(data, from, target);
        if (converted.isEmpty()) {
            if (candidates.size() == 1)
                throw Error(QStringLiteral("Cannot safely convert %1 → %2 slots. The image is active, damaged, or data/program exceeds the smaller pool. Nothing was loaded; the original is unchanged.")
                                .arg(memory16Slots(from)).arg(memory16Slots(target)));
            return unchanged(QStringLiteral("Ambiguous raw memory layout; using current %1-slot firmware layout unchanged (no safe common conversion).").arg(memory16Slots(target)), true);
        }
        conversions.push_back(converted);
    }
    for (const QByteArray &c : conversions) if (c != conversions.front())
        return unchanged(QStringLiteral("Ambiguous raw memory layout; using current %1-slot firmware layout unchanged (interpretations differ).").arg(memory16Slots(target)), true);
    const Memory16Layout from = candidates.front();
    const QString why = candidates.size() > 1 ? QStringLiteral("all supported interpretations produce identical bytes")
        : knownSource ? QStringLiteral("connected self-test") : QStringLiteral("structural evidence");
    const QString description = candidates.size() > 1
        ? QStringLiteral("Automatically normalized compatible raw layouts to %1 slots").arg(memory16Slots(target))
        : QStringLiteral("Automatically converted %1 → %2 slots").arg(memory16Slots(from)).arg(memory16Slots(target));
    Memory16Import r;
    r.data = conversions.front(); r.source = from; r.converted = true; r.ambiguous = candidates.size() > 1;
    r.note = QStringLiteral("%1 (%2); original unchanged. Undo is available after loading.").arg(description, why);
    return r;
}

}  // namespace vc
