// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
#include "ProgramDocument.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <algorithm>
#include <cstdlib>

namespace vc {

static QString u(const char *s) { return QString::fromUtf8(s); }

bool ProgramDocument::isEmpty() const {
    auto allEmpty = [](const QMap<QString, QString> &m) { for (const auto &v : m) if (!v.isEmpty()) return false; return true; };
    for (const auto &r : registers) if (!r.isEmpty()) return false;
    return title.isEmpty() && author.isEmpty() && description.isEmpty() && allEmpty(labels) && allEmpty(flags) && comments.empty();
}

bool ProgramDocument::operator==(const ProgramDocument &o) const {
    return title == o.title && author == o.author && description == o.description && model == o.model && labels == o.labels
        && flags == o.flags && registers == o.registers && comments == o.comments;
}

QByteArray ProgramDocument::encoded() const {
    QJsonObject o;
    o["format"] = QStringLiteral("voyagerdoc"); o["version"] = 1;
    o["title"] = title; o["author"] = author; o["description"] = description; o["model"] = model;
    QJsonObject l, f, r;
    for (auto it = labels.begin(); it != labels.end(); ++it) l[it.key()] = it.value();
    for (auto it = flags.begin(); it != flags.end(); ++it) f[it.key()] = it.value();
    for (auto it = registers.begin(); it != registers.end(); ++it) r[it.key()] = QJsonObject{{"name", it->name}, {"meaning", it->meaning}};
    o["labels"] = l; o["flags"] = f; o["registers"] = r;
    QJsonArray c;
    for (const StepComment &s : comments)
        c.append(QJsonObject{{"step", s.step}, {"text", s.text}, {"keys", s.keys}, {"before", QJsonArray::fromStringList(s.before)},
                             {"after", QJsonArray::fromStringList(s.after)}, {"orphaned", s.orphaned}});
    o["comments"] = c;
    return QJsonDocument(o).toJson(QJsonDocument::Indented);
}

std::optional<ProgramDocument> ProgramDocument::decode(const QByteArray &json) {
    QJsonParseError err;
    QJsonDocument d = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !d.isObject()) return std::nullopt;
    QJsonObject o = d.object();
    if (o["format"].toString() != QLatin1String("voyagerdoc")) return std::nullopt;
    ProgramDocument p;
    p.title = o["title"].toString(); p.author = o["author"].toString(); p.description = o["description"].toString(); p.model = o["model"].toString();
    for (auto k : o["labels"].toObject().keys()) p.labels[k] = o["labels"].toObject()[k].toString();
    for (auto k : o["flags"].toObject().keys()) p.flags[k] = o["flags"].toObject()[k].toString();
    for (auto k : o["registers"].toObject().keys()) {
        QJsonObject r = o["registers"].toObject()[k].toObject();
        p.registers[k] = {r["name"].toString(), r["meaning"].toString()};
    }
    auto sl = [](const QJsonArray &a) { QStringList l; for (auto v : a) l << v.toString(); return l; };
    for (auto v : o["comments"].toArray()) {
        QJsonObject c = v.toObject();
        StepComment s;
        s.step = c["step"].toInt(); s.text = c["text"].toString(); s.keys = c["keys"].toString();
        s.before = sl(c["before"].toArray()); s.after = sl(c["after"].toArray()); s.orphaned = c["orphaned"].toBool();
        p.comments.push_back(s);
    }
    return p;
}

QString ProgramDocument::comment(int step) const {
    for (const auto &c : comments) if (c.step == step && !c.orphaned) return c.text;
    return QString();
}

std::vector<StepComment> ProgramDocument::orphanedComments() const {
    std::vector<StepComment> o;
    for (const auto &c : comments) if (c.orphaned) o.push_back(c);
    return o;
}

static void context(const std::vector<ProgramLine> &lines, int i, QStringList *b, QStringList *a) {
    for (int k = std::max(0, i - 2); k < i; ++k) *b << lines[size_t(k)].keys;
    for (int k = i + 1; k < std::min(int(lines.size()), i + 3); ++k) *a << lines[size_t(k)].keys;
}

void ProgramDocument::setComment(const QString &text, int step, const std::vector<ProgramLine> &lines) {
    comments.erase(std::remove_if(comments.begin(), comments.end(), [&](const StepComment &c) { return c.step == step && !c.orphaned; }), comments.end());
    QString t = text.trimmed();
    auto it = std::find_if(lines.begin(), lines.end(), [&](const ProgramLine &l) { return l.line == step; });
    if (t.isEmpty() || it == lines.end()) return;
    int i = int(it - lines.begin());
    StepComment c;
    c.step = step; c.text = t; c.keys = lines[size_t(i)].keys;
    context(lines, i, &c.before, &c.after);
    comments.push_back(c);
    std::stable_sort(comments.begin(), comments.end(), [](const auto &a, const auto &b) { return a.step < b.step; });
}

