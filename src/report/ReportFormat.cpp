#include "ReportFormat.h"
#include "core/Competitor.h"
#include "core/Types.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {
struct Tr {
    Q_DECLARE_TR_FUNCTIONS(Report)
};
}


QString ReportFormat::storageId(ReportType type)
{
    switch (type) {
    case ReportType::Competitors: return QStringLiteral("competitors");
    case ReportType::CompetitorsPoints: return QStringLiteral("competitors_points");
    case ReportType::StartList: return QStringLiteral("start_list");
    case ReportType::RunRanking: return QStringLiteral("run_ranking");
    case ReportType::RankingAfterRun: return QStringLiteral("ranking_after_run");
    case ReportType::FinalRanking: return QStringLiteral("final_ranking");
    case ReportType::TimeRecords: return QStringLiteral("time_records");
    case ReportType::TimeRecordsByBib: return QStringLiteral("time_records_bib");
    case ReportType::TimingPaper: return QStringLiteral("timing_paper");
    case ReportType::FinalRankingPoints: return QStringLiteral("final_ranking_points");
    case ReportType::PenaltyCalculation: return QStringLiteral("penalty_calculation");
    }
    return QStringLiteral("unknown");
}

bool ReportFormat::layoutPerRun(ReportType type)
{
    return type == ReportType::StartList || type == ReportType::RunRanking || type == ReportType::RankingAfterRun;
}

ReportFormat ReportFormat::forRun(const ReportFormat& f, ReportType type, int from, int to)
{
    using namespace ReportField;
    ReportFormat out = f;
    if (from == to)
        return out;
    if (type == ReportType::StartList) {
        // run 1 shows the list points, the next runs the time of the previous run
        for (auto& c : out.fields) {
            const bool previous = (from == 1 && c.field == Points) || (from > 1 && c.field == RunTime + from - 1);
            if (!previous)
                continue;
            if (to == 1) {
                c.field = Points;
                c.title = Tr::tr("List points");
            } else {
                c.field = RunTime + to - 1;
                c.title.clear();
            }
        }
    } else if (type == ReportType::RankingAfterRun) {
        // runs after `to` do not exist yet; the missing runs come after the last run column
        // (or before the time when there is none)
        int last = -1;
        QList<ReportColumn> cols;
        for (const auto& c : out.fields) {
            const bool runCol = c.field > RunTimeRank && c.field <= RunTimeRank + kMaxRuns;
            if (runCol && c.field > RunTimeRank + to)
                continue;
            cols << c;
            if (runCol)
                last = cols.size() - 1;
        }
        int at = last + 1;
        if (last < 0) {
            at = cols.size();
            for (int i = 0; i < cols.size(); ++i)
                if (cols.at(i).field == Time) {
                    at = i;
                    break;
                }
        }
        for (int r = 1; r <= qMin(to, kMaxRuns); ++r) {
            bool has = false;
            for (const auto& c : cols)
                has = has || c.field == RunTimeRank + r;
            if (has)
                continue;
            cols.insert(at++, ReportColumn{ RunTimeRank + r, 2, 12 });
        }
        out.fields = cols;
    }
    return out;
}

QString ReportFormat::storageId(ReportType type, int run)
{
    if (!layoutPerRun(type) || run < 1)
        return storageId(type);
    return QStringLiteral("%1_run%2").arg(storageId(type)).arg(run);
}

QString ReportFormat::typeName(ReportType type)
{
    switch (type) {
    case ReportType::Competitors: return Tr::tr("List of Competitors");
    case ReportType::CompetitorsPoints: return Tr::tr("List of Competitors (points)");
    case ReportType::StartList: return Tr::tr("Run Start List");
    case ReportType::RunRanking: return Tr::tr("Run ranking");
    case ReportType::RankingAfterRun: return Tr::tr("Ranking after a run");
    case ReportType::FinalRanking: return Tr::tr("Final ranking");
    case ReportType::TimeRecords: return Tr::tr("Times records");
    case ReportType::TimeRecordsByBib: return Tr::tr("Time records (by numbers)");
    case ReportType::TimingPaper: return Tr::tr("Timing device paper");
    case ReportType::FinalRankingPoints: return Tr::tr("Final ranking (points)");
    case ReportType::PenaltyCalculation: return Tr::tr("Penalty calculation");
    }
    return QString();
}

