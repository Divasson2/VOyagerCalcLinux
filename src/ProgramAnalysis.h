// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
//
// ProgramAnalysis.h - what a program uses, read from its listing: labels (defined / called), registers,
// flags, 15C matrix and complex operations (port of ProgramAnalysis.swift).
#pragma once
#include "Inspector.h"
#include <QStringList>
#include <vector>

namespace vc {

struct ProgramAnalysis {
    struct Label {
        QString name;                        // "A", "7", ".5", "I (indirect)"; 12C: "05" (a line number)
        std::vector<int> defined, called;
        QStringList callKinds;
        bool isIndirect = false;
    };
    enum RegisterKind { Storage, Index, Indirect, Financial, Random, Matrix, Result };
    struct Register {
        QString name;                        // "R4", "R.6", "RA" (16C), "I", "(i)", "PMT", "Matrix A", "RAN#"
        RegisterKind kind = Storage;
        std::vector<int> steps;
        QStringList operations;
    };
    struct Flag {
        QString name;                        // "0" ... "9", "I (indirect)"
        std::vector<int> steps;
        QStringList operations;
        QString meaning;
    };
    struct MatrixOp {
        QString item;                        // "Matrix A", "RESULT", "MATRIX n", "DIM (i)", "Complex", "SOLVE/∫ workspace"
        std::vector<int> steps;
        QStringList operations, notes;
    };

    int model = 0;
    std::vector<Label> labels;
    std::vector<Register> registers;
    std::vector<Flag> flags;
    std::vector<MatrixOp> matrixOps;

    ProgramAnalysis() = default;
    ProgramAnalysis(const std::vector<ProgramLine> &lines, int model);
    std::vector<Label> undefinedLabels() const;
    const Label *label(const QString &n) const;
    const Register *reg(const QString &n) const;
    const Flag *flag(const QString &n) const;
    const MatrixOp *matrixOp(const QString &n) const;
};

}  // namespace vc
