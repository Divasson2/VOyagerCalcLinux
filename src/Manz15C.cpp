// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
//
// Manz15C.cpp - HP-15C Simulator program files (see Manz15C.h; port of Manz15CProgram.swift, 0.7.2).
#include "Manz15C.h"
#include <QMap>
#include <QStringDecoder>
#include <algorithm>
#include <functional>

namespace vc {

static QString u(const char *s) { return QString::fromUtf8(s); }

QString HP15CToken::text() const {
    switch (kind) {
    case Key: return value < 10 ? QStringLiteral(" %1").arg(value) : QString::number(value);
    case Dot: return QStringLiteral(".%1").arg(value);
    case User: return QStringLiteral(" u");
    }
    return {};
}

HP15CTokens HP15CStep::plainTokens() const {
    HP15CTokens o; for (auto &t : tokens) if (t.kind != HP15CToken::User) o.push_back(t); return o;
}
bool HP15CStep::isUser() const { return std::any_of(tokens.begin(), tokens.end(), [](auto &t) { return t.kind == HP15CToken::User; }); }
QString HP15CStep::keycodeText() const {
    QStringList l; for (auto &t : tokens) l << t.text(); return l.join(QLatin1Char(' '));
}
std::optional<QString> HP15CStep::mnemonic() const { return HP15CKeycodes::mnemonic(tokens); }

// ---- keycodes -----------------------------------------------------------------------------------------------------

namespace HP15CKeycodes {
namespace {
const QMap<int, QString> &plain() {
    static const QMap<int, QString> m = {{10, u("÷")}, {11, u("√x̅")}, {12, u("eˣ")}, {13, u("10ˣ")}, {14, u("yˣ")}, {15, u("1/x")}, {16, u("CHS")},
        {20, u("×")}, {23, u("SIN")}, {24, u("COS")}, {25, u("TAN")}, {26, u("EEX")}, {30, u("−")}, {31, u("R/S")}, {33, u("R⬇")}, {34, u("x↔y")},
        {36, u("ENTER")}, {40, u("+")}, {48, u(".")}, {49, u("Σ+")}};
    return m;
}
const QMap<int, QString> &gold() {
    static const QMap<int, QString> m = {{0, u("x!")}, {1, u("→R")}, {2, u("→H.MS")}, {3, u("→RAD")}, {24, u("(i)")}, {25, u("I")}, {30, u("Re↔Im")},
        {31, u("PSE")}, {32, u("CLEAR Σ")}, {34, u("CLEAR REG")}, {36, u("RAN #")}, {40, u("Py,x")}, {44, u("FRAC")}, {48, u("ŷ,r")}, {49, u("L.R.")}};
    return m;
}
const QMap<int, QString> &blue() {
    static const QMap<int, QString> m = {{0, u("x̄")}, {1, u("→P")}, {2, u("→H")}, {3, u("→DEG")}, {7, u("DEG")}, {8, u("RAD")}, {9, u("GRD")},
        {10, u("x≤y")}, {11, u("x²")}, {12, u("LN")}, {13, u("LOG")}, {14, u("%")}, {15, u("Δ%")}, {16, u("ABS")}, {20, u("x=0")}, {23, u("SIN⁻¹")},
        {24, u("COS⁻¹")}, {25, u("TAN⁻¹")}, {26, u("π")}, {32, u("RTN")}, {33, u("R⬆")}, {34, u("RND")}, {35, u("CLx")}, {36, u("LSTx")},
        {40, u("Cy,x")}, {44, u("INT")}, {48, u("s")}, {49, u("Σ−")}};
    return m;
}
const char *kTests[] = {"x≠0", "x>0", "x<0", "x≥0", "x≤0", "x=y", "x≠y", "x>y", "x<y", "x≥y"};
using Opt = std::optional<QString>;
using Arg = std::function<Opt(const HP15CToken &)>;

Opt letterOf(int k) { if (k >= 11 && k <= 15) return QString(QChar(u'A' + (k - 11))); return std::nullopt; }
Opt digit(const HP15CToken &t) { if (t.kind == HP15CToken::Key && t.value <= 9) return QString::number(t.value); return std::nullopt; }
Opt label(const HP15CToken &t) {
    if (auto d = digit(t)) return d;
    if (t.kind == HP15CToken::Dot) return QStringLiteral(".%1").arg(t.value);
    if (t.kind == HP15CToken::Key) return letterOf(t.value);
    return std::nullopt;
}
Opt reg(const HP15CToken &t) {
    if (auto d = digit(t)) return d;
    if (t.kind == HP15CToken::Dot) return QStringLiteral(".%1").arg(t.value);
    if (t.isKey(25)) return u("I");
    if (t.isKey(24)) return u("(i)");
    return std::nullopt;
}
Opt matrix(const HP15CToken &t) { if (t.kind == HP15CToken::Key) return letterOf(t.value); return std::nullopt; }
Opt trig(const HP15CToken &t) { if (t.isKey(23)) return u("SIN"); if (t.isKey(24)) return u("COS"); if (t.isKey(25)) return u("TAN"); return std::nullopt; }
Opt digitOrI(const HP15CToken &t) { if (t.isKey(25)) return u("I"); return digit(t); }
Opt orElse(Opt a, Opt b) { return a ? a : b; }
Opt arith(int k) { switch (k) { case 10: return u("÷"); case 20: return u("×"); case 30: return u("−"); case 40: return u("+"); } return std::nullopt; }

Opt storeRecall(const QString &name, const HP15CTokens &r, bool recall) {
    if (r.empty()) return std::nullopt;
    const HP15CToken b = r[0];
    HP15CTokens arg(r.begin() + 1, r.end());
    if (arg.size() == 1 && arg[0].kind == HP15CToken::User) {
        if (auto x = orElse(matrix(b), b.isKey(24) ? Opt(u("(i)")) : std::nullopt)) return name + u(" ") + *x;
    }
    if (arg.empty()) {
        if (auto x = orElse(reg(b), matrix(b))) return name + u(" ") + *x;
        if (b.isKey(36)) return name + u(" RAN #");
        if (b.isKey(26)) return name + u(" RESULT");
        if (recall && b.isKey(49)) return u("RCL Σ+");
        return std::nullopt;
    }
    if (arg.size() != 1 || b.kind != HP15CToken::Key) return std::nullopt;
    const int k = b.value; const HP15CToken x = arg[0];
    if (auto op = arith(k)) if (auto rr = orElse(reg(x), matrix(x))) return name + u(" ") + *op + u(" ") + *rr;
    if (k == 16) if (auto m = matrix(x)) return name + u(" MATRIX ") + *m;
    if (k == 43) if (auto m = orElse(matrix(x), x.isKey(24) ? Opt(u("(i)")) : std::nullopt)) return name + u(" g ") + *m;
    if (recall && k == 23) if (auto m = orElse(matrix(x), x.isKey(24) ? Opt(u("(i)")) : x.isKey(25) ? Opt(u("I")) : std::nullopt)) return u("RCL DIM ") + *m;
    return std::nullopt;
}

HP15CTokens normalized(const HP15CTokens &t) {
    if (t.size() < 3) return t;
    HP15CTokens out;
    for (size_t i = 0; i < t.size();) {
        if (i > 0 && t[i].isKey(48) && i + 1 < t.size() && t[i + 1].kind == HP15CToken::Key && t[i + 1].value <= 9) {
            out.push_back(HP15CToken::dot(t[i + 1].value)); i += 2;
        } else out.push_back(t[i++]);
    }
    return out;
}

std::optional<HP15CTokens> parseWords(const QStringList &words, bool allowComma) {
    HP15CTokens out;
    for (const QString &w : words) {
        if (w.isEmpty()) continue;
        if (w == u("u")) { out.push_back(HP15CToken::user()); continue; }
        if (w.size() == 2 && (w[0] == u'.' || (allowComma && w[0] == u',')) && w[1].isDigit() && w[1].unicode() < 128) {
            out.push_back(HP15CToken::dot(w[1].unicode() - '0')); continue;
        }
        if (w.size() > 2) return std::nullopt;
        for (QChar c : w) if (c.unicode() < '0' || c.unicode() > '9') return std::nullopt;
        out.push_back(HP15CToken::key(w.toInt()));
    }
    return out;
}
}  // namespace

Opt mnemonic(const HP15CTokens &t) {
    if (t.empty() || t[0].kind != HP15CToken::Key) return std::nullopt;
    const int a = t[0].value;
    HP15CTokens rest(t.begin() + 1, t.end());
    if (a >= 0 && a <= 9) return rest.empty() ? Opt(QString::number(a)) : std::nullopt;
    if (a == 42 || a == 43) {
        if (rest.empty() || rest[0].kind != HP15CToken::Key) return std::nullopt;
        const int k = rest[0].value;
        HP15CTokens arg(rest.begin() + 1, rest.end());
        const QString sh = a == 42 ? u("f ") : u("g ");
        auto op = [&](const char *name, const Arg &f) -> Opt {
            if (arg.size() != 1) return std::nullopt;
            auto x = f(arg[0]); if (!x) return std::nullopt;
            return sh + u(name) + u(" ") + *x;
        };
        if (a == 42) {
            switch (k) {
            case 4: return op("x↔", reg);
            case 5: return op("DSE", reg);
            case 6: return op("ISG", reg);
            case 7: return op("FIX", digitOrI);
            case 8: return op("SCI", digitOrI);
            case 9: return op("ENG", digitOrI);
            case 10: return op("SOLVE", label);
            case 20: return op("∫xy", label);
            case 16: return op("MATRIX", digit);
            case 21: return op("LBL", label);
            case 22: return op("HYP", trig);
            case 23: return op("DIM", [](const HP15CToken &x) { return orElse(matrix(x), x.isKey(24) ? Opt(u("(i)")) : x.isKey(25) ? Opt(u("I")) : std::nullopt); });
            case 26: return op("RESULT", matrix);
            default: if (arg.empty() && gold().contains(k)) return sh + gold()[k]; return std::nullopt;
            }
        }
        switch (k) {
        case 4: return op("SF", digitOrI);
        case 5: return op("CF", digitOrI);
        case 6: return op("F?", digitOrI);
        case 22: return op("HYP⁻¹", trig);
        case 30: return op("TEST", [](const HP15CToken &x) -> Opt { if (auto d = digit(x)) return u(kTests[d->toInt()]); return std::nullopt; });
        default: if (arg.empty() && blue().contains(k)) return sh + blue()[k]; return std::nullopt;
        }
    }
    if (a == 22 || a == 32) {
        if (rest.size() != 1) return std::nullopt;
        auto x = orElse(label(rest[0]), rest[0].isKey(25) ? Opt(u("I")) : std::nullopt);
        if (!x) return std::nullopt;
        return (a == 22 ? u("GTO ") : u("GSB ")) + *x;
    }
    if (a == 44 || a == 45) return storeRecall(a == 44 ? u("STO") : u("RCL"), rest, a == 45);
    if (rest.empty() && plain().contains(a)) return plain()[a];
    return std::nullopt;
}

std::optional<HP15CTokens> parseManz(const QString &s) {
    QString x = s; x.replace(QLatin1Char('\t'), QLatin1Char(' '));
    auto t = parseWords(x.split(QLatin1Char(' '), Qt::SkipEmptyParts), true);
    if (!t) return std::nullopt;
    return normalized(*t);
}

std::optional<HP15CTokens> parseDisplay(const QString &s) {
    QString x = s; x.replace(QLatin1Char(','), QLatin1Char(' '));
    auto t = parseWords(x.split(QLatin1Char(' '), Qt::SkipEmptyParts), false);
    if (!t) return std::nullopt;
    return normalized(*t);
}
}  // namespace HP15CKeycodes

// ---- keys to press --------------------------------------------------------------------------------------------------

namespace HP15CKeyEntry {
std::optional<int> keyIndex(int code) {
    switch (code) {
    case 7: case 8: case 9: return code - 1;
    case 4: case 5: case 6: return code + 12;
    case 1: case 2: case 3: return code + 25;
    case 0: return 36;
    default: break;
    }
    if (code >= 10 && code <= 49) {
        const int row = code / 10, col = code % 10;
        if (!((col >= 1 && col <= 6) || col == 0 || code == 48 || code == 49) || code == 46) return std::nullopt;
        return (row - 1) * 10 + (col == 0 ? 9 : col - 1);
    }
    return std::nullopt;
}

std::optional<std::vector<int>> keys(const HP15CStep &step) {
    if (!step.isValid()) return std::nullopt;
    std::vector<int> out;
    const HP15CTokens t = step.plainTokens();
    for (size_t i = 0; i < t.size(); ++i) {
        const HP15CToken &tok = t[i];
        if (tok.kind == HP15CToken::Key) {
            const int k = tok.value;
            if (i == 1 && (t[0].isKey(44) || t[0].isKey(45)) && (k == 16 || k == 23 || k == 26 || k == 36)) out.push_back(keyF);
            auto idx = keyIndex(k); if (!idx) return std::nullopt;
            out.push_back(*idx);
        } else if (tok.kind == HP15CToken::Dot) {
            auto p = keyIndex(48), n = keyIndex(tok.value);
            if (!p || !n) return std::nullopt;
            out.push_back(*p); out.push_back(*n);
        }
    }
    return out;
}
}  // namespace HP15CKeyEntry

// ---- the file -------------------------------------------------------------------------------------------------------

QString Manz15CProgram::ReadError::message() const {
    switch (kind) {
    case NotText: return u("The file is not a text file.");
    case InvalidLine: return u("Line %1 is not a program step (no keycodes between { and }): “%2”.").arg(line).arg(text);
    case UnknownStep: return u("Line %1 has keycodes the HP-15C does not know as a program step: {%2}.").arg(line).arg(text);
    case TooManySteps: return u("The program has %1 steps; a program file holds at most 999.").arg(line);
    }
    return {};
}

static QString decodeCp1252(const QByteArray &d) {
    static const char16_t hi[32] = {0x20AC, 0x81, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x8D, 0x017D, 0x8F,
                                     0x90, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D, 0x017E, 0x0178};
    QString s; s.reserve(d.size());
    for (char c : d) { const unsigned char b = c; s += QChar(b >= 0x80 && b < 0xA0 ? hi[b - 0x80] : char16_t(b)); }
    return s;
}

std::optional<QString> Manz15CProgram::decodeText(const QByteArray &data) {
    if (data.size() >= 2 && uchar(data[0]) == 0xFF && uchar(data[1]) == 0xFE) {
        QByteArray body = data.mid(2);
        if (body.size() % 2) return std::nullopt;
        QStringDecoder dec(QStringDecoder::Utf16LE);
        QString s = dec.decode(body);
        if (dec.hasError()) return std::nullopt;
        return s;
    }
    if (data.size() >= 3 && uchar(data[0]) == 0xEF && uchar(data[1]) == 0xBB && uchar(data[2]) == 0xBF) {
        QStringDecoder dec(QStringDecoder::Utf8);
        QString s = dec.decode(data.mid(3));
        if (dec.hasError()) return std::nullopt;
        return s;
    }
    QStringDecoder dec(QStringDecoder::Utf8);
    QString s = dec.decode(data);
    if (!dec.hasError()) return s;
    return decodeCp1252(data);
}

std::optional<Manz15CProgram> Manz15CProgram::read(const QByteArray &data, ReadError *err) {
    auto text = decodeText(data);
    if (!text) { if (err) *err = {ReadError::NotText, 0, {}}; return std::nullopt; }
    return parse(*text, err);
}

std::optional<Manz15CProgram> Manz15CProgram::parse(const QString &text, ReadError *err) {
    auto fail = [&](ReadError::Kind k, int line, const QString &t) -> std::optional<Manz15CProgram> {
        if (err) *err = {k, line, t};
        return std::nullopt;
    };
    Manz15CProgram p;
    QStringList desc;
    bool first = true;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        const int n = i + 1;
        const QString line = lines[i].trimmed();
        if (line.isEmpty()) continue;
        if (line.startsWith(QLatin1Char('#'))) {
            const int colon = line.indexOf(QLatin1Char(':'));
            if (colon < 0) continue;
            const QString key = line.mid(1, colon - 1), value = line.mid(colon + 1);
            if (key.isEmpty() || !QStringLiteral("TDLRF").contains(key[0])) continue;
            if (key == u("D")) desc << value;
            else if (key == u("T")) p.title = value;
            else if (key[0] != u'D' && key[0] != u'T') p.labelNotes.push_back({key, value});
            continue;
        }
        const int open = line.indexOf(QLatin1Char('{')), close = line.lastIndexOf(QLatin1Char('}'));
        if (open < 0 || close < 0 || open >= close) return fail(ReadError::InvalidLine, n, line);
        const QString inner = line.mid(open + 1, close - open - 1);
        const QString tokens = inner.trimmed();
        if (tokens.isEmpty()) {
            if (first) { first = false; continue; }
            return fail(ReadError::UnknownStep, n, inner);
        }
        first = false;
        auto t = HP15CKeycodes::parseManz(tokens);
        if (!t || !HP15CKeycodes::mnemonic(*t)) return fail(ReadError::UnknownStep, n, tokens);
        p.steps.emplace_back(*t);
    }
    p.description = desc.join(QLatin1Char('\n'));
    if (int(p.steps.size()) > maxSteps) return fail(ReadError::TooManySteps, int(p.steps.size()), {});
    return p;
}

