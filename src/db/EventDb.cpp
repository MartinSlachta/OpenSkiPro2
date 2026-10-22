#include "EventDb.h"

#include <QAtomicInt>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

static QAtomicInt s_connectionCounter;

EventDb::EventDb()
    : m_connection(QStringLiteral("osp_event_%1").arg(s_connectionCounter.fetchAndAddRelaxed(1)))
{
}

EventDb::~EventDb()
{
    close();
}

bool EventDb::fail(const QString& what)
{
    QSqlDatabase db = QSqlDatabase::database(m_connection, false);
    m_lastError = what + QStringLiteral(": ") + db.lastError().text();
    qWarning("EventDb: %s", qPrintable(m_lastError));
    return false;
}

bool EventDb::open(const QString& path, bool create, QString* error)
{
    close();
    if (!create && !QFileInfo::exists(path)) {
        if (error)
            *error = QStringLiteral("File not found: %1").arg(path);
        return false;
    }
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connection);
    db.setDatabaseName(path);
    if (!db.open()) {
        if (error)
            *error = db.lastError().text();
        QSqlDatabase::removeDatabase(m_connection);
        return false;
    }
    m_path = path;
    exec(QStringLiteral("PRAGMA foreign_keys = OFF"));
    exec(QStringLiteral("PRAGMA synchronous = FULL"));
    if (!createSchema()) {
        if (error)
            *error = m_lastError;
        close();
        return false;
    }
    if (create && meta(QStringLiteral("format_version")).isEmpty()) {
        setMeta(QStringLiteral("format_version"), QString::number(kFormatVersion));
        setMeta(QStringLiteral("application"), QStringLiteral("OpenSkiPro2 " OSP_VERSION));
    }
    const int ver = meta(QStringLiteral("format_version")).toInt();
    if (ver > kFormatVersion) {
        if (error)
            *error = QStringLiteral("The event was created by a newer version of OpenSkiPro2.");
        close();
        return false;
    }
    return true;
}

