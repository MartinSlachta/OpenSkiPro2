#include "LiveState.h"
#include "ApiJson.h"
#include "core/Event.h"
#include "core/Ranking.h"
#include "timing/TimingEngine.h"

#include <QSet>
#include <algorithm>
#include <climits>

namespace {
constexpr int kMaxListed = 10; // on start / last finished entries of GET /live

bool isOut(LiveState::State s)
{
    return s == LiveState::State::DNS || s == LiveState::State::NPS || s == LiveState::State::DNF
        || s == LiveState::State::DSQ;
}

int order(LiveState::State s)
{
    switch (s) {
    case LiveState::State::NotStarted: return 0;
    case LiveState::State::OnCourse: return 1;
    default: return 2;
    }
}
} // namespace

QString LiveState::stateName(State s)
{
    switch (s) {
    case State::NotStarted: return QStringLiteral("notStarted");
    case State::OnCourse: return QStringLiteral("onCourse");
    case State::Finished: return QStringLiteral("finished");
    case State::DNS: return QStringLiteral("dns");
    case State::NPS: return QStringLiteral("nps");
    case State::DNF: return QStringLiteral("dnf");
    case State::DSQ: return QStringLiteral("dsq");
    }
    return QString();
}

void LiveState::reset()
{
    m_run = 0;
    m_state.clear();
    m_runRank.clear();
    m_totalRank.clear();
    m_prevRank.clear();
    for (auto& v : m_interTimes)
        v.clear();
}

LiveState::Competitor LiveState::compute(const RunData& run, int bib)
{
    Competitor c;
    const TimeInfo info = run.info(bib);
    bool anyStart = false, anyFinish = false;
    for (const auto& r : run.records) {
        if (r.bib != bib || !r.isActive())
            continue;
        if (r.point == Point::Start) {
            anyStart = true;
            if (r.hasTime() && c.startUs < 0)
                c.startUs = r.dayTimeUs;
        } else if (r.point == Point::Finish) {
            anyFinish = true;
            if (r.hasTime() && c.finishUs < 0)
                c.finishUs = r.dayTimeUs;
        } else if (r.point >= 1 && r.point <= kMaxInter) {
            c.lastPoint = qMax(c.lastPoint, r.point);
            if (r.hasTime() && c.interUs[size_t(r.point - 1)] < 0)
                c.interUs[size_t(r.point - 1)] = r.dayTimeUs;
        }
    }
    c.status = info.status;
    c.interMs = info.interMs;
    c.timeMs = info.timeMs;
    c.penaltyMs = info.penaltyMs;
    c.reason = info.reason;
    switch (info.status) {
    case ResultStatus::DNS: c.state = State::DNS; break;
    case ResultStatus::NPS: c.state = State::NPS; break;
    case ResultStatus::DNF: c.state = State::DNF; break;
    case ResultStatus::DSQ: c.state = State::DSQ; break;
    default:
        if (info.isClassified() || anyFinish)
            c.state = State::Finished;
        else if (anyStart)
            c.state = State::OnCourse;
        break;
    }
    if (c.state != State::Finished)
        c.timeMs = TimeUtil::kNoTime;
    return c;
}

QVector<LiveState::Change> LiveState::update(const Event& ev, int runNo, bool baseline)
{
    const RunData& run = ev.run(runNo);
    QSet<int> bibs;
    for (int b : Ranking::participants(run))
        bibs.insert(b);
    for (const auto& r : run.records)
        if (r.bib > 0)
            bibs.insert(r.bib);
    QHash<int, Competitor> state;
    for (int b : bibs)
        state.insert(b, compute(run, b));

    QVector<Change> changes;
    if (!baseline && runNo == m_run) {
        QSet<int> all = bibs;
        for (auto it = m_state.cbegin(); it != m_state.cend(); ++it)
            all.insert(it.key());
        QList<int> sorted(all.cbegin(), all.cend());
        std::sort(sorted.begin(), sorted.end());
        for (int bib : sorted) {
            const Competitor o = m_state.value(bib);
            const Competitor n = state.value(bib);
            if (o == n)
                continue;
            Change ch;
            ch.bib = bib;
            if (isOut(n.state) && n.state != o.state) {
                ch.change = QStringLiteral("status");
            } else if (isOut(o.state) && !isOut(n.state)) {
                ch.change = QStringLiteral("reverted");
            } else if (order(n.state) > order(o.state)) {
                ch.change = n.state == State::Finished ? QStringLiteral("finished") : QStringLiteral("started");
            } else if (order(n.state) < order(o.state)) {
                ch.change = QStringLiteral("reverted");
            } else if (n.state == State::OnCourse && n.lastPoint > o.lastPoint) {
                ch.change = QStringLiteral("intermediate");
                ch.point = n.lastPoint;
            } else if (n.state == State::OnCourse && n.lastPoint < o.lastPoint) {
                ch.change = QStringLiteral("reverted");
            } else {
                ch.change = QStringLiteral("corrected");
            }
            changes << ch;
        }
    }
    m_run = runNo;
    m_state = state;
    computeRanks(ev);
    return changes;
}

