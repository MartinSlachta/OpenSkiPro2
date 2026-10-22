#include "Event.h"
#include "db/EventDb.h"

#include <QCoreApplication>

Event::Event(QObject* parent)
    : QObject(parent)
    , m_params(EventParams::defaults())
{
    for (int i = 0; i < kMaxRuns; ++i)
        m_runs[size_t(i)].run = i + 1;
}

Event::~Event()
{
    close();
}

bool Event::create(const QString& path, QString* error)
{
    close();
    auto db = std::make_unique<EventDb>();
    if (!db->open(path, true, error))
        return false;
    m_db = std::move(db);
    m_params = EventParams::defaults();
    m_categories = CategoryGrid();
    m_competitors.clear();
    for (int i = 0; i < kMaxRuns; ++i) {
        m_runs[size_t(i)] = RunData();
        m_runs[size_t(i)].run = i + 1;
    }
    m_db->saveParams(m_params.toMap());
    Q_EMIT opened();
    return true;
}

bool Event::open(const QString& path, QString* error)
{
    close();
    auto db = std::make_unique<EventDb>();
    if (!db->open(path, false, error))
        return false;
    m_db = std::move(db);
    const QVariantMap p = m_db->loadParams();
    m_params.fromMap(p);
    m_categories = CategoryGrid::fromString(p.value(QStringLiteral("categories")).toString());
    m_competitors = m_db->loadCompetitors();
    for (int i = 0; i < kMaxRuns; ++i) {
        m_runs[size_t(i)] = RunData();
        m_runs[size_t(i)].run = i + 1;
        m_db->loadRun(m_runs[size_t(i)]);
    }
    Q_EMIT opened();
    return true;
}

void Event::close()
{
    if (!m_db)
        return;
    m_db->close();
    m_db.reset();
    m_competitors.clear();
    for (int i = 0; i < kMaxRuns; ++i) {
        m_runs[size_t(i)] = RunData();
        m_runs[size_t(i)].run = i + 1;
    }
    m_params = EventParams::defaults();
    m_categories = CategoryGrid();
    Q_EMIT closed();
}

bool Event::isOpen() const
{
    return m_db && m_db->isOpen();
}

QString Event::path() const
{
    return m_db ? m_db->path() : QString();
}

QString Event::lastError() const
{
    return m_db ? m_db->lastError() : m_error;
}

void Event::setParams(const EventParams& p)
{
    m_params = p;
    if (m_db) {
        QVariantMap map = m_params.toMap();
        map.insert(QStringLiteral("categories"), m_categories.toString());
        m_db->saveParams(map);
    }
    Q_EMIT paramsChanged();
}

void Event::setCategories(const CategoryGrid& g)
{
    m_categories = g;
    setParams(m_params);
}

// ---- competitors --------------------------------------------------------------

const Competitor* Event::competitorByBib(int bib) const
{
    if (bib <= 0)
        return nullptr;
    for (const auto& c : m_competitors)
        if (c.bib == bib)
            return &c;
    return nullptr;
}

const Competitor* Event::competitorById(int id) const
{
    for (const auto& c : m_competitors)
        if (c.id == id)
            return &c;
    return nullptr;
}

QString Event::competitorName(int bib) const
{
    if (const Competitor* c = competitorByBib(bib))
        return c->fullName();
    if (isForerunner(bib))
        return QCoreApplication::translate("Event", "Forerunner");
    return QStringLiteral("???");
}

bool Event::isForerunner(int bib) const
{
    return bib >= m_params.timing.forerunnerFrom && bib <= m_params.timing.forerunnerTo && bib > 0;
}

bool Event::addCompetitor(Competitor& c)
{
    if (!m_db || !m_db->insertCompetitor(c))
        return false;
    m_competitors << c;
    announceCompetitors();
    return true;
}

bool Event::addCompetitors(QVector<Competitor>& list)
{
    if (!m_db)
        return false;
    const bool tx = txBegin();
    bool ok = true;
    for (auto& c : list)
        ok = ok && m_db->insertCompetitor(c);
    txEnd(tx, ok);
    m_competitors = m_db->loadCompetitors();
    announceCompetitors();
    return ok;
}

bool Event::updateCompetitor(const Competitor& c)
{
    if (!m_db || !m_db->updateCompetitor(c))
        return false;
    for (auto& x : m_competitors)
        if (x.id == c.id)
            x = c;
    announceCompetitors();
    return true;
}

bool Event::removeCompetitors(const QList<int>& ids)
{
    if (!m_db)
        return false;
    const bool tx = txBegin();
    bool ok = true;
    for (int id : ids)
        ok = m_db->deleteCompetitor(id) && ok;
    txEnd(tx, ok);
    m_competitors = m_db->loadCompetitors();
    announceCompetitors();
    return ok;
}

bool Event::replaceCompetitors(const QVector<Competitor>& list)
{
    if (!m_db)
        return false;
    const bool tx = txBegin();
    bool ok = m_db->deleteAllCompetitors();
    QVector<Competitor> copy = list;
    for (auto& c : copy) {
        c.id = 0;
        ok = ok && m_db->insertCompetitor(c);
    }
    txEnd(tx, ok);
    m_competitors = m_db->loadCompetitors();
    announceCompetitors();
    return ok;
}

bool Event::txBegin()
{
    if (m_batchDepth > 0)
        return false;
    return m_db->beginTransaction();
}

