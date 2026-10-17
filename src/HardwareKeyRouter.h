// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
//
// HardwareKeyRouter.h - turns computer keyboard events into calculator keys (port of HardwareKeyRouter.swift).
// Shift tapped on its own = f, Alt tapped on its own = g (Shift still types * + _ % as usual). Alt + key = g,
// then the key. Each physical key holds its calculator key down while it is held, so chords work.
#pragma once
#include "Model.h"
#include <functional>
#include <map>

namespace vc {

class HardwareKeyRouter {
public:
    enum Modifier { ShiftKey, AltKey };
    std::function<const Model &()> model = []() -> const Model & { return Model::hp16c(); };
    std::function<void(int)> press = [](int) {};
    std::function<void(int, int)> pressShifted = [](int, int) {};      // shift key code (31 / 32), key code
    std::function<void(int)> release = [](int) {};

    /// True when the calculator used the key.
    bool keyDown(int id, const Shortcut &s, bool isRepeat) {
        lone_[ShiftKey] = lone_[AltKey] = false;
        const Model &m = model();
        const KeySpec *key = m.keyForShortcut(s);
        if (!key || key->style == KeyStyle::FShift || key->style == KeyStyle::GShift) return false;
        if (isRepeat || held_.count(id)) return true;
        held_[id] = key->code;
        if (altDown_ && key->g[0] != 0) {
            if (const KeySpec *g = m.keyWithStyle(KeyStyle::GShift)) { pressShifted(g->code, key->code); return true; }
        }
        press(key->code);
        return true;
    }
    bool keyUp(int id) {
        auto it = held_.find(id);
        if (it == held_.end()) return false;
        int code = it->second; held_.erase(it);
        release(code);
        return true;
    }
    void modifier(Modifier mod, bool down) {
        if (mod == AltKey) altDown_ = down;
        if (down) { lone_[mod] = true; return; }
        if (!lone_[mod]) return;
        lone_[mod] = false;
        if (const KeySpec *k = model().keyWithStyle(mod == ShiftKey ? KeyStyle::FShift : KeyStyle::GShift)) { press(k->code); release(k->code); }
    }
    void releaseAll() { for (auto &p : held_) release(p.second); held_.clear(); }
    bool holding() const { return !held_.empty(); }

private:
    std::map<int, int> held_;
    std::map<Modifier, bool> lone_;
    bool altDown_ = false;
};

}  // namespace vc
