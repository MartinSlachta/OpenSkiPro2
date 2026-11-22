#include "SkiAlpExporter.h"

#include "SkiAlpImporter.h"
#include "SkiProImporter.h"
#include "core/Event.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QTemporaryDir>
#include <QUuid>
#include <QtCore/private/qzipreader_p.h>
#include <QtCore/private/qzipwriter_p.h>

#include <algorithm>

namespace {

constexpr auto kFormat = SkiProImporter::Format::SkiAlp11;
const QString kFile1 = QStringLiteral("File1");
const QString kFile2 = QStringLiteral("File2");
const QString kNoPoints = QStringLiteral("100000"); // SkiAlp Pro: no FIS points

// A read-write connection to one database of the archive, removed on destruction.
class WriteDb {
public:
    explicit WriteDb(const QString& path)
        : m_name(QStringLiteral("skialp-export-") + QUuid::createUuid().toString(QUuid::WithoutBraces))
    {
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_name);
            db.setDatabaseName(path);
            m_ok = db.open();
            if (!m_ok)
                m_error = db.lastError().text();
        }
    }
    ~WriteDb()
    {
        {
            QSqlDatabase db = QSqlDatabase::database(m_name, false);
            if (db.isOpen())
                db.close();
        }
        QSqlDatabase::removeDatabase(m_name);
    }
    bool ok() const { return m_ok; }
    QString error() const { return m_error; }
    QSqlDatabase db() const { return QSqlDatabase::database(m_name, false); }
    bool hasTable(const QString& t) const { return db().tables().contains(t, Qt::CaseInsensitive); }

    // count of rows, -1 when the table does not exist
    int rowCount(const QString& table) const
    {
        if (!hasTable(table))
            return -1;
        QSqlQuery q(db());
        return q.exec(QStringLiteral("SELECT COUNT(*) FROM %1").arg(table)) && q.next() ? q.value(0).toInt() : -1;
    }

    bool exec(const QString& sql, QString* error)
    {
        QSqlQuery q(db());
        if (q.exec(sql))
            return true;
        if (error)
            *error = QStringLiteral("%1: %2").arg(sql.left(80), q.lastError().text());
        return false;
    }

    // Column names and default values of a table (0 for INTEGER columns, '' for the others,
    // as SkiAlp Pro itself writes its rows).
    QList<QPair<QString, QVariant>> columns(const QString& table) const
    {
        QList<QPair<QString, QVariant>> cols;
        QSqlQuery q(db());
        if (q.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table)))
            while (q.next()) {
                const QString type = q.value(2).toString().toUpper();
                cols << qMakePair(q.value(1).toString(),
                                  type.startsWith(QLatin1String("INT")) ? QVariant(0) : QVariant(QString()));
            }
        return cols;
    }

    // Inserts one row: the defaults of `columns` overridden by `values`.
    bool insert(const QString& table, const QList<QPair<QString, QVariant>>& cols, const QHash<QString, QVariant>& values,
                QString* error)
    {
        QStringList names, marks;
        for (const auto& c : cols) {
            names << c.first;
            marks << QStringLiteral("?");
        }
        QSqlQuery q(db());
        q.prepare(QStringLiteral("INSERT INTO %1 (%2) VALUES (%3)").arg(table, names.join(QLatin1Char(',')), marks.join(QLatin1Char(','))));
        for (const auto& c : cols)
            q.addBindValue(values.contains(c.first) ? values.value(c.first) : c.second);
        if (q.exec())
            return true;
        if (error)
            *error = QStringLiteral("%1: %2").arg(table, q.lastError().text());
        return false;
    }

private:
    QString m_name;
    bool m_ok = false;
    QString m_error;
};

// Writes the key / value table TPARAMETERS and counts the changes.
class Params {
public:
    Params(WriteDb& db, QString* error)
        : m_db(db), m_error(error)
    {
    }
    int changes() const { return m_changes; }
    bool ok() const { return m_ok; }

    void set(const QString& key, const QString& value)
    {
        if (!m_ok)
            return;
        QSqlQuery q(m_db.db());
        q.prepare(QStringLiteral("INSERT OR REPLACE INTO TPARAMETERS (C_PARAM, C_VALUE) VALUES (?, ?)"));
        q.addBindValue(key);
        q.addBindValue(value);
        fail(q.exec(), q);
        ++m_changes;
    }
    void set(const QString& key, int value) { set(key, QString::number(value)); }
    // only when there is something to write (the template has no keys of an empty header)
    void setIfAny(const QString& key, const QString& value)
    {
        if (!value.trimmed().isEmpty())
            set(key, value.trimmed());
    }
    void removeLike(const QString& pattern)
    {
        if (!m_ok)
            return;
        QSqlQuery q(m_db.db());
        q.prepare(QStringLiteral("DELETE FROM TPARAMETERS WHERE C_PARAM LIKE ?"));
        q.addBindValue(pattern);
        if (fail(q.exec(), q))
            m_changes += q.numRowsAffected();
    }

private:
    bool fail(bool done, const QSqlQuery& q)
    {
        if (!done && m_ok) {
            m_ok = false;
            if (m_error)
                *m_error = QStringLiteral("TPARAMETERS: %1").arg(q.lastError().text());
        }
        return done;
    }
    WriteDb& m_db;
    QString* m_error;
    int m_changes = 0;
    bool m_ok = true;
};

