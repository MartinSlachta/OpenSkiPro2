#pragma once

#include "Category.h"
#include "Competitor.h"
#include "EventParams.h"
#include "RunData.h"

#include <QObject>
#include <QVector>
#include <array>
#include <memory>

class EventDb;

// An open event: parameters, competitors and all runs. All mutations go
// through this class, are persisted immediately and announced by signals.
class Event : public QObject {
    Q_OBJECT
public:
    static constexpr const char* kFileSuffix = "osp";

    explicit Event(QObject* parent = nullptr);
    ~Event() override;

    bool create(const QString& path, QString* error);
    bool open(const QString& path, QString* error);
    void close();
    bool isOpen() const;
    QString path() const;
    QString lastError() const;

    // -- parameters
    const EventParams& params() const { return m_params; }
    void setParams(const EventParams& p);
    const CategoryGrid& categories() const { return m_categories; }
    void setCategories(const CategoryGrid& g);
    int runCount() const { return m_params.type.runCount; }
    int interCount() const { return m_params.type.interCount; }
    Precision precision() const { return m_params.type.timePrecision; }

    // -- competitors
    const QVector<Competitor>& competitors() const { return m_competitors; }
    const Competitor* competitorByBib(int bib) const;
    const Competitor* competitorById(int id) const;
    QString competitorName(int bib) const; // "LAST First" or "???"
    bool addCompetitor(Competitor& c);
    bool updateCompetitor(const Competitor& c);
    bool removeCompetitors(const QList<int>& ids);
    bool addCompetitors(QVector<Competitor>& list); // bulk insert in one transaction
    bool replaceCompetitors(const QVector<Competitor>& list);
    bool isForerunner(int bib) const;

    // -- runs (1-based)
    const RunData& run(int r) const;
    bool setStartList(int r, const QVector<StartEntry>& list);
    bool saveRecord(int r, const TimeRecord& rec);   // insert or update
    bool deleteRecord(int r, int id);
    bool saveInfo(int r, const TimeInfo& info);
    bool removeInfo(int r, int bib);
    bool replaceInfos(int r, const QHash<int, TimeInfo>& infos);
    bool appendPaper(int r, const PaperLine& line);
    bool setReferenceDay(int r, int day);
    bool clearRun(int r, bool startList, bool timing);
    // Bulk replacement of a whole run (used by importers)
    bool replaceRun(const RunData& data);

    // -- report formats (serialized by ReportFormat)
    QString reportFormatData(const QString& id) const;
    bool setReportFormatData(const QString& id, const QString& data);

    // Transaction helpers for bulk operations
    bool beginBatch();
    bool commitBatch();

Q_SIGNALS:
    void opened();
    void closed();
    void paramsChanged();
    void competitorsChanged();
    void runChanged(int run);
    void recordChanged(int run, int recordId);
    void paperAppended(int run);

private:
    RunData& runRef(int r);
    void announceRun(int r);
    void announceCompetitors();
    bool txBegin();
    void txEnd(bool started, bool ok);

    std::unique_ptr<EventDb> m_db;
    EventParams m_params;
    CategoryGrid m_categories;
    QVector<Competitor> m_competitors;
    std::array<RunData, kMaxRuns> m_runs;
    QString m_error;
    int m_batchDepth = 0;
    bool m_batchRunDirty[kMaxRuns] = {};
    bool m_batchCompetitorsDirty = false;
};