bool EventDb::peek(const QString& path, QString* title1, QString* title2, QDate* date, int* runCount)
{
    if (!QFileInfo::exists(path))
        return false;
    const QString name = QStringLiteral("osp_peek_%1").arg(s_connectionCounter.fetchAndAddRelaxed(1));
    bool ok = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(path);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (db.open()) {
            QSqlQuery q(db);
            if (q.exec(QStringLiteral("SELECT key, value FROM params WHERE key IN"
                                      " ('general.title1','general.title2','general.date','type.runCount')"))) {
                ok = true;
                while (q.next()) {
                    const QString k = q.value(0).toString();
                    if (k == QLatin1String("general.title1") && title1)
                        *title1 = q.value(1).toString();
                    else if (k == QLatin1String("general.title2") && title2)
                        *title2 = q.value(1).toString();
                    else if (k == QLatin1String("general.date") && date)
                        *date = QDate::fromString(q.value(1).toString(), Qt::ISODate);
                    else if (k == QLatin1String("type.runCount") && runCount)
                        *runCount = q.value(1).toInt();
                }
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(name);
    return ok;
}

void EventDb::close()
{
    if (QSqlDatabase::contains(m_connection)) {
        {
            QSqlDatabase db = QSqlDatabase::database(m_connection, false);
            if (db.isOpen())
                db.close();
        }
        QSqlDatabase::removeDatabase(m_connection);
    }
    m_path.clear();
}

bool EventDb::isOpen() const
{
    return QSqlDatabase::contains(m_connection) && QSqlDatabase::database(m_connection, false).isOpen();
}

bool EventDb::exec(const QString& sql)
{
    QSqlQuery q(QSqlDatabase::database(m_connection));
    if (!q.exec(sql)) {
        m_lastError = q.lastError().text() + QStringLiteral(" [") + sql + QLatin1Char(']');
        qWarning("EventDb: %s", qPrintable(m_lastError));
        return false;
    }
    return true;
}

bool EventDb::createSchema()
{
    const char* stmts[] = {
        "CREATE TABLE IF NOT EXISTS meta(key TEXT PRIMARY KEY, value TEXT)",
        "CREATE TABLE IF NOT EXISTS params(key TEXT PRIMARY KEY, value TEXT)",
        "CREATE TABLE IF NOT EXISTS competitors("
        " id INTEGER PRIMARY KEY AUTOINCREMENT, bib INTEGER, transponder TEXT,"
        " last_name TEXT, first_name TEXT, code TEXT, gender INTEGER, year INTEGER,"
        " category TEXT, nation TEXT, region TEXT, club TEXT, team TEXT, from_list TEXT,"
        " pts_dh INTEGER, pts_sg INTEGER, pts_gs INTEGER, pts_sl INTEGER, pts_sc INTEGER,"
        " custom TEXT)",
        "CREATE TABLE IF NOT EXISTS start_list(run INTEGER, position INTEGER, bib INTEGER,"
        " start_time INTEGER, PRIMARY KEY(run, position))",
        "CREATE TABLE IF NOT EXISTS time_records(run INTEGER, id INTEGER, point INTEGER,"
        " bib INTEGER, flags INTEGER, day_time INTEGER, device INTEGER, device_channel INTEGER,"
        " device_seq INTEGER, note TEXT, PRIMARY KEY(run, id))",
        "CREATE TABLE IF NOT EXISTS time_infos(run INTEGER, bib INTEGER, status INTEGER,"
        " time_ms INTEGER, penalty_ms INTEGER, inter1 INTEGER, inter2 INTEGER, inter3 INTEGER,"
        " manual INTEGER, reason TEXT, PRIMARY KEY(run, bib))",
        "CREATE TABLE IF NOT EXISTS device_paper(run INTEGER, line INTEGER, device INTEGER,"
        " received INTEGER, text TEXT, error INTEGER, PRIMARY KEY(run, line))",
        "CREATE TABLE IF NOT EXISTS run_meta(run INTEGER PRIMARY KEY, reference_day INTEGER)",
        "CREATE TABLE IF NOT EXISTS report_formats(id TEXT PRIMARY KEY, data TEXT)",
    };
    for (const char* s : stmts)
        if (!exec(QString::fromLatin1(s)))
            return false;
    return true;
}

bool EventDb::beginTransaction()
{
    return QSqlDatabase::database(m_connection).transaction();
}

bool EventDb::commit()
{
    return QSqlDatabase::database(m_connection).commit();
}

void EventDb::rollback()
{
    QSqlDatabase::database(m_connection).rollback();
}

QVariantMap EventDb::loadParams()
{
    QVariantMap m;
    QSqlQuery q(QSqlDatabase::database(m_connection));
    if (q.exec(QStringLiteral("SELECT key, value FROM params")))
        while (q.next())
            m.insert(q.value(0).toString(), q.value(1).toString());
    return m;
}

bool EventDb::saveParams(const QVariantMap& params)
{
    QSqlDatabase db = QSqlDatabase::database(m_connection);
    const bool own = db.transaction();
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("DELETE FROM params"))) {
        if (own)
            db.rollback();
        return fail(QStringLiteral("saveParams"));
    }
    q.prepare(QStringLiteral("INSERT INTO params(key, value) VALUES(?, ?)"));
    for (auto it = params.cbegin(); it != params.cend(); ++it) {
        q.addBindValue(it.key());
        const QVariant& v = it.value();
        q.addBindValue(v.typeId() == QMetaType::Bool ? QString(v.toBool() ? QStringLiteral("1") : QStringLiteral("0"))
                                                     : v.toString());
        if (!q.exec()) {
            if (own)
                db.rollback();
            return fail(QStringLiteral("saveParams"));
        }
    }
    return own ? db.commit() : true;
}

QString EventDb::meta(const QString& key) const
{
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.prepare(QStringLiteral("SELECT value FROM meta WHERE key = ?"));
    q.addBindValue(key);
    if (q.exec() && q.next())
        return q.value(0).toString();
    return QString();
}

bool EventDb::setMeta(const QString& key, const QString& value)
{
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO meta(key, value) VALUES(?, ?)"));
    q.addBindValue(key);
    q.addBindValue(value);
    return q.exec() || fail(QStringLiteral("setMeta"));
}

// ---- competitors -------------------------------------------------------------