int timeAccuracy(Precision p)
{
    switch (p) {
    case Precision::Second: return 2;
    case Precision::Tenth: return 3;
    case Precision::Hundredth: return 4;
    case Precision::Thousandth: return 5;
    }
    return 4;
}

int sapSex(Gender g)
{
    return g == Gender::Male ? 1 : g == Gender::Female ? 2 : 0;
}

QString runTables(int r)
{
    return QStringLiteral("HEAT%1").arg(r);
}

// The text of an output group as SkiAlp Pro stores it (SkiProImporter parseFilter() reads it).
QString filterText(const OutputGroup& g)
{
    const QString cr = QStringLiteral("\\CR\\");
    QStringList parts{ g.label, QStringLiteral("v1"), g.anyMode ? QStringLiteral("1") : QStringLiteral("0") };
    for (const GroupCriterion& c : g.criteria) {
        parts << QString::number(SkiProImporter::fieldToSource(c.field, kFormat)) << QString::number(c.op) << c.value
              << (c.op == GroupCriterion::Between ? c.value2 : QStringLiteral("0")) << QStringLiteral("0");
    }
    return parts.join(cr);
}

void writeHeader(Params& p, const HeaderParams& h)
{
    auto person = [&](const QString& key, const QString& sap, bool jury) {
        const QString name = h.value(key + QStringLiteral(".name"));
        const QString nation = h.value(key + QStringLiteral(".nation"));
        p.setIfAny(QStringLiteral("HEADER_FIS_") + sap, name);
        p.setIfAny(QStringLiteral("HEADER_FIS_%12").arg(sap), nation);
        p.setIfAny(QStringLiteral("HEADER_FIS_%1C").arg(sap), h.value(key + QStringLiteral(".code")));
        if (jury)
            p.setIfAny(QStringLiteral("HEADER_FIS_%1_NATION").arg(sap), nation);
    };
    person(QLatin1String(HeaderKeys::TechnicalDelegate), QStringLiteral("DT"), true);
    person(QLatin1String(HeaderKeys::Referee), QStringLiteral("REF"), true);
    person(QLatin1String(HeaderKeys::AssistantReferee), QStringLiteral("AREF"), true);
    person(QLatin1String(HeaderKeys::ChiefOfRace), QStringLiteral("CHIEFOFRACE"), true);
    for (int r = 1; r <= 2; ++r) {
        const QString k = QStringLiteral("run%1.").arg(r);
        const QString n = QStringLiteral("_%1").arg(r);
        p.setIfAny(QStringLiteral("HEADER_FIS_COURSESETTER") + n, h.value(k + QStringLiteral("setter.name")));
        p.setIfAny(QStringLiteral("HEADER_FIS_COURSESETTER_NATION") + n, h.value(k + QStringLiteral("setter.nation")));
        for (char c = 'A'; c <= 'E'; ++c) {
            const QString f = QStringLiteral("HEADER_FIS_FORERUNNER") + QLatin1Char(c);
            const QString fk = k + QStringLiteral("fore") + QLatin1Char(c);
            p.setIfAny(f + QStringLiteral("LN") + n, h.value(fk + QStringLiteral(".name")));
            p.setIfAny(f + QStringLiteral("_NATION") + n, h.value(fk + QStringLiteral(".nation")));
        }
        const QList<QPair<QString, QString>> runKeys = {
            { QStringLiteral("gates"), QStringLiteral("GATE") },
            { QStringLiteral("directions"), QStringLiteral("DIR") },
            { QStringLiteral("starttime"), QStringLiteral("STARTTIME") },
            { QLatin1String(HeaderKeys::Course), QStringLiteral("COURSE") },
            { QLatin1String(HeaderKeys::Homologation), QStringLiteral("HOMOLOGATION") },
            { QLatin1String(HeaderKeys::StartAltitude), QStringLiteral("START") },
            { QLatin1String(HeaderKeys::FinishAltitude), QStringLiteral("FINISH") },
            { QLatin1String(HeaderKeys::Drop), QStringLiteral("DROP") },
            { QLatin1String(HeaderKeys::Length), QStringLiteral("LENGTH") },
        };
        for (const auto& rk : runKeys)
            p.setIfAny(QStringLiteral("HEADER_FIS_") + rk.second + n, h.runValue(r, rk.first));
    }
    p.setIfAny(QStringLiteral("HEADER_FIS_WEATHER"), h.value(QLatin1String(HeaderKeys::Weather)));
    p.setIfAny(QStringLiteral("HEADER_FIS_SNOW"), h.value(QLatin1String(HeaderKeys::Snow)));
    // SkiAlp Pro: temperatures in tenths of a degree
    for (const auto& t : { qMakePair(QLatin1String(HeaderKeys::TempStart), QStringLiteral("TEMPSTART")),
                           qMakePair(QLatin1String(HeaderKeys::TempFinish), QStringLiteral("TEMPFINISH")) }) {
        bool ok = false;
        const double v = h.value(t.first).trimmed().replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&ok);
        if (ok)
            p.set(QStringLiteral("HEADER_FIS_") + t.second, QString::number(qRound(v * 10)));
    }
    p.setIfAny(QStringLiteral("IMPORT_LIST_NUMBER"), h.value(QLatin1String(HeaderKeys::ListNumber)));
}