static QString rule() { return u("# ") + QString(78, QLatin1Char('-')); }

QByteArray Manz15CProgram::fileData() const {
    QByteArray d("\xFF\xFE", 2);
    const QString t = text();
    d.append(reinterpret_cast<const char *>(t.utf16()), t.size() * 2);     // QString is UTF-16 in host order; all targets are little-endian
    return d;
}

QString Manz15CProgram::stepLine(int n, const HP15CStep *step) {
    const QString keys = step ? step->keycodeText() : QString();
    return QStringLiteral("   %1 {").arg(n, 3, 10, QLatin1Char('0')) + keys.rightJustified(12, QLatin1Char(' ')) + u(" } ")
        + (step ? step->mnemonic().value_or(QString()) : QString());
}

QString Manz15CProgram::text() const {
    QStringList l = {rule(), u("# HEWLETT·PACKARD 15C Simulator program"), u("# Created with version 4.6.00"), rule()};
    if (!title.isEmpty()) l << u("#T:") + title;
    if (!description.isEmpty()) for (const QString &d : description.split(QLatin1Char('\n'))) l << u("#D:") + d;
    for (auto &n : labelNotes) l << u("#") + n.key + u(":") + n.text;
    l << rule() << QString() << stepLine(0, nullptr);
    for (size_t i = 0; i < steps.size(); ++i) l << stepLine(int(i) + 1, &steps[i]);
    l << QString() << rule();
    return l.join(u("\r\n"));
}

