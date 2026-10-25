// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
//
// ProgramEntry15.h - key a program into the emulated HP-15C (0.7.2, File ▸ Import Program (.15c)…; port of
// ProgramEntry15.swift): program mode, CLEAR PRGM, then every step's keys as a person would press them, so the
// calculator's own firmware encodes and stores the program. Registers and matrices are kept. Each step is checked as it
// goes in; at the end the listing is compared with the file. On any problem the memory from before is put back.
#pragma once
#include "Machine.h"
#include "Manz15C.h"

namespace vc {

struct ProgramEntryReport {
    int steps = 0;
    struct Mismatch { int step; QString file, calculator; };
    std::vector<Mismatch> mismatches;
};

/// Runs on the engine thread. Throws vc::Error with a message for the user (memory restored).
ProgramEntryReport enterProgram15(Machine &m, const std::vector<HP15CStep> &steps);

}  // namespace vc