bool writeParams(WriteDb& db, const SkiAlpExporter::Data& d, int* changes, QStringList* warnings, QString* error)
{
    Params p(db, error);
    const EventParams& ep = d.params;

    // ---- general
    p.set(QStringLiteral("TITLE1"), ep.general.title1);
    p.set(QStringLiteral("TITLE2"), ep.general.title2);
    p.set(QStringLiteral("DATE"), SkiProImporter::toDelphiDate(ep.general.date));
    p.set(QStringLiteral("LOCATION"), ep.general.location);
    p.set(QStringLiteral("COMMITTEE"), ep.general.nation);
    p.set(QStringLiteral("ORGANISATION"), ep.general.organisation);
    p.set(QStringLiteral("TIMING_BY"), ep.general.timingBy);
    p.set(QStringLiteral("CODEX"), ep.general.codex);
    // the relay token of SkiAlpBridge: never carried over, a fallback event must not
    // publish into another race (Mako, 2026-11-22)
    p.set(QStringLiteral("HEADER_FIS_TIMEKEEPERC"), QString());

    // ---- event type
    p.set(QStringLiteral("DISCIPLINE"), int(ep.type.discipline));
    p.set(QStringLiteral("DISCIPLINE_TYPE"), 0);
    p.set(QStringLiteral("HEAT_NUMBER"), ep.type.runCount);
    p.set(QStringLiteral("INTER_NUMBER"), 0);
    p.set(QStringLiteral("TIME_ACCURACY"), timeAccuracy(ep.type.timePrecision));
    p.set(QStringLiteral("SPEED_ACCURACY"), int(ep.type.speedPrecision));
    p.set(QStringLiteral("AVG_SPEED_ACCURACY"), int(ep.type.avgSpeedPrecision));
    if (ep.type.courseLength > 0.0)
        p.set(QStringLiteral("COURSE_LENGTH"), qRound(ep.type.courseLength * 1000.0));
    p.set(QStringLiteral("FINAL_RANKING_TYPE"), ep.type.finalType == FinalRankingType::BestOfRuns ? 1 : 0);
    for (int i = 0; i < kMaxRuns; ++i) {
        p.set(QStringLiteral("FINAL_RANKING_USE_HEAT%1").arg(i + 1), ep.type.runsUsed[size_t(i)] ? 1 : 0);
        const QString name = ep.type.runNames[size_t(i)].trimmed();
        p.set(QStringLiteral("HEAT%1_NAME").arg(i + 1), name.isEmpty() ? QStringLiteral("Run %1").arg(i + 1) : name);
    }

    // ---- header (only the values OpenSkiPro2 has; the free header stays as in the template)
    writeHeader(p, ep.header);

    // ---- categories
    // SkiAlp Pro keeps a fixed number of slots, the unused ones empty (name '', 0, 0, 0)
    int slotCount = 0;
    {
        QSqlQuery q(db.db());
        if (q.exec(QStringLiteral("SELECT COUNT(*) FROM TPARAMETERS WHERE C_PARAM LIKE 'COMPETITORS_CATEGORY%_NAME'")) && q.next())
            slotCount = q.value(0).toInt();
    }
    const int categories = int(d.categories.categories.size());
    for (int i = 0; i < qMax(slotCount, categories); ++i) {
        const QString base = QStringLiteral("COMPETITORS_CATEGORY%1_").arg(i + 1);
        const CategoryDef c = i < categories ? d.categories.categories.at(i) : CategoryDef();
        p.set(base + QStringLiteral("NAME"), c.name);
        p.set(base + QStringLiteral("SEX"), sapSex(c.gender));
        p.set(base + QStringLiteral("YEAR_MIN"), c.yearMin);
        p.set(base + QStringLiteral("YEAR_MAX"), c.yearMax);
    }

    // ---- output groups (filter list 1), one line each (Mako: one category per group)
    p.removeLike(QStringLiteral("FILTERS_LIST1_INDEX%"));
    int gi = 0;
    for (const OutputGroup& g : ep.outputGroups) {
        const QString text = filterText(g);
        const QString base = QStringLiteral("FILTERS_LIST1_INDEX%1_").arg(++gi);
        // _COUNT = length in bytes (UTF-8); SkiAlp Pro writes groups of 115 bytes in one line too
        p.set(base + QStringLiteral("COUNT"), int(text.toUtf8().size()));
        p.set(base + QStringLiteral("LINE1"), text);
    }
    p.set(QStringLiteral("FILTERS_LIST1_COUNT"), gi);

    // ---- competitor fields: active flags, own labels, the grid
    for (const auto& def : competitorFieldDefs()) {
        const int src = SkiProImporter::fieldToSource(def.id, kFormat);
        if (src <= 0)
            continue;
        p.set(QStringLiteral("COMPETITORS_INFO_ACTIVE%1").arg(src), ep.fields.isActive(def.id) ? 1 : 0);
        const QString label = ep.fields.labels.value(def.id).trimmed();
        if (!label.isEmpty())
            p.set(QStringLiteral("COMPETITORS_INFO_NAME%1").arg(src), label);
    }
    int col = 0;
    for (const auto& gcol : ep.fields.grid) {
        const int src = SkiProImporter::fieldToSource(gcol.first, kFormat);
        if (src <= 0)
            continue;
        ++col;
        p.set(QStringLiteral("COMPETITORS_GRID_INFO%1").arg(col), src);
        p.set(QStringLiteral("COMPETITORS_GRID_WIDTH%1").arg(col), gcol.second);
    }
    if (col > 0)
        p.set(QStringLiteral("COMPETITORS_GRID_COUNT"), col);

    *changes = p.changes();
    return p.ok();
}

