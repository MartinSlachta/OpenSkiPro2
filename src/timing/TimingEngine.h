#pragma once

#include "TimingDevice.h"
#include "core/RunData.h"

#include <QHash>
#include <QObject>
#include <QVector>
#include <array>

class Event;

// Sequential timing of one run (start / intermediates / finish), reproducing
// SkiPro 2.27:
//  - every measuring point has a list of "numbers to follow" (index 0 = the
//    next one). The start list follows the start list order, the other points
//    the order of the times at the previous point. The operator can reorder it.
//  - every point has a queue of numbers typed by the operator; the next
//    impulse takes the first typed number, otherwise (point not locked) the
//    first number to follow
//  - an impulse within the "timing impulse delay" after the previous impulse
//    of the point, below the minimum time, or a second impulse of the same
//    competitor within 5 s is recorded inactive
//  - DNS / NPS / DNF / DSQ are statuses of time records
// Results (TimeInfo) are always derived from the records, except when entered
// manually ("Modify data").
class TimingEngine : public QObject {
    Q_OBJECT
public:
    enum class Group { Start = 0, Inter = 1, Finish = 2 };

    explicit TimingEngine(Event* ev, QObject* parent = nullptr);

    void setRun(int run);
    int run() const { return m_run; }
    const RunData& data() const;
    Event* event() const { return m_ev; }

    void setDeviceConfig(int slot, const TimingDeviceConfig& cfg);
    const TimingDeviceConfig& deviceConfig(int slot) const;

    void setLocked(Group g, bool locked);
    bool isLocked(Group g) const { return m_locked[size_t(g)]; }
    static Group groupOf(int point);

    // Points of the run in course order: start, intermediates, finish.
    QVector<int> points() const;

    // -- input
    int handleImpulse(const TimingImpulse& imp);                 // returns the record id (0 = ignored)
    int insertPassage(int point, qint64 dayTimeUs, int bib);     // passage with a given time
    int duplicateRecord(int recordId);                           // Insert a passage (F5): copy of a record

    // -- numbers to follow (index 0 = next expected)
    void rebuildFollow();
    QVector<int> follow(int point) const { return m_follow.value(point); }
    bool moveFollow(int point, int bib, int delta);              // +1 = later, -1 = earlier
    bool placeInFollow(int point, int bib, int index);           // false if the number is not in the list
    // Start: inserts a number which is not in the start list into the start list and
    // into the numbers to follow at `index`.
    bool insertIntoStartList(int bib, int index = 0);
    int expectedPoint(int bib) const;                            // point where the competitor is expected, -1 none

    // -- queue of numbers typed for the next impulses of a point
    void queueNumber(int point, int bib);
    QVector<int> typedQueue(int point) const { return m_typed.value(point); }
    void setTypedQueue(int point, const QVector<int>& bibs);

    // -- operator actions on records
    bool setRecordBib(int recordId, int bib);
    bool setRecordTime(int recordId, qint64 dayTimeUs);
    bool setRecordActive(int recordId, bool active);
    bool setRecordStatus(int recordId, ResultStatus status);     // Classified = no status
    bool setRecordNote(int recordId, const QString& note);       // operator comment, empty = none
    bool deleteRecord(int recordId);

    // -- operator actions on competitors
    // Status record (DNS/NPS/DNF/DSQ) or, with Classified, a time record (PC time) at a point.
    int statusAtPoint(int point, int bib, ResultStatus status, const QString& reason = QString(), qint64 dayTimeUs = -1);
    int noTime(int point, int bib);                              // passed without time
    // Status of a competitor at the point where he is expected (Enter a DNS number ...).
    // Classified removes the statuses of the competitor.
    bool setStatus(int bib, ResultStatus status, const QString& reason = QString(), qint64 pcTimeUs = -1);
    bool setPenalty(int bib, qint64 penaltyMs);
    bool setManualInfo(const TimeInfo& info);  // stores a manual result (Modify data)
    bool clearManual(int bib);                 // back to results derived from the records

    // -- queries
    int nextToStart() const;
    QVector<int> numbersToFollow(int max) const;
    QVector<int> onCourse() const;              // competitors started and still expected somewhere
    int nextToFinish() const;
    int pointForChannel(int slot, int channel) const; // Point::*, -1 = not used
    const TimeRecord* activeRecord(int bib, int point) const;          // any active record (time, no time, status)
    const TimeRecord* activeTimeRecord(int bib, int point) const;      // active record with a time
    const TimeRecord* findDeviceRecord(int device, int channel, int seq) const;
    bool hasDuplicate(const TimeRecord& rec) const;   // another active record of the same point and bib

    void recompute(int bib);
    void recomputeAll();

    static qint64 currentDayTimeUs();
    // Time of the timing device (PC time + offset measured on the impulses), used for
    // everything entered by the operator so that it is in the order of the impulses.
    void setClockOffsetUs(qint64 offsetUs)
    {
        m_clockOffsetUs = offsetUs;
        m_clockSynced = true;
    }
    bool isClockSynced() const { return m_clockSynced; } // false: PC time until the first impulse
    // Forgets the offset: PC time until the next impulse of the timing device.
    void resetClockSync()
    {
        m_clockOffsetUs = 0;
        m_clockSynced = false;
    }
    qint64 clockOffsetUs() const { return m_clockOffsetUs; }
    qint64 nowUs() const;

Q_SIGNALS:
    void recordAdded(int run, int recordId);
    void unassignedRecord(int run, int recordId);
    void competitorFinished(int bib);
    void message(const QString& text);
    void playSound(const QString& file);
    void followChanged();

private:
    int addRecord(TimeRecord rec);
    void afterRecordChange(int bib);
    void startedIntoStartList();              // started bibs missing in the start list (CP540 number)
    bool isOut(int bib) const;                 // DNS / NPS / DNF / DSQ
    qint64 minTimeMs(int point) const;

    Event* m_ev;
    int m_run = 1;
    std::array<TimingDeviceConfig, 3> m_cfg; // index 1 and 2 used
    std::array<bool, 3> m_locked{ {false, false, false} };
    QHash<int, QVector<int>> m_follow;       // point -> numbers to follow
    QHash<int, QVector<int>> m_typed;        // point -> numbers typed for the next impulses
    qint64 m_clockOffsetUs = 0;
    bool m_clockSynced = false;
};
