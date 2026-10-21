// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
#include "Model.h"
#include "ModelData.h"
#include "voyager_core.h"
#include <algorithm>

namespace vc {

const KeySpec *Model::key(int code) const {
    if (code == 35) code = 25;
    for (int i = 0; i < 39; ++i) if (keys_[i].code == code) return &keys_[i];
    return nullptr;
}

const KeySpec *Model::keyWithStyle(KeyStyle st) const {
    for (int i = 0; i < 39; ++i) if (keys_[i].style == st) return &keys_[i];
    return nullptr;
}

static char32_t lower(char32_t c) { return c < 128 ? char32_t(QChar::toLower(char16_t(c))) : c; }

const KeySpec *Model::keyForShortcut(const Shortcut &s) const {
    for (int i = 0; i < 39; ++i)
        for (int j = 0; j < keys_[i].nShortcuts; ++j) {
            const Shortcut &t = keys_[i].shortcuts[j];
            if (s.special >= 0 ? t.special == s.special : (t.special < 0 && lower(t.ch) == lower(s.ch))) return &keys_[i];
        }
    return nullptr;
}

static std::vector<LegendBracket> vec(const LegendBracket *b, size_t n) { return {b, b + n}; }
static std::vector<Annunciator> vec(const Annunciator *a, size_t n) { return {a, a + n}; }
#define ARR(x) x, sizeof(x) / sizeof(x[0])

const Model &Model::hp12c() { static Model m(12, "12C", "bin12c", kKeys12C, vec(ARR(kBrackets12C)), vec(ARR(kVoyagerAnnunciators)), kGZoneFraction12C); return m; }
const Model &Model::hp15c() { static Model m(15, "15C", "bin15c", kKeys15C, vec(ARR(kBrackets15C)), vec(ARR(kVoyagerAnnunciators)), kGZoneFraction15C); return m; }
const Model &Model::hp16c() { static Model m(16, "16C", "bin16c", kKeys16C, vec(ARR(kBrackets16C)), vec(ARR(kVoyagerAnnunciators)), kGZoneFraction16C); return m; }
const Model &Model::with(int id) { return id == 12 ? hp12c() : id == 16 ? hp16c() : hp15c(); }

QString shortcutLabel(const Shortcut &s) {
    if (s.special < 0) return QString::fromUcs4(&s.ch, 1).toUpper();
    switch (s.special) {
    case SK_Return: return QStringLiteral("⏎ Return");
    case SK_Delete: return QStringLiteral("⌫ Backspace");
    case SK_Escape: return QStringLiteral("Esc");
    case SK_Shift: return QStringLiteral("⇧ Shift");
    case SK_Option: return QStringLiteral("Alt");
    case SK_Up: return QStringLiteral("↑");
    case SK_Down: return QStringLiteral("↓");
    case SK_Left: return QStringLiteral("←");
    case SK_Right: return QStringLiteral("→");
    default: return QStringLiteral("⇥ Tab");
    }
}

QString shortcutHint(const KeySpec &k) {
    QStringList l;
    for (int i = 0; i < k.nShortcuts; ++i) l << (k.shortcuts[i].special == SK_Return ? QStringLiteral("⏎") : shortcutLabel(k.shortcuts[i]));
    return l.join(QLatin1Char(' '));
}

QString shortcutHelp(const KeySpec &k) {
    QString face = QString::fromUtf8(k.face);
    if (k.nShortcuts == 0) return face;
    QStringList l;
    for (int i = 0; i < k.nShortcuts; ++i) l << shortcutLabel(k.shortcuts[i]);
    return face + QStringLiteral(" — keyboard: ") + l.join(QStringLiteral(" or "));
}

bool shortcutFromEvent(const QKeyEvent *e, Shortcut *out) {
    switch (e->key()) {
    case Qt::Key_Return: case Qt::Key_Enter: *out = {0, SK_Return}; return true;
    case Qt::Key_Backspace: case Qt::Key_Delete: *out = {0, SK_Delete}; return true;
    case Qt::Key_Escape: *out = {0, SK_Escape}; return true;
    case Qt::Key_Tab: *out = {0, SK_Tab}; return true;
    case Qt::Key_Left: *out = {0, SK_Left}; return true;
    case Qt::Key_Right: *out = {0, SK_Right}; return true;
    case Qt::Key_Down: *out = {0, SK_Down}; return true;
    case Qt::Key_Up: *out = {0, SK_Up}; return true;
    default: break;
    }
    // keep Shift (* + _ %) but not Alt (Alt changes characters on some layouts): the text the key types
    const QString t = e->text();
    if (t.isEmpty() || !t.at(0).isPrint()) {
        // Alt + key often reports no text: use the key itself (letters come as capitals; the match ignores case)
        int k = e->key();
        if (k >= 0x20 && k < 0x7F) { *out = {char32_t(k), -1}; return true; }
        return false;
    }
    const auto u = t.toUcs4();
    *out = {u[0], -1};
    return true;
}

// --- chords ----------------------------------------------------------------------------------------

const std::vector<ChordInfo> &allChords() {
    static const std::vector<ChordInfo> c = {
        {KeyChord::ServiceMenu, {32, 25}, QStringLiteral("g-ENTER-ON"), QStringLiteral("Service Menu"),
         QStringLiteral("Checksum (2), model (4), speed (5), key settings (6-8). ON leaves the menu."), false},
        {KeyChord::UsbMode, {31, 25}, QStringLiteral("f-ENTER-ON"), QStringLiteral("USB Mode"),
         QStringLiteral("On the calculator this waits for USB transfers. In the emulator the calculator just turns on."), false},
        {KeyChord::MemoryReset, {29}, QStringLiteral("ON + −"), QStringLiteral("Memory Reset"),
         QStringLiteral("Clears all programs, registers and settings (Pr Error)."), true},
        {KeyChord::SelfTest, {19}, QStringLiteral("ON + ×"), QStringLiteral("Self-Test"),
         QStringLiteral("Runs the calculator's self-test and shows all segments when it passes."), false},
        {KeyChord::SelfTestLoop, {39}, QStringLiteral("ON + +"), QStringLiteral("Continuous Self-Test"),
         QStringLiteral("Repeats the self-test until you press a key."), false},
        {KeyChord::KeyboardTest, {9}, QStringLiteral("ON + ÷"), QStringLiteral("Keyboard Test"),
         QStringLiteral("Press every key in order, row by row; ON ends the test."), false},
        {KeyChord::RadixMark, {37}, QStringLiteral("ON + ."), QStringLiteral("Swap Decimal Point and Comma"),
         QStringLiteral("Switches the number display between 1,234.56 and 1.234,56."), false},
    };
    return c;
}

const ChordInfo &chordInfo(KeyChord c) { for (const auto &i : allChords()) if (i.chord == c) return i; return allChords()[0]; }

bool chordAvailable(KeyChord c, int model, int family) {
    if (family == VC_FAMILY_STOCK) return c == KeyChord::SelfTest || c == KeyChord::MemoryReset || c == KeyChord::RadixMark;
    return c == KeyChord::RadixMark ? model != 16 : true;
}

// --- latch -----------------------------------------------------------------------------------------

static bool has(const std::vector<int> &v, int k) { return std::find(v.begin(), v.end(), k) != v.end(); }
static void eraseOne(std::vector<int> &v, int k) { v.erase(std::remove(v.begin(), v.end(), k), v.end()); }

std::vector<KeyLatch::Action> KeyLatch::unlatchAll() {
    std::vector<Action> a;
    for (int k : latched_) a.push_back({Action::Release, {k}});
    latched_.clear();
    return a;
}

std::vector<KeyLatch::Action> KeyLatch::toggle(int k) {
    if (has(latched_, k)) return unlatchAll();
    latched_.push_back(k); std::sort(latched_.begin(), latched_.end());
    return {{Action::Press, {k}}};
}

std::vector<KeyLatch::Action> KeyLatch::press(int k) {
    if (has(latched_, k)) { swallow_.push_back(k); return unlatchAll(); }
    return {{Action::Press, {k}}};
}

std::vector<KeyLatch::Action> KeyLatch::release(int k) {
    if (has(swallow_, k)) { eraseOne(swallow_, k); return {}; }
    if (has(latched_, k)) return {};
    std::vector<Action> a{{Action::Release, {k}}};
    if (!latched_.empty()) { a.push_back({Action::ReleaseLater, latched_}); latched_.clear(); }
    return a;
}

}  // namespace vc