bool writeCompetitors(WriteDb& db, const QVector<Competitor>& list, int* changes, QString* error)
{
    // the last used C_IDX
    int lastIdx = 0;
    {
        QSqlQuery q(db.db());
        if (q.exec(QStringLiteral("SELECT KEYVALUE FROM TKEYVALUES WHERE TABLENAME = 'TCOMPETITORS'")) && q.next())
            lastIdx = q.value(0).toInt();
    }
    QVector<Competitor> sorted = list;
    std::stable_sort(sorted.begin(), sorted.end(), [](const Competitor& a, const Competitor& b) { return a.bib < b.bib; });
    const auto cols = db.columns(QStringLiteral("TCOMPETITORS"));
    for (const Competitor& c : sorted) {
        QHash<QString, QVariant> v;
        // what SkiAlp Pro writes for "no points" / handicap (rows of a real race)
        for (int i = 29; i <= 33; ++i)
            v.insert(QStringLiteral("C_I%1").arg(i), 100000);
        for (int i = 37; i <= 41; ++i)
            v.insert(QStringLiteral("C_I%1").arg(i), 100000);
        v.insert(QStringLiteral("C_I46"), 10000);
        v.insert(QStringLiteral("C_IDX"), ++lastIdx);
        v.insert(QStringLiteral("C_NUM"), c.bib);
        v.insert(QStringLiteral("C_LAST_NAME"), c.lastName);
        v.insert(QStringLiteral("C_FIRST_NAME"), c.firstName);
        v.insert(QStringLiteral("C_CODE"), c.code);
        v.insert(QStringLiteral("C_SEX"), sapSex(c.gender));
        v.insert(QStringLiteral("C_YEAR"), c.year);
        v.insert(QStringLiteral("C_CATEGORY"), c.category);
        v.insert(QStringLiteral("C_NATION"), c.nation);
        v.insert(QStringLiteral("C_COMMITTEE"), c.region);
        v.insert(QStringLiteral("C_CLUB"), c.club);
        v.insert(QStringLiteral("C_TEAM"), c.team);
        v.insert(QStringLiteral("C_I22"), c.fromList);
        for (int i = 0; i < kDisciplineCount; ++i) {
            const int pts = c.points[size_t(i)];
            v.insert(QStringLiteral("C_I%1").arg(29 + i), pts >= 0 ? pts : 100000);
        }
        if (!db.insert(QStringLiteral("TCOMPETITORS"), cols, v, error))
            return false;
    }
    QSqlQuery q(db.db());
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO TKEYVALUES (TABLENAME, KEYVALUE) VALUES ('TCOMPETITORS', ?)"));
    q.addBindValue(lastIdx);
    if (!q.exec()) {
        if (error)
            *error = QStringLiteral("TKEYVALUES: %1").arg(q.lastError().text());
        return false;
    }
    *changes = int(sorted.size());
    return true;
}