// ---- notes <-> ProgramDocument -----------------------------------------------------------------------------------------

std::optional<std::pair<QChar, QString>> Manz15CProgram::documentName(const QString &key) {
    if (key.isEmpty()) return std::nullopt;
    const QChar k = key[0]; const QString rest = key.mid(1);
    bool ok = false; const int n = rest.toInt(&ok);
    if (k == u'L') {
        if (!ok) return std::nullopt;
        if (n >= -5 && n <= -1) return std::make_pair(k, QString(QChar(u'A' + (-n - 1))));
        if (n >= 0 && n <= 9) return std::make_pair(k, QString::number(n));
        if (n >= 10 && n <= 19) return std::make_pair(k, QStringLiteral(".%1").arg(n - 10));
        return std::nullopt;
    }
    if (k == u'R') {
        if (rest == u("I") || rest == u("(i)")) return std::make_pair(k, rest);
        if (!ok) return std::nullopt;
        if (n >= 0 && n <= 9) return std::make_pair(k, QStringLiteral("R%1").arg(n));
        if (n >= 10 && n <= 19) return std::make_pair(k, QStringLiteral("R.%1").arg(n - 10));
        return std::nullopt;
    }
    if (k == u'F') { if (ok && n >= 0 && n <= 9) return std::make_pair(k, QString::number(n)); return std::nullopt; }
    return std::nullopt;
}

