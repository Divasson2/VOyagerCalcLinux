// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
//
// Inspector.h - read-only view of the calculator's registers, modes, program, and the HP-15C memory pool
// (port of Inspector.swift and Pool15.swift; the decoding itself is in the C core, vc_inspect*).
#pragma once
#include "Emulator.h"
#include <QString>
#include <QStringList>
#include <optional>
#include <vector>

namespace vc {

struct RegisterValue {
    QString name;           // "X", "LAST X", "I", "R0", "R.1", "R615", "PMT"
    QString text;           // as the calculator shows it
    QString decimal;        // decimal value (16C integers per the complement mode)
    bool isInteger = false; // 16C integer mode
    bool isNegative = false;
    double number = 0;
    uint64_t word = 0, raw = 0;
    uint64_t stored() const { return isInteger ? word : raw; }
    /// A matrix descriptor (RCL MATRIX A or a matrix result): -1 or 0..4 = A..E.
    int matrixDescriptor() const;
    bool operator==(const RegisterValue &o) const {
        return name == o.name && text == o.text && decimal == o.decimal && isInteger == o.isInteger && stored() == o.stored();
    }
};

struct ProgramLine {
    int line = 0;
    QString keys;           // keycodes as the calculator shows them: "43,22, A"
    QString mnemonic;       // readable function, without the shift key
    QString prefix;         // "f", "g" or ""
    QByteArray bytes;
    QString fullMnemonic() const { return prefix.isEmpty() ? mnemonic : prefix + QLatin1Char(' ') + mnemonic; }
    /// The step as the display shows it in program mode, e.g. "001-42,21,11" (12C: "01-43,33 00").
    QString screenView(int model) const;
    QString number(int model) const;
    bool operator==(const ProgramLine &o) const { return line == o.line && keys == o.keys && mnemonic == o.mnemonic && prefix == o.prefix; }
};

struct Pool15 {
    int variant = 150, base = 0xA0, top = 0x100, data = 0xB2;
    int starts[5] = {0}, pool = 0xB2, rows[5] = {0}, cols[5] = {0};
    int result = 0;
    bool complex = false;
    int scratch = 0, gap = 0, storage = 0, dimI = 1, matrixRegisters = 0;
    int programLow = 0x100, programRegisters = 0, programBytes = 0, free = 0;
    QString gmem;           // "19 78 00-0"

    enum class Kind { Workspace, Free, Complex, Storage, Matrix, Program };
    struct Segment { Kind kind; int matrix = 0; int first = 0, count = 0; QString label() const; };
    int size() const { return top - base; }
    QString resultLetter() const { return QString(QChar('A' + std::max(0, std::min(4, result)))); }
    QString dimension(int m) const { return QStringLiteral("%1×%2").arg(rows[m]).arg(cols[m]); }
    std::vector<Segment> segments() const;
    QString caption() const { return QStringLiteral("g MEM %1  ·  pool %2 registers + R0, R1").arg(gmem).arg(size()); }
    static QString letter(int m) { return QString(QChar('A' + m)); }
    bool operator==(const Pool15 &o) const;
};

/// The C core's pool (vc_inspect_pool15 / vc_image_pool15) as the app's Pool15.
Pool15 pool15FromCore(const vc_pool15 &p);

struct MatrixValue {
    int index = 0, rows = 0, cols = 0, first = 0;
    bool isResult = false;
    std::vector<RegisterValue> values;
    QString letter() const { return Pool15::letter(index); }
    bool isEmpty() const { return rows * cols == 0; }
    const RegisterValue *value(int r, int c) const;
    QString tsv() const {
        QStringList lines;
        for (int r = 1; r <= rows; ++r) { QStringList cells; for (int c = 1; c <= cols; ++c) cells << (value(r, c) ? value(r, c)->text : QString()); lines << cells.join(QLatin1Char('\t')); }
        return lines.join(QLatin1Char('\n'));
    }
    bool operator==(const MatrixValue &o) const {
        return index == o.index && rows == o.rows && cols == o.cols && isResult == o.isResult && values == o.values;
    }
};

struct InspectorSnapshot {
    int model = 0, variant = 0;     // 12/15/16, 120/150/152/160
    int base = 10;                  // 16C: 16, 10, 8, 2, or 0 = FLOAT
    int wordSize = 0, complement = 0;
    uint32_t flags = 0;
    bool programMode = false, running = false;
    int line = 0, lines = 0, registerCount = 0;
    bool hasI = false, hasProgram = false, extendedMemory = false;
    std::vector<RegisterValue> stack, registers;
    std::vector<ProgramLine> program;
    std::optional<Pool15> pool;
    std::vector<RegisterValue> imaginary;
    std::vector<MatrixValue> matrices;

    QString baseName() const;
    QString complementName() const;
    std::vector<int> setFlags() const;
    QString modeSummary() const;    // "HEX  WSIZE 16  2's" on the 16C
    bool isComplex() const { return model == 15 && (flags >> 8 & 1); }
    const RegisterValue *imaginaryOf(const QString &name) const;
    QString descriptorText(const RegisterValue &v) const;
    bool operator==(const InspectorSnapshot &o) const;
    bool operator!=(const InspectorSnapshot &o) const { return !(*this == o); }
};

/// Registers, modes and (optionally) the program, or nullopt when there is nothing to inspect.
std::optional<InspectorSnapshot> inspect(Emulator &emu, bool includeProgram = true, int maxRegisters = 4096);

}  // namespace vc