// Creates the tables (and their indexes) of run r from those of run 1.
bool ensureRunTables(WriteDb& db, int r, QStringList* created, QString* error)
{
    if (db.hasTable(QStringLiteral("TSTARTLIST_") + runTables(r)))
        return true;
    QSqlQuery q(db.db());
    if (!q.exec(QStringLiteral("SELECT type, name, sql FROM sqlite_master WHERE sql IS NOT NULL ORDER BY type DESC"))) {
        if (error)
            *error = q.lastError().text();
        return false;
    }
    const QRegularExpression heat1(QStringLiteral("HEAT1(?![0-9])"));
    QStringList statements;
    while (q.next()) {
        const QString name = q.value(1).toString();
        if (!name.contains(heat1))
            continue;
        QString sql = q.value(2).toString();
        sql.replace(heat1, runTables(r));
        statements << sql;
        if (q.value(0).toString() == QLatin1String("table"))
            created->append(QString(name).replace(heat1, runTables(r)));
    }
    q.finish();
    for (const QString& s : statements)
        if (!db.exec(s, error))
            return false;
    return true;
}

// Time of a record for SkiAlp Pro (µs of the day). A status record without a time gets
// the time of the record before it at the point (SkiAlp Pro stores a time with every record).
qint64 recordTime(const QVector<const TimeRecord*>& recs, int i)
{
    for (int k = i; k >= 0; --k)
        if (recs.at(k)->dayTimeUs >= 0)
            return recs.at(k)->dayTimeUs;
    for (int k = i + 1; k < recs.size(); ++k)
        if (recs.at(k)->dayTimeUs >= 0)
            return recs.at(k)->dayTimeUs;
    return 0;
}

int sapResultStatus(ResultStatus s)
{
    switch (s) {
    case ResultStatus::DNS:
    case ResultStatus::NPS: return 1; // SkiAlp Pro has no NPS (Mako does not use it)
    case ResultStatus::DNF: return 2;
    case ResultStatus::DSQ: return 3;
    default: return 0;
    }
}

// Forerunners are left out of the start list and the results: SkiAlp Pro has no forerunner
// range in the event and would rank them as competitors without a name. Their impulses stay.
bool writeRun(WriteDb& db, const RunData& run, const TimingOptions& timing, SkiAlpExporter::RunReport* rr,
              QHash<QString, int>* changes, QStringList* warnings, QString* error)
{
    auto forerunner = [&](int bib) { return bib > 0 && bib >= timing.forerunnerFrom && bib <= timing.forerunnerTo; };
    const QString h = runTables(run.run);
    // ---- start list
    {
        const QString t = QStringLiteral("TSTARTLIST_") + h;
        const auto cols = db.columns(t);
        int line = 0;
        for (const StartEntry& s : run.startList) {
            if (s.bib <= 0 || forerunner(s.bib))
                continue;
            QHash<QString, QVariant> v{ { QStringLiteral("C_LINE"), ++line },
                                        { QStringLiteral("C_NUM"), s.bib },
                                        { QStringLiteral("C_START"), s.startTimeUs > 0 ? s.startTimeUs : 0 } };
            if (!db.insert(t, cols, v, error))
                return false;
        }
        rr->startList = line;
        (*changes)[t] += line;
    }

    // ---- records of the start and the finish (no intermediates)
    QHash<int, QVector<qint64>> activeTimes[2]; // [0] start, [1] finish: bib -> times
    for (int pi = 0; pi < 2; ++pi) {
        const int point = pi == 0 ? Point::Start : Point::Finish;
        const QString t = QStringLiteral("TTIMERECORDS_%1_%2").arg(h, pi == 0 ? QStringLiteral("START") : QStringLiteral("FINISH"));
        const auto cols = db.columns(t);
        const QVector<const TimeRecord*> recs = run.pointRecords(point);
        for (int i = 0; i < recs.size(); ++i) {
            const TimeRecord& rec = *recs.at(i);
            const qint64 us = recordTime(recs, i);
            QHash<QString, QVariant> v{ { QStringLiteral("C_LINE"), i + 1 },
                                        { QStringLiteral("C_NUM"), rec.bib },
                                        { QStringLiteral("C_STATUS"), SkiAlpExporter::recordStatusWord(rec.flags) },
                                        { QStringLiteral("C_HOUR1"), QStringLiteral("0") },
                                        { QStringLiteral("C_HOUR2"), QString::number(us) } };
            if (!db.insert(t, cols, v, error))
                return false;
            if (rec.bib > 0 && rec.isActive() && rec.hasTime() && !rec.isStatusRecord())
                activeTimes[pi][rec.bib] << rec.dayTimeUs;
        }
        rr->records += int(recs.size());
        (*changes)[t] += int(recs.size());
    }

    // ---- results: one row per start list entry (as SkiAlp Pro), then the others
    {
        const QString t = QStringLiteral("TTIMEINFOS_") + h;
        const auto cols = db.columns(t);
        QVector<int> bibs;
        QSet<int> seen;
        for (const StartEntry& s : run.startList)
            if (s.bib > 0 && !forerunner(s.bib) && !seen.contains(s.bib)) {
                bibs << s.bib;
                seen.insert(s.bib);
            }
        QVector<int> others;
        for (auto it = run.infos.constBegin(); it != run.infos.constEnd(); ++it)
            if (it.key() > 0 && !forerunner(it.key()) && !seen.contains(it.key()) && it->status != ResultStatus::NotProcessed)
                others << it.key();
        std::sort(others.begin(), others.end());
        bibs += others;
        int results = 0;
        for (int bib : bibs) {
            const TimeInfo info = run.info(bib);
            const bool classified = info.status == ResultStatus::Classified && info.timeMs >= 0;
            const int status = sapResultStatus(info.status);
            QHash<QString, QVariant> v{ { QStringLiteral("C_NUM"), bib },
                                        { QStringLiteral("C_STATUS"), status },
                                        { QStringLiteral("C_TIME"), classified ? info.timeMs : 0 },
                                        { QStringLiteral("C_REASON"), info.status == ResultStatus::DSQ ? info.reason : QString() } };
            if (!db.insert(t, cols, v, error))
                return false;
            if (classified || status != 0)
                ++results;
            if (!classified)
                continue;
            // both tables must tell the same (unknown which one SkiAlp Pro believes)
            if (info.penaltyMs > 0)
                warnings->append(QStringLiteral("Run %1, bib %2: the time includes a penalty; SkiAlp Pro gets the time without it in its impulses.")
                                     .arg(run.run).arg(bib));
            if (info.manual) {
                warnings->append(QStringLiteral("Run %1, bib %2: the time was entered manually; the impulses in SkiAlp Pro may give another time.")
                                     .arg(run.run).arg(bib));
                continue;
            }
            const auto st = activeTimes[0].value(bib), fi = activeTimes[1].value(bib);
            if (st.size() == 1 && fi.size() == 1) {
                const qint64 net = (fi.first() - st.first()) / 1000;
                if (net != info.timeMs - info.penaltyMs)
                    warnings->append(QStringLiteral("Run %1, bib %2: the time %3 ms does not match the impulses (%4 ms).")
                                         .arg(run.run).arg(bib).arg(info.timeMs).arg(net));
            }
        }
        rr->results = results;
        (*changes)[t] += int(bibs.size());
    }
    return true;
}