ProgramDocument::RematchReport ProgramDocument::rematch(const std::vector<ProgramLine> &lines) {
    RematchReport report;
    std::vector<int> taken;
    std::vector<StepComment> out;
    auto score = [&](const StepComment &c, int i) {
        QStringList b, a; context(lines, i, &b, &a);
        int s = 0;
        for (int k = 0; k < c.before.size(); ++k) {
            // c.before reversed, compared with b from its end
            const QString &v = c.before[c.before.size() - 1 - k];
            if (k < b.size() && b[b.size() - 1 - k] == v) ++s;
        }
        for (int k = 0; k < c.after.size(); ++k) if (k < a.size() && a[k] == c.after[k]) ++s;
        return s;
    };
    std::vector<StepComment> sorted = comments;
    std::stable_sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) { return a.step < b.step; });
    for (StepComment c : sorted) {
        int old = c.step;
        int bi = -1, bs = 0, bd = 0;
        for (int i = 0; i < int(lines.size()); ++i) {
            const ProgramLine &l = lines[size_t(i)];
            if (l.keys != c.keys || std::find(taken.begin(), taken.end(), l.line) != taken.end()) continue;
            int s = score(c, i), d = std::abs(l.line - old);
            if (bi < 0 || s > bs || (s == bs && d < bd)) { bi = i; bs = s; bd = d; }
        }
        int contextSize = int(c.before.size() + c.after.size());
        if (bi >= 0 && (bs >= 3 || (bs >= 2 && bd <= 60) || (bs >= 1 && bd <= 3) || (contextSize == 0 && bd == 0) || (bs == contextSize && bd <= 60))) {
            const ProgramLine &l = lines[size_t(bi)];
            if (l.line == old && !c.orphaned) ++report.kept; else ++report.moved;
            c.step = l.line; c.orphaned = false;
            c.before.clear(); c.after.clear();
            context(lines, bi, &c.before, &c.after);
            taken.push_back(l.line);
        } else {
            if (!c.orphaned) ++report.orphaned;
            c.orphaned = true;
        }
        out.push_back(c);
    }
    std::stable_sort(out.begin(), out.end(), [](const auto &a, const auto &b) {
        return a.orphaned != b.orphaned ? (!a.orphaned && b.orphaned) : a.step < b.step;
    });
    comments = out;
    return report;
}

void ProgramDocument::prefill(const ProgramAnalysis &a) {
    for (const auto &l : a.labels) if (!labels.contains(l.name)) labels[l.name] = QString();
    for (const auto &r : a.registers) if (!registers.contains(r.name)) registers[r.name] = RegisterNote();
    for (const auto &f : a.flags) if (!flags.contains(f.name)) flags[f.name] = QString();
}

QString ProgramDocumentInfo::modelNameFor(int variant, int model) {
    if (variant == 152) return QStringLiteral("HP-15C (15.2)");
    if (variant == 150) return QStringLiteral("HP-15C");
    return QStringLiteral("HP-%1C").arg(model);
}

QString ProgramDocumentInfo::dateText() const { return QLocale(QLocale::English, QLocale::UnitedKingdom).toString(date, QStringLiteral("d MMMM yyyy")); }

ProgramDocumentContent::ProgramDocumentContent(ProgramDocument d, ProgramDocumentInfo i, std::vector<ProgramLine> l,
                                               std::optional<Pool15> p, std::vector<int> flags)
    : doc(std::move(d)), info(std::move(i)), lines(std::move(l)), setFlags(std::move(flags)) {
    analysis = ProgramAnalysis(lines, info.model);
    if (info.model == 15) pool = p;
}

QString ProgramDocumentContent::title() const {
    return doc.title.isEmpty() ? (info.memoryFile.isEmpty() ? QStringLiteral("Untitled program") : info.memoryFile) : doc.title;
}

QString ProgramDocumentContent::steps(const std::vector<int> &s) const {
    QStringList l;
    for (int x : s) l << (info.model == 12 ? QStringLiteral("%1").arg(x, 2, 10, QLatin1Char('0')) : QStringLiteral("%1").arg(x, 3, 10, QLatin1Char('0')));
    return l.join(QStringLiteral(", "));
}

