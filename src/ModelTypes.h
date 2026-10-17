// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
//
// ModelTypes.h - what differs between the 12C, 15C and 16C faces, as data (port of CalculatorModel.swift).
// The key matrix is the same on all three: code = 10 * row + column, ON = 30, and the tall ENTER key is
// code 25 (it also covers 35).
#pragma once
#include <QString>
#include <cstdint>

namespace vc {

enum class KeyStyle { Normal, FShift, GShift, Enter, On };
enum SpecialKey { SK_Return, SK_Delete, SK_Escape, SK_Shift, SK_Option, SK_Up, SK_Down, SK_Left, SK_Right, SK_Tab };

struct Shortcut {
    char32_t ch;      // a character (compared case-insensitively), or 0
    int special;      // SpecialKey, or -1 when it is a character
};

struct KeySpec {
    int code;
    const char *name;     // name the core knows (vc_key_code)
    const char *face;     // printed on the key
    const char *f;        // gold legend above the key ("" = none)
    const char *g;        // blue legend on the key's front ("" = none)
    int nShortcuts;
    Shortcut shortcuts[3];
    KeyStyle style;
};

struct LegendBracket { const char *label; int fromCode, toCode; };
struct Annunciator { const char *label; int bit; double cell; };

}  // namespace vc
