#pragma once

#include "RunData.h"
#include "Types.h"

#include <QString>
#include <QVector>
#include <array>
#include <functional>

class Event;

// One line of a ranking.
struct RankedEntry {
    int bib = 0;
    int rank = 0;
    qint64 timeMs = TimeUtil::kNoTime;   // full precision (ms)
    qint64 shownMs = TimeUtil::kNoTime;  // truncated to the event precision, used for display/ties
    qint64 gapMs = TimeUtil::kNoTime;    // shown gap to the winner (0 for the winner)
    qint64 penaltyMs = 0;
    std::array<qint64, kMaxRuns> runMs;  // shown time of each run (final ranking)
    std::array<int, kMaxRuns> runRank;   // rank of that run time in the run ranking
    std::array<qint64, kMaxInter> interMs;
    std::array<int, kMaxInter> interRank;
    ResultStatus status = ResultStatus::NotProcessed;
    int statusRun = 0;                   // run in which the status applies (final ranking)
    QString reason;

    RankedEntry()
    {
        runMs.fill(TimeUtil::kNoTime);
        runRank.fill(0);
        interMs.fill(TimeUtil::kNoTime);
        interRank.fill(0);
    }
};

struct RankingGroup {
    ResultStatus status = ResultStatus::NotProcessed;
    int run = 0;            // run the status belongs to
    bool notQualified = false;
    QVector<RankedEntry> entries;
    QString title(const QString& runName = QString()) const; // "Did Not Start - Run 1" (or the name of the run)
};

struct RunCounters {
    int list = 0;
    int classified = 0;
    int dns = 0;
    int nps = 0;
    int dnf = 0;
    int dsq = 0;
    int notProcessed = 0;
};

struct RankingResult {
    QVector<RankedEntry> classified;
    QVector<RankingGroup> unclassified;
    int lastRun = 0;
    QVector<int> runsUsed;  // runs taken into consideration (final ranking)
};

namespace Ranking {

// Ranking of a single run.
RankingResult runRanking(const Event& ev, int run);

// Final ranking over runs 1..uptoRun (0 = all runs of the event),
// honouring the final ranking type and the runs taken into consideration.
RankingResult finalRanking(const Event& ev, int uptoRun = 0);

RunCounters counters(const Event& ev, int run);

// The part of a ranking whose bibs are accepted by `inGroup`, ranked again inside it:
// ranks, gaps to the group winner, ranks of the run and intermediate times.
RankingResult subRanking(const RankingResult& all, const std::function<bool(int bib)>& inGroup, Precision prec);

// Order of bibs taking part in a run: start list order, then bibs that only
// have a result.
QVector<int> participants(const RunData& run);

} // namespace Ranking
