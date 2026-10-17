// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
//
// KeyGlyphs.h - which character of the key-cap font (designed by Eric Rechlin) draws each HP-15C key legend, by core
// key code and colour: primary (key face), gold (f) and blue (g). Port of HP15CKeyGlyphs.swift (same tables).
#pragma once
#include "Inspector.h"
#include <QChar>
#include <optional>
#include <utility>
#include <vector>

namespace vc {

using Glyph = std::pair<QChar, QString>;       // character in the key font, the text it stands for

/// The key-cap glyphs for a 15C program line: the shift key, the function in its colour, then the arguments.
/// nullopt when a part has no glyph.
std::optional<std::vector<Glyph>> keyGlyphsFor(const ProgramLine &l);

}  // namespace vc