static const char* kCompetitorColumns =
    "bib, transponder, last_name, first_name, code, gender, year, category, nation, region,"
    " club, team, from_list, pts_dh, pts_sg, pts_gs, pts_sl, pts_sc, custom";

static void bindCompetitor(QSqlQuery& q, const Competitor& c)
{
    q.addBindValue(c.bib);
    q.addBindValue(c.transponder);
    q.addBindValue(c.lastName);
    q.addBindValue(c.firstName);
    q.addBindValue(c.code);
    q.addBindValue(int(c.gender));
    q.addBindValue(c.year);
    q.addBindValue(c.category);
    q.addBindValue(c.nation);
    q.addBindValue(c.region);
    q.addBindValue(c.club);
    q.addBindValue(c.team);
    q.addBindValue(c.fromList);
    for (int p : c.points)
        q.addBindValue(p);
    q.addBindValue(c.custom);
}

QVector<Competitor> EventDb::loadCompetitors()
{
    QVector<Competitor> out;
    QSqlQuery q(QSqlDatabase::database(m_connection));
    if (!q.exec(QStringLiteral("SELECT id, %1 FROM competitors ORDER BY id").arg(QLatin1String(kCompetitorColumns))))
        return out;
    while (q.next()) {
        Competitor c;
        int i = 0;
        c.id = q.value(i++).toInt();
        c.bib = q.value(i++).toInt();
        c.transponder = q.value(i++).toString();
        c.lastName = q.value(i++).toString();
        c.firstName = q.value(i++).toString();
        c.code = q.value(i++).toString();
        c.gender = Gender(q.value(i++).toInt());
        c.year = q.value(i++).toInt();
        c.category = q.value(i++).toString();
        c.nation = q.value(i++).toString();
        c.region = q.value(i++).toString();
        c.club = q.value(i++).toString();
        c.team = q.value(i++).toString();
        c.fromList = q.value(i++).toString();
        for (auto& p : c.points)
            p = q.value(i++).toInt();
        c.custom = q.value(i++).toString();
        out << c;
    }
    return out;
}

bool EventDb::insertCompetitor(Competitor& c)
{
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.prepare(QStringLiteral("INSERT INTO competitors(%1) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)")
                  .arg(QLatin1String(kCompetitorColumns)));
    bindCompetitor(q, c);
    if (!q.exec())
        return fail(QStringLiteral("insertCompetitor"));
    c.id = q.lastInsertId().toInt();
    return true;
}

bool EventDb::updateCompetitor(const Competitor& c)
{
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.prepare(QStringLiteral(
        "UPDATE competitors SET bib=?, transponder=?, last_name=?, first_name=?, code=?, gender=?, year=?,"
        " category=?, nation=?, region=?, club=?, team=?, from_list=?, pts_dh=?, pts_sg=?, pts_gs=?,"
        " pts_sl=?, pts_sc=?, custom=? WHERE id=?"));
    bindCompetitor(q, c);
    q.addBindValue(c.id);
    return q.exec() || fail(QStringLiteral("updateCompetitor"));
}

bool EventDb::deleteCompetitor(int id)
{
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.prepare(QStringLiteral("DELETE FROM competitors WHERE id=?"));
    q.addBindValue(id);
    return q.exec() || fail(QStringLiteral("deleteCompetitor"));
}

bool EventDb::deleteAllCompetitors()
{
    return exec(QStringLiteral("DELETE FROM competitors"));
}

// ---- runs ------------------------------------------------------------------------

