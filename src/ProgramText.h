// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
//
// ProgramText.h - program steps as text (0.7.8, program copy / paste; port: ProgramText.swift). Pure logic, no UI.
//  * StepCatalog: every step the HP-15C / HP-16C can store, from the C core's own decoding tables (vc_decode_step):
//    keycodes as the calculator's listing shows them, the readable key names, and the bytes it takes in memory.
//  * parseProgramText(): read a listing in any of the accepted formats, line by line, with a message per line:
//    our clipboard text and "Export Listing as Text", HP-15C Simulator files ({ keycodes }), step + keycodes
//    ("001-42,21,11", "001 42 21 11", "012 { 43 22 A }") and key names only ("f LBL A", "STO 1", "x<>y").
//    Keycodes are authoritative when present; key names are only checked against them.
//  * programFragmentText(): what Copy puts on the clipboard.
//  * Label clashes and renumbering, and the memory-fit check before keying a program in.
// Written from the calculators' keycode rules and our own tables; no code or table from any other program is used.
#pragma once
#include "Inspector.h"
#include <QMap>
#include <QMultiMap>
#include <QSet>
#include <QString>
#include <QStringList>
#include <optional>
#include <vector>

namespace vc {

/// One program step: the calculator's keycodes ("42,21,11", "44 .3", "43,22, A") and, on the 15C, the USER flag
/// of a USER-mode STO / RCL A–E / (i) (the "u" the calculator shows at the right of the display).
struct ProgStep {
    QString keys;
    bool user = false;
    QString comment;            // travels with the step (program notes); not part of the identity
    bool operator==(const ProgStep &o) const { return keys == o.keys && user == o.user; }
    bool operator!=(const ProgStep &o) const { return !(*this == o); }
};

class StepCatalog {
public:
    struct Entry {
        QString keys;           // as the listing shows them
        QString mnemonic;       // as the inspector shows them, with the shift: "f LBL A", "g x≤y?", "STO (i)"
        int bytes = 1;          // memory taken (15C: 1 or 2)
        bool userCapable = false;
    };
    /// 15 or 16; nullptr for other models.
    static const StepCatalog *forModel(int model);
    int model() const { return model_; }
    QString modelName() const { return model_ == 15 ? QStringLiteral("HP-15C") : QStringLiteral("HP-16C"); }
    const std::vector<Entry> &entries() const { return entries_; }

    /// The step for keycodes written in any of the usual ways ("42,21,11", "42 21 11", "44 .3", "44 ,3", "45 48 3",
    /// "43 22 b", "44 24 u"). *user is set from a "u" token.
    const Entry *find(const QString &keycodes, bool *user = nullptr) const;
    /// Steps whose key name matches (see normalizeName). *exact: matched with the shift as written.
    std::vector<const Entry *> named(const QString &name, bool *user = nullptr) const;
    /// The entry with exactly this inspector mnemonic ("f LBL .1", "GSB A").
    const Entry *byMnemonic(const QString &mnemonic) const;
    int bytes(const ProgStep &s) const;
    QString displayKeys(const ProgStep &s) const { return s.user ? s.keys + QStringLiteral(" u") : s.keys; }
    QString mnemonic(const ProgStep &s) const;