void Event::txEnd(bool started, bool ok)
{
    if (!started)
        return;
    if (ok)
        m_db->commit();
    else
        m_db->rollback();
}

// ---- runs ---------------------------------------------------------------------------

const RunData& Event::run(int r) const
{
    return m_runs[size_t(qBound(1, r, kMaxRuns) - 1)];
}

RunData& Event::runRef(int r)
{
    return m_runs[size_t(qBound(1, r, kMaxRuns) - 1)];
}

void Event::announceCompetitors()
{
    if (m_batchDepth > 0) {
        m_batchCompetitorsDirty = true;
        return;
    }
    Q_EMIT competitorsChanged();
}

void Event::announceRun(int r)
{
    if (m_batchDepth > 0) {
        m_batchRunDirty[qBound(1, r, kMaxRuns) - 1] = true;
        return;
    }
    Q_EMIT runChanged(r);
}

bool Event::setStartList(int r, const QVector<StartEntry>& list)
{
    if (!m_db || !m_db->saveStartList(r, list))
        return false;
    runRef(r).startList = list;
    announceRun(r);
    return true;
}

bool Event::saveRecord(int r, const TimeRecord& rec)
{
    if (!m_db || !m_db->saveRecord(r, rec))
        return false;
    RunData& run = runRef(r);
    if (TimeRecord* existing = run.record(rec.id))
        *existing = rec;
    else
        run.records << rec;
    if (m_batchDepth == 0)
        Q_EMIT recordChanged(r, rec.id);
    announceRun(r);
    return true;
}

bool Event::deleteRecord(int r, int id)
{
    if (!m_db || !m_db->deleteRecord(r, id))
        return false;
    RunData& run = runRef(r);
    run.records.erase(std::remove_if(run.records.begin(), run.records.end(),
                                     [id](const TimeRecord& t) { return t.id == id; }),
                      run.records.end());
    announceRun(r);
    return true;
}

bool Event::saveInfo(int r, const TimeInfo& info)
{
    if (!m_db || !m_db->saveInfo(r, info))
        return false;
    runRef(r).infos.insert(info.bib, info);
    announceRun(r);
    return true;
}

bool Event::removeInfo(int r, int bib)
{
    if (!m_db || !m_db->deleteInfo(r, bib))
        return false;
    runRef(r).infos.remove(bib);
    announceRun(r);
    return true;
}

bool Event::replaceInfos(int r, const QHash<int, TimeInfo>& infos)
{
    if (!m_db || !m_db->replaceInfos(r, infos))
        return false;
    runRef(r).infos = infos;
    announceRun(r);
    return true;
}

bool Event::appendPaper(int r, const PaperLine& line)
{
    RunData& run = runRef(r);
    PaperLine l = line;
    l.seq = int(run.paper.size()) + 1;
    if (!m_db || !m_db->appendPaper(r, l.seq, l))
        return false;
    run.paper << l;
    Q_EMIT paperAppended(r);
    return true;
}

bool Event::setReferenceDay(int r, int day)
{
    if (!m_db || !m_db->setReferenceDay(r, day))
        return false;
    runRef(r).referenceDay = day;
    return true;
}

bool Event::clearRun(int r, bool startList, bool timing)
{
    if (!m_db || !m_db->clearRun(r, startList, timing))
        return false;
    RunData& run = runRef(r);
    if (startList)
        run.startList.clear();
    if (timing) {
        run.records.clear();
        run.infos.clear();
        run.paper.clear();
        run.referenceDay = -1;
    }
    announceRun(r);
    return true;
}

bool Event::replaceRun(const RunData& data)
{
    if (!m_db)
        return false;
    const int r = data.run;
    const bool tx = txBegin();
    bool ok = m_db->clearRun(r, true, true) && m_db->saveStartList(r, data.startList);
    for (const auto& rec : data.records)
        ok = ok && m_db->saveRecord(r, rec);
    for (const auto& info : data.infos)
        ok = ok && m_db->saveInfo(r, info);
    for (int i = 0; ok && i < data.paper.size(); ++i)
        ok = m_db->appendPaper(r, i + 1, data.paper.at(i));
    if (ok && data.referenceDay >= 0)
        ok = m_db->setReferenceDay(r, data.referenceDay);
    txEnd(tx, ok);
    if (!ok) {
        m_db->loadRun(runRef(r));
        announceRun(r);
        return false;
    }
    runRef(r) = data;
    announceRun(r);
    return true;
}

QString Event::reportFormatData(const QString& id) const
{
    return m_db ? m_db->reportFormat(id) : QString();
}

bool Event::setReportFormatData(const QString& id, const QString& data)
{
    return m_db && m_db->setReportFormat(id, data);
}

bool Event::beginBatch()
{
    if (!m_db)
        return false;
    if (m_batchDepth++ == 0)
        m_db->beginTransaction();
    return true;
}

bool Event::commitBatch()
{
    if (!m_db || m_batchDepth == 0)
        return false;
    if (--m_batchDepth > 0)
        return true;
    const bool ok = m_db->commit();
    if (m_batchCompetitorsDirty) {
        m_batchCompetitorsDirty = false;
        Q_EMIT competitorsChanged();
    }
    for (int i = 0; i < kMaxRuns; ++i) {
        if (m_batchRunDirty[i]) {
            m_batchRunDirty[i] = false;
            Q_EMIT runChanged(i + 1);
        }
    }
    return ok;
}