bool EventDb::loadRun(RunData& run)
{
    QSqlDatabase db = QSqlDatabase::database(m_connection);
    const int r = run.run;
    run.startList.clear();
    run.records.clear();
    run.infos.clear();
    run.paper.clear();
    run.referenceDay = -1;

    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT bib, start_time FROM start_list WHERE run=? ORDER BY position"));
    q.addBindValue(r);
    if (!q.exec())
        return fail(QStringLiteral("loadRun"));
    while (q.next()) {
        StartEntry e;
        e.bib = q.value(0).toInt();
        e.startTimeUs = q.value(1).isNull() ? TimeUtil::kNoTime : q.value(1).toLongLong();
        run.startList << e;
    }

    q.prepare(QStringLiteral("SELECT id, point, bib, flags, day_time, device, device_channel, device_seq, note"
                             " FROM time_records WHERE run=? ORDER BY id"));
    q.addBindValue(r);
    if (!q.exec())
        return fail(QStringLiteral("loadRun"));
    while (q.next()) {
        TimeRecord t;
        t.id = q.value(0).toInt();
        t.point = q.value(1).toInt();
        t.bib = q.value(2).toInt();
        t.flags = q.value(3).toInt();
        t.dayTimeUs = q.value(4).toLongLong();
        t.device = q.value(5).toInt();
        t.deviceChannel = q.value(6).toInt();
        t.deviceSeq = q.value(7).toInt();
        t.note = q.value(8).toString();
        run.records << t;
    }

    q.prepare(QStringLiteral("SELECT bib, status, time_ms, penalty_ms, inter1, inter2, inter3, manual, reason"
                             " FROM time_infos WHERE run=?"));
    q.addBindValue(r);
    if (!q.exec())
        return fail(QStringLiteral("loadRun"));
    while (q.next()) {
        TimeInfo t;
        t.bib = q.value(0).toInt();
        t.status = ResultStatus(q.value(1).toInt());
        t.timeMs = q.value(2).toLongLong();
        t.penaltyMs = q.value(3).toLongLong();
        for (int i = 0; i < kMaxInter; ++i)
            t.interMs[size_t(i)] = q.value(4 + i).toLongLong();
        t.manual = q.value(7).toInt() != 0;
        t.reason = q.value(8).toString();
        run.infos.insert(t.bib, t);
    }

    q.prepare(QStringLiteral("SELECT line, device, received, text, error FROM device_paper WHERE run=? ORDER BY line"));
    q.addBindValue(r);
    if (!q.exec())
        return fail(QStringLiteral("loadRun"));
    while (q.next()) {
        PaperLine p;
        p.seq = q.value(0).toInt();
        p.device = q.value(1).toInt();
        p.receivedUs = q.value(2).toLongLong();
        p.text = q.value(3).toString();
        p.error = q.value(4).toInt() != 0;
        run.paper << p;
    }

    q.prepare(QStringLiteral("SELECT reference_day FROM run_meta WHERE run=?"));
    q.addBindValue(r);
    if (q.exec() && q.next())
        run.referenceDay = q.value(0).toInt();
    return true;
}

bool EventDb::saveStartList(int run, const QVector<StartEntry>& list)
{
    QSqlDatabase db = QSqlDatabase::database(m_connection);
    const bool own = db.transaction();
    QSqlQuery q(db);
    q.prepare(QStringLiteral("DELETE FROM start_list WHERE run=?"));
    q.addBindValue(run);
    bool ok = q.exec();
    q.prepare(QStringLiteral("INSERT INTO start_list(run, position, bib, start_time) VALUES(?,?,?,?)"));
    for (int i = 0; ok && i < list.size(); ++i) {
        q.addBindValue(run);
        q.addBindValue(i + 1);
        q.addBindValue(list.at(i).bib);
        q.addBindValue(list.at(i).startTimeUs < 0 ? QVariant() : QVariant(list.at(i).startTimeUs));
        ok = q.exec();
    }
    if (!ok) {
        if (own)
            db.rollback();
        return fail(QStringLiteral("saveStartList"));
    }
    return own ? db.commit() : true;
}

bool EventDb::saveRecord(int run, const TimeRecord& t)
{
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO time_records(run, id, point, bib, flags, day_time, device,"
                             " device_channel, device_seq, note) VALUES(?,?,?,?,?,?,?,?,?,?)"));
    q.addBindValue(run);
    q.addBindValue(t.id);
    q.addBindValue(t.point);
    q.addBindValue(t.bib);
    q.addBindValue(t.flags);
    q.addBindValue(t.dayTimeUs);
    q.addBindValue(t.device);
    q.addBindValue(t.deviceChannel);
    q.addBindValue(t.deviceSeq);
    q.addBindValue(t.note);
    return q.exec() || fail(QStringLiteral("saveRecord"));
}