std::vector<std::pair<QString, QString>> ProgramDocumentContent::memoryRows() const {
    std::vector<std::pair<QString, QString>> r;
    if (!pool) return r;
    const Pool15 &p = *pool;
    QString last = p.dimI < 10 ? QStringLiteral("R%1").arg(p.dimI) : p.dimI < 20 ? QStringLiteral("R.%1").arg(p.dimI - 10) : QStringLiteral("R%1").arg(p.dimI);
    r.push_back({u("DIM (i)"), QStringLiteral("%1: R0–%2, %3 storage registers").arg(p.dimI).arg(last).arg(p.dimI + 1)});
    QStringList dims;
    for (int m = 0; m < 5; ++m) if (p.rows[m] * p.cols[m] > 0) dims << QStringLiteral("%1 %2").arg(Pool15::letter(m), p.dimension(m));
    r.push_back({u("Matrices"), dims.isEmpty() ? u("none dimensioned (all 0×0)") : dims.join(QStringLiteral(", ")) + QStringLiteral(" (%1 registers)").arg(p.matrixRegisters)});
    r.push_back({u("RESULT"), QStringLiteral("matrix %1").arg(p.resultLetter())});
    r.push_back({u("Complex mode"), p.complex ? u("on (flag 8): 5 registers hold the imaginary stack") : u("off")});
    QStringList fl;
    for (int f : setFlags) if (f <= 9) fl << (f == 8 ? u("8 (complex)") : f == 9 ? u("9 (overflow)") : QString::number(f));
    r.push_back({u("Flags set"), fl.isEmpty() ? u("none") : fl.join(QStringLiteral(", "))});
    r.push_back({u("g MEM"), QStringLiteral("%1   (DIM (i), free registers, program registers–bytes left; pool %2 registers + R0, R1)").arg(p.gmem).arg(p.size())});
    const auto *ws = analysis.matrixOp(u("SOLVE/∫ workspace"));
    int work = ws ? (ws->operations.contains(u("∫ʸₓ")) ? (ws->operations.contains(u("SOLVE")) ? 28 : 23) : 5) : 0;
    int need = p.storage + p.matrixRegisters + (p.complex ? 5 : 0) + p.programRegisters + work;
    QString needText = QStringLiteral("%1 of %2 pool registers (R2… %3, matrices %4, complex %5, program %6").arg(need).arg(p.size()).arg(p.storage).arg(p.matrixRegisters).arg(p.complex ? 5 : 0).arg(p.programRegisters);
    needText += work > 0 ? QStringLiteral(", SOLVE/∫ workspace %1)").arg(work) : QStringLiteral(")");
    r.push_back({u("Needs at least"), needText});
    return r;
}

std::vector<ProgramDocumentContent::MatrixOpRow> ProgramDocumentContent::matrixOpRows() const {
    std::vector<MatrixOpRow> rows;
    for (const auto &m : analysis.matrixOps) {
        QStringList notes = m.notes;
        if (m.item.startsWith(QStringLiteral("Matrix ")) && pool) {
            int i = m.item.mid(7).isEmpty() ? -1 : m.item.at(7).unicode() - 'A';
            if (i >= 0 && i < 5) notes.prepend(QStringLiteral("now ") + pool->dimension(i) + (pool->result == i ? u(", the RESULT matrix") : QString()));
        }
        rows.push_back({m.item, m.operations.join(QStringLiteral(", ")), steps(m.steps), notes.join(QStringLiteral("; "))});
    }
    return rows;
}

std::vector<ProgramDocumentContent::LabelRow> ProgramDocumentContent::labelRows() const {
    std::vector<LabelRow> rows;
    for (const auto &l : analysis.labels) {
        QString called = steps(l.called);
        if (!l.callKinds.isEmpty() && !l.called.empty()) called += QStringLiteral(" (") + l.callKinds.join(QStringLiteral(", ")) + QStringLiteral(")");
        QString defined = l.defined.empty() ? (l.isIndirect ? u("computed in I") : u("not defined")) : steps(l.defined);
        if (info.model == 12 && !l.isIndirect && l.defined.size() == 1 && l.defined.front() == 0) defined = u("00 (start)");     // line 00 always exists (0.6.9)
        QString name = info.model == 12 && !l.isIndirect ? QStringLiteral("line ") + l.name : (l.isIndirect ? l.name : QStringLiteral("LBL ") + l.name);
        rows.push_back({name, doc.labels.value(l.name), defined, called.isEmpty() ? u("—") : called});
    }
    return rows;
}

std::vector<ProgramDocumentContent::RegisterRow> ProgramDocumentContent::registerRows() const {
    std::vector<RegisterRow> rows;
    for (const auto &r : analysis.registers) {
        RegisterNote n = doc.registers.value(r.name);
        QStringList parts;
        if (!n.name.isEmpty()) parts << n.name;
        if (!n.meaning.isEmpty()) parts << n.meaning;
        rows.push_back({r.name, parts.join(QStringLiteral(": ")), r.operations.join(QStringLiteral(", ")), steps(r.steps)});
    }
    return rows;
}

std::vector<ProgramDocumentContent::RegisterRow> ProgramDocumentContent::flagRows() const {
    std::vector<RegisterRow> rows;
    for (const auto &f : analysis.flags) {
        QString mine = doc.flags.value(f.name);
        rows.push_back({QStringLiteral("Flag ") + f.name, mine.isEmpty() ? f.meaning : mine, f.operations.join(QStringLiteral(", ")), steps(f.steps)});
    }
    return rows;
}

