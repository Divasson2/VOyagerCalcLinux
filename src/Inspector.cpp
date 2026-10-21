// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
#include "Inspector.h"
#include <QStringList>
#include <algorithm>
#include <cstring>

namespace vc {

static RegisterValue convert(const vc_value &v) {
    RegisterValue r;
    r.name = QString::fromUtf8(v.name); r.text = QString::fromUtf8(v.text); r.decimal = QString::fromUtf8(v.dec);
    r.isInteger = v.kind == VC_VAL_INT; r.isNegative = v.negative != 0; r.number = v.number; r.word = v.word; r.raw = v.raw;
    return r;
}

int RegisterValue::matrixDescriptor() const {
    if (isInteger || (raw >> 52 & 0xF) != 1 || (raw & 0x0000FFFFFFFFFFFFull) != 0) return -1;
    int l = int(raw >> 48 & 0xF);
    return (l >= 0xA && l <= 0xE) ? l - 0xA : -1;
}

QString ProgramLine::number(int model) const { return model == 12 ? QStringLiteral("%1").arg(line, 2, 10, QLatin1Char('0')) : QStringLiteral("%1").arg(line, 3, 10, QLatin1Char('0')); }

QString ProgramLine::screenView(int model) const {
    const QString prefixText = number(model) + QLatin1Char('-');
    int cells = 0;
    for (QChar c : keys) if (c != QLatin1Char(',') && c != QLatin1Char('.')) ++cells;
    int pad = std::max(0, 10 - int(prefixText.size()) - cells);
    return prefixText + QString(pad, QLatin1Char(' ')) + keys;
}

QString Pool15::Segment::label() const {
    switch (kind) {
    case Kind::Workspace: return QStringLiteral("SOLVE/∫ workspace");
    case Kind::Free: return QStringLiteral("free");
    case Kind::Complex: return QStringLiteral("complex (imaginary stack)");
    case Kind::Storage: return count == 0 ? QString() : QStringLiteral("R2–R%1").arg(count + 1);
    case Kind::Matrix: return QStringLiteral("matrix %1").arg(Pool15::letter(matrix));
    default: return QStringLiteral("program");
    }
}

std::vector<Pool15::Segment> Pool15::segments() const {
    std::vector<Segment> s;
    int at = base;
    auto add = [&](Kind k, int n, int m = 0) { if (n > 0) s.push_back({k, m, at, n}); at += std::max(0, n); };
    add(Kind::Workspace, scratch);
    add(Kind::Free, gap);
    add(Kind::Complex, complex ? 5 : 0);
    at = data;
    add(Kind::Storage, storage);
    for (int m = 0; m < 5; ++m) { at = starts[m]; add(Kind::Matrix, rows[m] * cols[m], m); }
    at = pool;
    add(Kind::Free, programLow - pool);
    at = programLow;
    add(Kind::Program, programRegisters);
    return s;
}

bool Pool15::operator==(const Pool15 &o) const {
    return variant == o.variant && base == o.base && top == o.top && data == o.data && pool == o.pool && result == o.result
        && complex == o.complex && scratch == o.scratch && gap == o.gap && storage == o.storage && dimI == o.dimI
        && matrixRegisters == o.matrixRegisters && programLow == o.programLow && programRegisters == o.programRegisters
        && programBytes == o.programBytes && free == o.free && gmem == o.gmem
        && std::equal(starts, starts + 5, o.starts) && std::equal(rows, rows + 5, o.rows) && std::equal(cols, cols + 5, o.cols);
}

const RegisterValue *MatrixValue::value(int r, int c) const {
    int i = (r - 1) * cols + (c - 1);
    return i >= 0 && i < int(values.size()) ? &values[size_t(i)] : nullptr;
}

QString InspectorSnapshot::baseName() const {
    switch (base) { case 16: return QStringLiteral("HEX"); case 8: return QStringLiteral("OCT"); case 2: return QStringLiteral("BIN"); case 0: return QStringLiteral("FLOAT"); default: return QStringLiteral("DEC"); }
}
QString InspectorSnapshot::complementName() const {
    static const char *n[] = {"UNSGN", "1's", "2's"};
    return QString::fromLatin1(n[std::max(0, std::min(2, complement))]);
}
std::vector<int> InspectorSnapshot::setFlags() const {
    std::vector<int> f;
    for (int i = 0; i < 10; ++i) if (flags >> i & 1) f.push_back(i);
    return f;
}
QString InspectorSnapshot::modeSummary() const {
    if (model != 16) return QString();
    return base == 0 ? QStringLiteral("FLOAT") : QStringLiteral("%1  WSIZE %2  %3").arg(baseName()).arg(wordSize).arg(complementName());
}
const RegisterValue *InspectorSnapshot::imaginaryOf(const QString &name) const {
    if (!isComplex()) return nullptr;
    static const QStringList order = {QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z"), QStringLiteral("T"), QStringLiteral("LAST X")};
    int i = int(order.indexOf(name));
    return i >= 0 && i < int(imaginary.size()) ? &imaginary[size_t(i)] : nullptr;
}
QString InspectorSnapshot::descriptorText(const RegisterValue &v) const {
    int m = v.matrixDescriptor();
    if (model != 15 || m < 0) return QString();
    if (pool) return QStringLiteral("matrix %1 (%2)").arg(Pool15::letter(m), pool->dimension(m));
    return QStringLiteral("matrix %1").arg(Pool15::letter(m));
}
bool InspectorSnapshot::operator==(const InspectorSnapshot &o) const {
    return model == o.model && variant == o.variant && base == o.base && wordSize == o.wordSize && complement == o.complement
        && flags == o.flags && programMode == o.programMode && running == o.running && line == o.line && lines == o.lines
        && registerCount == o.registerCount && stack == o.stack && registers == o.registers && program == o.program
        && pool == o.pool && imaginary == o.imaginary && matrices == o.matrices;
}

Pool15 pool15FromCore(const vc_pool15 &p) {
    Pool15 o;
    o.variant = p.variant; o.base = p.base; o.top = p.top; o.data = p.data;
    for (int i = 0; i < 5; ++i) { o.starts[i] = p.start[i]; o.rows[i] = p.rows[i]; o.cols[i] = p.cols[i]; }
    o.pool = p.pool; o.result = p.result; o.complex = p.complex != 0; o.scratch = p.scratch; o.gap = p.gap;
    o.storage = p.storage; o.dimI = p.dim_i; o.matrixRegisters = p.matrices; o.programLow = p.prog_low;
    o.programRegisters = p.prog_regs; o.programBytes = p.prog_bytes; o.free = p.free;
    o.gmem = QString::fromLatin1(p.gmem);
    return o;
}

static void inspectPool15(Emulator &emu, InspectorSnapshot &s) {
    vc_calc *h = emu.handle();
    vc_pool15 p{};
    if (vc_inspect_pool15(h, &p) != VC_OK) return;
    Pool15 o = pool15FromCore(p);
    s.pool = o;
    vc_value v{};
    if (o.complex)
        for (int r : {VC_REG_X, VC_REG_Y, VC_REG_Z, VC_REG_T, VC_REG_LASTX})
            if (vc_inspect_imag(h, r, &v) == VC_OK) s.imaginary.push_back(convert(v));
    for (int m = 0; m < 5; ++m) {
        MatrixValue mv;
        mv.index = m; mv.rows = o.rows[m]; mv.cols = o.cols[m]; mv.first = o.starts[m]; mv.isResult = o.result == m;
        if (o.rows[m] * o.cols[m] <= 256)
            for (int r = 0; r < o.rows[m]; ++r)
                for (int c = 0; c < o.cols[m]; ++c)
                    if (vc_inspect_matrix(h, m, r + 1, c + 1, &v) == VC_OK) mv.values.push_back(convert(v));
        s.matrices.push_back(std::move(mv));
    }
}

std::optional<InspectorSnapshot> inspect(Emulator &emu, bool includeProgram, int maxRegisters) {
    vc_calc *h = emu.handle();
    vc_inspect_info info{};
    if (vc_inspect(h, &info) != VC_OK || info.model == 0) return std::nullopt;
    InspectorSnapshot s;
    s.model = info.model; s.variant = info.variant; s.base = info.base; s.wordSize = info.word_size;
    s.complement = info.complement; s.flags = info.flags; s.programMode = info.prgm_mode != 0; s.running = info.running != 0;
    s.line = info.line; s.lines = info.lines; s.registerCount = info.registers;
    s.hasI = info.has_i != 0; s.hasProgram = info.has_program != 0; s.extendedMemory = info.ext_memory != 0;
    vc_value v{};
    std::vector<int> regs;
    if (s.model == 12) regs = {VC_REG_X, VC_REG_Y, VC_REG_Z, VC_REG_T, VC_REG_LASTX, VC_REG_FIN_N, VC_REG_FIN_I, VC_REG_FIN_PV, VC_REG_FIN_PMT, VC_REG_FIN_FV};
    else { regs = {VC_REG_X, VC_REG_Y, VC_REG_Z, VC_REG_T, VC_REG_LASTX}; if (s.hasI) regs.push_back(VC_REG_I); }
    for (int r : regs) if (vc_inspect_stack(h, r, &v) == VC_OK) s.stack.push_back(convert(v));
    int n = std::min(s.registerCount, maxRegisters);
    s.registers.reserve(size_t(n));
    for (int i = 0; i < n; ++i) if (vc_inspect_register(h, i, &v) == VC_OK) s.registers.push_back(convert(v));
    if (s.model == 15) inspectPool15(emu, s);
    if (includeProgram && s.hasProgram && s.lines > 0) {
        std::vector<vc_prog_line> buf(size_t(s.lines));
        std::memset(buf.data(), 0, buf.size() * sizeof(vc_prog_line));
        int got = vc_inspect_program(h, 1, buf.data(), s.lines);
        for (int i = 0; i < got; ++i) {
            const vc_prog_line &p = buf[size_t(i)];
            ProgramLine l;
            l.line = p.line; l.keys = QString::fromUtf8(p.keys); l.mnemonic = QString::fromUtf8(p.mnemonic);
            l.prefix = p.prefix ? QString(QChar(uchar(p.prefix))) : QString();
            l.bytes = QByteArray(reinterpret_cast<const char *>(p.code), p.len);
            s.program.push_back(std::move(l));
        }
    }
    return s;
}

}  // namespace vc