void LiveState::computeRanks(const Event& ev)
{
    auto toMap = [&ev](const RankingResult& r) {
        QHash<int, Rank> m;
        const QHash<int, QJsonArray> groups = ApiJson::groupRanks(ev, r);
        for (const auto& e : r.classified) {
            Rank k;
            k.shownMs = e.shownMs;
            k.rank = e.rank;
            k.gapMs = e.gapMs;
            k.groups = groups.value(e.bib);
            m.insert(e.bib, k);
        }
        return m;
    };
    m_runRank = toMap(Ranking::runRanking(ev, m_run));
    m_totalRank = toMap(Ranking::finalRanking(ev, m_run));
    m_prevRank = m_run > 1 ? toMap(Ranking::finalRanking(ev, m_run - 1)) : QHash<int, Rank>();
    // intermediate ranks among everybody of the run who passed the intermediate
    const Precision prec = ev.precision();
    for (int i = 0; i < kMaxInter; ++i) {
        QVector<qint64>& times = m_interTimes[size_t(i)];
        times.clear();
        for (auto it = m_state.cbegin(); it != m_state.cend(); ++it)
            if (!ev.isForerunner(it.key()) && it->state != State::DNS && it->state != State::NPS && it->interMs[size_t(i)] >= 0)
                times << TimeUtil::truncateMs(it->interMs[size_t(i)], prec);
        std::sort(times.begin(), times.end());
    }
}

QJsonObject LiveState::rankJson(const QHash<int, Rank>& ranks, int bib, qint64 penaltyMs, bool withPenalty, Precision p) const
{
    auto it = ranks.constFind(bib);
    if (it == ranks.constEnd())
        return QJsonObject();
    QJsonObject o;
    ApiJson::putTime(o, QStringLiteral("time"), it->shownMs, p);
    o.insert(QStringLiteral("rank"), it->rank);
    ApiJson::putGap(o, it->gapMs, p);
    o.insert(QStringLiteral("groupRanks"), it->groups);
    if (withPenalty)
        o.insert(QStringLiteral("penaltyMs"), penaltyMs);
    return o;
}

QJsonArray LiveState::interJson(const Competitor& c, int interCount, Precision p) const
{
    QJsonArray a;
    for (int i = 0; i < interCount && i < kMaxInter; ++i) {
        const qint64 ms = c.interMs[size_t(i)];
        if (ms < 0)
            continue;
        const qint64 shown = TimeUtil::truncateMs(ms, p);
        const QVector<qint64>& times = m_interTimes[size_t(i)];
        QJsonObject o;
        o.insert(QStringLiteral("point"), i + 1);
        o.insert(QStringLiteral("dayMs"), ApiJson::dayMs(c.interUs[size_t(i)]));
        ApiJson::putTime(o, QStringLiteral("time"), shown, p);
        const auto pos = std::lower_bound(times.cbegin(), times.cend(), shown);
        const bool ranked = pos != times.cend() && *pos == shown;
        o.insert(QStringLiteral("rank"), ranked ? QJsonValue(int(pos - times.cbegin()) + 1) : QJsonValue(QJsonValue::Null));
        ApiJson::putGap(o, ranked ? shown - times.first() : -1, p);
        a << o;
    }
    return a;
}

static QJsonValue orNull(const QJsonObject& o)
{
    return o.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(o);
}

QJsonObject LiveState::competitorEvent(const Event& ev, const Change& ch) const
{
    const Precision prec = ev.precision();
    const Competitor c = m_state.value(ch.bib);
    ApiJson::CompetitorCache cache(ev);
    QJsonObject o;
    o.insert(QStringLiteral("run"), m_run);
    o.insert(QStringLiteral("bib"), ch.bib);
    o.insert(QStringLiteral("change"), ch.change);
    if (ch.change == QLatin1String("intermediate"))
        o.insert(QStringLiteral("point"), ch.point);
    o.insert(QStringLiteral("competitor"), cache.get(ch.bib));
    o.insert(QStringLiteral("state"), stateName(c.state));
    o.insert(QStringLiteral("status"), ApiJson::statusCode(c.state == State::Finished || isOut(c.state) ? c.status : ResultStatus::NotProcessed));
    o.insert(QStringLiteral("startDayMs"), ApiJson::dayMs(c.startUs));
    o.insert(QStringLiteral("finishDayMs"), ApiJson::dayMs(c.finishUs));
    o.insert(QStringLiteral("lastPoint"), c.lastPoint);
    o.insert(QStringLiteral("inter"), interJson(c, ev.interCount(), prec));
    QJsonObject run;
    if (c.timeMs >= 0) {
        run = rankJson(m_runRank, ch.bib, c.penaltyMs, true, prec);
        if (run.isEmpty()) { // forerunner, not ranked
            ApiJson::putTime(run, QStringLiteral("time"), c.timeMs, prec);
            run.insert(QStringLiteral("rank"), QJsonValue::Null);
            ApiJson::putGap(run, -1, prec);
            run.insert(QStringLiteral("penaltyMs"), c.penaltyMs);
        }
    }
    o.insert(QStringLiteral("thisRun"), orNull(run));
    o.insert(QStringLiteral("total"), orNull(rankJson(m_totalRank, ch.bib, 0, false, prec)));
    o.insert(QStringLiteral("previous"), orNull(rankJson(m_prevRank, ch.bib, 0, false, prec)));
    o.insert(QStringLiteral("reason"), c.reason.trimmed().isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(c.reason.trimmed()));
    return o;
}