    /// Canonical token form of keycodes ("43 22 B", "44 .3"), empty when not keycodes of this model.
    QString tokenKey(const QString &keycodes, bool *user = nullptr) const;
    /// Key-name form for matching: shift dropped by the caller, unicode glyphs to ASCII, aliases, no spaces, upper case.
    static QString normalizeName(const QString &name);

private:
    explicit StepCatalog(int model);
    void add(const Entry &e, const QStringList &names);
    int model_;
    std::vector<Entry> entries_;
    QMap<QString, int> byTokens_;
    QMap<QString, int> byMnemonic_;
    QMultiMap<QString, int> byFullName_, byBareName_;
};

struct ParsedLine {
    enum Kind { Blank, Note, Step, Error, Ambiguous };
    int line = 0;               // 1-based line number in the text
    QString text;
    Kind kind = Blank;
    ProgStep step;              // Step
    int block = 0;              // program file / header block the line belongs to (for label clashes)
    QString message;            // Error / Ambiguous / Note
    std::vector<ProgStep> choices;  // Ambiguous: the possible steps
};

struct ProgramParse {
    int model = 0;              // the model the text was read for
    int detectedModel = 0;      // from a header ("Model: HP-16C", "# VoyagerCalc program · HP-15C", a .15c file); 0 = none
    QString title;
    std::vector<ParsedLine> lines;
    std::vector<ProgStep> steps;
    std::vector<int> stepBlocks;    // block of each step
    int errors = 0, ambiguous = 0;
    QString modelError;         // the text is for another model
    bool ok() const { return errors == 0 && ambiguous == 0 && modelError.isEmpty() && !steps.empty(); }
    QString summary() const;    // "12 steps, 1 error"
};

/// Read program text for `model` (15 or 16). `choices`: line number -> keycodes chosen for an ambiguous key name.
ProgramParse parseProgramText(const QString &text, int model, const QMap<int, QString> &choices = {});
/// Text of a file: UTF-16 with BOM (HP-15C Simulator files), else UTF-8 (Latin-1 when it is not valid UTF-8).
QString decodeProgramFile(const QByteArray &data);

/// What Copy puts on the clipboard: a header naming the model, then "nnn-keycodes  key names  ; comment" per step.
QString programFragmentText(int model, const std::vector<ProgStep> &steps, int firstStep, const QString &title = QString());
/// The same steps as an HP-15C Simulator text (15C only; empty when a step cannot be written).
QString programManzText(const std::vector<ProgStep> &steps, const QString &title = QString());
/// Steps of a listing (InspectorSnapshot::program); the 15C USER flag is read from the step's bytes.
std::vector<ProgStep> stepsFromListing(int model, const std::vector<ProgramLine> &lines, const QMap<int, QString> &comments = {});

// ---- labels ---------------------------------------------------------------------------------------------------------

struct LabelUse { QSet<QString> defined, referenced; };
/// Labels a program defines (LBL) and uses (GTO, GSB, SOLVE, ∫); indirect GTO I / GSB I are not labels.
LabelUse labelUse(int model, const std::vector<ProgStep> &steps);
bool isLetterLabel(const QString &label);

struct RenumberPlan {
    struct Block { QMap<QString, QString> map; QStringList clashes, letterClashes, unresolved; };
    std::vector<Block> blocks;   // per pasted block, in order
    bool letterRenumbering = false;
    bool anyClash() const;
    bool anyLetterClash() const;
    QStringList describe() const;    // "LBL 1 → LBL 4", "LBL A is already used (not renumbered)"
};
/// Label clashes between the program in the calculator (`existing`) and the pasted steps (by block, in order), and the
/// renumbering: clashing digit labels always get a free digit label; letter labels only when `renumberLetters`.
RenumberPlan planRenumber(int model, const std::vector<ProgStep> &existing, const std::vector<ProgStep> &incoming,
                          const std::vector<int> &blocks, bool renumberLetters);
std::vector<ProgStep> applyRenumber(int model, const std::vector<ProgStep> &incoming, const std::vector<int> &blocks, const RenumberPlan &plan);

// ---- memory ---------------------------------------------------------------------------------------------------------

struct FitReport {
    bool fits = true;
    int need = 0, available = 0;     // 15C: bytes; 16C: lines
    int registersAfter = -1;         // 16C: storage registers left at this word size
    QStringList lostRegisters;       // 16C: registers holding a value that the longer program takes over
    QString message;                 // why it does not fit, or the warning
};
/// Will `program` (the whole new program) fit the calculator in the state `s` (a snapshot with registers)?
FitReport checkFit(const InspectorSnapshot &s, const std::vector<ProgStep> &program);
/// 16C: storage registers left when the program has `lines` lines (slots of 7 bytes, registers of ceil(ws/4) nibbles).
int registers16(int slotCount, int lines, int wordSize);

}  // namespace vc
