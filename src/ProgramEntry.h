// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
//
// ProgramEntry.h - key a whole program into the emulated HP-15C or HP-16C (0.7.8, program copy / paste; port:
// ProgramEntry.swift). Generalises ProgramEntry15: program mode, CLEAR PRGM, then every step's keys as a person would
// press them, so the calculator's own firmware encodes the steps and takes the memory they need (15C pool, 16C
// registers, stock or 206-slot layout). Registers, matrices and settings are kept. Each step is checked as it goes in
// and the final listing is compared step by step; on any problem the memory from before is put back.
// The memory file format is never touched: only keys are pressed.
#pragma once
#include "Machine.h"
#include "ProgramEntry15.h"
#include "ProgramText.h"
#include <functional>

namespace vc {

/// The face keys (Machine::tap indexes) for one step; empty when the step cannot be keyed on this model.
std::vector<int> stepKeys(int model, const ProgStep &s);

/// Replace the program with `steps` (the whole new program). Runs on the engine thread. Throws vc::Error with a message
/// for the user; the memory is then as before. `progress(done, total)` is called now and then.
ProgramEntryReport enterProgram(Machine &m, const std::vector<ProgStep> &steps, const std::function<void(int, int)> &progress = {});

/// The program after an edit (pure): delete steps first..last (1-based, inclusive), then insert `incoming` after step
/// `after` of the result (0 = at the top).
std::vector<ProgStep> deleteSteps(const std::vector<ProgStep> &program, int first, int last);
std::vector<ProgStep> insertSteps(const std::vector<ProgStep> &program, int after, const std::vector<ProgStep> &incoming);

}  // namespace vc
