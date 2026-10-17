// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
//
// Model.h - the 12C / 15C / 16C as data (port of CalculatorModel.swift), key chords and the latch logic
// (port of KeyChords.swift).
#pragma once
#include "ModelTypes.h"
#include <QKeyEvent>
#include <QString>
#include <vector>

namespace vc {

class Model {
public:
    int id() const { return id_; }                       // 12, 15, 16
    QString name() const { return name_; }               // "15C"
    QString fileExtension() const { return ext_; }       // "bin15c" (0.6.9; the old .15c is still read)
    const KeySpec *keys() const { return keys_; }        // 39 entries
    int keyCount() const { return 39; }
    const KeySpec *key(int code) const;                  // ENTER also answers for 35
    const KeySpec *keyForShortcut(const Shortcut &s) const;
    const KeySpec *keyWithStyle(KeyStyle st) const;
    const std::vector<LegendBracket> &brackets() const { return brackets_; }
    const std::vector<Annunciator> &annunciators() const { return annunciators_; }
    double gZoneFraction() const { return gZone_; }
    static const Model &with(int id);                    // unknown ids give the 15C
    static const Model &hp12c(); static const Model &hp15c(); static const Model &hp16c();

private:
    Model(int id, const char *name, const char *ext, const KeySpec *keys, std::vector<LegendBracket> b,
          std::vector<Annunciator> a, double g)
        : id_(id), name_(QString::fromLatin1(name)), ext_(QString::fromLatin1(ext)), keys_(keys), brackets_(std::move(b)),
          annunciators_(std::move(a)), gZone_(g) {}
    int id_; QString name_, ext_; const KeySpec *keys_;
    std::vector<LegendBracket> brackets_; std::vector<Annunciator> annunciators_; double gZone_;
};

/// Text for a shortcut ("Q", "⏎ Return", "⌫ Delete"...).
QString shortcutLabel(const Shortcut &s);
QString shortcutHint(const KeySpec &k);         // compact: "⏎" for Return
QString shortcutHelp(const KeySpec &k);         // "ENTER — keyboard: ⏎ Return or ="
/// The shortcut a keyboard event stands for, if any (characters with Shift kept, as typed).
bool shortcutFromEvent(const QKeyEvent *e, Shortcut *out);

/// Key combinations with ON (Calculator ▸ Keys menu): with the calculator off, hold the keys, press ON, let go.
enum class KeyChord { ServiceMenu, UsbMode, MemoryReset, SelfTest, SelfTestLoop, KeyboardTest, RadixMark };
struct ChordInfo { KeyChord chord; std::vector<int> keys; QString written, title, explanation; bool needsConfirmation; };
const std::vector<ChordInfo> &allChords();
const ChordInfo &chordInfo(KeyChord c);
/// `family` is VC_MODEL_* / VC_FAMILY_*; HP's original 12C firmware (98) only offers three chords.
bool chordAvailable(KeyChord c, int model, int family);

/// Keys the user latched down (right-click) to play a chord one key at a time. Pure logic.
class KeyLatch {
public:
    struct Action { enum Kind { Press, Release, ReleaseLater } kind; std::vector<int> keys; };
    static constexpr int kReleaseDelayMs = 200;
    std::vector<Action> toggle(int k);
    std::vector<Action> press(int k);
    std::vector<Action> release(int k);
    std::vector<Action> unlatchAll();
    const std::vector<int> &latched() const { return latched_; }
    bool isEmpty() const { return latched_.empty(); }
private:
    std::vector<int> latched_;      // sorted
    std::vector<int> swallow_;
};

}  // namespace vc
