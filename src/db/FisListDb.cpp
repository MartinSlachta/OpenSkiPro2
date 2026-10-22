#include "FisListDb.h"
#include "core/Competitor.h"

#include <QAtomicInt>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMap>
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringDecoder>
#include <QVariant>
#include <QtCore/private/qzipreader_p.h>

// ---- reading -------------------------------------------------------------------

namespace {

// Tab separated table with a header row, columns addressed by name (case insensitive).
struct Table {
    QHash<QString, int> columns;
    QList<QStringList> rows;

    QString value(const QStringList& row, const char* column) const
    {
        const int i = columns.value(QString::fromLatin1(column).toLower(), -1);
        return i >= 0 && i < row.size() ? row.at(i).trimmed() : QString();
    }
    bool has(const char* column) const { return columns.contains(QString::fromLatin1(column).toLower()); }
};

QString decode(const QByteArray& data)
{
    QStringDecoder utf8(QStringDecoder::Utf8);
    QString text = utf8.decode(data);
    if (utf8.hasError())
        text = QString::fromLatin1(data);
    if (text.startsWith(QChar(0xFEFF)))
        text.remove(0, 1);
    return text;
}

Table parseTable(const QByteArray& data)
{
    Table t;
    const QStringList lines = decode(data).split(QLatin1Char('\n'));
    bool header = true;
    for (QString line : lines) {
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);
        if (line.trimmed().isEmpty())
            continue;
        const QStringList cells = line.split(QLatin1Char('\t'));
        if (header) {
            for (int i = 0; i < cells.size(); ++i)
                t.columns.insert(cells.at(i).trimmed().toLower(), i);
            header = false;
        } else {
            t.rows << cells;
        }
    }
    return t;
}

QDate date(const QString& s)
{
    return QDate::fromString(s.left(10), Qt::ISODate);
}

double number(const QString& s)
{
    bool ok = false;
    const double v = QString(s).replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&ok);
    return ok ? v : 0.0;
}

// FIS points -> hundredths; no points (empty, 9999.99) -> -1
int points(const QString& s)
{
    bool ok = false;
    const double v = QString(s).replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&ok);
    if (!ok || v < 0 || v >= 9999.99)
        return -1;
    return int(v * 100.0 + 0.5);
}

int disciplineIndex(const QString& code)
{
    for (int i = 0; i < kDisciplineCount; ++i)
        if (code.compare(fisDisciplineCode(Discipline(i)), Qt::CaseInsensitive) == 0)
            return i;
    return -1;
}

// Files of one list: prefix "AL1627" -> kind ("hdr", "com", ...) -> content
using ListFiles = QMap<QString, QMap<QString, QByteArray>>;

const QRegularExpression& fileNamePattern()
{
    static const QRegularExpression re(QStringLiteral("^(AL\\d+)(hdr|cat|dis|com|pts)\\.csv$"),
                                       QRegularExpression::CaseInsensitiveOption);
    return re;
}

void addFile(ListFiles& files, const QString& fileName, const QByteArray& data)
{
    const auto m = fileNamePattern().match(QFileInfo(fileName).fileName());
    if (m.hasMatch())
        files[m.captured(1).toUpper()][m.captured(2).toLower()] = data;
}

bool collectFiles(const QString& path, ListFiles& files, QString* preferredPrefix, QString* error)
{
    const QFileInfo fi(path);
    if (fi.isFile() && fi.suffix().compare(QLatin1String("zip"), Qt::CaseInsensitive) == 0) {
        QZipReader zip(path);
        if (!zip.isReadable() || zip.status() != QZipReader::NoError) {
            if (error)
                *error = QCoreApplication::translate("FisList", "Cannot read the archive %1.").arg(QDir::toNativeSeparators(path));
            return false;
        }
        for (const auto& e : zip.fileInfoList())
            if (e.isFile && fileNamePattern().match(QFileInfo(e.filePath).fileName()).hasMatch())
                addFile(files, e.filePath, zip.fileData(e.filePath));
        return true;
    }
    QDir dir(fi.isDir() ? path : fi.absolutePath());
    if (!dir.exists()) {
        if (error)
            *error = QCoreApplication::translate("FisList", "The file %1 doesn't exist.").arg(QDir::toNativeSeparators(path));
        return false;
    }
    if (fi.isFile()) {
        const auto m = fileNamePattern().match(fi.fileName());
        if (m.hasMatch() && preferredPrefix)
            *preferredPrefix = m.captured(1).toUpper();
    }
    for (const QFileInfo& f : dir.entryInfoList(QDir::Files)) {
        if (!fileNamePattern().match(f.fileName()).hasMatch())
            continue;
        QFile file(f.absoluteFilePath());
        if (file.open(QIODevice::ReadOnly))
            addFile(files, f.fileName(), file.readAll());
    }
    return true;
}