bool bumpUpdateValues(WriteDb& db, const QHash<QString, int>& changes, QString* error)
{
    for (auto it = changes.constBegin(); it != changes.constEnd(); ++it) {
        const int n = qMax(1, it.value());
        QSqlQuery q(db.db());
        q.prepare(QStringLiteral("UPDATE TUPDATEVALUES SET UPDATEVALUE = UPDATEVALUE + ? WHERE TABLENAME = ?"));
        q.addBindValue(n);
        q.addBindValue(it.key());
        if (!q.exec()) {
            if (error)
                *error = QStringLiteral("TUPDATEVALUES: %1").arg(q.lastError().text());
            return false;
        }
        if (q.numRowsAffected() == 0) {
            QSqlQuery ins(db.db());
            ins.prepare(QStringLiteral("INSERT INTO TUPDATEVALUES (TABLENAME, UPDATEVALUE) VALUES (?, ?)"));
            ins.addBindValue(it.key());
            ins.addBindValue(n);
            if (!ins.exec()) {
                if (error)
                    *error = QStringLiteral("TUPDATEVALUES: %1").arg(ins.lastError().text());
                return false;
            }
        }
    }
    return true;
}

bool unpackTemplate(const QString& templateFile, const QString& dir, QString* error)
{
    QZipReader zip(templateFile);
    if (!zip.isReadable() || zip.status() != QZipReader::NoError) {
        if (error)
            *error = QStringLiteral("The template %1 is not a SkiAlp Pro event archive.").arg(QDir::toNativeSeparators(templateFile));
        return false;
    }
    for (const QString& name : { kFile1, kFile2 }) {
        const QByteArray data = zip.fileData(name);
        if (data.isEmpty()) {
            if (error)
                *error = QStringLiteral("The template does not contain %1, it is not a SkiAlp Pro event.").arg(name);
            return false;
        }
        QFile f(QDir(dir).filePath(name));
        if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size()) {
            if (error)
                *error = QStringLiteral("Cannot write %1: %2").arg(f.fileName(), f.errorString());
            return false;
        }
    }
    return true;
}