std::vector<ProgramDocumentContent::ListingRow> ProgramDocumentContent::listingRows() const {
    std::vector<ListingRow> rows;
    for (const auto &l : lines)
        rows.push_back({l.number(info.model), l.screenView(info.model), l.fullMnemonic(), doc.comment(l.line), l.mnemonic.startsWith(QStringLiteral("LBL "))});
    return rows;
}

QString ProgramDocumentContent::plainText() const {
    QStringList o;
    const QString t = title();
    o << t << QString(std::max<qsizetype>(8, t.size()), QLatin1Char('=')) << QStringLiteral("Model:     ") + info.modelName;
    if (!info.firmware.isEmpty()) o << QStringLiteral("Firmware:  ") + info.firmware;
    if (!info.memoryFile.isEmpty()) o << QStringLiteral("Memory:    ") + info.memoryFile;
    o << QStringLiteral("Steps:     %1").arg(lines.size()) << QStringLiteral("Date:      ") + info.dateText();
    if (!doc.author.isEmpty()) o << QStringLiteral("Author:    ") + doc.author;
    if (!doc.description.isEmpty()) { o << QString() << doc.description; }
    auto table = [&](const QStringList &head, const std::vector<QStringList> &rows) {
        std::vector<int> w;
        for (int i = 0; i < head.size(); ++i) {
            int m = int(head[i].size());
            for (const auto &r : rows) m = std::max(m, int(r[i].size()));
            w.push_back(m);
        }
        auto fmt = [&](const QStringList &r) {
            QStringList c;
            for (int i = 0; i < r.size(); ++i) c << (i == r.size() - 1 ? r[i] : r[i].leftJustified(w[size_t(i)]));
            QString s = c.join(QStringLiteral("  "));
            while (s.endsWith(QLatin1Char(' '))) s.chop(1);
            return s;
        };
        o << fmt(head);
        QStringList dash; for (int x : w) dash << QString(x, QLatin1Char('-'));
        o << fmt(dash);
        for (const auto &r : rows) o << fmt(r);
    };
    auto lr = labelRows();
    if (info.model == 12) {          // 0.6.9: the 12C text export always has the section, named as in the PDF
        o << QString() << u("BRANCH TARGETS") << QString();
        if (lr.empty()) o << u("No GTO in this program.");
        else {
            std::vector<QStringList> rows; for (auto &r : lr) rows.push_back({r.label, r.defined, r.called, r.notes});
            table({u("Line"), u("Defined"), u("Called from"), u("Notes")}, rows);
        }
    } else if (!lr.empty()) {
        o << QString() << u("LABELS") << QString();
        std::vector<QStringList> rows; for (auto &r : lr) rows.push_back({r.label, r.defined, r.called, r.notes});
        table({u("Label"), u("Defined"), u("Called from"), u("Notes")}, rows);
    }
    auto rr = registerRows(), fr = flagRows();
    if (!rr.empty() || !fr.empty()) {
        o << QString() << u("REGISTERS AND FLAGS") << QString();
        std::vector<QStringList> rows;
        for (auto &r : rr) rows.push_back({r.reg, r.use, r.steps, r.name});
        for (auto &r : fr) rows.push_back({r.reg, r.use, r.steps, r.name});
        table({u("Register"), u("Use"), u("Steps"), u("Name / meaning")}, rows);
    }
    auto mr = memoryRows();
    if (!mr.empty()) {
        o << QString() << u("MEMORY CONFIGURATION") << QString();
        for (auto &kv : mr) o << kv.first.leftJustified(16) + kv.second;
    }
    auto mo = matrixOpRows();
    if (!mo.empty()) {
        o << QString() << u("MATRICES AND COMPLEX") << QString();
        std::vector<QStringList> rows; for (auto &r : mo) rows.push_back({r.item, r.operations, r.steps, r.notes});
        table({u("Item"), u("Operations"), u("Steps"), u("Notes")}, rows);
    }
    o << QString() << u("LISTING") << QString();
    std::vector<QStringList> rows; for (auto &r : listingRows()) rows.push_back({r.step, r.screen, r.keys, r.comment});
    table({u("Step"), u("Display"), u("Keys"), u("Comment")}, rows);
    auto orph = doc.orphanedComments();
    if (!orph.empty()) {
        o << QString() << u("COMMENTS NO LONGER MATCHING A STEP") << QString();
        for (auto &c : orph) o << QStringLiteral("was %1 (%2): %3").arg(c.step, 3, 10, QLatin1Char('0')).arg(c.keys, c.text);
    }
    return o.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

}  // namespace vc
