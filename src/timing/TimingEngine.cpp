#include "TimingEngine.h"
#include "core/Event.h"

#include <QSet>
#include <QTime>
#include <algorithm>

namespace {
constexpr qint64 kDuplicateWindowUs = 5 * TimeUtil::kUsPerSecond; // SkiPro: 5 s
}

TimingEngine::TimingEngine(Event* ev, QObject* parent)
    : QObject(parent)
    , m_ev(ev)
{
    rebuildFollow();
}

void TimingEngine::setRun(int run)
{
    m_run = qBound(1, run, kMaxRuns);
    m_follow.clear();
    m_typed.clear();
    rebuildFollow();
}

const RunData& TimingEngine::data() const
{
    return m_ev->run(m_run);
}

void TimingEngine::setDeviceConfig(int slot, const TimingDeviceConfig& cfg)
{
    if (slot >= 1 && slot <= 2)
        m_cfg[size_t(slot)] = cfg;
}

const TimingDeviceConfig& TimingEngine::deviceConfig(int slot) const
{
    return m_cfg[size_t(qBound(0, slot, 2))];
}

void TimingEngine::setLocked(Group g, bool locked)
{
    m_locked[size_t(g)] = locked;
}

TimingEngine::Group TimingEngine::groupOf(int point)
{
    if (point == Point::Start)
        return Group::Start;
    if (point == Point::Finish)
        return Group::Finish;
    return Group::Inter;
}

QVector<int> TimingEngine::points() const
{
    QVector<int> p{ Point::Start };
    for (int i = 1; i <= m_ev->interCount(); ++i)
        p << i;
    p << Point::Finish;
    return p;
}

qint64 TimingEngine::currentDayTimeUs()
{
    return qint64(QTime::currentTime().msecsSinceStartOfDay()) * 1000;
}

qint64 TimingEngine::nowUs() const
{
    qint64 now = currentDayTimeUs() + m_clockOffsetUs;
    if (now < 0)
        now += TimeUtil::kUsPerDay;
    else if (now >= TimeUtil::kUsPerDay)
        now -= TimeUtil::kUsPerDay;
    // after midnight the times of the run continue above 24 h
    qint64 last = -1;
    for (const auto& r : data().records)
        if (!(r.flags & RecordFlag::Manual))
            last = qMax(last, r.dayTimeUs);
    while (last >= 0 && now + TimeUtil::kUsPerDay / 2 < last)
        now += TimeUtil::kUsPerDay;
    return now;
}

int TimingEngine::pointForChannel(int slot, int channel) const
{
    if (slot < 1 || slot > 2 || channel <= 0)
        return -1;
    const auto& c = m_cfg[size_t(slot)];
    if (channel == c.startChannel)
        return Point::Start;
    if (channel == c.finishChannel)
        return Point::Finish;
    for (int i = 0; i < m_ev->interCount() && i < int(c.interChannels.size()); ++i)
        if (c.interChannels[size_t(i)] == channel)
            return i + 1;
    return -1;
}

const TimeRecord* TimingEngine::activeRecord(int bib, int point) const
{
    if (bib <= 0)
        return nullptr;
    for (const auto& r : data().records)
        if (r.bib == bib && r.point == point && r.isActive())
            return &r;
    return nullptr;
}

const TimeRecord* TimingEngine::activeTimeRecord(int bib, int point) const
{
    if (bib <= 0)
        return nullptr;
    for (const auto& r : data().records)
        if (r.bib == bib && r.point == point && r.isActive() && r.hasTime())
            return &r;
    return nullptr;
}

const TimeRecord* TimingEngine::findDeviceRecord(int device, int channel, int seq) const
{
    if (seq <= 0)
        return nullptr;
    // the most recent one: sequential numbers restart with each run opened on the device
    const TimeRecord* found = nullptr;
    for (const auto& r : data().records)
        if (r.device == device && r.deviceChannel == channel && r.deviceSeq == seq && (!found || r.id > found->id))
            found = &r;
    return found;
}

bool TimingEngine::hasDuplicate(const TimeRecord& rec) const
{
    if (rec.bib <= 0 || !rec.isActive() || !rec.hasTime())
        return false;
    for (const auto& r : data().records)
        if (r.id != rec.id && r.bib == rec.bib && r.point == rec.point && r.isActive() && r.hasTime())
            return true;
    return false;
}