FisListHeader parseHeader(const QByteArray& data)
{
    FisListHeader h;
    const Table t = parseTable(data);
    if (t.rows.isEmpty())
        return h;
    const QStringList& r = t.rows.first();
    h.listId = t.value(r, "Listid").toInt();
    h.season = t.value(r, "Seasoncode").toInt();
    h.number = t.value(r, "Listnumber").toInt();
    h.name = t.value(r, "Listname");
    h.calculationDate = date(t.value(r, "Calculationdate"));
    h.startRaceDate = date(t.value(r, "Startracedate"));
    h.endRaceDate = date(t.value(r, "Endracedate"));
    h.validFrom = date(t.value(r, "Validfrom"));
    h.validTo = date(t.value(r, "Validto"));
    h.lastUpdate = t.value(r, "Lastupdate");
    return h;
}

} // namespace

namespace FisListReader {

bool read(const QString& path, FisList& list, QString* error)
{
    list = FisList();
    ListFiles files;
    QString prefix;
    if (!collectFiles(path, files, &prefix, error))
        return false;
    // several lists in a folder: the selected file's one, otherwise the most recent
    if (prefix.isEmpty() || !files.contains(prefix)) {
        prefix.clear();
        int best = -1;
        for (auto it = files.cbegin(); it != files.cend(); ++it) {
            const int id = parseHeader(it.value().value(QStringLiteral("hdr"))).listId;
            if (id > best) {
                best = id;
                prefix = it.key();
            }
        }
    }
    const QMap<QString, QByteArray> f = files.value(prefix);
    for (const char* kind : { "hdr", "com", "pts" }) {
        if (!f.contains(QLatin1String(kind))) {
            if (error)
                *error = prefix.isEmpty()
                    ? QCoreApplication::translate("FisList", "No FIS alpine points list found in %1.").arg(QDir::toNativeSeparators(path))
                    : QCoreApplication::translate("FisList", "The file %1 is missing in %2.").arg(prefix + QLatin1String(kind) + QLatin1String(".csv"),
                                                             QDir::toNativeSeparators(path));
            return false;
        }
    }

    list.header = parseHeader(f.value(QStringLiteral("hdr")));
    if (list.header.listId == 0 && list.header.number == 0) {
        if (error)
            *error = QCoreApplication::translate("FisList", "The header of the FIS list (%1) cannot be read.").arg(prefix + QLatin1String("hdr.csv"));
        return false;
    }

    const Table cat = parseTable(f.value(QStringLiteral("cat")));
    for (const auto& r : cat.rows) {
        FisCategoryPenalty p;
        p.catcode = cat.value(r, "Catcode");
        if (p.catcode.isEmpty())
            continue;
        p.minPenalty = number(cat.value(r, "Minfispoints"));
        p.maxPenalty = number(cat.value(r, "Maxfispoints"));
        list.penalties << p;
    }

    const Table dis = parseTable(f.value(QStringLiteral("dis")));
    for (const auto& r : dis.rows) {
        FisDisciplineParams d;
        d.discipline = dis.value(r, "Disciplinecode");
        if (d.discipline.isEmpty())
            continue;
        d.gender = parseGender(dis.value(r, "Gender"));
        d.zValue = number(dis.value(r, "Zvalue"));
        d.fValue = number(dis.value(r, "Fvalue"));
        d.maxPoints = number(dis.value(r, "Maxpoints"));
        for (int i = 0; i < 5; ++i)
            d.adders[size_t(i)] = number(dis.value(r, QStringLiteral("Adder%1").arg(i).toLatin1().constData()));
        list.disciplines << d;
    }

    const Table com = parseTable(f.value(QStringLiteral("com")));
    if (!com.has("Competitorid") || !com.has("Fiscode")) {
        if (error)
            *error = QCoreApplication::translate("FisList", "The competitors of the FIS list (%1) cannot be read.").arg(prefix + QLatin1String("com.csv"));
        return false;
    }
    QHash<int, int> byId;
    list.competitors.reserve(com.rows.size());
    for (const auto& r : com.rows) {
        const QString sector = com.value(r, "Sectorcode");
        if (!sector.isEmpty() && sector.compare(QLatin1String("AL"), Qt::CaseInsensitive) != 0)
            continue;
        FisCompetitor c;
        c.competitorId = com.value(r, "Competitorid").toInt();
        c.fisCode = com.value(r, "Fiscode");
        if (c.competitorId == 0 || c.fisCode.isEmpty())
            continue;
        c.lastName = com.value(r, "Lastname");
        c.firstName = com.value(r, "Firstname");
        c.gender = parseGender(com.value(r, "Gender"));
        c.year = com.value(r, "Birthdate").left(4).toInt();
        c.nation = com.value(r, "Nationcode");
        byId.insert(c.competitorId, int(list.competitors.size()));
        list.competitors << c;
    }

    const Table pts = parseTable(f.value(QStringLiteral("pts")));
    for (const auto& r : pts.rows) {
        const int d = disciplineIndex(pts.value(r, "Disciplinecode"));
        const int idx = byId.value(pts.value(r, "Competitorid").toInt(), -1);
        if (d < 0 || idx < 0)
            continue;
        list.competitors[idx].points[size_t(d)] = points(pts.value(r, "Fispoints"));
    }

    list.source = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
    list.importedAt = QDateTime::currentDateTime();
    return true;
}

} // namespace FisListReader

