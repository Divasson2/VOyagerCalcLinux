// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
//
// ProgramText.cpp - see ProgramText.h.
#include "ProgramText.h"
#include "Manz15C.h"
#include <QRegularExpression>
#include <QStringDecoder>
#include <algorithm>
#include <memory>

namespace vc {

static QString u8(const char *s) { return QString::fromUtf8(s); }

// ---- catalogue ------------------------------------------------------------------------------------------------------

namespace {
bool userCapable15(const QStringList &t) {
    if (t.size() != 2 || (t[0] != u8("44") && t[0] != u8("45"))) return false;
    bool ok = false; const int k = t[1].toInt(&ok);
    return ok && ((k >= 11 && k <= 15) || k == 24);
}
const QSet<int> &invalid16() {          // bytes the 16C refuses to key in (WINDOW 8/9, FLOAT B–F): Error 1
    static const QSet<int> s = {0x1E, 0x1F, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F};
    return s;
}
QString stripShift(const QString &n, QString *shift = nullptr) {
    if (n.size() > 2 && (n[0] == u'f' || n[0] == u'g' || n[0] == u'F' || n[0] == u'G') && n[1] == u' ') {
        if (shift) *shift = n.left(1).toLower();
        return n.mid(2).trimmed();
    }
    if (shift) shift->clear();
    return n;
}
}  // namespace

QString StepCatalog::tokenKey(const QString &keycodes, bool *user) const {
    if (user) *user = false;
    QString text = keycodes; text.replace(QLatin1Char('\t'), QLatin1Char(' '));
    static const QRegularExpression ws(QStringLiteral("\\s+"));
    QStringList raw;
    for (const QString &w : text.split(ws, Qt::SkipEmptyParts)) {
        if (model_ == 15 && w.size() == 2 && w[0] == u',' && w[1].isDigit()) { raw << QStringLiteral(".") + w[1]; continue; }   // HP-15C Simulator ",5"
        for (const QString &p : w.split(QLatin1Char(','), Qt::SkipEmptyParts)) raw << p;
    }
    if (raw.isEmpty()) return {};
    QStringList t;
    bool isUser = false;
    for (const QString &w : raw) {
        if (model_ == 15 && (w == u8("u") || w == u8("U"))) { isUser = true; continue; }
        if (w.size() == 2 && w[0] == u'.') {
            const QChar c = w[1].toUpper();
            if (c.isDigit() || (model_ == 16 && c >= u'A' && c <= u'F')) { t << QStringLiteral(".") + c; continue; }
            return {};
        }
        if ((w.size() == 1 || w.size() == 2) && std::all_of(w.begin(), w.end(), [](QChar c) { return c >= u'0' && c <= u'9'; })) { t << QString::number(w.toInt()); continue; }
        if (model_ == 16 && w.size() == 1 && w[0].toUpper() >= u'A' && w[0].toUpper() <= u'F') { t << w.toUpper(); continue; }
        return {};
    }
    if (model_ == 15 && t.size() >= 3) {                 // old HP-15C Simulator files: "45 48 3" = RCL .3
        QStringList o;
        for (int i = 0; i < t.size();) {
            if (i > 0 && t[i] == u8("48") && i + 1 < t.size() && t[i + 1].size() == 1 && t[i + 1][0].isDigit()) { o << QStringLiteral(".") + t[i + 1]; i += 2; }
            else o << t[i++];
        }
        t = o;
    }
    if (t.isEmpty()) return {};
    if (user) *user = isUser;
    return t.join(QLatin1Char(' '));
}

QString StepCatalog::normalizeName(const QString &name) {
    static const std::vector<std::pair<QString, QString>> glyphs = {
        {u8("x̄"), u8("XBAR")}, {u8("√x̅"), u8("SQRT")}, {u8("√x"), u8("SQRT")}, {u8("√"), u8("SQRT")},
        {u8("∫ʸₓ"), u8("INTEG")}, {u8("∫xy"), u8("INTEG")}, {u8("∫"), u8("INTEG")},
        {u8("≷"), u8("<>")}, {u8("↔"), u8("<>")}, {u8("⇄"), u8("<>")}, {u8("≤"), u8("<=")}, {u8("≥"), u8(">=")}, {u8("≠"), u8("!=")},
        {u8("⁻¹"), u8("^-1")}, {u8("eˣ"), u8("E^X")}, {u8("10ˣ"), u8("10^X")}, {u8("yˣ"), u8("Y^X")}, {u8("ˣ"), u8("^X")}, {u8("²"), u8("^2")},
        {u8("π"), u8("PI")}, {u8("Σ"), u8("SIGMA")}, {u8("Δ"), u8("DELTA")}, {u8("ŷ"), u8("YHAT")}, {u8("→"), u8("->")},
        {u8("⬇"), u8("DN")}, {u8("↓"), u8("DN")}, {u8("⬆"), u8("UP")}, {u8("↑"), u8("UP")},
        {u8("÷"), u8("/")}, {u8("×"), u8("*")}, {u8("−"), u8("-")}, {u8("–"), u8("-")}, {u8("·"), u8(".")}};
    QString s = name;
    for (const auto &g : glyphs) s.replace(g.first, g.second);
    QString o;
    for (QChar c : s) {
        if (c.isSpace() || c.category() == QChar::Mark_NonSpacing || c.category() == QChar::Mark_Enclosing || c.category() == QChar::Mark_SpacingCombining) continue;
        o += c.toUpper();
    }
    if (o.size() > 1 && o.startsWith(QLatin1Char('X')) && o.endsWith(QLatin1Char('?'))) o.chop(1);   // tests: "x=y?" = "x=y" (not B?, F?)
    static const QMap<QString, QString> alias = {
        {u8("LASTX"), u8("LSTX")}, {u8("RV"), u8("RDN")}, {u8("RDOWN"), u8("RDN")}, {u8("ROLLDOWN"), u8("RDN")}, {u8("R^"), u8("RUP")},
        {u8("SQRTX"), u8("SQRT")}, {u8("+/-"), u8("CHS")}, {u8("RAND"), u8("RAN#")}, {u8("RANDOM"), u8("RAN#")}, {u8("N!"), u8("X!")},
        {u8("%CH"), u8("DELTA%")}, {u8("D%"), u8("DELTA%")}, {u8("ENTER^"), u8("ENTER")}, {u8("ENTERUP"), u8("ENTER")}, {u8("RETURN"), u8("RTN")},
        {u8("SWAP"), u8("X<>Y")}, {u8("EXP"), u8("E^X")}, {u8("ASIN"), u8("SIN^-1")}, {u8("ACOS"), u8("COS^-1")}, {u8("ATAN"), u8("TAN^-1")},
        {u8("SIN-1"), u8("SIN^-1")}, {u8("COS-1"), u8("COS^-1")}, {u8("TAN-1"), u8("TAN^-1")}, {u8("->HMS"), u8("->H.MS")},
        {u8("S+"), u8("SIGMA+")}, {u8("S-"), u8("SIGMA-")}, {u8("CLRX"), u8("CLX")}, {u8("1/X"), u8("1/X")}, {u8("INV"), u8("1/X")},
        {u8("X^Y"), u8("Y^X")}, {u8("Y**X"), u8("Y^X")}, {u8("LOG10"), u8("LOG")}};
    auto it = alias.find(o);
    if (it != alias.end()) o = it.value();
    if (o.startsWith(u8("XEQ"))) o = u8("GSB") + o.mid(3);
    if (o.startsWith(u8("HYP-1"))) o = u8("HYP^-1") + o.mid(5);
    if (o.startsWith(u8("LABEL"))) o = u8("LBL") + o.mid(5);
    return o;
}

void StepCatalog::add(const Entry &e, const QStringList &names) {
    const QString tk = tokenKey(e.keys);
    if (tk.isEmpty()) return;
    auto it = byTokens_.find(tk);
    if (it != byTokens_.end()) {                         // the same keycodes from another byte value: keep the shorter
        Entry &old = entries_[size_t(it.value())];
        if (e.bytes < old.bytes) { old.bytes = e.bytes; old.mnemonic = e.mnemonic; }
        return;
    }
    const int i = int(entries_.size());
    entries_.push_back(e);
    byTokens_.insert(tk, i);
    if (!byMnemonic_.contains(e.mnemonic)) byMnemonic_.insert(e.mnemonic, i);
    QSet<QString> full, bare;
    for (const QString &n : names) {
        if (n.isEmpty()) continue;
        full.insert(normalizeName(n));
        bare.insert(normalizeName(stripShift(n)));
    }
    for (const QString &n : full) byFullName_.insert(n, i);
    for (const QString &n : bare) byBareName_.insert(n, i);
}

StepCatalog::StepCatalog(int model) : model_(model) {
    auto consider = [&](const vc_prog_line &p) {
        const QString keys = QString::fromUtf8(p.keys), mn = QString::fromUtf8(p.mnemonic);
        Entry e;
        e.keys = keys; e.bytes = p.len;
        e.mnemonic = p.prefix ? QString(QChar(uchar(p.prefix))) + QLatin1Char(' ') + mn : mn;
        QStringList names{e.mnemonic};
        if (model_ == 15) {
            auto toks = HP15CKeycodes::parseDisplay(keys);
            if (!toks) return;
            auto manz = HP15CKeycodes::mnemonic(*toks);
            if (!manz) return;                           // not a step the HP-15C can hold (decoding filler)
            names << *manz;
            static const QRegularExpression test(QStringLiteral("^g TEST (.+)$"));
            auto m = test.match(*manz);
            if (m.hasMatch()) names << m.captured(1);
            e.userCapable = userCapable15(tokenKey(keys).split(QLatin1Char(' ')));
        }
        add(e, names);
    };
    for (int b = 0; b < 256; ++b) {
        if (model_ == 16 && invalid16().contains(b)) continue;
        vc_prog_line p{};
        uint8_t c[2] = {uint8_t(b), 0};
        const int r = vc_decode_step(model_, c, 1, &p);
        if (r == 1) consider(p);
        else if (r == 0)
            for (int b2 = 0; b2 < 256; ++b2) { c[1] = uint8_t(b2); if (vc_decode_step(model_, c, 2, &p) == 2) consider(p); }
    }
}

const StepCatalog *StepCatalog::forModel(int model) {
    static std::unique_ptr<StepCatalog> c15, c16;
    if (model == 15) { if (!c15) c15.reset(new StepCatalog(15)); return c15.get(); }
    if (model == 16) { if (!c16) c16.reset(new StepCatalog(16)); return c16.get(); }
    return nullptr;
}

const StepCatalog::Entry *StepCatalog::find(const QString &keycodes, bool *user) const {
    bool us = false;
    const QString tk = tokenKey(keycodes, &us);
    if (user) *user = us;
    if (tk.isEmpty()) return nullptr;
    auto it = byTokens_.find(tk);
    if (it == byTokens_.end()) return nullptr;
    const Entry *e = &entries_[size_t(it.value())];
    if (us && !e->userCapable) return nullptr;
    return e;
}

std::vector<const StepCatalog::Entry *> StepCatalog::named(const QString &name, bool *user) const {
    QString n = name.trimmed();
    bool us = false;
    if (model_ == 15 && (n.endsWith(u8(" u")) || n.endsWith(u8(" U")))) { us = true; n.chop(2); n = n.trimmed(); }
    if (user) *user = us;
    std::vector<const Entry *> out;
    auto collect = [&](const QList<int> &ids, const QString &shift) {
        QSet<QString> seen;
        for (int i : ids) {
            const Entry *e = &entries_[size_t(i)];
            if (us && !e->userCapable) continue;
            if (!shift.isEmpty() && !e->mnemonic.startsWith(shift + QLatin1Char(' '))) continue;
            if (seen.contains(e->keys)) continue;
            seen.insert(e->keys); out.push_back(e);
        }
    };
    auto byKeys = [](const Entry *a, const Entry *b) { return a->keys < b->keys; };
    collect(byFullName_.values(normalizeName(n)), QString());
    if (!out.empty()) { std::sort(out.begin(), out.end(), byKeys); return out; }
    QString shift;
    const QString bare = stripShift(n, &shift);
    const QList<int> ids = byBareName_.values(normalizeName(bare));
    collect(ids, shift);
    if (out.empty() && !shift.isEmpty()) collect(ids, QString());   // shift written wrongly: the name decides
    std::sort(out.begin(), out.end(), byKeys);
    return out;
}

const StepCatalog::Entry *StepCatalog::byMnemonic(const QString &m) const {
    auto it = byMnemonic_.find(m);
    return it == byMnemonic_.end() ? nullptr : &entries_[size_t(it.value())];
}

int StepCatalog::bytes(const ProgStep &s) const {
    const Entry *e = find(s.keys);
    const int b = e ? e->bytes : 1;
    return s.user && b == 1 ? 2 : b;                    // USER-mode STO / RCL: two bytes (BF xx)
}

QString StepCatalog::mnemonic(const ProgStep &s) const {
    const Entry *e = find(s.keys);
    if (!e) return QString();
    return s.user ? e->mnemonic + QStringLiteral(" u") : e->mnemonic;
}

// ---- reading text ---------------------------------------------------------------------------------------------------

QString ProgramParse::summary() const {
    QStringList p;
    p << (steps.size() == 1 ? QStringLiteral("1 step") : QStringLiteral("%1 steps").arg(steps.size()));
    if (errors) p << (errors == 1 ? QStringLiteral("1 error") : QStringLiteral("%1 errors").arg(errors));
    if (ambiguous) p << (ambiguous == 1 ? QStringLiteral("1 line to choose") : QStringLiteral("%1 lines to choose").arg(ambiguous));
    return p.join(QStringLiteral(", "));
}

QString decodeProgramFile(const QByteArray &data) {
    if (auto t = Manz15CProgram::decodeText(data)) return *t;
    auto dec = QStringDecoder(QStringDecoder::Utf8);
    QString s = dec.decode(data);
    if (dec.hasError()) return QString::fromLatin1(data);
    return s;
}

namespace {
int modelFromText(const QString &s) {
    static const QRegularExpression re(QStringLiteral("HP-?\\s?(1[256])\\s?C"), QRegularExpression::CaseInsensitiveOption);
    auto m = re.match(s);
    return m.hasMatch() ? m.captured(1).toInt() : 0;
}
bool looksLikeKeycodes(const QString &s) {
    static const QRegularExpression re(QStringLiteral("^[0-9A-Fa-f.,\\s]+$"));
    return re.match(s).hasMatch() && s.contains(QRegularExpression(QStringLiteral("[0-9]")));
}
}  // namespace

ProgramParse parseProgramText(const QString &text, int model, const QMap<int, QString> &choices) {
    ProgramParse r;
    r.model = model;
    const StepCatalog *cat = StepCatalog::forModel(model);
    if (!cat) { r.modelError = QStringLiteral("Programs can be pasted into the HP-15C and HP-16C."); return r; }
    const StepCatalog *other = StepCatalog::forModel(model == 15 ? 16 : 15);
    static const QRegularExpression nl(QStringLiteral("\r\n|\r|\n"));
    const QStringList lines = text.split(nl);

    // our "Export Listing as Text": only the LISTING section is the program
    int listingStart = -1, listingEnd = int(lines.size());
    for (int i = 0; i < lines.size(); ++i)
        if (lines[i].trimmed() == u8("LISTING")) { listingStart = i; break; }
    if (listingStart >= 0)
        for (int i = listingStart + 1; i < lines.size(); ++i)
            if (lines[i].trimmed().startsWith(u8("COMMENTS NO LONGER MATCHING"))) { listingEnd = i; break; }

    // a listing with a step number on (almost) every line
    static const QRegularExpression numbered(QStringLiteral("^\\s*(\\d{1,3})(?:\\s+|\\s*-\\s*)(\\S.*)$"));
    bool isNumbered = false;
    {
        int content = 0, withNo = 0, rising = 0, last = -1;
        for (int i = std::max(0, listingStart + 1); i < listingEnd; ++i) {
            const QString s = lines[i].trimmed();
            if (s.isEmpty() || s.startsWith(QLatin1Char('#')) || s.startsWith(u8("//"))) continue;
            ++content;
            auto m = numbered.match(s);
            if (m.hasMatch()) { ++withNo; const int n = m.captured(1).toInt(); if (n == last + 1) ++rising; last = n; }
        }
        isNumbered = content > 0 && withNo * 10 >= content * 6 && rising * 10 >= withNo * 6;
    }

    static const QRegularExpression exportRow(QStringLiteral("^(\\d{1,3})\\s+(\\d{1,3})-\\s*(.*)$"));
    static const QRegularExpression displayRow(QStringLiteral("^(\\d{1,3})-\\s*(.*)$"));
    static const QRegularExpression columns(QStringLiteral("\\s{2,}|\\t"));
    static const QRegularExpression ws(QStringLiteral("\\s+"));
    static const QRegularExpression ourHeader(QStringLiteral("VoyagerCalc program"), QRegularExpression::CaseInsensitiveOption);
    int block = 0;
    bool stepsInBlock = false;
    auto newBlock = [&] { if (stepsInBlock) { ++block; stepsInBlock = false; } };

    struct Try { const StepCatalog::Entry *e = nullptr; bool user = false; QString rest; };
    auto tryKeycodes = [&](const QString &s) -> Try {
        Try t;
        const QStringList words = s.split(ws, Qt::SkipEmptyParts);
        for (int k = std::min<int>(4, int(words.size())); k >= 1; --k) {
            bool us = false;
            if (auto e = cat->find(words.mid(0, k).join(QLatin1Char(' ')), &us)) {
                t.e = e; t.user = us;
                // the rest, with its original spacing
                int pos = 0;
                for (int w = 0; w < k; ++w) { pos = int(s.indexOf(words[w], pos)) + int(words[w].size()); }
                t.rest = s.mid(pos).trimmed();
                return t;
            }
        }
        return t;
    };

    for (int i = 0; i < lines.size(); ++i) {
        ParsedLine pl;
        pl.line = i + 1; pl.text = lines[i];
        QString s = lines[i].trimmed();
        auto finish = [&] { pl.block = block; r.lines.push_back(pl); };
        if (listingStart >= 0 && (i <= listingStart || i >= listingEnd)) {          // export: header and other sections
            if (s.startsWith(u8("Model:"))) { if (int m = modelFromText(s)) r.detectedModel = m; }
            if (i == 0 && !s.isEmpty()) r.title = s;
            pl.kind = s.isEmpty() ? ParsedLine::Blank : ParsedLine::Note;
            finish(); continue;
        }
        if (s.isEmpty()) { finish(); continue; }
        if (s.startsWith(QLatin1Char('#')) || s.startsWith(u8("//"))) {
            pl.kind = ParsedLine::Note;
            if (ourHeader.match(s).hasMatch()) {
                newBlock();
                if (int m = modelFromText(s)) r.detectedModel = m;
            } else if (s.contains(u8("15C Simulator")) || s.contains(u8("PACKARD 15C"))) {
                newBlock();
                r.detectedModel = 15;
            } else if (s.startsWith(u8("#T:"))) {
                if (r.title.isEmpty()) r.title = s.mid(3).trimmed();
            } else if (s.contains(QRegularExpression(QStringLiteral("^#\\s*(file|program)\\s*:"), QRegularExpression::CaseInsensitiveOption))) {
                newBlock();
            } else if (s.startsWith(u8("# Title:"))) {
                if (r.title.isEmpty()) r.title = s.mid(8).trimmed();
            }
            finish(); continue;
        }
        if (listingStart >= 0 && (s.startsWith(u8("Step")) || QRegularExpression(QStringLiteral("^[-\\s]+$")).match(s).hasMatch())) {
            pl.kind = ParsedLine::Note; finish(); continue;
        }
        QString comment;
        const int semi = int(s.indexOf(QLatin1Char(';')));
        if (semi >= 0) { comment = s.mid(semi + 1).trimmed(); s = s.left(semi).trimmed(); }
        if (s.isEmpty()) { pl.kind = ParsedLine::Note; finish(); continue; }

        auto setStep = [&](const StepCatalog::Entry *e, bool user, const QString &rest) {
            pl.kind = ParsedLine::Step;
            pl.step.keys = e->keys; pl.step.user = user;
            QString c = comment;
            if (c.isEmpty() && !rest.isEmpty()) {                // "f LBL A  comment": the key names, then the comment
                const QStringList cols = rest.split(columns, Qt::SkipEmptyParts);
                if (cols.size() > 1) c = cols.mid(1).join(QLatin1Char(' '));
            }
            pl.step.comment = c;
        };
        auto error = [&](const QString &m) { pl.kind = ParsedLine::Error; pl.message = m; };
        auto tryNames = [&](const QString &rest) -> bool {
            const QStringList cols = rest.split(columns, Qt::SkipEmptyParts);
            if (cols.isEmpty()) return false;
            bool us = false;
            auto found = cat->named(cols[0], &us);
            if (found.empty() && cols.size() > 1) { found = cat->named(rest, &us); if (!found.empty()) { setStep(found[0], us, QString()); } }
            if (found.empty()) return false;
            if (found.size() == 1) { setStep(found[0], us, cols.size() > 1 ? rest : QString()); return true; }
            const QString chosen = choices.value(i + 1);
            for (auto *e : found) if (!chosen.isEmpty() && e->keys == chosen) { setStep(e, us, cols.size() > 1 ? rest : QString()); return true; }
            pl.kind = ParsedLine::Ambiguous;
            QStringList names;
            for (auto *e : found) { ProgStep st; st.keys = e->keys; st.user = us; pl.choices.push_back(st); names << QStringLiteral("%1 {%2}").arg(e->mnemonic, e->keys); }
            pl.message = QStringLiteral("“%1” can be %2: choose one.").arg(cols[0], names.join(u8(" or ")));
            pl.step.comment = comment;
            return true;
        };
        auto explain = [&](const QString &what) {
            const QString w = what.trimmed();
            if (looksLikeKeycodes(w)) {
                if (other && other->find(w)) return error(QStringLiteral("{%1} is an %2 step, not an %3 step.").arg(w, other->modelName(), cat->modelName()));
                return error(QStringLiteral("{%1} are not %2 keycodes.").arg(w, cat->modelName()));
            }
            const QString name = w.split(columns, Qt::SkipEmptyParts).value(0);
            if (other && !other->named(name).empty()) return error(QStringLiteral("“%1” is an %2 function, not an %3 one.").arg(name, other->modelName(), cat->modelName()));
            error(QStringLiteral("Unknown key name “%1”.").arg(name));
        };

        // { keycodes } (HP-15C Simulator, JRPN-style listings)
        const int open = int(s.indexOf(QLatin1Char('{')));
        if (open >= 0) {
            const int close = int(s.indexOf(QLatin1Char('}'), open));
            if (close < 0) { error(QStringLiteral("“{” without “}”.")); finish(); continue; }
            const QString inner = s.mid(open + 1, close - open - 1).trimmed();
            if (inner.isEmpty()) { pl.kind = ParsedLine::Blank; finish(); continue; }      // step 000
            bool us = false;
            if (auto e = cat->find(inner, &us)) setStep(e, us, QString());
            else explain(inner);
            if (pl.kind == ParsedLine::Step) stepsInBlock = true;
            finish(); continue;
        }

        bool done = false;
        QRegularExpressionMatch m = exportRow.match(s);
        if (!m.hasMatch()) m = displayRow.match(s);
        if (m.hasMatch()) {                                  // "001-42,21,11 ..." / export "001  001-42,21,11 ..."
            const QString rest = m.captured(m.lastCapturedIndex());
            Try t = tryKeycodes(rest);
            if (t.e) { setStep(t.e, t.user, t.rest); done = true; }
            else if (tryNames(rest)) done = true;
            else { explain(rest); done = true; }
        }
        if (!done && !isNumbered) {
            Try t = tryKeycodes(s);
            if (t.e && t.rest.isEmpty()) { setStep(t.e, t.user, QString()); done = true; }
        }
        if (!done) {
            auto nm = numbered.match(s);
            if (nm.hasMatch()) {
                const QString rest = nm.captured(2);
                Try t = tryKeycodes(rest);
                if (t.e) { setStep(t.e, t.user, t.rest); done = true; }
                else if (tryNames(rest)) done = true;
                else if (isNumbered) { explain(rest); done = true; }
            }
        }
        if (!done) {
            if (tryNames(s)) done = true;
            else {
                Try t = tryKeycodes(s);
                if (t.e) setStep(t.e, t.user, t.rest);
                else explain(s);
            }
        }
        if (pl.kind == ParsedLine::Step || pl.kind == ParsedLine::Ambiguous) stepsInBlock = true;
        finish();
    }
    for (const ParsedLine &pl : r.lines) {
        if (pl.kind == ParsedLine::Step) { r.steps.push_back(pl.step); r.stepBlocks.push_back(pl.block); }
        else if (pl.kind == ParsedLine::Error) ++r.errors;
        else if (pl.kind == ParsedLine::Ambiguous) ++r.ambiguous;
    }
    if (r.detectedModel && r.detectedModel != model) {
        const QString name = r.detectedModel == 12 ? u8("HP-12C") : StepCatalog::forModel(r.detectedModel)->modelName();
        r.modelError = QStringLiteral("This text is an %1 program, but the calculator is in %2 mode. Switch the model first.").arg(name, cat->modelName());
    }
    return r;
}

// ---- writing text ---------------------------------------------------------------------------------------------------

QString programFragmentText(int model, const std::vector<ProgStep> &steps, int firstStep, const QString &title) {
    const StepCatalog *cat = StepCatalog::forModel(model);
    if (!cat) return QString();
    QStringList o;
    const int last = firstStep + int(steps.size()) - 1;
    o << QStringLiteral("# VoyagerCalc program · %1 · %2 (%3–%4)").arg(cat->modelName())
             .arg(steps.size() == 1 ? QStringLiteral("1 step") : QStringLiteral("%1 steps").arg(steps.size()))
             .arg(firstStep, 3, 10, QLatin1Char('0')).arg(last, 3, 10, QLatin1Char('0'));
    if (!title.isEmpty()) o << QStringLiteral("# Title: ") + title;
    for (size_t i = 0; i < steps.size(); ++i) {
        const ProgStep &s = steps[i];
        QString line = QStringLiteral("%1-%2").arg(firstStep + int(i), 3, 10, QLatin1Char('0')).arg(cat->displayKeys(s)).leftJustified(16);
        const QString mn = cat->mnemonic(s);
        line += s.comment.isEmpty() ? mn : mn.leftJustified(14) + QStringLiteral("  ; ") + s.comment;
        while (line.endsWith(QLatin1Char(' '))) line.chop(1);
        o << line;
    }
    return o.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

QString programManzText(const std::vector<ProgStep> &steps, const QString &title) {
    Manz15CProgram p;
    p.title = title;
    for (const ProgStep &s : steps) {
        auto t = HP15CKeycodes::parseDisplay(s.keys);
        if (!t) return QString();
        HP15CTokens tok = *t;
        if (s.user) tok.push_back(HP15CToken::user());
        p.steps.push_back(HP15CStep(tok));
    }
    return p.text();
}

std::vector<ProgStep> stepsFromListing(int model, const std::vector<ProgramLine> &lines, const QMap<int, QString> &comments) {
    const StepCatalog *cat = StepCatalog::forModel(model);
    std::vector<ProgStep> out;
    for (const ProgramLine &l : lines) {
        ProgStep s;
        s.keys = l.keys;
        if (cat) {
            if (const auto *e = cat->find(l.keys)) {
                s.keys = e->keys;
                s.user = model == 15 && e->userCapable && e->bytes == 1 && l.bytes.size() == 2;
            }
        }
        s.comment = comments.value(l.line);
        out.push_back(s);
    }
    return out;
}

// ---- labels ---------------------------------------------------------------------------------------------------------

namespace {
const QRegularExpression &labelRe() {
    static const QRegularExpression re(QString::fromUtf8("^((?:[fg] )?(?:LBL|GTO|GSB|SOLVE|∫ʸₓ)) (\\.?[0-9A-F])$"));
    return re;
}
QStringList digitLabels(int model) {
    QStringList l; for (int i = 0; i < 10; ++i) l << QString::number(i);
    if (model == 15) for (int i = 0; i < 10; ++i) l << QStringLiteral(".%1").arg(i);
    return l;
}
QStringList letterLabels(int model) { return model == 15 ? QStringList{"A", "B", "C", "D", "E"} : QStringList{"A", "B", "C", "D", "E", "F"}; }
}  // namespace

bool isLetterLabel(const QString &l) { return l.size() == 1 && l[0] >= u'A' && l[0] <= u'F'; }

LabelUse labelUse(int model, const std::vector<ProgStep> &steps) {
    LabelUse u;
    const StepCatalog *cat = StepCatalog::forModel(model);
    if (!cat) return u;
    for (const ProgStep &s : steps) {
        const auto *e = cat->find(s.keys);
        if (!e) continue;
        auto m = labelRe().match(e->mnemonic);
        if (!m.hasMatch()) continue;
        if (m.captured(1).endsWith(u8("LBL"))) u.defined.insert(m.captured(2)); else u.referenced.insert(m.captured(2));
    }
    return u;
}

bool RenumberPlan::anyClash() const { for (auto &b : blocks) if (!b.clashes.isEmpty()) return true; return false; }
bool RenumberPlan::anyLetterClash() const { for (auto &b : blocks) if (!b.letterClashes.isEmpty()) return true; return false; }

QStringList RenumberPlan::describe() const {
    QStringList o;
    for (size_t b = 0; b < blocks.size(); ++b) {
        const Block &k = blocks[b];
        const QString where = blocks.size() > 1 ? QStringLiteral("Program %1: ").arg(b + 1) : QString();
        for (auto it = k.map.begin(); it != k.map.end(); ++it)
            o << where + QStringLiteral("LBL %1 is already used: renumbered to LBL %2 (its GTO / GSB too).").arg(it.key(), it.value());
        for (const QString &l : k.letterClashes)
            if (!k.map.contains(l)) o << where + QStringLiteral("LBL %1 is already used and was not renumbered: GSB %1 / GTO %1 will find the first LBL %1 after the current line.").arg(l);
        for (const QString &l : k.unresolved) o << where + QStringLiteral("LBL %1 is already used and no free label is left to renumber it.").arg(l);
    }
    return o;
}

RenumberPlan planRenumber(int model, const std::vector<ProgStep> &existing, const std::vector<ProgStep> &incoming,
                          const std::vector<int> &blocks, bool renumberLetters) {
    RenumberPlan plan;
    plan.letterRenumbering = renumberLetters;
    const LabelUse ex = labelUse(model, existing);
    QSet<QString> defined = ex.defined;
    QSet<QString> taken = ex.defined + ex.referenced;
    {   // every label the pasted text mentions is also taken
        const LabelUse in = labelUse(model, incoming);
        taken += in.defined + in.referenced;
    }
    std::vector<int> order;
    for (int b : blocks) if (std::find(order.begin(), order.end(), b) == order.end()) order.push_back(b);
    if (order.empty()) order.push_back(0);
    for (int b : order) {
        std::vector<ProgStep> sub;
        for (size_t i = 0; i < incoming.size(); ++i) if ((i < blocks.size() ? blocks[i] : 0) == b) sub.push_back(incoming[i]);
        const LabelUse u = labelUse(model, sub);
        RenumberPlan::Block k;
        QStringList mine(u.defined.begin(), u.defined.end());
        std::sort(mine.begin(), mine.end());
        QSet<QString> nowDefined;
        for (const QString &l : mine) {
            if (!defined.contains(l)) { nowDefined.insert(l); continue; }
            k.clashes << l;
            const bool letter = isLetterLabel(l);
            if (letter) k.letterClashes << l;
            if (letter && !renumberLetters) { nowDefined.insert(l); continue; }
            const QStringList pool = letter ? letterLabels(model) : digitLabels(model);
            QString pick;
            for (const QString &c : pool) if (!taken.contains(c)) { pick = c; break; }
            if (pick.isEmpty()) { k.unresolved << l; nowDefined.insert(l); continue; }
            k.map.insert(l, pick);
            taken.insert(pick);
            nowDefined.insert(pick);
        }
        defined += nowDefined;
        plan.blocks.push_back(k);
    }
    return plan;
}

std::vector<ProgStep> applyRenumber(int model, const std::vector<ProgStep> &incoming, const std::vector<int> &blocks, const RenumberPlan &plan) {
    const StepCatalog *cat = StepCatalog::forModel(model);
    std::vector<int> order;
    for (int b : blocks) if (std::find(order.begin(), order.end(), b) == order.end()) order.push_back(b);
    if (order.empty()) order.push_back(0);
    std::vector<ProgStep> out = incoming;
    if (!cat) return out;
    for (size_t i = 0; i < out.size(); ++i) {
        const int b = i < blocks.size() ? blocks[i] : 0;
        const size_t bi = size_t(std::find(order.begin(), order.end(), b) - order.begin());
        if (bi >= plan.blocks.size()) continue;
        const auto &map = plan.blocks[bi].map;
        if (map.isEmpty()) continue;
        const auto *e = cat->find(out[i].keys);
        if (!e) continue;
        auto m = labelRe().match(e->mnemonic);
        if (!m.hasMatch() || !map.contains(m.captured(2))) continue;
        if (const auto *n = cat->byMnemonic(m.captured(1) + QLatin1Char(' ') + map.value(m.captured(2)))) out[i].keys = n->keys;
    }
    return out;
}

// ---- memory ---------------------------------------------------------------------------------------------------------

int registers16(int slotCount, int lines, int wordSize) {
    const int nibbles = std::max(1, (wordSize + 3) / 4);
    const int freeSlots = std::max(0, slotCount - (lines + 6) / 7);
    return freeSlots * 14 / nibbles;
}

FitReport checkFit(const InspectorSnapshot &s, const std::vector<ProgStep> &program) {
    FitReport f;
    const StepCatalog *cat = StepCatalog::forModel(s.model);
    if (!cat) { f.fits = false; f.message = QStringLiteral("Programs can only be edited on the HP-15C and HP-16C."); return f; }
    if (s.model == 15) {
        for (const ProgStep &p : program) f.need += cat->bytes(p);
        if (!s.pool) return f;
        f.available = (s.pool->free + s.pool->programRegisters) * 7;
        if (f.need > f.available) {
            f.fits = false;
            f.message = QStringLiteral("The program would need %1 bytes, but the calculator has %2 for programs (free registers × 7 + the program's own). "
                                       "Free registers or matrices (DIM (i), MATRIX 0)%3, then try again.")
                            .arg(f.need).arg(f.available).arg(s.variant == 150 ? QStringLiteral(" or use the 15C (15.2) mode") : QString());
        }
        return f;
    }
    f.need = int(program.size());
    if (!s.pool16) return f;
    const int slotCount = s.pool16->slotCount();
    f.available = slotCount * 7;
    if (f.need > f.available) {
        f.fits = false;
        f.message = QStringLiteral("The program would have %1 lines, but this %2 holds at most %3 (%4 slots of 7 bytes, with no registers left).")
                        .arg(f.need).arg(s.pool16->title()).arg(f.available).arg(slotCount);
        return f;
    }
    f.registersAfter = registers16(slotCount, f.need, s.wordSize ? s.wordSize : 16);
    for (size_t i = size_t(std::max(0, f.registersAfter)); i < s.registers.size(); ++i)
        if (s.registers[i].stored() != 0) f.lostRegisters << s.registers[i].name;
    if (!f.lostRegisters.isEmpty())
        f.message = QStringLiteral("The longer program takes memory from the registers: only %1 are left at word size %2, and %3 %4 a value that will be lost.")
                        .arg(f.registersAfter).arg(s.wordSize).arg(f.lostRegisters.join(QStringLiteral(", ")))
                        .arg(f.lostRegisters.size() == 1 ? QStringLiteral("holds") : QStringLiteral("hold"));
    return f;
}

}  // namespace vc
