#include "ReportBuilder.h"
#include "core/Event.h"
#include "core/FisPenalty.h"
#include "core/Ranking.h"

#include <QCoreApplication>
#include <algorithm>

namespace {
struct Tr {
    Q_DECLARE_TR_FUNCTIONS(Report)
};
}

namespace ReportBuilder {


static QString statusShort(ResultStatus s)
{
    switch (s) {
    case ResultStatus::DNS: return Tr::tr("DNS");
    case ResultStatus::NPS: return Tr::tr("NPS");
    case ResultStatus::DNF: return Tr::tr("DNF");
    case ResultStatus::DSQ: return Tr::tr("DSQ");
    case ResultStatus::Classified: return QString();
    default: return QString();
    }
}

struct Ctx {
    const Event& ev;
    const ReportFormat& fmt;
    int run;
    Precision prec;
    QHash<int, int> racePoints; // FIS race points of the time (without penalty) by bib
};

static QString competitorCell(const Ctx& c, int field, int bib)
{
    const Competitor* comp = c.ev.competitorByBib(bib);
    if (field == CompField::Bib)
        return bib > 0 ? QString::number(bib) : QString();
    if (!comp)
        return field == CompField::Name ? c.ev.competitorName(bib) : QString();
    return comp->value(field);
}

static QString pointsCell(const Ctx& c, int bib)
{
    const Competitor* comp = c.ev.competitorByBib(bib);
    if (!comp)
        return QString();
    return TimeUtil::formatPoints(comp->pointsFor(c.ev.params().type.discipline));
}

static QString rankedCell(const Ctx& c, int field, const RankedEntry& e, bool classified)
{
    using namespace ReportField;
    if (field > 0 && field < 100)
        return competitorCell(c, field, e.bib);
    if (field > RunTime && field <= RunTime + kMaxRuns) {
        const int r = field - RunTime;
        return TimeUtil::formatResult(e.runMs[size_t(r - 1)], c.prec);
    }
    if (field > RunTimeRank && field <= RunTimeRank + kMaxRuns) {
        const int r = field - RunTimeRank;
        const qint64 t = e.runMs[size_t(r - 1)];
        if (t < 0)
            return QString();
        if (!classified) // not classified: the time of the run without its rank
            return TimeUtil::formatResult(t, c.prec);
        return QStringLiteral("%1 (%2)").arg(TimeUtil::formatResult(t, c.prec)).arg(e.runRank[size_t(r - 1)]);
    }
    if (field > RunRank && field <= RunRank + kMaxRuns) {
        const int r = field - RunRank;
        const int rank = e.runRank[size_t(r - 1)];
        return classified && rank > 0 && e.runMs[size_t(r - 1)] >= 0 ? QStringLiteral("(%1)").arg(rank) : QString();
    }
    if (field > Inter && field <= Inter + kMaxInter) {
        const int i = field - Inter - 1;
        const qint64 t = e.interMs[size_t(i)];
        if (t < 0)
            return QString();
        if (e.interRank[size_t(i)] > 0 && classified)
            return QStringLiteral("%1 (%2)").arg(TimeUtil::formatResult(t, c.prec)).arg(e.interRank[size_t(i)]);
        return TimeUtil::formatResult(t, c.prec);
    }
    switch (field) {
    case Rank: return classified ? QString::number(e.rank) : QString();
    case Time: return classified ? TimeUtil::formatResult(e.shownMs, c.prec) : QString();
    case Gap: return (classified && e.gapMs > 0) ? TimeUtil::formatGap(e.gapMs, c.prec) : QString();
    case Status: return statusShort(e.status);
    case Reason: return e.reason;
    case Penalty: return e.penaltyMs > 0 ? TimeUtil::formatResult(e.penaltyMs, c.prec) : QString();
    case Points: return pointsCell(c, e.bib);
    case RacePoints: {
        const auto it = c.racePoints.constFind(e.bib);
        return classified && it != c.racePoints.constEnd() ? TimeUtil::formatPoints(*it) : QString();
    }
    case Dotted: return QStringLiteral(".........................");
    default: break;
    }
    return QString();
}

static void fillRanking(const Ctx& c, ReportDocument& doc, const RankingResult& res, const QList<OutputGroup>& groups)
{
    auto classifiedSection = [&](const RankingResult& r, const QString& heading) {
        ReportSection s;
        s.heading = heading;
        s.groupHeading = !heading.isEmpty();
        for (const auto& e : r.classified) {
            QStringList row;
            for (const auto& col : c.fmt.fields)
                row << rankedCell(c, col.field, e, true);
            s.rows << row;
        }
        doc.rowCount += s.rows.size();
        doc.sections << s;
    };
    if (groups.isEmpty()) {
        classifiedSection(res, QString());
    } else {
        // SkiPro / SkiAlp: the classified competitors by groups, ranked inside each group
        for (const OutputGroup& g : groups) {
            const RankingResult sub = Ranking::subRanking(res, [&](int bib) {
                const Competitor* comp = c.ev.competitorByBib(bib);
                return comp && g.matches(*comp);
            }, c.prec);
            if (!sub.classified.isEmpty())
                classifiedSection(sub, g.label);
        }
    }
    doc.infoText = Tr::tr("%n competitor(s) classified", nullptr, int(res.classified.size()));
    if (!c.fmt.printNotClassified)
        return;
    for (const auto& g : res.unclassified) {
        ReportSection s;
        s.heading = QStringLiteral("%1 (%2)").arg(g.title(c.ev.params().type.runName(g.run))).arg(g.entries.size());
        for (const auto& e : g.entries) {
            QStringList row;
            for (const auto& col : c.fmt.fields)
                row << rankedCell(c, col.field, e, false);
            s.rows << row;
            // gate of the disqualification at the end of the line, like SkiPro / SkiAlp
            QString note;
            if (e.status == ResultStatus::DSQ && !e.reason.trimmed().isEmpty()) {
                bool number = false;
                e.reason.trimmed().toInt(&number);
                note = number ? Tr::tr("Gate %1").arg(e.reason.trimmed()) : e.reason.trimmed();
            }
            s.notes << note;
        }
        doc.rowCount += s.rows.size();
        doc.sections << s;
    }
}

static void buildCompetitors(const Ctx& c, ReportDocument& doc)
{
    QVector<const Competitor*> list;
    for (const auto& comp : c.ev.competitors())
        list << &comp;
    std::stable_sort(list.begin(), list.end(), [](const Competitor* a, const Competitor* b) {
        const int cmp = QString::localeAwareCompare(a->fullName(), b->fullName());
        return cmp != 0 ? cmp < 0 : a->bib < b->bib;
    });
    ReportSection s;
    for (const Competitor* comp : list) {
        s.owners << comp;
        QStringList row;
        for (const auto& col : c.fmt.fields) {
            if (col.field == ReportField::Points)
                row << TimeUtil::formatPoints(comp->pointsFor(c.ev.params().type.discipline));
            else if (col.field > 0 && col.field < 100)
                row << comp->value(col.field);
            else
                row << QString();
        }
        s.rows << row;
    }
    doc.rowCount = s.rows.size();
    doc.sections << s;
    doc.summary = Tr::tr("%n competitor(s)", nullptr, int(list.size()));
    doc.infoText = doc.summary;
}

static void buildStartList(const Ctx& c, ReportDocument& doc)
{
    const RunData& run = c.ev.run(c.run);
    const RunData& prev = c.ev.run(qMax(1, c.run - 1));
    ReportSection s;
    int order = 0;
    for (const auto& entry : run.startList) {
        ++order;
        s.owners << c.ev.competitorByBib(entry.bib);
        QStringList row;
        for (const auto& col : c.fmt.fields) {
            using namespace ReportField;
            if (col.field > 0 && col.field < 100)
                row << competitorCell(c, col.field, entry.bib);
            else if (col.field == Rank)
                row << QString::number(order);
            else if (col.field == Points)
                row << pointsCell(c, entry.bib);
            else if (col.field == PreviousRun) {
                if (c.run <= 1)
                    row << pointsCell(c, entry.bib);
                else {
                    const TimeInfo info = prev.info(entry.bib);
                    row << (info.isClassified() ? TimeUtil::formatResult(info.timeMs, c.prec) : QString());
                }
            } else if (col.field == StartTime)
                row << (entry.startTimeUs >= 0 ? TimeUtil::formatClock(entry.startTimeUs, 0) : QString());
            else if (col.field == Dotted)
                row << QStringLiteral(".........................");
            else if (col.field > RunTime && col.field <= RunTime + kMaxRuns) {
                const TimeInfo info = c.ev.run(col.field - RunTime).info(entry.bib);
                row << (info.isClassified() ? TimeUtil::formatResult(info.timeMs, c.prec) : statusShort(info.status));
            } else
                row << QString();
        }
        s.rows << row;
    }
    doc.rowCount = s.rows.size();
    doc.sections << s;
    doc.infoText = Tr::tr("%n competitor(s)", nullptr, int(s.rows.size()));
}

static QString pointName(int point)
{
    if (point == Point::Start)
        return Tr::tr("Start");
    if (point == Point::Finish)
        return Tr::tr("Finish");
    return Tr::tr("Int. %1").arg(point);
}

static void buildRecords(const Ctx& c, ReportDocument& doc, bool byBib)
{
    const RunData& run = c.ev.run(c.run);
    QVector<const TimeRecord*> recs;
    for (const auto& r : run.records)
        recs << &r;
    std::stable_sort(recs.begin(), recs.end(), [byBib](const TimeRecord* a, const TimeRecord* b) {
        if (byBib && a->bib != b->bib)
            return (a->bib <= 0 ? INT_MAX : a->bib) < (b->bib <= 0 ? INT_MAX : b->bib);
        if (a->point != b->point)
            return a->point < b->point;
        return a->id < b->id;
    });
    ReportSection s;
    for (const TimeRecord* r : recs) {
        s.owners << (r->bib > 0 ? c.ev.competitorByBib(r->bib) : nullptr);
        QStringList row;
        for (const auto& col : c.fmt.fields) {
            using namespace ReportField;
            switch (col.field) {
            case RecSeq: row << QString::number(r->id); break;
            case RecPoint: row << pointName(r->point); break;
            case RecStatus:
                row << ((r->flags & RecordFlag::DNS)        ? Tr::tr("DNS")
                            : (r->flags & RecordFlag::NPS)  ? Tr::tr("NPS")
                            : (r->flags & RecordFlag::DNF)  ? Tr::tr("DNF")
                            : !r->isActive()                ? Tr::tr("--")
                            : r->bib <= 0                   ? Tr::tr("?")
                                                            : Tr::tr("OK"));
                break;
            case RecNum: row << (r->bib > 0 ? QString::number(r->bib) : QString()); break;
            case RecHour: row << (r->hasTime() ? TimeUtil::formatDayTime(r->dayTimeUs) : QString()); break;
            case RecTime: {
                QString t;
                if (r->point != Point::Start && r->bib > 0) {
                    const TimeInfo info = run.info(r->bib);
                    if (r->point == Point::Finish && info.timeMs >= 0)
                        t = TimeUtil::formatResult(info.timeMs, c.prec);
                    else if (r->point >= 1 && r->point <= kMaxInter && info.interMs[size_t(r->point - 1)] >= 0)
                        t = TimeUtil::formatResult(info.interMs[size_t(r->point - 1)], c.prec);
                }
                row << t;
                break;
            }
            default:
                row << (col.field > 0 && col.field < 100 ? competitorCell(c, col.field, r->bib) : QString());
                break;
            }
        }
        s.rows << row;
    }
    doc.rowCount = s.rows.size();
    doc.sections << s;
    doc.infoText = QString::number(s.rows.size());
}

static void buildPaper(const Ctx& c, ReportDocument& doc)
{
    const RunData& run = c.ev.run(c.run);
    ReportSection s;
    for (const auto& p : run.paper) {
        QStringList row;
        for (const auto& col : c.fmt.fields) {
            using namespace ReportField;
            switch (col.field) {
            case PaperSeq: row << QString::number(p.seq); break;
            case PaperDevice: row << QString::number(p.device); break;
            case PaperReceived: row << (p.receivedUs >= 0 ? TimeUtil::formatClock(p.receivedUs, 0) : QString()); break;
            case PaperText: row << (p.error ? p.text + QStringLiteral("  ") + Tr::tr("(checksum error)") : p.text); break;
            default: row << QString(); break;
            }
        }
        s.rows << row;
    }
    doc.rowCount = s.rows.size();
    doc.sections << s;
    doc.infoText = QString::number(s.rows.size());
}

// FIS penalty calculation sheet (like SkiPro / SkiAlp Pro): per gender the 10 first with the
// 5 best FIS points, the 5 best at start and the calculation.
static void buildPenalty(const Ctx& c, ReportDocument& doc)
{
    using namespace ReportField;
    const QVector<Gender> genders = FisPenalty::genders(c.ev);
    auto pts = [](int v) { return TimeUtil::formatPoints(v); };
    for (Gender g : genders) {
        const FisPenalty::Result r = FisPenalty::compute(c.ev, g);
        if (genders.size() > 1) {
            ReportSection h;
            h.heading = g == Gender::Female ? Tr::tr("Women") : Tr::tr("Men");
            h.groupHeading = true;
            doc.sections << h;
        }
        if (!r.valid) {
            ReportSection s;
            s.heading = r.error;
            doc.sections << s;
            continue;
        }
        const int maxValue = c.ev.params().fisPoints.forGender(g).maxValue;
        auto lineRow = [&](const FisPenalty::Line& l, bool top10) {
            QStringList row;
            for (const auto& col : c.fmt.fields) {
                if (col.field == Rank) {
                    if (l.rank > 0)
                        row << QString::number(l.rank);
                    else
                        row << statusShort(l.status);
                } else if (col.field == Points)
                    row << pts(l.listPoints);
                else if (col.field == Best5)
                    row << (!top10 || l.selected ? pts(l.usedPoints) : QStringLiteral("-"));
                else if (col.field == TimePoints)
                    row << (top10 && l.selected ? pts(l.usedRacePoints) : (top10 ? QStringLiteral("-") : QString()));
                else if (col.field > 0 && col.field < 100)
                    row << competitorCell(c, col.field, l.bib);
                else
                    row << QString();
            }
            return row;
        };
        auto maxRow = [&](bool withRace) {
            QStringList row;
            for (const auto& col : c.fmt.fields) {
                if (col.field == CompField::Name || col.field == CompField::LastName)
                    row << Tr::tr("(maximum value)");
                else if (col.field == Best5)
                    row << pts(maxValue);
                else if (col.field == TimePoints && withRace)
                    row << pts(maxValue);
                else
                    row << QString();
            }
            return row;
        };
        ReportSection top;
        top.heading = Tr::tr("Best 10 at finish");
        for (const auto& l : r.top10)
            top.rows << lineRow(l, true);
        for (int i = 0; i < r.missingA; ++i)
            top.rows << maxRow(true);
        ReportSection start;
        start.heading = Tr::tr("Best 5 at start");
        for (const auto& l : r.best5Start)
            start.rows << lineRow(l, false);
        for (int i = 0; i < r.missingB; ++i)
            start.rows << maxRow(false);
        // the sums under the columns they come from (FIS Alpine Data Booklet, appendix)
        auto column = [&](int field) {
            for (int i = 0; i < c.fmt.fields.size(); ++i)
                if (c.fmt.fields.at(i).field == field)
                    return QString::number(i);
            return QStringLiteral("-1");
        };
        const QString colPoints = column(Points), colBest = column(Best5), colRace = column(TimePoints);
        ReportSection legend;
        legend.textRows = true;
        legend.rows = { { Tr::tr("(DNF Did Not Finish, DSQ Disqualified)"), QString(), QString(), QStringLiteral("-1") } };
        ReportSection total;
        total.heading = Tr::tr("TOTAL");
        total.textRows = true;
        total.rows = {
            { Tr::tr("(B) List points of best 5 at start"), pts(r.sumB), QStringLiteral("box"), colPoints },
            { Tr::tr("(A) List points of the best 5 to finish in top 10"), pts(r.sumA), QStringLiteral("box"), colBest },
            { Tr::tr("(C) Race points of corresponding competitors"), pts(r.sumC), QStringLiteral("box"), colRace },
        };
        ReportSection calc;
        calc.heading = QStringLiteral(" ");
        calc.textRows = true;
        const qint64 sum = qint64(r.sumA) + r.sumB - r.sumC;
        calc.rows = {
            { Tr::tr("Calculated penalty (A + B - C) = %1 / 10").arg(pts(int(sum))), pts(r.calculated), QString(), colPoints },
            { Tr::tr("Correction value (Z)"), pts(r.zValue), QString(), colPoints },
            { Tr::tr("Category adder"), pts(r.adder), QString(), colPoints },
        };
        // minimum / maximum penalty only when they change the penalty
        const int raw = r.calculated + r.zValue + r.adder;
        if (raw < r.minPenalty)
            calc.rows << QStringList{ r.doubledMinimum ? Tr::tr("Minimum penalty (2 x maximum value)") : Tr::tr("Minimum penalty"),
                                      pts(r.minPenalty), QString(), colPoints };
        else if (raw > r.maxPenalty)
            calc.rows << QStringList{ Tr::tr("Maximum penalty"), pts(r.maxPenalty), QString(), colPoints };
        if (r.overridden)
            calc.rows << QStringList{ Tr::tr("Calculated applied penalty"), pts(r.computedApplied), QString(), colPoints };
        calc.rows << QStringList{ r.overridden ? Tr::tr("Applied penalty (Technical Delegate)") : Tr::tr("Applied penalty"),
                                  pts(r.applied), QStringLiteral("black"), colPoints };
        doc.rowCount += int(top.rows.size() + start.rows.size() + total.rows.size() + calc.rows.size() + 1);
        doc.sections << top << start << legend << total << calc;
    }
    doc.infoText = Tr::tr("%n gender(s)", nullptr, int(genders.size()));
    if (genders.isEmpty())
        doc.infoText = Tr::tr("No competitor");
}

// Splits the list of the report into one section per output group.
static void splitByGroups(ReportDocument& doc, const QList<OutputGroup>& groups)
{
    if (doc.sections.isEmpty())
        return;
    const ReportSection all = doc.sections.takeFirst();
    QVector<ReportSection> parts;
    doc.rowCount = 0;
    for (const OutputGroup& g : groups) {
        ReportSection s;
        s.heading = g.label;
        s.groupHeading = true;
        for (int i = 0; i < all.rows.size(); ++i) {
            const Competitor* comp = all.owners.value(i);
            if (comp && g.matches(*comp)) {
                s.rows << all.rows.at(i);
                s.owners << comp;
            }
        }
        if (!s.rows.isEmpty()) {
            doc.rowCount += s.rows.size();
            parts << s;
        }
    }
    for (const ReportSection& s : doc.sections)
        doc.rowCount += s.rows.size();
    doc.sections = parts + doc.sections;
}

ReportDocument build(const Event& ev, const ReportFormat& format, int run)
{
    ReportDocument doc;
    doc.format = format;
    doc.run = qBound(1, run, ev.runCount());
    // the fields of the second lines are computed as extra columns, then joined to their cell
    const bool twoLines = format.hasSecondLines();
    ReportFormat cells = format;
    if (twoLines)
        for (const auto& col : format.fields)
            cells.fields << ReportColumn{ col.field2, col.align, col.width };
    Ctx c{ ev, cells, doc.run, ev.precision(), {} };
    bool racePoints = false;
    for (const auto& col : cells.fields)
        racePoints = racePoints || col.field == ReportField::RacePoints;
    if (racePoints)
        for (Gender g : FisPenalty::genders(ev)) {
            const FisPenalty::Result r = FisPenalty::compute(ev, g);
            if (r.valid)
                for (auto it = r.racePoints.cbegin(); it != r.racePoints.cend(); ++it)
                    c.racePoints.insert(it.key(), *it);
        }

    QString title = format.title;
    if (title.contains(QLatin1String("%1")))
        title = title.arg(doc.run);
    doc.docTitle = title;
    auto label = [&](int field) {
        // runs by their names (Event type)
        const auto& type = ev.params().type;
        if (field > ReportField::RunTime && field <= ReportField::RunTime + kMaxRuns)
            return type.runName(field - ReportField::RunTime);
        if (field > ReportField::RunTimeRank && field <= ReportField::RunTimeRank + kMaxRuns)
            return Tr::tr("%1(Rank)").arg(type.runName(field - ReportField::RunTimeRank));
        if (field == ReportField::PreviousRun && doc.run > 1)
            return type.runName(doc.run - 1);
        if (field > 0 && field < 100) {
            const QString custom = ev.params().fields.labels.value(field);
            if (!custom.isEmpty())
                return custom;
        }
        return reportFieldLabel(field, format.type, doc.run);
    };
    for (const auto& col : format.fields) {
        QString l = col.title.isEmpty() ? label(col.field) : col.title;
        if (col.field2 > 0)
            l += QLatin1Char('\n') + label(col.field2);
        doc.columnLabels << l;
    }

    const QList<OutputGroup> groups =
        format.byGroups && reportSupportsGroups(format.type) ? ev.params().outputGroups : QList<OutputGroup>();

    switch (format.type) {
    case ReportType::Competitors:
    case ReportType::CompetitorsPoints:
        buildCompetitors(c, doc);
        break;
    case ReportType::StartList:
        buildStartList(c, doc);
        break;
    case ReportType::RunRanking:
        fillRanking(c, doc, Ranking::runRanking(ev, doc.run), groups);
        break;
    case ReportType::RankingAfterRun:
        fillRanking(c, doc, Ranking::finalRanking(ev, doc.run), groups);
        break;
    case ReportType::FinalRanking:
    case ReportType::FinalRankingPoints:
        fillRanking(c, doc, Ranking::finalRanking(ev, 0), groups);
        break;
    case ReportType::PenaltyCalculation:
        buildPenalty(c, doc);
        break;
    case ReportType::TimeRecords:
        buildRecords(c, doc, false);
        break;
    case ReportType::TimeRecordsByBib:
        buildRecords(c, doc, true);
        break;
    case ReportType::TimingPaper:
        buildPaper(c, doc);
        break;
    }
    const bool ranking = format.type == ReportType::RunRanking || format.type == ReportType::RankingAfterRun
        || format.type == ReportType::FinalRanking || format.type == ReportType::FinalRankingPoints;
    if (!groups.isEmpty() && !ranking)
        splitByGroups(doc, groups);
    if (twoLines) {
        const int n = int(format.fields.size());
        for (auto& s : doc.sections)
            for (auto& row : s.rows) {
                for (int i = 0; i < n; ++i) {
                    const QString second = row.value(n + i);
                    if (format.fields.at(i).field2 > 0 && !second.isEmpty())
                        row[i] += QLatin1Char('\n') + second;
                }
                row = row.mid(0, n);
            }
    }
    return doc;
}

} // namespace ReportBuilder