// ---- storage -------------------------------------------------------------------

namespace {

constexpr int kFormatVersion = 1;
QAtomicInt s_connectionCounter;

// Scoped connection to the list database.
class Connection {
public:
    Connection(const QString& path, bool readOnly)
        : m_name(QStringLiteral("osp_fislist_%1").arg(s_connectionCounter.fetchAndAddRelaxed(1)))
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_name);
        db.setDatabaseName(path);
        if (readOnly)
            db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        m_ok = db.open();
        if (!m_ok)
            m_error = db.lastError().text();
    }
    ~Connection()
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

private:
    QString m_name;
    bool m_ok = false;
    QString m_error;
};

bool fail(const QSqlQuery& q, QString* error)
{
    if (error)
        *error = q.lastError().text();
    return false;
}

QVariant pointsValue(int hundredths)
{
    return hundredths < 0 ? QVariant(QMetaType(QMetaType::Int)) : QVariant(hundredths);
}

QString dateText(const QDate& d)
{
    return d.isValid() ? d.toString(Qt::ISODate) : QString();
}

bool readHeader(QSqlDatabase db, FisList& list, QString* error)
{
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("SELECT key, value FROM header")))
        return fail(q, error);
    QHash<QString, QString> h;
    while (q.next())
        h.insert(q.value(0).toString(), q.value(1).toString());
    list.header.listId = h.value(QStringLiteral("listId")).toInt();
    list.header.season = h.value(QStringLiteral("season")).toInt();
    list.header.number = h.value(QStringLiteral("number")).toInt();
    list.header.name = h.value(QStringLiteral("name"));
    list.header.calculationDate = date(h.value(QStringLiteral("calculationDate")));
    list.header.startRaceDate = date(h.value(QStringLiteral("startRaceDate")));
    list.header.endRaceDate = date(h.value(QStringLiteral("endRaceDate")));
    list.header.validFrom = date(h.value(QStringLiteral("validFrom")));
    list.header.validTo = date(h.value(QStringLiteral("validTo")));
    list.header.lastUpdate = h.value(QStringLiteral("lastUpdate"));
    list.source = h.value(QStringLiteral("source"));
    list.importedAt = QDateTime::fromString(h.value(QStringLiteral("importedAt")), Qt::ISODate);
    return true;
}

} // namespace

