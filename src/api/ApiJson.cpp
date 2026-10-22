#include "ApiJson.h"
#include "core/Event.h"
#include "core/Ranking.h"

#include <algorithm>
#include <climits>

namespace ApiJson {

static QJsonValue text(const QString& s)
{
    const QString t = s.trimmed();
    return t.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(t);
}

void putTime(QJsonObject& o, const QString& key, qint64 ms, Precision p)
{
    if (ms < 0) {
        o.insert(key + QStringLiteral("Ms"), QJsonValue::Null);
        o.insert(key, QJsonValue::Null);
        return;
    }
    const qint64 shown = TimeUtil::truncateMs(ms, p);
    o.insert(key + QStringLiteral("Ms"), shown);
    o.insert(key, TimeUtil::formatResult(shown, p));
}

void putGap(QJsonObject& o, qint64 gapMs, Precision p)
{
    if (gapMs < 0) {
        o.insert(QStringLiteral("gapMs"), QJsonValue::Null);
        o.insert(QStringLiteral("gap"), QJsonValue::Null);
        return;
    }
    o.insert(QStringLiteral("gapMs"), gapMs);
    o.insert(QStringLiteral("gap"), gapMs == 0 ? QJsonValue(QJsonValue::Null) : QJsonValue(TimeUtil::formatGap(gapMs, p)));
}

QJsonValue dayMs(qint64 dayTimeUs)
{
    return dayTimeUs < 0 ? QJsonValue(QJsonValue::Null) : QJsonValue(dayTimeUs / 1000);
}

QJsonValue statusCode(ResultStatus s)
{
    switch (s) {
    case ResultStatus::Classified: return QStringLiteral("QLF");
    case ResultStatus::DNS: return QStringLiteral("DNS");
    case ResultStatus::NPS: return QStringLiteral("NPS");
    case ResultStatus::DNF: return QStringLiteral("DNF");
    case ResultStatus::DSQ: return QStringLiteral("DSQ");
    default: return QJsonValue::Null;
    }
}

// ---- competitors ----------------------------------------------------------------------

QJsonArray groupsOf(const Event& ev, const Competitor& c)
{
    QJsonArray a;
    const auto& groups = ev.params().outputGroups;
    for (int i = 0; i < groups.size(); ++i)
        if (groups.at(i).matches(c))
            a << i;
    return a;
}

QJsonObject competitor(const Event& ev, const Competitor& c)
{
    QJsonObject o;
    o.insert(QStringLiteral("id"), c.id);
    o.insert(QStringLiteral("bib"), c.bib);
    o.insert(QStringLiteral("lastName"), c.lastName);
    o.insert(QStringLiteral("firstName"), c.firstName);
    o.insert(QStringLiteral("name"), c.fullName());
    o.insert(QStringLiteral("code"), text(c.code));
    o.insert(QStringLiteral("gender"), c.gender == Gender::Male ? QJsonValue(QStringLiteral("M"))
                                           : c.gender == Gender::Female ? QJsonValue(QStringLiteral("F"))
                                                                        : QJsonValue(QJsonValue::Null));
    o.insert(QStringLiteral("year"), c.year > 0 ? QJsonValue(c.year) : QJsonValue(QJsonValue::Null));
    o.insert(QStringLiteral("category"), text(c.category));
    o.insert(QStringLiteral("nation"), text(c.nation));
    o.insert(QStringLiteral("region"), text(c.region));
    o.insert(QStringLiteral("club"), text(c.club));
    o.insert(QStringLiteral("team"), text(c.team));
    o.insert(QStringLiteral("forerunner"), ev.isForerunner(c.bib));
    o.insert(QStringLiteral("groups"), groupsOf(ev, c));
    return o;
}

CompetitorCache::CompetitorCache(const Event& ev)
    : m_ev(ev)
{
}

QJsonObject CompetitorCache::get(int bib)
{
    auto it = m_cache.constFind(bib);
    if (it != m_cache.constEnd())
        return *it;
    QJsonObject o;
    if (const Competitor* c = m_ev.competitorByBib(bib)) {
        o = competitor(m_ev, *c);
    } else {
        // a number without competitor (forerunner, unknown number)
        o.insert(QStringLiteral("id"), QJsonValue::Null);
        o.insert(QStringLiteral("bib"), bib);
        o.insert(QStringLiteral("name"), m_ev.competitorName(bib));
        o.insert(QStringLiteral("forerunner"), m_ev.isForerunner(bib));
        o.insert(QStringLiteral("groups"), QJsonArray());
    }
    m_cache.insert(bib, o);
    return o;
}

QJsonObject competitors(const Event& ev)
{
    QVector<const Competitor*> list;
    for (const auto& c : ev.competitors())
        list << &c;
    std::stable_sort(list.begin(), list.end(), [](const Competitor* a, const Competitor* b) {
        // competitors without a bib at the end
        const int ba = a->bib > 0 ? a->bib : INT_MAX, bb = b->bib > 0 ? b->bib : INT_MAX;
        return ba < bb;
    });
    QJsonArray a;
    for (const Competitor* c : list)
        a << competitor(ev, *c);
    QJsonObject o;
    o.insert(QStringLiteral("competitors"), a);
    return o;
}

// ---- event --------------------------------------------------------------------------------

static QString disciplineCode(Discipline d)
{
    switch (d) {
    case Discipline::Downhill: return QStringLiteral("DH");
    case Discipline::SuperG: return QStringLiteral("SG");
    case Discipline::Giant: return QStringLiteral("GS");
    case Discipline::Slalom: return QStringLiteral("SL");
    case Discipline::AlpineCombined: return QStringLiteral("AC");
    }
    return QString();
}

// Person row of the header ("<role>.code", ".name", ".nation"), null when empty.
static QJsonValue person(const HeaderParams& h, const QString& key)
{
    const QString code = h.value(key + QStringLiteral(".code")).trimmed();
    const QString name = h.value(key + QStringLiteral(".name")).trimmed();
    const QString nation = h.value(key + QStringLiteral(".nation")).trimmed();
    if (code.isEmpty() && name.isEmpty() && nation.isEmpty())
        return QJsonValue::Null;
    QJsonObject o;
    o.insert(QStringLiteral("code"), text(code));
    o.insert(QStringLiteral("name"), text(name));
    o.insert(QStringLiteral("nation"), text(nation));
    return o;
}

static QList<int> usedRuns(const Event& ev)
{
    QList<int> l;
    for (int r = 1; r <= ev.runCount(); ++r)
        if (ev.params().type.runsUsed[size_t(r - 1)])
            l << r;
    return l;
}

QJsonObject event(const Event& ev)
{
    const EventParams& p = ev.params();
    QJsonObject o;

    QJsonObject g;
    g.insert(QStringLiteral("title1"), text(p.general.title1));
    g.insert(QStringLiteral("title2"), text(p.general.title2));
    g.insert(QStringLiteral("date"), p.general.date.isValid() ? QJsonValue(p.general.date.toString(Qt::ISODate))
                                                                : QJsonValue(QJsonValue::Null));
    g.insert(QStringLiteral("location"), text(p.general.location));
    g.insert(QStringLiteral("nation"), text(p.general.nation));
    g.insert(QStringLiteral("organisation"), text(p.general.organisation));
    g.insert(QStringLiteral("timingBy"), text(p.general.timingBy));
    g.insert(QStringLiteral("codex"), text(p.general.codex));
    o.insert(QStringLiteral("general"), g);

    QJsonObject t;
    t.insert(QStringLiteral("regulation"), p.type.regulation == Regulation::FIS ? QStringLiteral("FIS") : QStringLiteral("National"));
    t.insert(QStringLiteral("fisCategory"), p.type.regulation == Regulation::FIS ? text(p.type.fisCategory) : QJsonValue(QJsonValue::Null));
    t.insert(QStringLiteral("discipline"), disciplineCode(p.type.discipline));
    t.insert(QStringLiteral("disciplineName"), disciplineName(p.type.discipline));
    t.insert(QStringLiteral("runCount"), ev.runCount());
    t.insert(QStringLiteral("interCount"), ev.interCount());
    t.insert(QStringLiteral("precision"), TimeUtil::decimalsFor(ev.precision()));
    t.insert(QStringLiteral("finalRanking"),
             p.type.finalType == FinalRankingType::BestOfRuns ? QStringLiteral("bestOfRuns") : QStringLiteral("totalOfRuns"));
    QJsonArray used;
    for (int r : usedRuns(ev))
        used << r;
    t.insert(QStringLiteral("runsUsed"), used);
    t.insert(QStringLiteral("courseLength"), p.type.courseLength > 0 ? QJsonValue(p.type.courseLength) : QJsonValue(QJsonValue::Null));
    o.insert(QStringLiteral("type"), t);

    QJsonArray runs;
    for (int r = 1; r <= ev.runCount(); ++r) {
        QJsonObject ro;
        ro.insert(QStringLiteral("run"), r);
        ro.insert(QStringLiteral("name"), p.type.runName(r));
        ro.insert(QStringLiteral("used"), bool(p.type.runsUsed[size_t(r - 1)]));
        runs << ro;
    }
    o.insert(QStringLiteral("runs"), runs);

    const HeaderParams& h = p.header;
    const QList<QPair<const char*, const char*>> roles = {
        { "technicalDelegate", HeaderKeys::TechnicalDelegate }, { "referee", HeaderKeys::Referee },
        { "assistantReferee", HeaderKeys::AssistantReferee },   { "chiefOfRace", HeaderKeys::ChiefOfRace },
        { "chiefOfCourse", HeaderKeys::CourseChief },           { "startReferee", HeaderKeys::StartReferee },
        { "finishReferee", HeaderKeys::FinishReferee },
    };
    QJsonArray jury;
    for (const auto& r : roles) {
        const QJsonValue pv = person(h, QLatin1String(r.second));
        if (pv.isNull())
            continue;
        QJsonObject po = pv.toObject();
        po.insert(QStringLiteral("role"), QLatin1String(r.first));
        jury << po;
    }
    o.insert(QStringLiteral("jury"), jury);

    QJsonArray courses;
    for (int r = 1; r <= ev.runCount(); ++r) {
        const QString k = QStringLiteral("run%1.").arg(r);
        auto rv = [&](const char* key) { return text(h.runValue(r, QLatin1String(key))); };
        QJsonObject c;
        c.insert(QStringLiteral("run"), r);
        c.insert(QStringLiteral("course"), rv(HeaderKeys::Course));
        c.insert(QStringLiteral("homologation"), rv(HeaderKeys::Homologation));
        const QString start = h.runValue(r, QLatin1String(HeaderKeys::StartAltitude));
        const QString finish = h.runValue(r, QLatin1String(HeaderKeys::FinishAltitude));
        c.insert(QStringLiteral("startAltitude"), text(start));
        c.insert(QStringLiteral("finishAltitude"), text(finish));
        QString drop = HeaderParams::dropOf(start, finish);
        if (drop.isEmpty())
            drop = h.runValue(r, QLatin1String(HeaderKeys::Drop));
        c.insert(QStringLiteral("drop"), text(drop));
        c.insert(QStringLiteral("length"), rv(HeaderKeys::Length));
        c.insert(QStringLiteral("gates"), text(h.value(k + QStringLiteral("gates"))));
        c.insert(QStringLiteral("turningGates"), text(h.value(k + QStringLiteral("directions"))));
        c.insert(QStringLiteral("startTime"), text(h.value(k + QStringLiteral("starttime"))));
        c.insert(QStringLiteral("setter"), person(h, k + QStringLiteral("setter")));
        QJsonArray fore;
        for (char ch = 'A'; ch <= 'E'; ++ch) {
            const QJsonValue pv = person(h, k + QStringLiteral("fore") + QLatin1Char(ch));
            if (!pv.isNull())
                fore << pv;
        }
        c.insert(QStringLiteral("forerunners"), fore);
        courses << c;
    }
    o.insert(QStringLiteral("courses"), courses);

    QJsonObject cond;
    cond.insert(QStringLiteral("weather"), text(h.value(QLatin1String(HeaderKeys::Weather))));
    cond.insert(QStringLiteral("snow"), text(h.value(QLatin1String(HeaderKeys::Snow))));
    cond.insert(QStringLiteral("tempStart"), text(h.value(QLatin1String(HeaderKeys::TempStart))));
    cond.insert(QStringLiteral("tempFinish"), text(h.value(QLatin1String(HeaderKeys::TempFinish))));
    o.insert(QStringLiteral("conditions"), cond);

    QJsonArray groups;
    for (int i = 0; i < p.outputGroups.size(); ++i) {
        QJsonObject go;
        go.insert(QStringLiteral("index"), i);
        go.insert(QStringLiteral("label"), p.outputGroups.at(i).label);
        go.insert(QStringLiteral("description"), p.outputGroups.at(i).describe());
        groups << go;
    }
    o.insert(QStringLiteral("groups"), groups);

    QJsonObject fr;
    fr.insert(QStringLiteral("from"), p.timing.forerunnerFrom);
    fr.insert(QStringLiteral("to"), p.timing.forerunnerTo);
    o.insert(QStringLiteral("forerunners"), fr);
    return o;
}

// ---- runs / start lists ---------------------------------------------------------------------

static QJsonObject countersJson(const RunCounters& c)
{
    QJsonObject o;
    o.insert(QStringLiteral("startList"), c.list);
    o.insert(QStringLiteral("classified"), c.classified);
    o.insert(QStringLiteral("dns"), c.dns);
    o.insert(QStringLiteral("nps"), c.nps);
    o.insert(QStringLiteral("dnf"), c.dnf);
    o.insert(QStringLiteral("dsq"), c.dsq);
    o.insert(QStringLiteral("notProcessed"), c.notProcessed);
    return o;
}

QJsonObject runs(const Event& ev)
{
    QJsonArray a;
    for (int r = 1; r <= ev.runCount(); ++r) {
        const RunData& run = ev.run(r);
        const RunCounters c = Ranking::counters(ev, r);
        bool anyData = !run.records.isEmpty();
        for (auto it = run.infos.cbegin(); it != run.infos.cend() && !anyData; ++it)
            anyData = it->status != ResultStatus::NotProcessed;
        QString state = QStringLiteral("notStarted");
        if (c.list > 0 && c.notProcessed == 0)
            state = QStringLiteral("finished");
        else if (anyData)
            state = QStringLiteral("inProgress");
        QJsonObject o;
        o.insert(QStringLiteral("run"), r);
        o.insert(QStringLiteral("name"), ev.params().type.runName(r));
        o.insert(QStringLiteral("used"), bool(ev.params().type.runsUsed[size_t(r - 1)]));
        o.insert(QStringLiteral("state"), state);
        o.insert(QStringLiteral("counters"), countersJson(c));
        a << o;
    }
    QJsonObject o;
    o.insert(QStringLiteral("runs"), a);
    return o;
}

QJsonObject startList(const Event& ev, int runNo)
{
    CompetitorCache cache(ev);
    QJsonArray a;
    int order = 0;
    for (const auto& s : ev.run(runNo).startList) {
        if (s.bib <= 0)
            continue;
        QJsonObject e;
        e.insert(QStringLiteral("order"), ++order);
        e.insert(QStringLiteral("bib"), s.bib);
        e.insert(QStringLiteral("plannedStartDayMs"), dayMs(s.startTimeUs));
        e.insert(QStringLiteral("competitor"), cache.get(s.bib));
        a << e;
    }
    QJsonObject o;
    o.insert(QStringLiteral("run"), runNo);
    o.insert(QStringLiteral("entries"), a);
    return o;
}

// ---- results ------------------------------------------------------------------------------

static RankingResult inGroup(const Event& ev, const RankingResult& all, int group)
{
    if (group < 0)
        return all;
    const OutputGroup& g = ev.params().outputGroups.at(group);
    return Ranking::subRanking(
        all,
        [&](int bib) {
            const Competitor* c = ev.competitorByBib(bib);
            return c && g.matches(*c);
        },
        ev.precision());
}

static QString groupKind(const RankingGroup& g)
{
    if (g.notQualified)
        return QStringLiteral("DNQ");
    switch (g.status) {
    case ResultStatus::DNS: return QStringLiteral("DNS");
    case ResultStatus::NPS: return QStringLiteral("NPS");
    case ResultStatus::DNF: return QStringLiteral("DNF");
    case ResultStatus::DSQ: return QStringLiteral("DSQ");
    default: return QStringLiteral("notProcessed");
    }
}

static QJsonArray unclassifiedJson(const RankingResult& res, CompetitorCache& cache)
{
    QJsonArray a;
    for (const auto& g : res.unclassified) {
        QJsonArray entries;
        for (const auto& e : g.entries) {
            QJsonObject eo;
            eo.insert(QStringLiteral("bib"), e.bib);
            eo.insert(QStringLiteral("competitor"), cache.get(e.bib));
            eo.insert(QStringLiteral("reason"), text(e.reason));
            entries << eo;
        }
        QJsonObject go;
        go.insert(QStringLiteral("kind"), groupKind(g));
        go.insert(QStringLiteral("run"), g.run);
        go.insert(QStringLiteral("entries"), entries);
        a << go;
    }
    return a;
}

QJsonObject runResults(const Event& ev, int runNo, int group)
{
    const Precision prec = ev.precision();
    const RankingResult res = inGroup(ev, Ranking::runRanking(ev, runNo), group);
    CompetitorCache cache(ev);

    // best intermediate times of the ranking, for the gaps
    std::array<qint64, kMaxInter> bestInter{ {-1, -1, -1} };
    for (const auto& e : res.classified)
        for (int i = 0; i < kMaxInter; ++i) {
            const qint64 v = e.interMs[size_t(i)] >= 0 ? TimeUtil::truncateMs(e.interMs[size_t(i)], prec) : -1;
            if (v >= 0 && (bestInter[size_t(i)] < 0 || v < bestInter[size_t(i)]))
                bestInter[size_t(i)] = v;
        }

    QJsonArray classified;
    for (const auto& e : res.classified) {
        QJsonObject o;
        o.insert(QStringLiteral("rank"), e.rank);
        o.insert(QStringLiteral("bib"), e.bib);
        o.insert(QStringLiteral("competitor"), cache.get(e.bib));
        putTime(o, QStringLiteral("time"), e.shownMs, prec);
        putGap(o, e.gapMs, prec);
        o.insert(QStringLiteral("penaltyMs"), e.penaltyMs);
        QJsonArray inter;
        for (int i = 0; i < ev.interCount(); ++i) {
            QJsonObject io;
            io.insert(QStringLiteral("point"), i + 1);
            const qint64 v = e.interMs[size_t(i)];
            putTime(io, QStringLiteral("time"), v, prec);
            io.insert(QStringLiteral("rank"), v >= 0 && e.interRank[size_t(i)] > 0 ? QJsonValue(e.interRank[size_t(i)])
                                                                                     : QJsonValue(QJsonValue::Null));
            putGap(io, v >= 0 ? TimeUtil::truncateMs(v, prec) - bestInter[size_t(i)] : -1, prec);
            inter << io;
        }
        o.insert(QStringLiteral("inter"), inter);
        classified << o;
    }

    QJsonObject o;
    o.insert(QStringLiteral("run"), runNo);
    o.insert(QStringLiteral("group"), group < 0 ? QJsonValue(QJsonValue::Null) : QJsonValue(group));
    o.insert(QStringLiteral("counters"), countersJson(Ranking::counters(ev, runNo)));
    o.insert(QStringLiteral("classified"), classified);
    o.insert(QStringLiteral("unclassified"), unclassifiedJson(res, cache));
    return o;
}

QJsonObject finalResults(const Event& ev, int upto, int group)
{
    const Precision prec = ev.precision();
    const RankingResult res = inGroup(ev, Ranking::finalRanking(ev, upto), group);
    CompetitorCache cache(ev);

    QJsonArray classified;
    for (const auto& e : res.classified) {
        QJsonObject o;
        o.insert(QStringLiteral("rank"), e.rank);
        o.insert(QStringLiteral("bib"), e.bib);
        o.insert(QStringLiteral("competitor"), cache.get(e.bib));
        putTime(o, QStringLiteral("time"), e.shownMs, prec);
        putGap(o, e.gapMs, prec);
        QJsonArray runs;
        for (int r : res.runsUsed) {
            QJsonObject ro;
            ro.insert(QStringLiteral("run"), r);
            putTime(ro, QStringLiteral("time"), e.runMs[size_t(r - 1)], prec);
            const int rank = e.runRank[size_t(r - 1)];
            ro.insert(QStringLiteral("rank"), rank > 0 ? QJsonValue(rank) : QJsonValue(QJsonValue::Null));
            runs << ro;
        }
        o.insert(QStringLiteral("runs"), runs);
        classified << o;
    }
    QJsonArray used;
    for (int r : res.runsUsed)
        used << r;

    QJsonObject o;
    o.insert(QStringLiteral("upto"), res.lastRun);
    o.insert(QStringLiteral("group"), group < 0 ? QJsonValue(QJsonValue::Null) : QJsonValue(group));
    o.insert(QStringLiteral("runsUsed"), used);
    o.insert(QStringLiteral("classified"), classified);
    o.insert(QStringLiteral("unclassified"), unclassifiedJson(res, cache));
    return o;
}

} // namespace ApiJson