bool ReportFormat::hasSecondLines() const
{
    for (const auto& c : fields)
        if (c.field2 > 0)
            return true;
    return false;
}

bool reportSupportsGroups(ReportType type)
{
    return type != ReportType::TimingPaper && type != ReportType::PenaltyCalculation;
}

QString ReportFormat::toString() const
{
    QJsonObject o;
    o[QStringLiteral("title")] = title;
    o[QStringLiteral("printHeader")] = printHeader;
    o[QStringLiteral("columns")] = columns;
    o[QStringLiteral("linesFirstPage")] = linesFirstPage;
    o[QStringLiteral("landscape")] = landscape;
    o[QStringLiteral("notes")] = notes;
    o[QStringLiteral("printNotClassified")] = printNotClassified;
    o[QStringLiteral("byGroups")] = byGroups;
    o[QStringLiteral("tableWidth")] = tableWidth;
    QJsonArray sig;
    for (const auto& s : signatures)
        sig.append(s);
    o[QStringLiteral("signatures")] = sig;
    QJsonArray f;
    for (const auto& c : fields)
        f.append(QJsonArray{ c.field, c.align, c.width, c.field2, c.bold, c.title });
    o[QStringLiteral("fields")] = f;
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

ReportFormat ReportFormat::fromString(const QString& s, ReportType type)
{
    ReportFormat f = defaults(type);
    const QJsonDocument doc = QJsonDocument::fromJson(s.toUtf8());
    if (!doc.isObject())
        return f;
    const QJsonObject o = doc.object();
    f.title = o.value(QStringLiteral("title")).toString(f.title);
    f.printHeader = o.value(QStringLiteral("printHeader")).toBool(f.printHeader);
    f.columns = qBound(1, o.value(QStringLiteral("columns")).toInt(f.columns), 4);
    f.linesFirstPage = o.value(QStringLiteral("linesFirstPage")).toInt(0);
    f.landscape = o.value(QStringLiteral("landscape")).toBool(false);
    f.notes = o.value(QStringLiteral("notes")).toString();
    f.printNotClassified = o.value(QStringLiteral("printNotClassified")).toBool(true);
    f.byGroups = o.value(QStringLiteral("byGroups")).toBool(false);
    f.tableWidth = qBound(30, o.value(QStringLiteral("tableWidth")).toInt(100), 100);
    const QJsonArray sig = o.value(QStringLiteral("signatures")).toArray();
    for (int i = 0; i < 3 && i < sig.size(); ++i)
        f.signatures[size_t(i)] = sig.at(i).toString();
    const QJsonArray fa = o.value(QStringLiteral("fields")).toArray();
    if (!fa.isEmpty()) {
        f.fields.clear();
        for (const auto& v : fa) {
            const QJsonArray a = v.toArray();
            if (a.size() >= 3)
                f.fields << ReportColumn{ a.at(0).toInt(), a.at(1).toInt(), qMax(1, a.at(2).toInt()), a.at(3).toInt(),
                                          a.at(4).toBool(), a.at(5).toString() };
        }
    }
    return f;
}

ReportFormat ReportFormat::defaults(ReportType type, int run)
{
    run = qMax(1, run);
    ReportFormat f;
    f.type = type;
    using namespace ReportField;
    const ReportColumn rank{ Rank, 1, 5 }, bib{ CompField::Bib, 1, 5 }, code{ CompField::Code, 2, 12 },
        name{ CompField::Name, 0, 24 }, year{ CompField::Year, 0, 10 }, nation{ CompField::Nation, 0, 10 };
    switch (type) {
    case ReportType::Competitors:
        f.title = Tr::tr("List of competitors");
        f.fields = { bib, code, name, year, nation, { CompField::Club, 0, 24 } };
        break;
    case ReportType::CompetitorsPoints:
        f.title = Tr::tr("List of competitors");
        f.fields = { code, name, year, nation, { CompField::PointsDH, 2, 10 }, { CompField::PointsSG, 2, 10 },
                     { CompField::PointsGS, 2, 10 }, { CompField::PointsSL, 2, 10 }, { CompField::PointsAC, 2, 10 } };
        break;
    case ReportType::StartList:
        f.title = Tr::tr("Start list for run %1");
        f.printHeader = true;
        // run 1: the list points, next runs: the time of the previous run
        f.fields = { rank, bib, code, name, year, nation };
        if (run == 1)
            f.fields << ReportColumn{ Points, 2, 10, 0, false, Tr::tr("List points") };
        else
            f.fields << ReportColumn{ RunTime + run - 1, 2, 10 };
        f.fields << ReportColumn{ Dotted, 1, 15 };
        break;
    case ReportType::RunRanking:
        f.title = Tr::tr("Run Ranking %1");
        f.printHeader = true;
        f.fields = { rank, bib, code, name, year, nation, { Time, 2, 10 }, { Gap, 2, 10 } };
        break;
    case ReportType::RankingAfterRun:
        f.title = Tr::tr("Ranking after run %1");
        f.printHeader = true;
        f.fields = { rank, bib, code, name, year, nation };
        for (int r = 1; r <= qMin(run, kMaxRuns); ++r)
            f.fields << ReportColumn{ RunTimeRank + r, 2, 12 };
        f.fields << ReportColumn{ Time, 2, 10 } << ReportColumn{ Gap, 2, 10 };
        break;
    case ReportType::FinalRanking:
        f.title = Tr::tr("Unofficial ranking");
        f.printHeader = true;
        f.fields = { rank, bib, code, name, year, nation, { RunTimeRank + 1, 2, 12 }, { RunTimeRank + 2, 2, 12 },
                     { Time, 2, 10 }, { Gap, 2, 10 } };
        break;
    case ReportType::FinalRankingPoints:
        f.title = Tr::tr("Official ranking");
        f.printHeader = true;
        f.fields = { rank, bib, code, { CompField::Name, 0, 22 }, { CompField::Year, 0, 7 }, { CompField::Nation, 0, 8 },
                     { RunTimeRank + 1, 2, 13 }, { RunTimeRank + 2, 2, 13 }, { Time, 2, 10 }, { Gap, 2, 9 },
                     { RacePoints, 2, 12 } };
        break;
    case ReportType::PenaltyCalculation:
        f.title = Tr::tr("Penalty calculation");
        f.fields = { { Rank, 1, 5, 0, true }, bib, code, { CompField::Name, 0, 24, 0, true }, year, nation, { Points, 2, 10 },
                     { Best5, 2, 10 }, { TimePoints, 2, 10 } };
        f.signatures[1] = Tr::tr("Technical Delegate");
        break;
    case ReportType::TimeRecords:
    case ReportType::TimeRecordsByBib:
        f.title = type == ReportType::TimeRecords ? Tr::tr("Times records") : Tr::tr("Time records (by numbers)");
        f.columns = 2;
        f.fields = { { RecSeq, 1, 6 }, { RecPoint, 0, 7 }, { RecStatus, 1, 5 }, { RecNum, 0, 6 }, { RecHour, 2, 14 },
                     { RecTime, 2, 10 } };
        break;
    case ReportType::TimingPaper:
        f.title = Tr::tr("Timing device paper");
        f.columns = 2;
        f.fields = { { PaperSeq, 1, 6 }, { PaperDevice, 1, 5 }, { PaperReceived, 1, 12 }, { PaperText, 2, 40 } };
        break;
    }
    return f;
}

QString reportFieldLabel(int field, ReportType type, int run)
{
    using namespace ReportField;
    if (field > 0 && field < 100) {
        if (const auto* d = competitorFieldDef(field))
            return QCoreApplication::translate("Competitor", d->label);
        return QString();
    }
    if (field > RunTime && field <= RunTime + kMaxRuns)
        return Tr::tr("Run %1").arg(field - RunTime);
    if (field > RunTimeRank && field <= RunTimeRank + kMaxRuns)
        return Tr::tr("Run %1(Rank)").arg(field - RunTimeRank);
    if (field > RunRank && field <= RunRank + kMaxRuns)
        return Tr::tr("(Rank)");
    if (field > Inter && field <= Inter + kMaxInter)
        return Tr::tr("Inter %1").arg(field - Inter);
    switch (field) {
    case Rank:
        return (type == ReportType::StartList) ? Tr::tr("Order") : Tr::tr("Rank");
    case Time: return Tr::tr("Time");
    case Gap: return Tr::tr("Gap");
    case Status: return Tr::tr("Status");
    case Reason: return Tr::tr("Reason");
    case Penalty: return Tr::tr("Penalty");
    case Points: return type == ReportType::PenaltyCalculation ? Tr::tr("List points") : Tr::tr("Points");
    case RacePoints: return Tr::tr("Race points");
    case Best5: return Tr::tr("Best 5");
    case TimePoints: return Tr::tr("Race points");
    case StartTime: return Tr::tr("Start Time");
    case Dotted: return QString();
    case PreviousRun: return run <= 1 ? Tr::tr("Points") : Tr::tr("Run %1").arg(run - 1);
    case RecSeq: return Tr::tr("Seq");
    case RecStatus: return Tr::tr("Status");
    case RecNum: return Tr::tr("Num");
    case RecHour: return Tr::tr("Hour Cell.");
    case RecTime: return Tr::tr("Time");
    case RecPoint: return Tr::tr("Point");
    case PaperSeq: return Tr::tr("Seq");
    case PaperDevice: return Tr::tr("Dev.");
    case PaperReceived: return Tr::tr("Received");
    case PaperText: return Tr::tr("Data");
    default: break;
    }
    return QString();
}

QList<ReportFieldDef> availableReportFields(ReportType type, int runCount, int interCount)
{
    using namespace ReportField;
    QList<ReportFieldDef> out;
    auto add = [&](int f, int align, int width) { out << ReportFieldDef{ f, reportFieldLabel(f, type), align, width }; };

    if (type == ReportType::TimeRecords || type == ReportType::TimeRecordsByBib) {
        add(RecSeq, 1, 6);
        add(RecPoint, 0, 7);
        add(RecStatus, 1, 5);
        add(RecNum, 0, 6);
        add(CompField::Name, 0, 24);
        add(RecHour, 2, 14);
        add(RecTime, 2, 10);
        return out;
    }
    if (type == ReportType::TimingPaper) {
        add(PaperSeq, 1, 6);
        add(PaperDevice, 1, 5);
        add(PaperReceived, 1, 12);
        add(PaperText, 2, 40);
        return out;
    }
    if (type != ReportType::Competitors && type != ReportType::CompetitorsPoints)
        add(Rank, 1, 5);
    for (const auto& d : competitorFieldDefs())
        add(d.id, d.numeric ? 2 : 0, d.id == CompField::Name ? 24 : 10);
    switch (type) {
    case ReportType::Competitors:
    case ReportType::CompetitorsPoints:
        add(Points, 2, 10);
        break;
    case ReportType::StartList:
        add(Points, 2, 10);
        add(PreviousRun, 2, 10);
        add(StartTime, 2, 10);
        add(Dotted, 1, 15);
        break;
    case ReportType::RunRanking:
        add(Time, 2, 10);
        add(Gap, 2, 10);
        add(Penalty, 2, 8);
        add(Points, 2, 10);
        for (int i = 1; i <= interCount; ++i)
            add(Inter + i, 2, 12);
        add(Reason, 0, 12);
        break;
    case ReportType::PenaltyCalculation:
        add(Points, 2, 10);
        add(Best5, 2, 10);
        add(TimePoints, 2, 10);
        break;
    case ReportType::RankingAfterRun:
    case ReportType::FinalRanking:
    case ReportType::FinalRankingPoints:
        for (int r = 1; r <= runCount; ++r) {
            add(RunTime + r, 2, 10);
            add(RunTimeRank + r, 2, 12);
            add(RunRank + r, 2, 4);
        }
        add(Time, 2, 10);
        add(Gap, 2, 10);
        add(Points, 2, 10);
        if (type == ReportType::FinalRankingPoints)
            add(RacePoints, 2, 10);
        add(Reason, 0, 12);
        break;
    default:
        break;
    }
    return out;
}

QStringList signatureRoles()
{
    return { QString(), Tr::tr("Technical Delegate"), Tr::tr("Referee"), Tr::tr("Chief of Race"), Tr::tr("Timekeeper"),
             Tr::tr("Race Secretary") };
}
