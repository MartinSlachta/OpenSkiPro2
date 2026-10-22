#include "Ranking.h"
#include "Event.h"

#include <QCoreApplication>
#include <QHash>
#include <algorithm>

QString RankingGroup::title(const QString& runName) const
{
    const QString name = runName.isEmpty() ? QCoreApplication::translate("Ranking", "Run %1").arg(run) : runName;
    if (notQualified)
        return QCoreApplication::translate("Ranking", "Did Not Qualify - %1").arg(name);
    QString s;
    switch (status) {
    case ResultStatus::DNS: s = QCoreApplication::translate("Ranking", "Did Not Start"); break;
    case ResultStatus::NPS: s = QCoreApplication::translate("Ranking", "Not Permitted to Start"); break;
    case ResultStatus::DNF: s = QCoreApplication::translate("Ranking", "Did Not Finish"); break;
    case ResultStatus::DSQ: s = QCoreApplication::translate("Ranking", "Disqualified"); break;
    default: s = QCoreApplication::translate("Ranking", "Not Processed"); break;
    }
    if (run > 0)
        s += QStringLiteral(" - ") + name;
    return s;
}

namespace Ranking {

QVector<int> participants(const RunData& run)
{
    QVector<int> bibs;
    bibs.reserve(run.startList.size() + 4);
    for (const auto& s : run.startList)
        if (s.bib > 0 && !bibs.contains(s.bib))
            bibs << s.bib;
    QList<int> extra;
    for (auto it = run.infos.cbegin(); it != run.infos.cend(); ++it)
        if (it.key() > 0 && !bibs.contains(it.key()) && it->status != ResultStatus::NotProcessed)
            extra << it.key();
    std::sort(extra.begin(), extra.end());
    for (int b : extra)
        bibs << b;
    return bibs;
}

// Assigns ranks on an already sorted list: equal shown values share a rank.
template <typename ShownFn>
static void assignRanks(QVector<RankedEntry>& list, ShownFn shown)
{
    for (int i = 0; i < list.size(); ++i) {
        if (i > 0 && shown(list[i]) == shown(list[i - 1]))
            list[i].rank = list[i - 1].rank;
        else
            list[i].rank = i + 1;
    }
}

// Rank of each bib in a run (classified only), keyed by bib.
static QHash<int, int> runRankMap(const RankingResult& r)
{
    QHash<int, int> m;
    for (const auto& e : r.classified)
        m.insert(e.bib, e.rank);
    return m;
}

RankingResult runRanking(const Event& ev, int runNo)
{
    RankingResult res;
    res.lastRun = runNo;
    res.runsUsed = { runNo };
    const RunData& run = ev.run(runNo);
    const Precision prec = ev.precision();
    const QVector<int> bibs = participants(run);

    QVector<RankedEntry> dns, nps, dnf, dsq, np;
    for (int bib : bibs) {
        if (ev.isForerunner(bib))
            continue;
        const TimeInfo info = run.info(bib);
        RankedEntry e;
        e.bib = bib;
        e.status = info.status;
        e.statusRun = runNo;
        e.reason = info.reason;
        e.penaltyMs = info.penaltyMs;
        e.interMs = info.interMs;
        if (info.isClassified()) {
            e.timeMs = info.timeMs;
            e.shownMs = TimeUtil::truncateMs(info.timeMs, prec);
            e.runMs[size_t(runNo - 1)] = e.shownMs;
            res.classified << e;
        } else {
            switch (info.status) {
            case ResultStatus::DNS: dns << e; break;
            case ResultStatus::NPS: nps << e; break;
            case ResultStatus::DNF: dnf << e; break;
            case ResultStatus::DSQ: dsq << e; break;
            default:
                e.status = ResultStatus::NotProcessed;
                np << e;
                break;
            }
        }
    }

    // same rank (same shown time): higher bib first
    std::stable_sort(res.classified.begin(), res.classified.end(), [](const RankedEntry& a, const RankedEntry& b) {
        if (a.shownMs != b.shownMs)
            return a.shownMs < b.shownMs;
        return a.bib > b.bib;
    });
    assignRanks(res.classified, [](const RankedEntry& e) { return e.shownMs; });
    if (!res.classified.isEmpty()) {
        const qint64 best = res.classified.first().shownMs;
        for (auto& e : res.classified) {
            e.gapMs = e.shownMs - best;
            e.runRank[size_t(runNo - 1)] = e.rank;
        }
    }

    // intermediate ranks among everybody who has the intermediate time
    for (int i = 0; i < kMaxInter; ++i) {
        QVector<RankedEntry*> withInter;
        for (auto& e : res.classified)
            if (e.interMs[size_t(i)] >= 0)
                withInter << &e;
        std::stable_sort(withInter.begin(), withInter.end(), [i](const RankedEntry* a, const RankedEntry* b) {
            return a->interMs[size_t(i)] < b->interMs[size_t(i)];
        });
        for (int k = 0; k < withInter.size(); ++k) {
            const qint64 cur = TimeUtil::truncateMs(withInter[k]->interMs[size_t(i)], prec);
            const qint64 prev = k > 0 ? TimeUtil::truncateMs(withInter[k - 1]->interMs[size_t(i)], prec) : -2;
            withInter[k]->interRank[size_t(i)] = (k > 0 && cur == prev) ? withInter[k - 1]->interRank[size_t(i)] : k + 1;
        }
    }

    auto addGroup = [&](ResultStatus st, QVector<RankedEntry>& list) {
        if (list.isEmpty())
            return;
        RankingGroup g;
        g.status = st;
        g.run = runNo;
        g.entries = list;
        res.unclassified << g;
    };
    addGroup(ResultStatus::DNS, dns);
    addGroup(ResultStatus::NPS, nps);
    addGroup(ResultStatus::DNF, dnf);
    addGroup(ResultStatus::DSQ, dsq);
    addGroup(ResultStatus::NotProcessed, np);
    return res;
}

RankingResult finalRanking(const Event& ev, int uptoRun)
{
    RankingResult res;
    const auto& type = ev.params().type;
    const int last = (uptoRun <= 0 || uptoRun > ev.runCount()) ? ev.runCount() : uptoRun;
    res.lastRun = last;
    for (int r = 1; r <= last; ++r)
        if (type.runsUsed[size_t(r - 1)] || last == 1)
            res.runsUsed << r;
    if (res.runsUsed.isEmpty())
        res.runsUsed << 1;
    const Precision prec = ev.precision();
    const bool best = type.finalType == FinalRankingType::BestOfRuns;

    // run rankings for the "(rank)" columns
    QHash<int, QHash<int, int>> runRanks;
    for (int r : res.runsUsed)
        runRanks.insert(r, runRankMap(runRanking(ev, r)));

    // base order: start list of the last considered run, then earlier runs
    QVector<int> order;
    for (int i = res.runsUsed.size() - 1; i >= 0; --i)
        for (int b : participants(ev.run(res.runsUsed.at(i))))
            if (!order.contains(b) && !ev.isForerunner(b))
                order << b;

    // groups of unclassified, keyed by (run, status)
    struct GroupKey {
        int run;
        int kind; // 0 DNS 1 NPS 2 DNF 3 DSQ 4 not qualified 5 not processed
    };
    QVector<QPair<GroupKey, RankedEntry>> unclassified;

    for (int bib : order) {
        RankedEntry e;
        e.bib = bib;
        qint64 totalShown = 0;
        qint64 totalFull = 0;
        bool classified = !best;
        bool anyClassified = false;
        qint64 bestShown = -1, bestFull = -1;
        GroupKey key{ 0, -1 };

        for (int r : res.runsUsed) {
            const RunData& run = ev.run(r);
            const TimeInfo info = run.info(bib);
            if (info.isClassified()) {
                const qint64 shown = TimeUtil::truncateMs(info.timeMs, prec);
                e.runMs[size_t(r - 1)] = shown;
                e.runRank[size_t(r - 1)] = runRanks.value(r).value(bib);
                totalShown += shown;
                totalFull += info.timeMs;
                anyClassified = true;
                if (bestShown < 0 || info.timeMs < bestFull) {
                    bestShown = shown;
                    bestFull = info.timeMs;
                }
                continue;
            }
            if (key.kind < 0) {
                key.run = r;
                if (info.status == ResultStatus::DNS)
                    key.kind = 0;
                else if (info.status == ResultStatus::NPS)
                    key.kind = 1;
                else if (info.status == ResultStatus::DNF)
                    key.kind = 2;
                else if (info.status == ResultStatus::DSQ)
                    key.kind = 3;
                else if (r > res.runsUsed.first() && run.startPosition(bib) < 0)
                    key.kind = 4; // classified before, not in the start list of this run
                else
                    key.kind = 5;
                e.reason = info.reason;
            }
            if (!best)
                classified = false;
        }

        if (best ? anyClassified : classified) {
            e.status = ResultStatus::Classified;
            e.shownMs = best ? bestShown : totalShown;
            e.timeMs = best ? bestFull : totalFull;
            res.classified << e;
        } else {
            switch (key.kind) {
            case 0: e.status = ResultStatus::DNS; break;
            case 1: e.status = ResultStatus::NPS; break;
            case 2: e.status = ResultStatus::DNF; break;
            case 3: e.status = ResultStatus::DSQ; break;
            default: e.status = ResultStatus::NotProcessed; break;
            }
            e.statusRun = key.run;
            unclassified << qMakePair(key, e);
        }
    }

    std::stable_sort(res.classified.begin(), res.classified.end(), [](const RankedEntry& a, const RankedEntry& b) {
        // same rank (same shown total): higher bib first
        if (a.shownMs != b.shownMs)
            return a.shownMs < b.shownMs;
        return a.bib > b.bib;
    });
    assignRanks(res.classified, [](const RankedEntry& e) { return e.shownMs; });
    if (!res.classified.isEmpty()) {
        const qint64 bestTotal = res.classified.first().shownMs;
        for (auto& e : res.classified)
            e.gapMs = e.shownMs - bestTotal;
    }

    // build groups ordered by run, then DNS, NPS, DNF, DSQ, not qualified, not processed
    for (int r : res.runsUsed) {
        for (int kind = 0; kind <= 5; ++kind) {
            RankingGroup g;
            g.run = r;
            g.notQualified = kind == 4;
            g.status = kind == 0 ? ResultStatus::DNS
                : kind == 1      ? ResultStatus::NPS
                : kind == 2      ? ResultStatus::DNF
                : kind == 3      ? ResultStatus::DSQ
                                 : ResultStatus::NotProcessed;
            for (const auto& p : unclassified)
                if (p.first.run == r && p.first.kind == kind)
                    g.entries << p.second;
            if (!g.entries.isEmpty()) {
                // inside a group: start order of that run
                const RunData& run = ev.run(r);
                std::stable_sort(g.entries.begin(), g.entries.end(), [&](const RankedEntry& a, const RankedEntry& b) {
                    int pa = run.startPosition(a.bib), pb = run.startPosition(b.bib);
                    if (pa < 0)
                        pa = 100000 + a.bib;
                    if (pb < 0)
                        pb = 100000 + b.bib;
                    return pa < pb;
                });
                res.unclassified << g;
            }
        }
    }
    return res;
}

RankingResult subRanking(const RankingResult& all, const std::function<bool(int bib)>& inGroup, Precision prec)
{
    RankingResult res;
    res.lastRun = all.lastRun;
    res.runsUsed = all.runsUsed;
    for (const auto& e : all.classified)
        if (inGroup(e.bib))
            res.classified << e;
    // the order of the full ranking stays valid inside the group
    assignRanks(res.classified, [](const RankedEntry& e) { return e.shownMs; });
    if (!res.classified.isEmpty()) {
        const qint64 best = res.classified.first().shownMs;
        for (auto& e : res.classified)
            e.gapMs = e.shownMs - best;
    }
    // rank of each run time and intermediate time among the group
    auto rankBy = [&res](auto value, auto setRank) {
        QVector<RankedEntry*> with;
        for (auto& e : res.classified)
            if (value(e) >= 0)
                with << &e;
        std::stable_sort(with.begin(), with.end(), [&](const RankedEntry* a, const RankedEntry* b) { return value(*a) < value(*b); });
        int rank = 0;
        for (int k = 0; k < with.size(); ++k) {
            if (k == 0 || value(*with[k]) != value(*with[k - 1]))
                rank = k + 1;
            setRank(*with[k], rank);
        }
    };
    for (int r = 0; r < kMaxRuns; ++r)
        rankBy([r](const RankedEntry& e) { return e.runMs[size_t(r)]; },
               [r](RankedEntry& e, int rank) { e.runRank[size_t(r)] = rank; });
    for (int i = 0; i < kMaxInter; ++i)
        rankBy([i, prec](const RankedEntry& e) {
                   return e.interMs[size_t(i)] >= 0 ? TimeUtil::truncateMs(e.interMs[size_t(i)], prec) : TimeUtil::kNoTime;
               },
               [i](RankedEntry& e, int rank) { e.interRank[size_t(i)] = rank; });
    for (const auto& g : all.unclassified) {
        RankingGroup sub = g;
        sub.entries.clear();
        for (const auto& e : g.entries)
            if (inGroup(e.bib))
                sub.entries << e;
        if (!sub.entries.isEmpty())
            res.unclassified << sub;
    }
    return res;
}

RunCounters counters(const Event& ev, int runNo)
{
    RunCounters c;
    const RunData& run = ev.run(runNo);
    for (int bib : participants(run)) {
        if (ev.isForerunner(bib))
            continue;
        ++c.list;
        const TimeInfo info = run.info(bib);
        switch (info.status) {
        case ResultStatus::Classified:
            if (info.timeMs >= 0)
                ++c.classified;
            else
                ++c.notProcessed;
            break;
        case ResultStatus::DNS: ++c.dns; break;
        case ResultStatus::NPS: ++c.nps; break;
        case ResultStatus::DNF: ++c.dnf; break;
        case ResultStatus::DSQ: ++c.dsq; break;
        default: ++c.notProcessed; break;
        }
    }
    return c;
}

} // namespace Ranking