bool EventDb::deleteRecord(int run, int id)
{
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.prepare(QStringLiteral("DELETE FROM time_records WHERE run=? AND id=?"));
    q.addBindValue(run);
    q.addBindValue(id);
    return q.exec() || fail(QStringLiteral("deleteRecord"));
}

bool EventDb::saveInfo(int run, const TimeInfo& t)
{
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO time_infos(run, bib, status, time_ms, penalty_ms, inter1,"
                             " inter2, inter3, manual, reason) VALUES(?,?,?,?,?,?,?,?,?,?)"));
    q.addBindValue(run);
    q.addBindValue(t.bib);
    q.addBindValue(int(t.status));
    q.addBindValue(t.timeMs);
    q.addBindValue(t.penaltyMs);
    for (int i = 0; i < kMaxInter; ++i)
        q.addBindValue(t.interMs[size_t(i)]);
    q.addBindValue(t.manual ? 1 : 0);
    q.addBindValue(t.reason);
    return q.exec() || fail(QStringLiteral("saveInfo"));
}

bool EventDb::deleteInfo(int run, int bib)
{
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.prepare(QStringLiteral("DELETE FROM time_infos WHERE run=? AND bib=?"));
    q.addBindValue(run);
    q.addBindValue(bib);
    return q.exec() || fail(QStringLiteral("deleteInfo"));
}

bool EventDb::replaceInfos(int run, const QHash<int, TimeInfo>& infos)
{
    QSqlDatabase db = QSqlDatabase::database(m_connection);
    const bool own = db.transaction();
    QSqlQuery q(db);
    q.prepare(QStringLiteral("DELETE FROM time_infos WHERE run=?"));
    q.addBindValue(run);
    bool ok = q.exec();
    for (auto it = infos.cbegin(); ok && it != infos.cend(); ++it)
        ok = saveInfo(run, it.value());
    if (!ok) {
        if (own)
            db.rollback();
        return fail(QStringLiteral("replaceInfos"));
    }
    return own ? db.commit() : true;
}

bool EventDb::appendPaper(int run, int line, const PaperLine& p)
{
    QSqlQuery q(QSqlDatabase::database(m_connection));
    // INSERT only: a line of the paper is never replaced
    q.prepare(QStringLiteral("INSERT INTO device_paper(run, line, device, received, text, error) VALUES(?,?,?,?,?,?)"));
    q.addBindValue(run);
    q.addBindValue(line);
    q.addBindValue(p.device);
    q.addBindValue(p.receivedUs);
    q.addBindValue(p.text);
    q.addBindValue(p.error ? 1 : 0);
    return q.exec() || fail(QStringLiteral("appendPaper"));
}

bool EventDb::setReferenceDay(int run, int day)
{
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO run_meta(run, reference_day) VALUES(?,?)"));
    q.addBindValue(run);
    q.addBindValue(day);
    return q.exec() || fail(QStringLiteral("setReferenceDay"));
}

bool EventDb::clearRun(int run, bool startList, bool timing)
{
    QSqlDatabase db = QSqlDatabase::database(m_connection);
    const bool own = db.transaction();
    QSqlQuery q(db);
    QStringList tables;
    if (startList)
        tables << QStringLiteral("start_list");
    if (timing)
        tables << QStringLiteral("time_records") << QStringLiteral("time_infos") << QStringLiteral("device_paper")
               << QStringLiteral("run_meta");
    bool ok = true;
    for (const QString& t : tables) {
        q.prepare(QStringLiteral("DELETE FROM %1 WHERE run=?").arg(t));
        q.addBindValue(run);
        ok = ok && q.exec();
    }
    if (!ok) {
        if (own)
            db.rollback();
        return fail(QStringLiteral("clearRun"));
    }
    return own ? db.commit() : true;
}

QString EventDb::reportFormat(const QString& id)
{
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.prepare(QStringLiteral("SELECT data FROM report_formats WHERE id=?"));
    q.addBindValue(id);
    if (q.exec() && q.next())
        return q.value(0).toString();
    return QString();
}

bool EventDb::setReportFormat(const QString& id, const QString& data)
{
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO report_formats(id, data) VALUES(?,?)"));
    q.addBindValue(id);
    q.addBindValue(data);
    return q.exec() || fail(QStringLiteral("setReportFormat"));
}