namespace FisListDb {

bool save(const QString& dbPath, const FisList& list, QString* error)
{
    QDir().mkpath(QFileInfo(dbPath).absolutePath());
    Connection c(dbPath, false);
    if (!c.ok()) {
        if (error)
            *error = c.error();
        return false;
    }
    QSqlDatabase db = c.db();
    QSqlQuery q(db);
    const QStringList schema = {
        QStringLiteral("CREATE TABLE IF NOT EXISTS header (key TEXT PRIMARY KEY, value TEXT)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS penalties (catcode TEXT PRIMARY KEY, min_penalty REAL, max_penalty REAL)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS disciplines (discipline TEXT, gender INTEGER, z_value REAL, f_value REAL,"
                       " max_points REAL, adder0 REAL, adder1 REAL, adder2 REAL, adder3 REAL, adder4 REAL,"
                       " PRIMARY KEY (discipline, gender))"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS competitors (competitor_id INTEGER PRIMARY KEY, fis_code TEXT,"
                       " last_name TEXT, first_name TEXT, gender INTEGER, year INTEGER, nation TEXT,"
                       " points_dh INTEGER, points_sg INTEGER, points_gs INTEGER, points_sl INTEGER, points_ac INTEGER)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS competitors_code ON competitors (fis_code)"),
    };
    for (const QString& sql : schema)
        if (!q.exec(sql))
            return fail(q, error);
    if (!db.transaction()) {
        if (error)
            *error = db.lastError().text();
        return false;
    }
    auto abort = [&](const QSqlQuery& failed) {
        const bool r = fail(failed, error);
        db.rollback();
        return r;
    };
    for (const char* t : { "header", "penalties", "disciplines", "competitors" })
        if (!q.exec(QStringLiteral("DELETE FROM %1").arg(QLatin1String(t))))
            return abort(q);

    const FisListHeader& h = list.header;
    const QList<QPair<QString, QString>> header = {
        { QStringLiteral("formatVersion"), QString::number(kFormatVersion) },
        { QStringLiteral("listId"), QString::number(h.listId) },
        { QStringLiteral("season"), QString::number(h.season) },
        { QStringLiteral("number"), QString::number(h.number) },
        { QStringLiteral("name"), h.name },
        { QStringLiteral("calculationDate"), dateText(h.calculationDate) },
        { QStringLiteral("startRaceDate"), dateText(h.startRaceDate) },
        { QStringLiteral("endRaceDate"), dateText(h.endRaceDate) },
        { QStringLiteral("validFrom"), dateText(h.validFrom) },
        { QStringLiteral("validTo"), dateText(h.validTo) },
        { QStringLiteral("lastUpdate"), h.lastUpdate },
        { QStringLiteral("source"), list.source },
        { QStringLiteral("importedAt"), list.importedAt.toString(Qt::ISODate) },
    };
    q.prepare(QStringLiteral("INSERT INTO header (key, value) VALUES (?, ?)"));
    for (const auto& kv : header) {
        q.addBindValue(kv.first);
        q.addBindValue(kv.second);
        if (!q.exec())
            return abort(q);
    }
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO penalties (catcode, min_penalty, max_penalty) VALUES (?, ?, ?)"));
    for (const auto& p : list.penalties) {
        q.addBindValue(p.catcode);
        q.addBindValue(p.minPenalty);
        q.addBindValue(p.maxPenalty);
        if (!q.exec())
            return abort(q);
    }
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO disciplines (discipline, gender, z_value, f_value, max_points,"
                             " adder0, adder1, adder2, adder3, adder4) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    for (const auto& d : list.disciplines) {
        q.addBindValue(d.discipline);
        q.addBindValue(int(d.gender));
        q.addBindValue(d.zValue);
        q.addBindValue(d.fValue);
        q.addBindValue(d.maxPoints);
        for (double a : d.adders)
            q.addBindValue(a);
        if (!q.exec())
            return abort(q);
    }
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO competitors (competitor_id, fis_code, last_name, first_name, gender,"
                             " year, nation, points_dh, points_sg, points_gs, points_sl, points_ac)"
                             " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    for (const auto& x : list.competitors) {
        q.addBindValue(x.competitorId);
        q.addBindValue(x.fisCode);
        q.addBindValue(x.lastName);
        q.addBindValue(x.firstName);
        q.addBindValue(int(x.gender));
        q.addBindValue(x.year);
        q.addBindValue(x.nation);
        for (int p : x.points)
            q.addBindValue(pointsValue(p));
        if (!q.exec())
            return abort(q);
    }
    if (!db.commit()) {
        if (error)
            *error = db.lastError().text();
        db.rollback();
        return false;
    }
    return true;
}

bool loadHeader(const QString& dbPath, FisList& list, int* competitorCount, QString* error)
{
    list = FisList();
    if (competitorCount)
        *competitorCount = 0;
    if (!QFileInfo::exists(dbPath))
        return true;
    Connection c(dbPath, true);
    if (!c.ok()) {
        if (error)
            *error = c.error();
        return false;
    }
    if (!readHeader(c.db(), list, error))
        return false;
    QSqlQuery q(c.db());
    if (!q.exec(QStringLiteral("SELECT catcode, min_penalty, max_penalty FROM penalties")))
        return fail(q, error);
    while (q.next())
        list.penalties << FisCategoryPenalty{ q.value(0).toString(), q.value(1).toDouble(), q.value(2).toDouble() };
    if (!q.exec(QStringLiteral("SELECT discipline, gender, z_value, f_value, max_points,"
                               " adder0, adder1, adder2, adder3, adder4 FROM disciplines")))
        return fail(q, error);
    while (q.next()) {
        FisDisciplineParams d;
        d.discipline = q.value(0).toString();
        d.gender = Gender(q.value(1).toInt());
        d.zValue = q.value(2).toDouble();
        d.fValue = q.value(3).toDouble();
        d.maxPoints = q.value(4).toDouble();
        for (int i = 0; i < 5; ++i)
            d.adders[size_t(i)] = q.value(5 + i).toDouble();
        list.disciplines << d;
    }
    if (competitorCount) {
        if (!q.exec(QStringLiteral("SELECT COUNT(*) FROM competitors")))
            return fail(q, error);
        if (q.next())
            *competitorCount = q.value(0).toInt();
    }
    return true;
}

bool load(const QString& dbPath, FisList& list, QString* error)
{
    if (!loadHeader(dbPath, list, nullptr, error))
        return false;
    if (!QFileInfo::exists(dbPath))
        return true;
    Connection c(dbPath, true);
    if (!c.ok()) {
        if (error)
            *error = c.error();
        return false;
    }
    QSqlQuery q(c.db());
    q.setForwardOnly(true);
    if (!q.exec(QStringLiteral("SELECT competitor_id, fis_code, last_name, first_name, gender, year, nation,"
                               " points_dh, points_sg, points_gs, points_sl, points_ac FROM competitors"
                               " ORDER BY last_name, first_name")))
        return fail(q, error);
    while (q.next()) {
        FisCompetitor x;
        x.competitorId = q.value(0).toInt();
        x.fisCode = q.value(1).toString();
        x.lastName = q.value(2).toString();
        x.firstName = q.value(3).toString();
        x.gender = Gender(q.value(4).toInt());
        x.year = q.value(5).toInt();
        x.nation = q.value(6).toString();
        for (int i = 0; i < kDisciplineCount; ++i)
            x.points[size_t(i)] = q.value(7 + i).isNull() ? -1 : q.value(7 + i).toInt();
        list.competitors << x;
    }
    return true;
}

} // namespace FisListDb
