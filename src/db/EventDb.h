#pragma once

#include "core/Category.h"
#include "core/Competitor.h"
#include "core/RunData.h"

#include <QDate>
#include <QSqlDatabase>
#include <QString>
#include <QVariantMap>
#include <QVector>

// SQLite storage of one event (OpenSkiPro2 native format, *.osp).
// Every mutation is written immediately so that a crash during timing
// never loses an impulse.
class EventDb {
public:
    static constexpr int kFormatVersion = 1;

    EventDb();
    ~EventDb();
    EventDb(const EventDb&) = delete;
    EventDb& operator=(const EventDb&) = delete;

    bool open(const QString& path, bool create, QString* error);
    // Reads title / discipline / date of an event file without opening it as current event.
    static bool peek(const QString& path, QString* title1, QString* title2, QDate* date, int* runCount = nullptr);
    void close();
    bool isOpen() const;
    QString path() const { return m_path; }
    QString lastError() const { return m_lastError; }

    bool beginTransaction();
    bool commit();
    void rollback();

    // params / meta
    QVariantMap loadParams();
    bool saveParams(const QVariantMap& params);
    QString meta(const QString& key) const;
    bool setMeta(const QString& key, const QString& value);

    // competitors
    QVector<Competitor> loadCompetitors();
    bool insertCompetitor(Competitor& c); // assigns c.id
    bool updateCompetitor(const Competitor& c);
    bool deleteCompetitor(int id);
    bool deleteAllCompetitors();

    // runs
    bool loadRun(RunData& run);
    bool saveStartList(int run, const QVector<StartEntry>& list);
    bool saveRecord(int run, const TimeRecord& rec);
    bool deleteRecord(int run, int id);
    bool saveInfo(int run, const TimeInfo& info);
    bool deleteInfo(int run, int bib);
    bool replaceInfos(int run, const QHash<int, TimeInfo>& infos);
    bool appendPaper(int run, int line, const PaperLine& p);
    bool setReferenceDay(int run, int day);
    bool clearRun(int run, bool startList, bool timing);

    // report formats (opaque serialized data)
    QString reportFormat(const QString& id);
    bool setReportFormat(const QString& id, const QString& data);

private:
    bool exec(const QString& sql);
    bool createSchema();
    bool fail(const QString& what);

    QString m_connection;
    QString m_path;
    QString m_lastError;
};