std::optional<QString> Manz15CProgram::manzKey(QChar kind, const QString &name) {
    bool ok = false;
    if (kind == u'L') {
        if (name.size() == 1 && name[0] >= u'A' && name[0] <= u'E') return QStringLiteral("L-%1").arg(name[0].unicode() - u'A' + 1);
        int n = name.toInt(&ok); if (ok && n >= 0 && n <= 9 && !name.startsWith(QLatin1Char('.'))) return QStringLiteral("L%1").arg(n);
        if (name.startsWith(QLatin1Char('.'))) { n = name.mid(1).toInt(&ok); if (ok && n >= 0 && n <= 9) return QStringLiteral("L%1").arg(n + 10); }
        return std::nullopt;
    }
    if (kind == u'R') {
        if (name == u("I") || name == u("(i)")) return u("R") + name;
        if (name.startsWith(u("R."))) { int n = name.mid(2).toInt(&ok); if (ok && n >= 0 && n <= 9) return QStringLiteral("R%1").arg(n + 10); return std::nullopt; }
        if (name.startsWith(u("R"))) { int n = name.mid(1).toInt(&ok); if (ok && n >= 0 && n <= 9) return QStringLiteral("R%1").arg(n); }
        return std::nullopt;
    }
    if (kind == u'F') { int n = name.toInt(&ok); if (ok && n >= 0 && n <= 9) return QStringLiteral("F%1").arg(n); }
    return std::nullopt;
}

