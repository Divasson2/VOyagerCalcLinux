// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
#include "KeyGlyphs.h"
#include "Model.h"
#include <QHash>

namespace vc {

namespace {
static const QHash<int, QString> kPrimary = {
    {0, QStringLiteral("¤")},
    {1, QStringLiteral("'")},
    {2, QStringLiteral("@")},
    {3, QStringLiteral("y")},
    {4, QStringLiteral("⁄")},
    {5, QStringLiteral("”")},
    {6, QStringLiteral("7")},
    {7, QStringLiteral("8")},
    {8, QStringLiteral("9")},
    {9, QStringLiteral("÷")},
    {10, QStringLiteral("Â")},
    {11, QStringLiteral("t")},
    {12, QStringLiteral("[")},
    {13, QStringLiteral("\\")},
    {14, QStringLiteral("]")},
    {15, QStringLiteral("“")},
    {16, QStringLiteral("4")},
    {17, QStringLiteral("5")},
    {18, QStringLiteral("6")},
    {19, QStringLiteral("*")},
    {20, QStringLiteral("¦")},
    {21, QStringLiteral("G")},
    {22, QStringLiteral(")")},
    {23, QStringLiteral("®")},
    {24, QStringLiteral("−")},
    {25, QStringLiteral("v")},
    {26, QStringLiteral("1")},
    {27, QStringLiteral("2")},
    {28, QStringLiteral("3")},
    {29, QStringLiteral("-")},
    {30, QStringLiteral("=")},
    {31, QStringLiteral("´")},
    {32, QStringLiteral("|")},
    {33, QStringLiteral("O")},
    {34, QStringLiteral("l")},
    {36, QStringLiteral("0")},
    {37, QStringLiteral(".")},
    {38, QStringLiteral("z")},
    {39, QStringLiteral("+")}
};

static const QHash<int, QString> kGold = {
    {0, QStringLiteral("A")},
    {1, QStringLiteral("B")},
    {2, QStringLiteral("C")},
    {3, QStringLiteral("Á")},
    {4, QStringLiteral("E")},
    {5, QStringLiteral(">")},
    {6, QStringLiteral("•")},
    {7, QStringLiteral("i")},
    {8, QStringLiteral("^")},
    {9, QStringLiteral("_")},
    {10, QStringLiteral("b")},
    {11, QStringLiteral("P")},
    {12, QStringLiteral("m")},
    {13, QStringLiteral("%")},
    {14, QStringLiteral("V")},
    {15, QStringLiteral("<")},
    {16, QStringLiteral("X")},
    {17, QStringLiteral("e")},
    {18, QStringLiteral("I")},
    {19, QStringLiteral("f")},
    {20, QStringLiteral("©")},
    {21, QStringLiteral("∑")},
    {22, QStringLiteral("M")},
    {23, QStringLiteral("Q")},
    {24, QStringLiteral("u")},
    {25, QStringLiteral("#")},
    {26, QStringLiteral(";")},
    {27, QStringLiteral("h")},
    {28, QStringLiteral("r")},
    {29, QStringLiteral("}")},
    {33, QStringLiteral("q")},
    {34, QStringLiteral("U")},
    {36, QStringLiteral("!")},
    {37, QStringLiteral("j")},
    {38, QStringLiteral("L")},
    {39, QStringLiteral("p")}
};

static const QHash<int, QString> kBlue = {
    {0, QStringLiteral("x")},
    {1, QStringLiteral("N")},
    {2, QStringLiteral("o")},
    {3, QStringLiteral("k")},
    {4, QStringLiteral("∆")},
    {5, QStringLiteral("a")},
    {6, QStringLiteral("D")},
    {7, QStringLiteral("R")},
    {8, QStringLiteral("g")},
    {9, QStringLiteral("£")},
    {10, QStringLiteral("‚")},
    {11, QStringLiteral("H")},
    {12, QStringLiteral(",")},
    {13, QStringLiteral("{")},
    {14, QStringLiteral("/")},
    {15, QStringLiteral("$")},
    {16, QStringLiteral("F")},
    {17, QStringLiteral("\"")},
    {18, QStringLiteral("?")},
    {19, QStringLiteral("~")},
    {20, QStringLiteral("¥")},
    {21, QStringLiteral("n")},
    {22, QStringLiteral("(")},
    {23, QStringLiteral("&")},
    {24, QStringLiteral("`")},
    {25, QStringLiteral("K")},
    {26, QStringLiteral(":")},
    {27, QStringLiteral("À")},
    {28, QStringLiteral("d")},
    {29, QStringLiteral("T")},
    {33, QStringLiteral("‘")},
    {34, QStringLiteral("W")},
    {36, QStringLiteral("’")},
    {37, QStringLiteral("S")},
    {38, QStringLiteral("w")},
    {39, QStringLiteral("c")}
};
enum Colour { Primary, Gold, Blue };
const QChar kFKey(0x00B4), kGKey('|');        // "´" and "|"

const QHash<int, QString> &table(Colour c) { return c == Primary ? kPrimary : c == Gold ? kGold : kBlue; }

struct Entry { QChar ch; int code; };
using Lookup = QHash<QString, Entry>;

const Lookup *lookup() {
    static Lookup m[3];
    static bool built = false;
    if (!built) {
        built = true;
        const Model &mod = Model::hp15c();
        for (int i = 0; i < mod.keyCount(); ++i) {
            const KeySpec &k = mod.keys()[i];
            auto add = [&](Colour col, std::initializer_list<QString> names) {
                auto it = table(col).find(k.code);
                if (it == table(col).end()) return;
                for (const QString &n : names) if (!n.isEmpty()) m[col][n] = {it->at(0), k.code};
            };
            add(Primary, {QString::fromUtf8(k.name), QString::fromUtf8(k.face)});
            add(Gold, {QString::fromUtf8(k.f)});
            add(Blue, {QString::fromUtf8(k.g)});
        }
        auto extra = [&](Colour col, std::initializer_list<std::pair<const char *, int>> l) {
            for (auto &p : l) { auto it = table(col).find(p.second); if (it != table(col).end()) m[col][QString::fromUtf8(p.first)] = {it->at(0), p.second}; }
        };
        extra(Primary, {{"e^x", 1}, {"10^x", 2}, {"y^x", 3}, {"1/x", 4}, {"x<>y", 23}, {"x≷y", 23}, {"⬅", 24}, {"←", 24}, {"·", 37}, {".", 37}, {"-", 29}, {"*", 19}, {"/", 9}});
        extra(Gold, {{"∫", 19}, {"∫ʸₓ", 19}, {"∫xy", 19}, {"CLEAR", 21}, {"Σ", 21}, {"RAN #", 25}, {"x≷", 16}, {"Re≷Im", 29}, {"Re<>Im", 29}, {"x<>", 16}});
        extra(Blue, {{"LSTx", 25}, {"LST x", 25}, {"CLx", 24}, {"CL x", 24}, {"x̄", 36}, {"x≤y", 9}, {"x=0", 19}});
    }
    return m;
}

std::optional<Glyph> find(const QString &name, std::initializer_list<Colour> prefer) {
    QStringList cands{name};
    if (name.endsWith(QLatin1Char('?')) && name != QLatin1String("F?")) cands << name.chopped(1);
    const Lookup *l = lookup();
    for (const QString &n : cands)
        for (Colour c : prefer) { auto it = l[c].find(n); if (it != l[c].end()) return Glyph{it->ch, name}; }
    return std::nullopt;
}
}  // namespace

std::optional<std::vector<Glyph>> keyGlyphsFor(const ProgramLine &l) {
    std::vector<Glyph> out;
    QStringList tokens = l.mnemonic.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (tokens.isEmpty()) return std::nullopt;
    Colour first = Primary;
    if (l.prefix == QLatin1String("f")) { out.push_back({kFKey, QStringLiteral("f")}); first = Gold; }
    else if (l.prefix == QLatin1String("g")) { out.push_back({kGKey, QStringLiteral("g")}); first = Blue; }
    const QString head = tokens.takeFirst();
    auto h = find(head, {first, Gold, Primary, Blue});
    if (!h) return std::nullopt;
    out.push_back(*h);
    for (const QString &t : tokens) {
        if (t == QLatin1String("f")) { out.push_back({kFKey, QStringLiteral("f")}); continue; }
        if (t == QLatin1String("g")) { out.push_back({kGKey, QStringLiteral("g")}); continue; }
        if (auto g = find(t, {Gold, Primary, Blue})) { out.push_back(*g); continue; }
        std::vector<Glyph> parts;                      // ".5", "25", ".1": one key per character
        for (QChar c : t) {
            auto g = find(QString(c), {Primary, Gold});
            if (!g) return std::nullopt;
            parts.push_back(*g);
        }
        out.insert(out.end(), parts.begin(), parts.end());
    }
    return out;
}

}  // namespace vc
