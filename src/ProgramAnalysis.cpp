// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
#include "ProgramAnalysis.h"
#include <QMap>
#include <algorithm>

namespace vc {

static bool isNumberChar(QChar c) { return c.isDigit(); }
static bool isMatrixName(const QString &a) { return a.size() == 1 && QStringLiteral("ABCDE").contains(a[0]); }
static bool isLabelName(const QString &a, int model) {
    const QString letters = model == 16 ? QStringLiteral("ABCDEF") : model == 15 ? QStringLiteral("ABCDE") : QString();
    if (a.size() == 1) return isNumberChar(a[0]) || letters.contains(a[0]);
    if (a.size() == 2 && a[0] == QLatin1Char('.')) return isNumberChar(a[1]);
    return false;
}
static bool isRegisterName(const QString &a, int model) {
    if (a.size() == 1) return isNumberChar(a[0]) || (model == 16 && QStringLiteral("ABCDEF").contains(a[0]));
    if (a.size() == 2 && a[0] == QLatin1Char('.')) return isNumberChar(a[1]) || (model == 16 && QStringLiteral("ABCDEF").contains(a[1]));
    return false;
}
static bool in(const QString &s, std::initializer_list<const char *> l) { for (const char *x : l) if (s == QString::fromUtf8(x)) return true; return false; }
static QString u(const char *s) { return QString::fromUtf8(s); }
static void addUnique(std::vector<int> &v, int step) { if (v.empty() || v.back() != step) v.push_back(step); }
static void addOp(QStringList &l, const QString &op) { if (!l.contains(op)) l << op; }

static QString labelOrder(const ProgramAnalysis::Label &l, int model) {
    if (l.isIndirect) return QStringLiteral("9");
    if (model == 12) return QStringLiteral("0") + l.name;
    const QString &n = l.name;
    if (n.startsWith(QLatin1Char('.'))) return QStringLiteral("2") + n;
    if (!n.isEmpty() && isNumberChar(n[0])) return QStringLiteral("1") + n;
    return QStringLiteral("3") + n;
}
static QString regOrder(const QString &n) {
    static const QMap<QString, QString> fin = {{"n", "1"}, {"i", "2"}, {"PV", "3"}, {"PMT", "4"}, {"FV", "5"}};
    if (fin.contains(n)) return fin[n];
    if (n.startsWith(QStringLiteral("R."))) return QStringLiteral("2") + n;
    if (n.startsWith(QLatin1Char('R')) && n.size() > 1 && isNumberChar(n[1])) return QStringLiteral("1") + n;
    return QStringLiteral("3") + n;
}

static const QMap<QString, QString> &matrixFunctions() {
    static const QMap<QString, QString> m = {
        {"0", "MATRIX 0: all matrices to 0×0 (memory returned)"},
        {"1", "MATRIX 1: R0 = R1 = 1 (start of element-by-element access)"},
        {"2", "MATRIX 2: complex matrix, partitioned form to the inverse-ready form"},
        {"3", "MATRIX 3: inverse of MATRIX 2"},
        {"4", "MATRIX 4: transpose"},
        {"5", "MATRIX 5: transpose(Y) × X"},
        {"6", "MATRIX 6: residual"},
        {"7", "MATRIX 7: row norm"},
        {"8", "MATRIX 8: Frobenius norm"},
        {"9", "MATRIX 9: determinant"},
    };
    return m;
}

static std::vector<ProgramAnalysis::MatrixOp> matrixOperations(const std::vector<ProgramLine> &lines) {
    QMap<QString, ProgramAnalysis::MatrixOp> items;
    QStringList order;
    auto add = [&](const QString &item, const QString &op, int step, const QString &note = QString()) {
        if (!items.contains(item)) { ProgramAnalysis::MatrixOp m; m.item = item; items[item] = m; order << item; }
        auto &m = items[item];
        addUnique(m.steps, step);
        addOp(m.operations, op);
        if (!note.isEmpty() && !m.notes.contains(note)) m.notes << note;
    };
    auto sn3 = [](int step) { return QStringLiteral("%1").arg(step, 3, 10, QLatin1Char('0')); };
    for (const ProgramLine &l : lines) {
        QStringList t = l.mnemonic.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (t.isEmpty()) continue;
        const QString op = t[0];
        int step = l.line;
        if (op == u("DIM") && t.size() == 2) {
            if (isMatrixName(t[1])) add(u("Matrix ") + t[1], u("DIM"), step, QStringLiteral("dimensioned by the program at step %1 (size from the stack)").arg(sn3(step)));
            else if (t[1] == u("(i)")) add(u("DIM (i)"), u("DIM (i)"), step, QStringLiteral("storage registers set by the program at step %1 (from X)").arg(sn3(step)));
        } else if (op == u("MATRIX") && t.size() == 2) {
            add(u("MATRIX n"), u("MATRIX ") + t[1], step, matrixFunctions().value(t[1]));
        } else if (op == u("RESULT") && t.size() == 2 && isMatrixName(t[1])) {
            add(u("RESULT"), u("RESULT ") + t[1], step, QStringLiteral("results go to matrix %1").arg(t[1]));
            add(u("Matrix ") + t[1], u("RESULT"), step);
        } else if (op == u("I")) {
            add(u("Complex"), u("f I"), step, u("f I and Re≷Im turn on complex mode (flag 8)"));
        } else if (op == u("Re≷Im")) {
            add(u("Complex"), u("Re≷Im"), step, u("f I and Re≷Im turn on complex mode (flag 8)"));
        } else if (op == u("SF") || op == u("CF") || op == u("F?")) {
            if (t.size() == 2 && t[1] == u("8")) add(u("Complex"), op + u(" 8"), step, u("flag 8 = complex mode (5 registers)"));
        } else if (op == u("SOLVE")) {
            add(u("SOLVE/∫ workspace"), u("SOLVE"), step, u("SOLVE borrows 5 registers while it runs"));
        } else if (op == u("∫ʸₓ")) {
            add(u("SOLVE/∫ workspace"), u("∫ʸₓ"), step, u("∫ borrows 23 registers while it runs"));
        } else if (op == u("STO") || op == u("RCL") || op == u("x≷")) {
            QStringList rest = t.mid(1);
            QString name = op;
            if (rest.size() >= 2 && in(rest[0], {"+", "−", "×", "÷"})) { name += QLatin1Char(' ') + rest[0]; rest.removeFirst(); }
            if (rest.size() == 2 && rest[0] == u("DIM")) {
                if (isMatrixName(rest[1])) add(u("Matrix ") + rest[1], name + u(" DIM"), step);
                else if (rest[1] == u("(i)")) add(u("DIM (i)"), name + u(" DIM (i)"), step);
            } else if (rest.size() == 2 && rest[0] == u("MATRIX") && isMatrixName(rest[1])) {
                add(u("Matrix ") + rest[1], name + u(" MATRIX"), step, name == u("RCL") ? u("descriptor recalled to X (for matrix arithmetic)") : QString());
            } else if (rest.size() == 2 && rest[0] == u("g") && isMatrixName(rest[1])) {
                add(u("Matrix ") + rest[1], name + u(" g"), step, u("element at row Y, column X"));
            } else if (rest.size() == 1 && isMatrixName(rest[0])) {
                add(u("Matrix ") + rest[0], name, step, u("element at R0, R1 (USER mode steps the indices)"));
            } else if (rest.size() == 1 && rest[0] == u("RESULT")) {
                add(u("RESULT"), name + u(" RESULT"), step);
            }
        }
    }
    auto rank = [](const QString &i) {
        return i.startsWith(QStringLiteral("Matrix")) ? u("1") + i : i == u("RESULT") ? u("2") : i == u("MATRIX n") ? u("3")
             : i == u("DIM (i)") ? u("0") : i == u("Complex") ? u("4") : u("5");
    };
    std::vector<ProgramAnalysis::MatrixOp> out;
    for (const QString &k : order) out.push_back(items[k]);
    std::stable_sort(out.begin(), out.end(), [&](const auto &a, const auto &b) { return rank(a.item) < rank(b.item); });
    return out;
}

ProgramAnalysis::ProgramAnalysis(const std::vector<ProgramLine> &lines, int model_) : model(model_) {
    QMap<QString, Label> labs; QMap<QString, Register> regs; QMap<QString, Flag> fl;
    auto label = [&](const QString &n, bool indirect = false) -> Label & {
        if (!labs.contains(n)) { Label l; l.name = n; l.isIndirect = indirect; labs[n] = l; }
        return labs[n];
    };
    auto use = [&](const QString &name, RegisterKind kind, const QString &op, int step) {
        if (!regs.contains(name)) { Register r; r.name = name; r.kind = kind; regs[name] = r; }
        Register &r = regs[name];
        addUnique(r.steps, step);
        addOp(r.operations, op);
    };
    auto flag = [&](const QString &name, const QString &op, int step) {
        if (!fl.contains(name)) { Flag f; f.name = name; fl[name] = f; }
        Flag &f = fl[name];
        addUnique(f.steps, step);
        addOp(f.operations, op);
    };
    for (const ProgramLine &l : lines) {
        QStringList t = l.mnemonic.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (t.isEmpty()) continue;
        const QString op = t[0];
        const int step = l.line;
        if (op == u("LBL") && t.size() == 2 && isLabelName(t[1], model)) { label(t[1]).defined.push_back(step); continue; }
        if (in(op, {"GTO", "GSB", "SOLVE", "∫ʸₓ"}) && t.size() == 2) {
            const QString a = t[1];
            bool ok = false; int n = a.toInt(&ok);
            if (model == 12 && op == u("GTO") && a.size() == 2 && ok) {
                Label &k = label(a); k.called.push_back(step);
                addOp(k.callKinds, QStringLiteral("GTO"));
                // line 00 always exists (the top of program memory, 0.6.9); other lines only when the program reaches them
                if ((n == 0 || (n >= 1 && n <= int(lines.size()))) && k.defined.empty()) k.defined = {n};
                continue;
            }
            if (a == u("I") || a == u("(i)")) {
                Label &k = label(u("I (indirect)"), true); k.called.push_back(step);
                addOp(k.callKinds, op);
                use(u("I"), Index, op, step);
                continue;
            }
            if (isLabelName(a, model)) { Label &k = label(a); k.called.push_back(step); addOp(k.callKinds, op); }
            continue;
        }
        if (in(op, {"SF", "CF", "F?"}) && t.size() == 2 && model != 12) {
            if (t[1] == u("I") || t[1] == u("(i)")) { flag(u("I (indirect)"), op, step); use(u("I"), Index, op, step); }
            else if (t[1].size() == 1 && isNumberChar(t[1][0])) flag(t[1], op, step);
            continue;
        }
        if (model == 16) {
            if (op == u("DSZ") || op == u("ISZ")) { use(u("I"), Index, op, step); continue; }
            if (op == u("x≷I")) { use(u("I"), Index, u("x≷"), step); continue; }
            if (op == u("x≷(i)")) { use(u("(i)"), Indirect, u("x≷"), step); use(u("I"), Index, u("(i)"), step); continue; }
        }
        if (model == 15 && in(op, {"DIM", "RESULT"}) && t.size() == 2 && isMatrixName(t[1])) { use(u("Matrix ") + t[1], Matrix, op, step); continue; }
        if (model == 15 && (op == u("DIM") || op == u("MATRIX"))) continue;
        if (model == 15 && in(op, {"STO", "RCL"}) && t.size() == 3 && t[1] == u("DIM") && t[2] == u("(i)")) continue;
        if (!in(op, {"STO", "RCL", "x≷", "DSE", "ISG"}) || t.size() < 2) continue;
        QString opName = op;
        QStringList rest = t.mid(1);
        if (rest.size() >= 2 && in(rest[0], {"+", "−", "×", "÷"})) { opName += QLatin1Char(' ') + rest[0]; rest.removeFirst(); }
        if (rest.size() >= 2 && in(rest[0], {"g", "MATRIX", "DIM"})) {
            if (model == 12) continue;
            opName += QLatin1Char(' ') + rest[0]; rest.removeFirst();
        }
        if (rest.size() != 1) continue;
        const QString a = rest[0];
        if (a == u("I")) use(u("I"), Index, opName, step);
        else if (a == u("(i)")) { use(u("(i)"), Indirect, opName, step); use(u("I"), Index, u("(i)"), step); }
        else if (a == u("RAN#")) use(u("RAN#"), Random, opName, step);
        else if (a == u("RESULT")) use(u("RESULT"), Result, opName, step);
        else if (model == 12 && in(a, {"n", "i", "PV", "PMT", "FV"})) use(a, Financial, opName, step);
        else if (model == 15 && isMatrixName(a)) use(u("Matrix ") + a, Matrix, opName, step);
        else if (isRegisterName(a, model)) use(QStringLiteral("R") + a, Storage, opName, step);
    }
    if (model == 15) {
        for (auto it = fl.begin(); it != fl.end(); ++it) {
            if (it.key() == u("8")) it->meaning = u("complex mode (5 registers for the imaginary stack)");
            if (it.key() == u("9")) it->meaning = u("overflow (blinking display)");
        }
        matrixOps = matrixOperations(lines);
    }
    for (const Label &l : labs) labels.push_back(l);
    std::stable_sort(labels.begin(), labels.end(), [&](const Label &a, const Label &b) { return labelOrder(a, model) < labelOrder(b, model); });
    for (const Register &r : regs) registers.push_back(r);
    std::stable_sort(registers.begin(), registers.end(), [](const Register &a, const Register &b) {
        return a.kind != b.kind ? a.kind < b.kind : regOrder(a.name) < regOrder(b.name);
    });
    for (const Flag &f : fl) flags.push_back(f);
    std::stable_sort(flags.begin(), flags.end(), [](const Flag &a, const Flag &b) {
        return a.name.size() != b.name.size() ? a.name.size() < b.name.size() : a.name < b.name;
    });
}

std::vector<ProgramAnalysis::Label> ProgramAnalysis::undefinedLabels() const {
    std::vector<Label> o;
    for (const Label &l : labels) if (l.defined.empty() && !l.isIndirect) o.push_back(l);
    return o;
}
const ProgramAnalysis::Label *ProgramAnalysis::label(const QString &n) const { for (const auto &l : labels) if (l.name == n) return &l; return nullptr; }
const ProgramAnalysis::Register *ProgramAnalysis::reg(const QString &n) const { for (const auto &l : registers) if (l.name == n) return &l; return nullptr; }
const ProgramAnalysis::Flag *ProgramAnalysis::flag(const QString &n) const { for (const auto &l : flags) if (l.name == n) return &l; return nullptr; }
const ProgramAnalysis::MatrixOp *ProgramAnalysis::matrixOp(const QString &n) const { for (const auto &l : matrixOps) if (l.item == n) return &l; return nullptr; }

}  // namespace vc