// The template must be a SkiAlp Pro event without competitors and without run data.
bool checkUnpacked(const QString& dir, QString* error)
{
    WriteDb main(QDir(dir).filePath(kFile1));
    WriteDb runs(QDir(dir).filePath(kFile2));
    if (!main.ok() || !runs.ok()) {
        if (error)
            *error = QStringLiteral("The template cannot be opened: %1").arg(main.ok() ? runs.error() : main.error());
        return false;
    }
    for (const QString& t : { QStringLiteral("TPARAMETERS"), QStringLiteral("TCOMPETITORS"), QStringLiteral("TKEYVALUES"),
                              QStringLiteral("TUPDATEVALUES") })
        if (!main.hasTable(t)) {
            if (error)
                *error = QStringLiteral("The template is not a SkiAlp Pro event (%1 missing).").arg(t);
            return false;
        }
    for (const QString& t : { QStringLiteral("TSTARTLIST_HEAT1"), QStringLiteral("TTIMEINFOS_HEAT1"),
                              QStringLiteral("TTIMERECORDS_HEAT1_START"), QStringLiteral("TTIMERECORDS_HEAT1_FINISH"),
                              QStringLiteral("TUPDATEVALUES") })
        if (!runs.hasTable(t)) {
            if (error)
                *error = QStringLiteral("The template is not a SkiAlp Pro event (%1 missing).").arg(t);
            return false;
        }
    const int competitors = main.rowCount(QStringLiteral("TCOMPETITORS"));
    int runRows = 0;
    for (const QString& t : runs.db().tables())
        if (t.startsWith(QLatin1String("TSTARTLIST_HEAT")) || t.startsWith(QLatin1String("TTIMEINFOS_HEAT"))
            || t.startsWith(QLatin1String("TTIMERECORDS_HEAT")))
            runRows += qMax(0, runs.rowCount(t));
    if (competitors != 0 || runRows != 0) {
        if (error)
            *error = QStringLiteral("The template is not empty (%1 competitors, %2 rows of runs). "
                                    "Empty the event in SkiAlp Pro and export it again.")
                         .arg(competitors)
                         .arg(runRows);
        return false;
    }
    return true;
}

} // namespace

int SkiAlpExporter::recordStatusWord(int flags)
{
    int word = 0;
    if (flags & (RecordFlag::Inactive | RecordFlag::Cancelled))
        word |= 0x000001;
    int status = 0;
    if (flags & RecordFlag::DSQ)
        status = 3;
    else if (flags & RecordFlag::DNF)
        status = 2;
    else if (flags & (RecordFlag::DNS | RecordFlag::NPS))
        status = 1;
    word |= status << 8;
    // a status entered manually (SkiAlp Pro "Enter a DNS / DNF / DSQ number")
    if (status != 0 && (flags & (RecordFlag::Manual | RecordFlag::StatusEntry)))
        word |= 0x020000;
    return word;
}

SkiAlpExporter::Data SkiAlpExporter::fromEvent(const Event& ev)
{
    Data d;
    d.params = ev.params();
    d.categories = ev.categories();
    d.competitors = ev.competitors();
    for (int r = 1; r <= ev.runCount(); ++r)
        d.runs << ev.run(r);
    return d;
}

QString SkiAlpExporter::defaultFileName(const QString& eventPath, const QDateTime& now)
{
    QString base = QFileInfo(eventPath).completeBaseName();
    if (base.isEmpty())
        base = QStringLiteral("Event");
    return QStringLiteral("%1-SAP-%2.spro").arg(base, now.toString(QStringLiteral("yyyyMMdd-HHmm")));
}

bool SkiAlpExporter::checkTemplate(const QString& templateFile, QString* error)
{
    if (templateFile.isEmpty() || !QFileInfo::exists(templateFile)) {
        if (error)
            *error = QStringLiteral("Set the SkiAlp Pro 11 template in Local configuration > Folders.");
        return false;
    }
    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        if (error)
            *error = QStringLiteral("Cannot create a temporary folder.");
        return false;
    }
    return unpackTemplate(templateFile, tmp.path(), error) && checkUnpacked(tmp.path(), error);
}

bool SkiAlpExporter::write(const Event& ev, const QString& templateFile, const QString& target, Report* report,
                           QString* error)
{
    return write(fromEvent(ev), templateFile, target, report, error);
}