QJsonObject LiveState::live(const Event& ev, const TimingEngine* engine) const
{
    QJsonObject o;
    QJsonArray onStart, onCourse, finished;
    if (m_run <= 0) {
        o.insert(QStringLiteral("run"), QJsonValue::Null);
        o.insert(QStringLiteral("onStart"), onStart);
        o.insert(QStringLiteral("onCourse"), onCourse);
        o.insert(QStringLiteral("nextToFinish"), QJsonValue::Null);
        o.insert(QStringLiteral("finished"), finished);
        return o;
    }
    const Precision prec = ev.precision();
    ApiJson::CompetitorCache cache(ev);

    QVector<int> next;
    if (engine) {
        next = engine->numbersToFollow(kMaxListed);
    } else {
        for (const auto& s : ev.run(m_run).startList)
            if (s.bib > 0 && m_state.value(s.bib).state == State::NotStarted && next.size() < kMaxListed)
                next << s.bib;
    }
    for (int bib : next) {
        QJsonObject e;
        e.insert(QStringLiteral("bib"), bib);
        e.insert(QStringLiteral("competitor"), cache.get(bib));
        e.insert(QStringLiteral("previous"), orNull(rankJson(m_prevRank, bib, 0, false, prec)));
        onStart << e;
    }

    QVector<int> course, done;
    for (auto it = m_state.cbegin(); it != m_state.cend(); ++it) {
        if (it->state == State::OnCourse)
            course << it.key();
        else if (it->finishUs >= 0 || it->state == State::Finished)
            done << it.key();
    }
    auto byTime = [this](qint64 Competitor::*field, bool desc) {
        return [this, field, desc](int a, int b) {
            qint64 ta = m_state.value(a).*field, tb = m_state.value(b).*field;
            if (ta < 0)
                ta = desc ? -1 : LLONG_MAX; // without a time: last
            if (tb < 0)
                tb = desc ? -1 : LLONG_MAX;
            if (ta != tb)
                return desc ? ta > tb : ta < tb;
            return a < b;
        };
    };
    std::sort(course.begin(), course.end(), byTime(&Competitor::startUs, false));
    std::sort(done.begin(), done.end(), byTime(&Competitor::finishUs, true));

    for (int bib : course) {
        const Competitor c = m_state.value(bib);
        QJsonObject e;
        e.insert(QStringLiteral("bib"), bib);
        e.insert(QStringLiteral("competitor"), cache.get(bib));
        e.insert(QStringLiteral("startDayMs"), ApiJson::dayMs(c.startUs));
        e.insert(QStringLiteral("lastPoint"), c.lastPoint);
        e.insert(QStringLiteral("inter"), interJson(c, ev.interCount(), prec));
        e.insert(QStringLiteral("previous"), orNull(rankJson(m_prevRank, bib, 0, false, prec)));
        onCourse << e;
    }
    for (int i = 0; i < done.size() && i < kMaxListed; ++i) {
        Change ch;
        ch.bib = done.at(i);
        QJsonObject e = competitorEvent(ev, ch);
        for (const char* k : { "run", "change", "lastPoint" })
            e.remove(QLatin1String(k));
        finished << e;
    }
    // the next number expected at the finish in the timing window; before anybody passed
    // the last intermediate: the first competitor on course
    int nextFinish = engine ? engine->nextToFinish() : 0;
    if (nextFinish <= 0 && !course.isEmpty())
        nextFinish = course.first();
    o.insert(QStringLiteral("run"), m_run);
    o.insert(QStringLiteral("onStart"), onStart);
    o.insert(QStringLiteral("onCourse"), onCourse);
    o.insert(QStringLiteral("nextToFinish"), nextFinish > 0 ? QJsonValue(nextFinish) : QJsonValue(QJsonValue::Null));
    o.insert(QStringLiteral("finished"), finished);
    return o;
}