bool TimingEngine::isOut(int bib) const
{
    const ResultStatus s = data().info(bib).status;
    return s == ResultStatus::DNS || s == ResultStatus::NPS || s == ResultStatus::DNF || s == ResultStatus::DSQ;
}

qint64 TimingEngine::minTimeMs(int point) const
{
    const auto& to = m_ev->params().timing;
    if (point == Point::Finish)
        return to.minTimeFinishMs;
    if (point >= 1 && point <= kMaxInter)
        return to.minTimeInterMs[size_t(point - 1)];
    return 0;
}

// ---- numbers to follow ---------------------------------------------------------------

void TimingEngine::rebuildFollow()
{
    if (!m_ev->isOpen())
        return;
    const RunData& run = data();
    const QVector<int> pts = points();

    // --- start: start list order, without the competitors who started / have a status
    {
        QVector<int>& list = m_follow[Point::Start];
        auto leaves = [&](int bib) { return run.startPosition(bib) < 0 || activeRecord(bib, Point::Start) || isOut(bib); };
        list.erase(std::remove_if(list.begin(), list.end(), leaves), list.end());
        for (int pos = 0; pos < run.startList.size(); ++pos) {
            const int bib = run.startList.at(pos).bib;
            if (bib <= 0 || list.contains(bib) || leaves(bib))
                continue;
            int idx = 0;
            for (int j = int(list.size()) - 1; j >= 0; --j) {
                if (run.startPosition(list.at(j)) < pos) {
                    idx = j + 1;
                    break;
                }
            }
            list.insert(idx, bib);
        }
    }

    // --- intermediates / finish: expected at the point after the last point passed,
    //     ordered by the time at that previous point
    QHash<int, QVector<QPair<qint64, int>>> expected; // point -> (time at previous point, bib)
    QSet<int> seen;
    for (const auto& r : run.records) {
        const int bib = r.bib;
        if (bib <= 0 || seen.contains(bib))
            continue;
        seen.insert(bib);
        const TimeRecord* start = activeRecord(bib, Point::Start);
        if (!start || isOut(bib) || start->recordStatus() != ResultStatus::Classified)
            continue;
        int last = 0;
        for (int i = 1; i < pts.size(); ++i)
            if (activeRecord(bib, pts.at(i)))
                last = i;
        if (last + 1 >= pts.size())
            continue; // finished
        const TimeRecord* prev = activeRecord(bib, pts.at(last));
        expected[pts.at(last + 1)] << qMakePair(prev ? prev->dayTimeUs : start->dayTimeUs, bib);
    }
    for (int i = 1; i < pts.size(); ++i) {
        const int point = pts.at(i);
        QVector<QPair<qint64, int>> items = expected.value(point);
        std::stable_sort(items.begin(), items.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        QHash<int, qint64> key;
        for (const auto& it : items)
            key.insert(it.second, it.first);
        QVector<int>& list = m_follow[point];
        list.erase(std::remove_if(list.begin(), list.end(), [&](int bib) { return !key.contains(bib); }), list.end());
        for (const auto& it : items) {
            if (list.contains(it.second))
                continue;
            int idx = 0;
            for (int j = int(list.size()) - 1; j >= 0; --j) {
                if (key.value(list.at(j)) <= it.first) {
                    idx = j + 1;
                    break;
                }
            }
            list.insert(idx, it.second);
        }
    }
    // remove points that do not exist anymore
    for (auto it = m_follow.begin(); it != m_follow.end();) {
        if (!pts.contains(it.key()))
            it = m_follow.erase(it);
        else
            ++it;
    }
    Q_EMIT followChanged();
}

bool TimingEngine::moveFollow(int point, int bib, int delta)
{
    QVector<int>& list = m_follow[point];
    const int idx = int(list.indexOf(bib));
    if (idx < 0)
        return false;
    const int to = idx + delta;
    if (to < 0 || to >= list.size())
        return false;
    list.move(idx, to);
    Q_EMIT followChanged();
    return true;
}

bool TimingEngine::placeInFollow(int point, int bib, int index)
{
    QVector<int>& list = m_follow[point];
    const int idx = int(list.indexOf(bib));
    if (idx < 0)
        return false;
    list.move(idx, qBound(0, index, int(list.size()) - 1));
    Q_EMIT followChanged();
    return true;
}

bool TimingEngine::insertIntoStartList(int bib, int index)
{
    const RunData& run = data();
    if (bib <= 0 || run.startPosition(bib) >= 0)
        return false;
    const QVector<int> list = m_follow.value(Point::Start);
    int pos = int(run.startList.size());
    if (index >= 0 && index < list.size())
        pos = run.startPosition(list.at(index));
    else if (!list.isEmpty())
        pos = run.startPosition(list.last()) + 1;
    QVector<StartEntry> sl = run.startList;
    StartEntry e;
    e.bib = bib;
    sl.insert(qBound(0, pos, int(sl.size())), e);
    if (!m_ev->setStartList(m_run, sl))
        return false;
    rebuildFollow();
    placeInFollow(Point::Start, bib, index);
    return true;
}

int TimingEngine::expectedPoint(int bib) const
{
    for (int p : points())
        if (m_follow.value(p).contains(bib))
            return p;
    return -1;
}

void TimingEngine::queueNumber(int point, int bib)
{
    if (bib > 0)
        m_typed[point] << bib;
    Q_EMIT followChanged();
}

void TimingEngine::setTypedQueue(int point, const QVector<int>& bibs)
{
    m_typed[point] = bibs;
    Q_EMIT followChanged();
}

int TimingEngine::nextToStart() const
{
    return m_follow.value(Point::Start).value(0);
}

QVector<int> TimingEngine::numbersToFollow(int max) const
{
    return m_follow.value(Point::Start).mid(0, max);
}

int TimingEngine::nextToFinish() const
{
    return m_follow.value(Point::Finish).value(0);
}

QVector<int> TimingEngine::onCourse() const
{
    QVector<QPair<qint64, int>> items;
    for (int p : points()) {
        if (p == Point::Start)
            continue;
        for (int bib : m_follow.value(p)) {
            const TimeRecord* s = activeRecord(bib, Point::Start);
            items << qMakePair(s ? s->dayTimeUs : 0, bib);
        }
    }
    std::stable_sort(items.begin(), items.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    QVector<int> out;
    for (const auto& it : items)
        out << it.second;
    return out;
}

// ---- records ------------------------------------------------------------------------

void TimingEngine::afterRecordChange(int bib)
{
    recompute(bib);
    rebuildFollow();
}

int TimingEngine::addRecord(TimeRecord rec)
{
    rec.id = data().nextRecordId();
    if (!m_ev->saveRecord(m_run, rec))
        return 0;
    afterRecordChange(rec.bib);
    Q_EMIT recordAdded(m_run, rec.id);
    return rec.id;
}

int TimingEngine::handleImpulse(const TimingImpulse& imp)
{
    if (imp.dayTimeUs < 0)
        return 0;
    const TimingDeviceConfig& cfg = deviceConfig(imp.device);

    // DNF sent by the timing device (CP540: TC with the number typed on the keyboard)
    if (imp.type == TimingImpulse::Type::StatusDNF) {
        if (imp.bib > 0)
            setStatus(imp.bib, ResultStatus::DNF, QString(), imp.dayTimeUs);
        return 0;
    }

    // modifications of existing records
    if (imp.type == TimingImpulse::Type::Cancelled || imp.type == TimingImpulse::Type::IdRemoved
        || imp.type == TimingImpulse::Type::NewId) {
        const TimeRecord* found = findDeviceRecord(imp.device, imp.channel, imp.seq);
        if (!found)
            return 0;
        TimeRecord rec = *found;
        const int oldBib = rec.bib;
        if (imp.type == TimingImpulse::Type::Cancelled) {
            rec.flags |= RecordFlag::Cancelled;
        } else if (imp.type == TimingImpulse::Type::IdRemoved) {
            rec.bib = 0;
        } else {
            rec.bib = imp.bib;
        }
        m_ev->saveRecord(m_run, rec);
        recompute(oldBib);
        afterRecordChange(rec.bib);
        return rec.id;
    }

    // midnight handling: days since the first impulse of the run
    qint64 dayTime = imp.dayTimeUs;
    if (imp.day >= 0) {
        const int ref = data().referenceDay < 0 ? imp.day : data().referenceDay;
        dayTime += qint64(imp.day - ref) * TimeUtil::kUsPerDay;
    }
    const int point = pointForChannel(imp.device, imp.channel);
    const auto& to = m_ev->params().timing;

    // already known? (answer to a recall, frame repeated after a reconnection ...)
    // The sequential number restarts with every run opened on the device, so the time must match too.
    if (imp.seq > 0) {
        for (const auto& r : data().records)
            if (r.device == imp.device && r.deviceChannel == imp.channel && r.deviceSeq == imp.seq && r.dayTimeUs == dayTime)
                return 0;
    }

    if (imp.day >= 0 && data().referenceDay < 0)
        m_ev->setReferenceDay(m_run, imp.day);
    if (!cfg.soundFor(imp.channel).isEmpty())
        Q_EMIT playSound(cfg.soundFor(imp.channel));
    if (point < 0)
        return 0;

    TimeRecord rec;
    rec.point = point;
    rec.dayTimeUs = dayTime;
    rec.device = imp.device;
    rec.deviceChannel = imp.channel;
    rec.deviceSeq = imp.seq;
    if (imp.manual)
        rec.flags |= RecordFlag::DeviceKey;

    // "Timing impulse delay": minimum interval between two impulses of a point (anti-bounce)
    const qint64 delayMs = point == Point::Start ? to.delayStartMs : point == Point::Finish ? to.delayFinishMs : to.delayInterMs;
    bool bounce = false;
    if (delayMs > 0) {
        for (const auto& r : data().records)
            if (r.point == point && r.hasTime() && r.dayTimeUs <= dayTime && dayTime - r.dayTimeUs < delayMs * 1000)
                bounce = true;
    }
    if (bounce) {
        rec.flags |= RecordFlag::Inactive;
        Q_EMIT message(tr("Impulse ignored (within the timing impulse delay): %1").arg(TimeUtil::formatDayTime(dayTime)));
    } else {
        int bib = 0;
        if (imp.bib > 0) {
            bib = imp.bib;
        } else if (imp.type == TimingImpulse::Type::Recalled) {
            Q_EMIT message(tr("Recovered time %1 (channel %2) - please assign a number")
                               .arg(TimeUtil::formatDayTime(dayTime))
                               .arg(imp.channel));
        } else if (!m_typed.value(point).isEmpty()) {
            bib = m_typed[point].takeFirst();
        } else if (!isLocked(groupOf(point))) {
            bib = m_follow.value(point).value(0);
        }
        // minimum time: false impulse
        if (bib > 0 && point != Point::Start && minTimeMs(point) > 0) {
            const TimeRecord* start = activeTimeRecord(bib, Point::Start);
            if (start && TimeUtil::netTimeMs(start->dayTimeUs, dayTime) < minTimeMs(point)) {
                rec.flags |= RecordFlag::Inactive;
                bib = 0;
                Q_EMIT message(tr("False impulse ignored (below minimum time): %1").arg(TimeUtil::formatDayTime(dayTime)));
            }
        }
        // second impulse of the same competitor within 5 s: inactive
        if (bib > 0) {
            for (const auto& r : data().records) {
                if (r.bib == bib && r.point == point && r.isActive() && r.hasTime()
                    && qAbs(r.dayTimeUs - dayTime) < kDuplicateWindowUs) {
                    rec.flags |= RecordFlag::Inactive;
                    Q_EMIT message(tr("Second impulse of %1 within 5 s: recorded inactive").arg(bib));
                    break;
                }
            }
        }
        rec.bib = bib;
    }

    const int id = addRecord(rec);
    if (!id)
        return 0;
    if (rec.bib > 0 && rec.isActive() && point == Point::Finish)
        Q_EMIT competitorFinished(rec.bib);
    if (rec.bib == 0 && rec.isActive())
        Q_EMIT unassignedRecord(m_run, id);
    return id;
}

int TimingEngine::insertPassage(int point, qint64 dayTimeUs, int bib)
{
    TimeRecord rec;
    rec.point = point;
    rec.dayTimeUs = dayTimeUs;
    rec.flags = RecordFlag::Manual | RecordFlag::Inserted;
    rec.bib = qMax(0, bib);
    const int id = addRecord(rec);
    if (id && rec.bib > 0 && point == Point::Finish)
        Q_EMIT competitorFinished(rec.bib);
    return id;
}

int TimingEngine::duplicateRecord(int recordId)
{
    const TimeRecord* found = data().record(recordId);
    if (!found)
        return 0;
    TimeRecord rec;
    rec.point = found->point;
    rec.dayTimeUs = found->dayTimeUs;
    rec.flags = RecordFlag::Manual | RecordFlag::Inserted;
    const int id = addRecord(rec);
    if (id)
        Q_EMIT unassignedRecord(m_run, id);
    return id;
}

bool TimingEngine::setRecordBib(int recordId, int bib)
{
    const TimeRecord* found = data().record(recordId);
    if (!found)
        return false;
    TimeRecord rec = *found;
    const int oldBib = rec.bib;
    rec.bib = qMax(0, bib);
    if (!m_ev->saveRecord(m_run, rec))
        return false;
    // the number of the record was typed: do not give it to the next impulse too
    if (rec.bib > 0)
        m_typed[rec.point].removeAll(rec.bib);
    recompute(oldBib);
    afterRecordChange(rec.bib);
    return true;
}

bool TimingEngine::setRecordTime(int recordId, qint64 dayTimeUs)
{
    const TimeRecord* found = data().record(recordId);
    if (!found || dayTimeUs < 0)
        return false;
    TimeRecord rec = *found;
    rec.dayTimeUs = dayTimeUs;
    rec.flags &= ~(RecordFlag::NoTime | RecordFlag::StatusEntry);
    rec.flags |= RecordFlag::Manual;
    if (!m_ev->saveRecord(m_run, rec))
        return false;
    afterRecordChange(rec.bib);
    return true;
}

bool TimingEngine::setRecordActive(int recordId, bool active)
{
    const TimeRecord* found = data().record(recordId);
    if (!found)
        return false;
    TimeRecord rec = *found;
    if (active)
        rec.flags &= ~(RecordFlag::Inactive | RecordFlag::Cancelled);
    else
        rec.flags |= RecordFlag::Inactive;
    if (!m_ev->saveRecord(m_run, rec))
        return false;
    afterRecordChange(rec.bib);
    return true;
}

bool TimingEngine::setRecordNote(int recordId, const QString& note)
{
    const TimeRecord* found = data().record(recordId);
    if (!found)
        return false;
    TimeRecord rec = *found;
    rec.note = note.trimmed();
    if (rec.note == found->note)
        return true;
    if (!m_ev->saveRecord(m_run, rec))
        return false;
    afterRecordChange(rec.bib);
    return true;
}

bool TimingEngine::setRecordStatus(int recordId, ResultStatus status)
{
    const TimeRecord* found = data().record(recordId);
    if (!found)
        return false;
    TimeRecord rec = *found;
    const bool wasDsq = rec.flags & RecordFlag::DSQ;
    rec.flags &= ~(RecordFlag::DNS | RecordFlag::NPS | RecordFlag::DNF | RecordFlag::DSQ);
    if (status == ResultStatus::DNS)
        rec.flags |= RecordFlag::DNS;
    else if (status == ResultStatus::NPS)
        rec.flags |= RecordFlag::NPS;
    else if (status == ResultStatus::DNF)
        rec.flags |= RecordFlag::DNF;
    else if (status == ResultStatus::DSQ)
        rec.flags |= RecordFlag::DSQ;
    m_ev->beginBatch();
    if ((rec.flags & RecordFlag::StatusEntry) && status == ResultStatus::Classified)
        m_ev->deleteRecord(m_run, rec.id); // a status only entry has no time to keep
    else
        m_ev->saveRecord(m_run, rec);
    if (wasDsq && status != ResultStatus::DSQ && rec.bib > 0) {
        TimeInfo info = data().info(rec.bib);
        if (info.status == ResultStatus::DSQ) {
            info.bib = rec.bib;
            info.status = ResultStatus::NotProcessed;
            info.reason.clear();
            m_ev->saveInfo(m_run, info);
        }
    }
    m_ev->commitBatch();
    afterRecordChange(rec.bib);
    return true;
}

bool TimingEngine::deleteRecord(int recordId)
{
    const TimeRecord* found = data().record(recordId);
    if (!found)
        return false;
    const int bib = found->bib;
    if (!m_ev->deleteRecord(m_run, recordId))
        return false;
    afterRecordChange(bib);
    return true;
}

// ---- competitors -----------------------------------------------------------------------

int TimingEngine::statusAtPoint(int point, int bib, ResultStatus status, const QString& reason, qint64 dayTimeUs)
{
    if (bib <= 0)
        return 0;
    TimeRecord rec;
    rec.point = point;
    rec.bib = bib;
    rec.dayTimeUs = dayTimeUs >= 0 ? dayTimeUs : nowUs();
    rec.flags = RecordFlag::Manual;
    switch (status) {
    case ResultStatus::DNS: rec.flags |= RecordFlag::DNS | RecordFlag::StatusEntry; break;
    case ResultStatus::NPS: rec.flags |= RecordFlag::NPS | RecordFlag::StatusEntry; break;
    case ResultStatus::DNF: rec.flags |= RecordFlag::DNF | RecordFlag::StatusEntry; break;
    case ResultStatus::DSQ: rec.flags |= RecordFlag::DSQ | RecordFlag::StatusEntry; break;
    default: break; // "Status : Time": manual impulse with the PC time
    }
    if (status == ResultStatus::DSQ && !reason.isEmpty()) {
        TimeInfo info = data().info(bib);
        info.bib = bib;
        info.reason = reason;
        m_ev->saveInfo(m_run, info);
    }
    const int id = addRecord(rec);
    if (id && status == ResultStatus::Classified && point == Point::Finish)
        Q_EMIT competitorFinished(bib);
    return id;
}

int TimingEngine::noTime(int point, int bib)
{
    if (bib <= 0)
        return 0;
    TimeRecord rec;
    rec.point = point;
    rec.bib = bib;
    rec.dayTimeUs = nowUs();
    rec.flags = RecordFlag::Manual | RecordFlag::NoTime;
    return addRecord(rec);
}

bool TimingEngine::setStatus(int bib, ResultStatus status, const QString& reason, qint64 pcTimeUs)
{
    if (bib <= 0)
        return false;

    // remove the statuses of the competitor
    m_ev->beginBatch();
    QVector<TimeRecord> withStatus;
    for (const auto& r : data().records)
        if (r.bib == bib && r.isStatusRecord())
            withStatus << r;
    for (TimeRecord r : withStatus) {
        const bool statusOnly = (r.flags & RecordFlag::StatusEntry) || !r.hasTime();
        if (statusOnly) {
            m_ev->deleteRecord(m_run, r.id);
        } else {
            r.flags &= ~(RecordFlag::DNS | RecordFlag::NPS | RecordFlag::DNF | RecordFlag::DSQ);
            m_ev->saveRecord(m_run, r);
        }
    }
    TimeInfo info = data().info(bib);
    info.bib = bib;
    info.manual = false;
    if (info.status == ResultStatus::DSQ || info.status == ResultStatus::DNS || info.status == ResultStatus::NPS
        || info.status == ResultStatus::DNF)
        info.status = ResultStatus::NotProcessed;
    info.reason = status == ResultStatus::DSQ ? reason : QString();
    m_ev->saveInfo(m_run, info);
    m_ev->commitBatch();
    afterRecordChange(bib);
    if (status == ResultStatus::Classified || status == ResultStatus::NotProcessed)
        return true;

    // a disqualified competitor who finished: status on the finish record
    if (status == ResultStatus::DSQ) {
        for (const auto& r : data().records) {
            if (r.bib == bib && r.point == Point::Finish && r.isActive() && r.hasTime()) {
                const int id = r.id;
                setRecordStatus(id, ResultStatus::DSQ);
                TimeInfo i2 = data().info(bib);
                i2.bib = bib;
                i2.reason = reason;
                m_ev->saveInfo(m_run, i2);
                return true;
            }
        }
    }
    int point = expectedPoint(bib);
    if (point < 0)
        point = status == ResultStatus::DNS || status == ResultStatus::NPS ? Point::Start : Point::Finish;
    statusAtPoint(point, bib, status, reason, pcTimeUs);
    return true;
}

bool TimingEngine::setPenalty(int bib, qint64 penaltyMs)
{
    if (bib <= 0)
        return false;
    TimeInfo info = data().info(bib);
    info.bib = bib;
    if (info.manual && info.timeMs >= 0)
        info.timeMs = info.timeMs - info.penaltyMs + penaltyMs;
    info.penaltyMs = penaltyMs;
    m_ev->saveInfo(m_run, info);
    afterRecordChange(bib);
    return true;
}

bool TimingEngine::setManualInfo(const TimeInfo& in)
{
    if (in.bib <= 0)
        return false;
    TimeInfo info = in;
    info.manual = true;
    if (info.status == ResultStatus::Classified && info.timeMs < 0)
        info.status = ResultStatus::NotProcessed;
    const bool ok = m_ev->saveInfo(m_run, info);
    rebuildFollow();
    return ok;
}

bool TimingEngine::clearManual(int bib)
{
    TimeInfo info = data().info(bib);
    if (!info.manual)
        return true;
    info.manual = false;
    if (info.status != ResultStatus::DSQ)
        info.status = ResultStatus::NotProcessed;
    m_ev->saveInfo(m_run, info);
    afterRecordChange(bib);
    return true;
}

// ---- computation ----------------------------------------------------------------

void TimingEngine::recompute(int bib)
{
    if (bib <= 0)
        return;
    const RunData& run = data();
    const TimeInfo old = run.info(bib);
    if (old.manual)
        return;

    const TimeRecord* start = nullptr;
    const TimeRecord* finish = nullptr;
    bool dns = false, nps = false, dnf = false, dsq = false, dsqRecord = false;
    std::array<const TimeRecord*, kMaxInter> inter{ {nullptr, nullptr, nullptr} };
    for (const auto& r : run.records) {
        if (r.bib == bib && (r.flags & RecordFlag::DSQ))
            dsqRecord = true;
        if (r.bib != bib || !r.isActive())
            continue;
        switch (r.recordStatus()) {
        case ResultStatus::DNS: dns = true; break;
        case ResultStatus::NPS: nps = true; break;
        case ResultStatus::DNF: dnf = true; break;
        case ResultStatus::DSQ: dsq = true; break;
        default: break;
        }
        if (!r.hasTime())
            continue;
        if (r.point == Point::Start && !start)
            start = &r;
        else if (r.point == Point::Finish && !finish)
            finish = &r;
        else if (r.point >= 1 && r.point <= kMaxInter && !inter[size_t(r.point - 1)])
            inter[size_t(r.point - 1)] = &r;
    }

    TimeInfo t;
    t.bib = bib;
    t.penaltyMs = old.penaltyMs;
    t.reason = old.reason;
    if (start) {
        for (int i = 0; i < kMaxInter; ++i)
            if (inter[size_t(i)])
                t.interMs[size_t(i)] = TimeUtil::netTimeMs(start->dayTimeUs, inter[size_t(i)]->dayTimeUs);
        if (finish) {
            const qint64 net = TimeUtil::netTimeMs(start->dayTimeUs, finish->dayTimeUs);
            if (net >= 0)
                t.timeMs = net + qMax<qint64>(0, t.penaltyMs);
        }
    }
    // a DSQ without DSQ record comes from an import or an older version: keep it
    if (dsq || (old.status == ResultStatus::DSQ && !dsqRecord))
        t.status = ResultStatus::DSQ;
    else if (nps)
        t.status = ResultStatus::NPS;
    else if (dns)
        t.status = ResultStatus::DNS;
    else if (dnf)
        t.status = ResultStatus::DNF;
    else if (t.timeMs >= 0)
        t.status = ResultStatus::Classified;
    else
        t.status = ResultStatus::NotProcessed;
    if (t.status == ResultStatus::DNS || t.status == ResultStatus::NPS || t.status == ResultStatus::DNF)
        t.timeMs = TimeUtil::kNoTime;

    const bool same = old.status == t.status && old.timeMs == t.timeMs && old.interMs == t.interMs
        && old.penaltyMs == t.penaltyMs && old.reason == t.reason && run.infos.contains(bib);
    if (same)
        return;
    const bool empty = t.status == ResultStatus::NotProcessed && t.timeMs < 0 && t.penaltyMs == 0 && t.reason.isEmpty()
        && std::all_of(t.interMs.cbegin(), t.interMs.cend(), [](qint64 v) { return v < 0; });
    if (empty) {
        if (run.infos.contains(bib))
            m_ev->removeInfo(m_run, bib);
        return;
    }
    m_ev->saveInfo(m_run, t);
}

void TimingEngine::recomputeAll()
{
    QSet<int> bibs;
    for (const auto& s : data().startList)
        bibs.insert(s.bib);
    for (const auto& r : data().records)
        bibs.insert(r.bib);
    for (auto it = data().infos.cbegin(); it != data().infos.cend(); ++it)
        bibs.insert(it.key());
    m_ev->beginBatch();
    for (int b : bibs)
        recompute(b);
    m_ev->commitBatch();
    rebuildFollow();
}