bool SkiAlpExporter::write(const Data& data, const QString& templateFile, const QString& target, Report* report,
                           QString* error)
{
    Report rep;
    auto fail = [&](const QString& msg) {
        if (error)
            *error = msg;
        return false;
    };
    if (templateFile.isEmpty() || !QFileInfo::exists(templateFile))
        return fail(QStringLiteral("Set the SkiAlp Pro 11 template in Local configuration > Folders."));
    if (data.competitors.isEmpty())
        return fail(QStringLiteral("The event has no competitors."));
    {
        QHash<int, int> count;
        int noBib = 0;
        for (const Competitor& c : data.competitors) {
            if (c.bib > 0)
                ++count[c.bib];
            else
                ++noBib;
        }
        QStringList dup;
        for (auto it = count.constBegin(); it != count.constEnd(); ++it)
            if (it.value() > 1)
                dup << QString::number(it.key());
        if (!dup.isEmpty()) {
            dup.sort();
            return fail(QStringLiteral("Several competitors have the same bib: %1.").arg(dup.join(QStringLiteral(", "))));
        }
        if (noBib > 0)
            rep.warnings << QStringLiteral("%1 competitor(s) without a bib.").arg(noBib);
    }

    QTemporaryDir tmp;
    if (!tmp.isValid())
        return fail(QStringLiteral("Cannot create a temporary folder."));
    QString err;
    if (!unpackTemplate(templateFile, tmp.path(), &err) || !checkUnpacked(tmp.path(), &err))
        return fail(err);

    const int runCount = qBound(1, data.params.type.runCount, kMaxRuns);
    {
        WriteDb main(tmp.filePath(kFile1));
        WriteDb runs(tmp.filePath(kFile2));
        if (!main.ok() || !runs.ok())
            return fail(main.ok() ? runs.error() : main.error());

        // ---- File1: parameters, competitors
        if (!main.db().transaction())
            return fail(main.db().lastError().text());
        QHash<QString, int> changes1;
        int n = 0;
        bool ok = writeParams(main, data, &n, &rep.warnings, &err);
        changes1.insert(QStringLiteral("TPARAMETERS"), n);
        if (ok)
            ok = writeCompetitors(main, data.competitors, &n, &err);
        changes1.insert(QStringLiteral("TCOMPETITORS"), n);
        if (ok)
            ok = bumpUpdateValues(main, changes1, &err);
        if (!ok) {
            main.db().rollback();
            return fail(err);
        }
        if (!main.db().commit())
            return fail(main.db().lastError().text());
        rep.competitors = int(data.competitors.size());

        // ---- File2: the runs
        if (!runs.db().transaction())
            return fail(runs.db().lastError().text());
        QHash<QString, int> changes2;
        for (int r = 1; r <= runCount && ok; ++r) {
            QStringList created;
            ok = ensureRunTables(runs, r, &created, &err);
            for (const QString& t : created)
                changes2.insert(t, 1);
            RunData run;
            run.run = r;
            for (const RunData& rd : data.runs)
                if (rd.run == r)
                    run = rd;
            RunReport rr;
            if (ok)
                ok = writeRun(runs, run, data.params.timing, &rr, &changes2, &rep.warnings, &err);
            rep.runs << rr;
        }
        if (ok)
            ok = bumpUpdateValues(runs, changes2, &err);
        if (!ok) {
            runs.db().rollback();
            return fail(err);
        }
        if (!runs.db().commit())
            return fail(runs.db().lastError().text());
        // no free pages of the template, smaller archive
        main.exec(QStringLiteral("VACUUM"), nullptr);
        runs.exec(QStringLiteral("VACUUM"), nullptr);
    }

    // ---- the archive, first next to the target, checked, then renamed
    QDir().mkpath(QFileInfo(target).absolutePath());
    const QString part = target + QStringLiteral(".part");
    QFile::remove(part);
    {
        QZipWriter zip(part);
        if (zip.status() != QZipWriter::NoError)
            return fail(QStringLiteral("Cannot write %1.").arg(QDir::toNativeSeparators(target)));
        zip.setCompressionPolicy(QZipWriter::AlwaysCompress);
        for (const QString& name : { kFile1, kFile2 }) {
            QFile f(tmp.filePath(name));
            if (!f.open(QIODevice::ReadOnly))
                return fail(QStringLiteral("Cannot read %1: %2").arg(f.fileName(), f.errorString()));
            zip.addFile(name, f.readAll());
        }
        zip.close();
        if (zip.status() != QZipWriter::NoError) {
            QFile::remove(part);
            return fail(QStringLiteral("Cannot write %1.").arg(QDir::toNativeSeparators(target)));
        }
    }

    // read back with the importer: the same competitors, start lists and results
    SkiProImporter::Result back;
    QStringList diff;
    if (!SkiAlpImporter::read(part, back, &err))
        diff << err;
    else {
        if (back.competitors.size() != rep.competitors)
            diff << QStringLiteral("%1 competitors instead of %2").arg(back.competitors.size()).arg(rep.competitors);
        if (back.runs.size() != runCount)
            diff << QStringLiteral("%1 runs instead of %2").arg(back.runs.size()).arg(runCount);
        for (int i = 0; i < qMin(int(back.runs.size()), int(rep.runs.size())); ++i) {
            if (back.runs.at(i).startList.size() != rep.runs.at(i).startList)
                diff << QStringLiteral("run %1: %2 in the start list instead of %3")
                            .arg(i + 1)
                            .arg(back.runs.at(i).startList.size())
                            .arg(rep.runs.at(i).startList);
            if (back.runs.at(i).infos.size() != rep.runs.at(i).results)
                diff << QStringLiteral("run %1: %2 results instead of %3").arg(i + 1).arg(back.runs.at(i).infos.size()).arg(rep.runs.at(i).results);
        }
    }
    if (!diff.isEmpty()) {
        QFile::remove(part);
        return fail(QStringLiteral("The exported file is not correct: %1").arg(diff.join(QStringLiteral("; "))));
    }
    if (QFile::exists(target) && !QFile::remove(target)) {
        QFile::remove(part);
        return fail(QStringLiteral("Cannot replace %1.").arg(QDir::toNativeSeparators(target)));
    }
    if (!QFile::rename(part, target)) {
        QFile::remove(part);
        return fail(QStringLiteral("Cannot write %1.").arg(QDir::toNativeSeparators(target)));
    }
    if (report)
        *report = rep;
    return true;
}
