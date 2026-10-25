#pragma once

#include "core/RunData.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>
#include <array>

class Event;
class TimingEngine;

// Live state of the run being timed (docs/api.md 4.8, 5.5): the state of every
// competitor of the run, derived from the records and results of the event, and the
// differences between two states, which become the "competitor" events. Deriving the
// events by comparison gives the same events for an impulse, an operator correction
// or an undo, whoever made the change.
class LiveState {
public:
    enum class State { NotStarted, OnCourse, Finished, DNS, NPS, DNF, DSQ };

    struct Competitor {
        State state = State::NotStarted;
        ResultStatus status = ResultStatus::NotProcessed;
        qint64 startUs = TimeUtil::kNoTime;
        qint64 finishUs = TimeUtil::kNoTime;
        std::array<qint64, kMaxInter> interMs{ {TimeUtil::kNoTime, TimeUtil::kNoTime, TimeUtil::kNoTime} };
        std::array<qint64, kMaxInter> interUs{ {TimeUtil::kNoTime, TimeUtil::kNoTime, TimeUtil::kNoTime} }; // time of day of the passage
        qint64 timeMs = TimeUtil::kNoTime;
        qint64 penaltyMs = 0;
        int lastPoint = 0; // last intermediate passed (0 none)
        QString reason;

        bool operator==(const Competitor& o) const
        {
            return state == o.state && status == o.status && startUs == o.startUs && finishUs == o.finishUs
                && interMs == o.interMs && interUs == o.interUs && timeMs == o.timeMs && penaltyMs == o.penaltyMs && lastPoint == o.lastPoint
                && reason == o.reason;
        }
        bool operator!=(const Competitor& o) const { return !(*this == o); }
    };

    struct Change {
        int bib = 0;
        QString change; // started, intermediate, finished, status, corrected, reverted
        int point = 0;  // intermediate passed (change "intermediate")
    };

    // Forgets everything (no run timed).
    void reset();
    int run() const { return m_run; }

    // Computes the state of the run `run` and returns the differences to the previous
    // state. A new run (or `baseline`) only takes the state, without changes.
    QVector<Change> update(const Event& ev, int run, bool baseline = false);

    // Message data of a "competitor" event / of GET /live, from the last update.
    QJsonObject competitorEvent(const Event& ev, const Change& c) const;
    QJsonObject live(const Event& ev, const TimingEngine* engine) const;

    static QString stateName(State s);
    static Competitor compute(const RunData& run, int bib);

private:
    struct Rank {
        qint64 shownMs = TimeUtil::kNoTime;
        int rank = 0;
        qint64 gapMs = TimeUtil::kNoTime;
        QJsonArray groups; // rank and gap in each output group (ApiJson::groupRanks)
    };
    void computeRanks(const Event& ev);
    QJsonObject rankJson(const QHash<int, Rank>& ranks, int bib, qint64 penaltyMs, bool withPenalty, Precision p) const;
    QJsonArray interJson(const Competitor& c, int interCount, Precision p) const;

    int m_run = 0;
    QHash<int, Competitor> m_state;
    QHash<int, Rank> m_runRank;    // ranking of the run
    QHash<int, Rank> m_totalRank;  // final ranking after the run
    QHash<int, Rank> m_prevRank;   // final ranking before the run (run 2+)
    std::array<QVector<qint64>, kMaxInter> m_interTimes; // sorted shown intermediate times of the run
};