void Manz15CProgram::apply(ProgramDocument &doc) const {
    if (!title.isEmpty()) doc.title = title;
    if (!description.isEmpty()) doc.description = description;
    for (auto &n : labelNotes) {
        auto d = documentName(n.key);
        if (!d) continue;
        if (d->first == u'L') doc.labels[d->second] = n.text;
        else if (d->first == u'R') doc.registers[d->second].meaning = n.text;
        else doc.flags[d->second] = n.text;
    }
}

std::vector<ManzNote> Manz15CProgram::notes(const ProgramDocument &doc, const std::vector<ManzNote> &extra) {
    auto order = [](const QString &key) {
        const QChar k = key[0]; const QString rest = key.mid(1);
        const int kind = k == u'L' ? 0 : k == u'R' ? 1 : 2;
        if (rest == u("I")) return std::make_pair(kind, 100);
        if (rest == u("(i)")) return std::make_pair(kind, 101);
        bool ok = false; int n = rest.toInt(&ok);
        if (!ok) return std::make_pair(kind, 200);
        return std::make_pair(kind, n < 0 ? -100 - n : n);       // labels A...E (-1...-5) first, in that order
    };
    std::vector<ManzNote> out;
    for (auto it = doc.labels.begin(); it != doc.labels.end(); ++it)
        if (!it.value().isEmpty()) if (auto k = manzKey(u'L', it.key())) out.push_back({*k, it.value()});
    for (auto it = doc.registers.begin(); it != doc.registers.end(); ++it) {
        const RegisterNote &r = it.value();
        if (r.isEmpty()) continue;
        const QString t = r.name.isEmpty() ? r.meaning : r.meaning.isEmpty() ? r.name : r.name + u(": ") + r.meaning;
        if (auto k = manzKey(u'R', it.key())) out.push_back({*k, t});
    }
    for (auto it = doc.flags.begin(); it != doc.flags.end(); ++it)
        if (!it.value().isEmpty()) if (auto k = manzKey(u'F', it.key())) out.push_back({*k, it.value()});
    for (auto &e : extra)
        if (!documentName(e.key) && std::none_of(out.begin(), out.end(), [&](auto &o) { return o.key == e.key; })) out.push_back(e);
    std::stable_sort(out.begin(), out.end(), [&](auto &a, auto &b) { return order(a.key) < order(b.key); });
    return out;
}

std::optional<std::vector<HP15CStep>> Manz15CProgram::stepsFromListing(const std::vector<QString> &keys, const std::vector<HP15CStep> *imported, ReadError *err) {
    std::vector<HP15CStep> out;
    for (size_t i = 0; i < keys.size(); ++i) {
        auto t = HP15CKeycodes::parseDisplay(keys[i]);
        if (!t) { if (err) *err = {ReadError::UnknownStep, int(i) + 1, keys[i]}; return std::nullopt; }
        out.emplace_back(*t);
    }
    if (imported && imported->size() == out.size()) {
        bool same = true;
        for (size_t i = 0; i < out.size() && same; ++i) same = (*imported)[i].plainTokens() == out[i].plainTokens();
        if (same) return *imported;
    }
    return out;
}

}  // namespace vc
