#pragma once

#include "TimeUtil.h"
#include "Types.h"

#include <QHash>
#include <QString>
#include <QVector>
#include <algorithm>
#include <array>
#include <limits>

struct StartEntry {
    int bib = 0;
    qint64 startTimeUs = TimeUtil::kNoTime; // planned start time (interval start), optional
};

// One raw time impulse ("hour cell") at a measuring point.
struct TimeRecord {
    int id = 0;                 // unique within the run, also the "Seq" shown in the grids
    int point = Point::Start;   // Point::Start, 1..kMaxInter, Point::Finish
    int bib = 0;                // 0 = not assigned
    int flags = RecordFlag::None;
    qint64 dayTimeUs = TimeUtil::kNoTime;
    int device = 0;             // 1/2 timing device, 0 = manual
    int deviceChannel = 0;
    int deviceSeq = 0;          // sequential number of the timing device (CP540 SSSS)
    QString note;

    bool isActive() const { return !(flags & (RecordFlag::Inactive | RecordFlag::Cancelled)); }
    bool isStatusRecord() const { return flags & (RecordFlag::DNS | RecordFlag::NPS | RecordFlag::DNF | RecordFlag::DSQ); }
    bool hasTime() const
    {
        return dayTimeUs >= 0 && !(flags & (RecordFlag::NoTime | RecordFlag::StatusEntry))
            && !((flags & (RecordFlag::DNS | RecordFlag::NPS | RecordFlag::DNF)) && (flags & RecordFlag::Manual));
    }
    ResultStatus recordStatus() const
    {
        if (flags & RecordFlag::DSQ)
            return ResultStatus::DSQ;
        if (flags & RecordFlag::DNF)
            return ResultStatus::DNF;
        if (flags & RecordFlag::DNS)
            return ResultStatus::DNS;
        if (flags & RecordFlag::NPS)
            return ResultStatus::NPS;
        return ResultStatus::Classified;
    }
};

// Result of one competitor in one run.
struct TimeInfo {
    int bib = 0;
    ResultStatus status = ResultStatus::NotProcessed;
    qint64 timeMs = TimeUtil::kNoTime;    // net time incl. penalty, truncated to 1/1000
    qint64 penaltyMs = 0;
    std::array<qint64, kMaxInter> interMs{ {TimeUtil::kNoTime, TimeUtil::kNoTime, TimeUtil::kNoTime} };
    bool manual = false;                  // time / status entered manually, not derived from records
    QString reason;                       // DSQ reason (gate, ...)

    bool isClassified() const { return status == ResultStatus::Classified && timeMs >= 0; }
};

// A line of the "timing device paper": a line received from a timing device, exactly as
// received. The paper is only appended, never modified by the operations of the program.
struct PaperLine {
    int seq = 0;              // line number in the run
    int device = 0;           // timing device 1 / 2
    qint64 receivedUs = -1;   // PC time of day of the reception
    QString text;             // raw line (with the checksum)
    bool error = false;       // checksum error
};

struct RunData {
    int run = 1;                         // 1-based
    QVector<StartEntry> startList;
    QVector<TimeRecord> records;
    QHash<int, TimeInfo> infos;          // by bib
    QVector<PaperLine> paper;
    int referenceDay = -1;               // CP540 day number of the first impulse (midnight handling)

    int nextRecordId() const
    {
        int m = 0;
        for (const auto& r : records)
            m = qMax(m, r.id);
        return m + 1;
    }
    TimeRecord* record(int id)
    {
        for (auto& r : records)
            if (r.id == id)
                return &r;
        return nullptr;
    }
    const TimeRecord* record(int id) const
    {
        for (const auto& r : records)
            if (r.id == id)
                return &r;
        return nullptr;
    }
    // Records of a point in chronological order. Entries without a time (DNS / DNF / DSQ,
    // "No time") stay where they were entered, right after the impulse entered before them:
    // their PC time is not comparable with the clock of the timing device.
    QVector<const TimeRecord*> pointRecords(int point) const
    {
        QVector<QPair<qint64, const TimeRecord*>> keyed;
        for (const auto& r : records) {
            if (r.point != point)
                continue;
            qint64 key = r.dayTimeUs;
            if (!r.hasTime()) {
                key = std::numeric_limits<qint64>::min();
                for (const auto& o : records)
                    if (o.point == point && o.id < r.id && o.hasTime())
                        key = qMax(key, o.dayTimeUs);
            }
            keyed << qMakePair(key, &r);
        }
        std::sort(keyed.begin(), keyed.end(), [](const auto& a, const auto& b) {
            if (a.first != b.first)
                return a.first < b.first;
            return a.second->id < b.second->id;
        });
        QVector<const TimeRecord*> out;
        out.reserve(keyed.size());
        for (const auto& k : keyed)
            out << k.second;
        return out;
    }
    int startPosition(int bib) const
    {
        for (int i = 0; i < startList.size(); ++i)
            if (startList.at(i).bib == bib)
                return i;
        return -1;
    }
    TimeInfo info(int bib) const
    {
        auto it = infos.constFind(bib);
        if (it != infos.constEnd())
            return *it;
        TimeInfo t;
        t.bib = bib;
        return t;
    }
};
