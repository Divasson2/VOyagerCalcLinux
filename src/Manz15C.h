// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
//
// Manz15C.h - read and write HP-15C program files in the plain-text format of Torsten Manz's HP-15C Simulator
// (".15c" / ".txt"), see docs/MANZ-15C-FORMAT.md (port of Manz15CProgram.swift, 0.7.2). Pure logic, no UI.
// Written from the HP-15C's own keycode rules, not from the simulator's (GPL) source: only the documented file
// convention is shared. Only the keycodes between "{" and "}" are read; line numbers and mnemonics are documentation.
#pragma once
#include "ProgramDocument.h"
#include <QByteArray>
#include <QString>
#include <optional>
#include <utility>
#include <vector>

namespace vc {

/// One keycode token: key(0...49), dot(d) (".3"), or user (Manz's trailing "u" of USER-mode STO / RCL).
struct HP15CToken {
    enum Kind { Key, Dot, User } kind = Key;
    int value = 0;
    static HP15CToken key(int k) { return {Key, k}; }
    static HP15CToken dot(int d) { return {Dot, d}; }
    static HP15CToken user() { return {User, 0}; }
    bool operator==(const HP15CToken &o) const { return kind == o.kind && (kind == User || value == o.value); }
    bool operator!=(const HP15CToken &o) const { return !(*this == o); }
    bool isKey(int k) const { return kind == Key && value == k; }
    QString text() const;            // "42", " 0", ".3", " u"
};
using HP15CTokens = std::vector<HP15CToken>;

struct HP15CStep {
    HP15CTokens tokens;
    HP15CStep() = default;
    explicit HP15CStep(HP15CTokens t) : tokens(std::move(t)) {}
    HP15CTokens plainTokens() const;
    bool isUser() const;
    QString keycodeText() const;     // "42 21 11", "44 .3", "44 24  u"
    std::optional<QString> mnemonic() const;
    bool isValid() const { return mnemonic().has_value(); }
    bool operator==(const HP15CStep &o) const { return tokens == o.tokens; }
    bool operator!=(const HP15CStep &o) const { return !(*this == o); }
};

namespace HP15CKeycodes {
std::optional<QString> mnemonic(const HP15CTokens &t);
std::optional<HP15CTokens> parseManz(const QString &s);      // "42 21 11", "44 .3", ",5", "44 24 u", old "45 48 3"
std::optional<HP15CTokens> parseDisplay(const QString &s);   // the calculator's listing: "42,21,11", "44 .3", "43,30, 0"
}

/// The face keys to press for a step (physical key indexes of the 15C face, as Machine::tap takes them).
namespace HP15CKeyEntry {
constexpr int keyF = 31, keyG = 32, keySTO = 33, keyRCL = 34, keyRS = 20, keyRDN = 22, keyBSP = 24;
std::optional<int> keyIndex(int code);
std::optional<std::vector<int>> keys(const HP15CStep &s);
}

struct ManzNote { QString key, text; bool operator==(const ManzNote &o) const { return key == o.key && text == o.text; } };

struct Manz15CProgram {
    QString title, description;      // description: the #D: lines joined with "\n"
    std::vector<ManzNote> labelNotes; // "L-1", "R0", "RI", "R(i)", "F3" ... in file order
    std::vector<HP15CStep> steps;     // step 001 onwards (000 is implied)
    static constexpr int maxSteps = 999;

    struct ReadError {
        enum Kind { NotText, InvalidLine, UnknownStep, TooManySteps } kind = NotText;
        int line = 0; QString text;
        QString message() const;
        bool operator==(const ReadError &o) const { return kind == o.kind && line == o.line && text == o.text; }
    };

    static std::optional<QString> decodeText(const QByteArray &data);
    /// Parse; on failure returns nullopt and fills *err.
    static std::optional<Manz15CProgram> read(const QByteArray &data, ReadError *err = nullptr);
    static std::optional<Manz15CProgram> parse(const QString &text, ReadError *err = nullptr);

    QByteArray fileData() const;     // UTF-16LE + BOM, CRLF, no line end after the last rule
    QString text() const;
    static QString stepLine(int n, const HP15CStep *step);

    static std::optional<std::pair<QChar, QString>> documentName(const QString &key);
    static std::optional<QString> manzKey(QChar kind, const QString &name);
    void apply(ProgramDocument &doc) const;
    static std::vector<ManzNote> notes(const ProgramDocument &doc, const std::vector<ManzNote> &extra = {});
    /// Steps from the calculator's listing (ProgramLine::keys); `imported` keeps the "u" markers when the keycodes match.
    static std::optional<std::vector<HP15CStep>> stepsFromListing(const std::vector<QString> &keys, const std::vector<HP15CStep> *imported = nullptr,
                                                                  ReadError *err = nullptr);
    bool operator==(const Manz15CProgram &o) const { return title == o.title && description == o.description && labelNotes == o.labelNotes && steps == o.steps; }
};

}  // namespace vc
