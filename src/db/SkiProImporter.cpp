#include "SkiProImporter.h"
#include "core/Event.h"

#include <QAtomicInt>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QStringDecoder>
#include <QVariant>
#include <memory>

namespace {

QAtomicInt s_counter;

QString decodeText(const QByteArray& bytes);

// RAII read-only SQLite connection
class ReadDb {
public:
    explicit ReadDb(const QString& path)
        : m_name(QStringLiteral("osp_skipro_%1").arg(s_counter.fetchAndAddRelaxed(1)))
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_name);
        db.setDatabaseName(path);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        m_ok = db.open();
        if (!m_ok)
            m_error = db.lastError().text();
    }
    ~ReadDb()
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

    QHash<QString, QString> keyValues(const QString& table) const
    {
        QHash<QString, QString> m;
        if (!hasTable(table))
            return m;
        QSqlQuery q(db());
        // raw bytes: some values (filters) are stored in Windows-1250 even in UTF-8 events
        if (q.exec(QStringLiteral("SELECT C_PARAM, CAST(C_VALUE AS BLOB) FROM %1").arg(table)))
            while (q.next())
                m.insert(q.value(0).toString().trimmed(), decodeText(q.value(1).toByteArray()));
        return m;
    }

private:
    QString m_name;
    bool m_ok = false;
    QString m_error;
};

// UTF-8 when valid, otherwise Windows-1250 (the ANSI code page of the Slovak / Czech SkiPro installations)
QString decodeText(const QByteArray& bytes)
{
    QStringDecoder utf8(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    const QString s = utf8(bytes);
    if (!utf8.hasError())
        return s;
    static const char16_t high[128] = {
        0x20AC, 0xFFFD, 0x201A, 0xFFFD, 0x201E, 0x2026, 0x2020, 0x2021, 0xFFFD, 0x2030, 0x0160, 0x2039, 0x015A, 0x0164, 0x017D, 0x0179,
        0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0xFFFD, 0x2122, 0x0161, 0x203A, 0x015B, 0x0165, 0x017E, 0x017A,
        0x00A0, 0x02C7, 0x02D8, 0x0141, 0x00A4, 0x0104, 0x00A6, 0x00A7, 0x00A8, 0x00A9, 0x015E, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x017B,
        0x00B0, 0x00B1, 0x02DB, 0x0142, 0x00B4, 0x00B5, 0x00B6, 0x00B7, 0x00B8, 0x0105, 0x015F, 0x00BB, 0x013D, 0x02DD, 0x013E, 0x017C,
        0x0154, 0x00C1, 0x00C2, 0x0102, 0x00C4, 0x0139, 0x0106, 0x00C7, 0x010C, 0x00C9, 0x0118, 0x00CB, 0x011A, 0x00CD, 0x00CE, 0x010E,
        0x0110, 0x0143, 0x0147, 0x00D3, 0x00D4, 0x0150, 0x00D6, 0x00D7, 0x0158, 0x016E, 0x00DA, 0x0170, 0x00DC, 0x00DD, 0x0162, 0x00DF,
        0x0155, 0x00E1, 0x00E2, 0x0103, 0x00E4, 0x013A, 0x0107, 0x00E7, 0x010D, 0x00E9, 0x0119, 0x00EB, 0x011B, 0x00ED, 0x00EE, 0x010F,
        0x0111, 0x0144, 0x0148, 0x00F3, 0x00F4, 0x0151, 0x00F6, 0x00F7, 0x0159, 0x016F, 0x00FA, 0x0171, 0x00FC, 0x00FD, 0x0163, 0x02D9,
    };
    QString out;
    out.reserve(bytes.size());
    for (char ch : bytes) {
        const auto b = static_cast<unsigned char>(ch);
        out.append(b < 0x80 ? QChar(b) : QChar(high[b - 0x80]));
    }
    return out;
}

// SkiAlp Pro numbers the competitor fields differently from SkiPro 2 (whose numbering we use).
int fieldFromSource(int id, SkiProImporter::Format format)
{
    if (format == SkiProImporter::Format::SkiPro2)
        return id;
    static const QHash<int, int> skiAlp = {
        { 1, CompField::Bib },         { 2, CompField::Transponder }, { 4, CompField::LastName },  { 5, CompField::FirstName },
        { 6, CompField::Code },        { 9, CompField::Gender },      { 10, CompField::Year },     { 11, CompField::Category },
        { 13, CompField::Nation },     { 14, CompField::Region },     { 19, CompField::Club },     { 20, CompField::Team },
        { 22, CompField::FromList },   { 29, CompField::PointsDH },   { 30, CompField::PointsSG }, { 31, CompField::PointsGS },
        { 32, CompField::PointsSL },   { 33, CompField::PointsAC },   { 74, CompField::Name },
    };
    return skiAlp.value(id, 0);
}

int fieldToSource(int id, SkiProImporter::Format format)
{
    if (format == SkiProImporter::Format::SkiPro2)
        return id;
    for (int s = 1; s <= 80; ++s)
        if (fieldFromSource(s, format) == id)
            return s;
    return 0;
}

// Filter lists: FILTERS_LISTn_INDEXk_COUNT / _LINEm, a group in each index:
// "Label\CR\v1\CR\<any>\CR\<field>\CR\<op>\CR\<value>\CR\<value2>\CR\<x>..." (5 parts per condition)
OutputGroup parseFilter(const QString& text, SkiProImporter::Format format)
{
    const QStringList parts = text.split(QStringLiteral("\\CR\\"));
    OutputGroup g;
    g.label = parts.value(0).trimmed();
    int i = 1;
    if (parts.value(i).startsWith(QLatin1Char('v')))
        ++i;
    g.anyMode = parts.value(i++).toInt() != 0;
    for (; i + 2 < parts.size(); i += 5) {
        GroupCriterion c;
        c.field = fieldFromSource(parts.value(i).toInt(), format);
        c.op = parts.value(i + 1).toInt();
        c.value = parts.value(i + 2);
        if (c.op == GroupCriterion::Between)
            c.value2 = parts.value(i + 3);
        if (c.field > 0 && c.op >= GroupCriterion::Equal && c.op <= GroupCriterion::Less)
            g.criteria << c;
    }
    return g;
}

// text stored in PREFIX_LINE1, PREFIX_LINE2, ...
QString joinLines(const QHash<QString, QString>& p, const QString& prefix)
{
    QString text;
    for (int l = 1; l < 100 && p.contains(prefix + QStringLiteral("_LINE%1").arg(l)); ++l)
        text += p.value(prefix + QStringLiteral("_LINE%1").arg(l));
    return text;
}

QList<OutputGroup> readFilterList(const QHash<QString, QString>& p, const QString& prefix, SkiProImporter::Format format)
{
    QList<OutputGroup> groups;
    for (int idx = 1; idx < 500; ++idx) {
        const QString base = QStringLiteral("%1_INDEX%2").arg(prefix).arg(idx);
        if (!p.contains(base + QStringLiteral("_COUNT")))
            break;
        const OutputGroup g = parseFilter(joinLines(p, base), format);
        if (!g.label.isEmpty())
            groups << g;
    }
    return groups;
}

// SkiPro is an ANSI program: texts imported from UTF-8 files are sometimes stored
// double encoded ("JasnÃ¡" instead of "Jasná"). Repairs such strings.
QString fixEncoding(const QString& s)
{
    bool suspicious = false;
    for (QChar c : s) {
        if (c.unicode() > 0xFF)
            return s; // real unicode text, keep it
        if (c.unicode() == 0xC3 || c.unicode() == 0xC2 || c.unicode() == 0xC4 || c.unicode() == 0xC5)
            suspicious = true;
    }
    if (!suspicious)
        return s;
    const QByteArray bytes = s.toLatin1();
    QStringDecoder utf8(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    const QString fixed = utf8(bytes);
    return utf8.hasError() ? s : fixed;
}

int pointsValue(const QVariant& v)
{
    if (v.isNull())
        return -1;
    const int p = v.toInt();
    return (p <= 0 || p >= 99990) ? -1 : p;
}

QFont skiproFont(const QString& s, const QFont& fallback)
{
    // "Arial;14;clBlack;1000"  -> family;size;color;style flags (bold italic underline strike)
    const QStringList p = s.split(QLatin1Char(';'));
    if (p.size() < 2)
        return fallback;
    QFont f(p.at(0));
    f.setPointSize(qMax(4, p.at(1).toInt()));
    const QString flags = p.value(3);
    f.setBold(flags.size() > 0 && flags.at(0) == QLatin1Char('1'));
    f.setItalic(flags.size() > 1 && flags.at(1) == QLatin1Char('1'));
    return f;
}

Precision timePrecision(int v)
{
    // SkiPro TIME_ACCURACY: 4 = 1/100 (observed). Neighbouring values assumed linear.
    switch (v) {
    case 2: return Precision::Second;
    case 3: return Precision::Tenth;
    case 4: return Precision::Hundredth;
    case 5: return Precision::Thousandth;
    default: return Precision::Hundredth;
    }
}

Precision speedPrecision(int v)
{
    return Precision(qBound(0, v, 3));
}

} // namespace

QDate SkiProImporter::fromDelphiDate(double days)
{
    if (days <= 0)
        return QDate();
    return QDate(1899, 12, 30).addDays(qint64(days));
}

bool SkiProImporter::read(const QString& mainFile, Result& out, QString* error, const QString& logosDir)
{
    // runs: tables live in the Ex file (SEPARATE_FILES=1) or in the main file
    const QFileInfo fi(mainFile);
    const QString exPath = fi.absoluteDir().absoluteFilePath(fi.completeBaseName() + QStringLiteral("Ex.") + fi.suffix());
    return readDatabases(mainFile, QFileInfo::exists(exPath) ? exPath : QString(), Format::SkiPro2, out, error, logosDir);
}

bool SkiProImporter::readDatabases(const QString& mainFile, const QString& runsFile, Format format, Result& out,
                                   QString* error, const QString& logosDir)
{
    out = Result();
    out.params = EventParams::defaults();
    ReadDb main(mainFile);
    if (!main.ok()) {
        if (error)
            *error = main.error();
        return false;
    }
    if (!main.hasTable(QStringLiteral("TPARAMETERS")) || !main.hasTable(QStringLiteral("TCOMPETITORS"))) {
        if (error)
            *error = QStringLiteral("The file is not a SkiPro event (TPARAMETERS/TCOMPETITORS missing).");
        return false;
    }

    const auto p = main.keyValues(QStringLiteral("TPARAMETERS"));
    const auto e = main.keyValues(QStringLiteral("TEDITINGPARAMETERS"));
    const auto tp = main.keyValues(QStringLiteral("TTIMEPARAMETERS"));
    EventParams& ep = out.params;

    // ---- general
    ep.general.title1 = p.value(QStringLiteral("TITLE1"));
    ep.general.title2 = p.value(QStringLiteral("TITLE2"));
    ep.general.date = fromDelphiDate(p.value(QStringLiteral("DATE")).toDouble());
    ep.general.location = p.value(QStringLiteral("LOCATION"));
    ep.general.nation = p.value(QStringLiteral("COMMITTEE"));
    ep.general.organisation = p.value(QStringLiteral("ORGANISATION"));
    ep.general.timingBy = p.value(QStringLiteral("TIMING_BY"));
    ep.general.codex = p.value(QStringLiteral("CODEX"));
    ep.general.useFreeHeader = p.value(QStringLiteral("USE_FREE_HEADER")) == QLatin1String("1");

    // ---- event type
    const int discipline = p.value(QStringLiteral("DISCIPLINE"), QStringLiteral("2")).toInt();
    if (p.value(QStringLiteral("DISCIPLINE_TYPE"), QStringLiteral("0")).toInt() != 0)
        out.warnings << QStringLiteral("Only alpine events are supported; the sport type was set to Alpine.");
    if (discipline >= 0 && discipline < kDisciplineCount)
        ep.type.discipline = Discipline(discipline);
    else
        out.warnings << QStringLiteral("Unsupported discipline %1, Giant Slalom used instead.").arg(discipline);
    // FIS race category ("Level 2 : NC") and the constants of the penalty calculation
    const QString fisCat = p.value(QStringLiteral("FIS_CATEGORY")).section(QLatin1Char(':'), -1).trimmed();
    if (!fisCat.isEmpty())
        ep.type.fisCategory = fisCat;
    const double coefF = p.value(QStringLiteral("COEF_F")).toDouble();
    if (coefF > 0) {
        ep.fisPoints.list = p.value(QStringLiteral("IMPORT_LIST_NUMBER"));
        for (int g = 0; g < 2; ++g) {
            const QString who = g == 0 ? QStringLiteral("MEN") : QStringLiteral("WOMEN");
            auto coef = [&](const char* name, double def) {
                const QString v = p.value(QStringLiteral("COEF_%1_%2").arg(who, QLatin1String(name)));
                return v.isEmpty() ? def : v.toDouble();
            };
            FisPointsParams& f = ep.fisPoints.gender[size_t(g)];
            f.fValue = coefF;
            f.maxValue = qRound(p.value(QStringLiteral("COEF_PM")).toDouble() * 100);
            f.zValue = qRound(coef("Z", 0) * 100);
            f.adder = qRound(coef("ADDER", 0) * 100);
            f.minPenalty = qRound(coef("PMIN", 0) * 100);
            f.maxPenalty = qRound(coef("PMAX", 999) * 100);
        }
    }
    ep.type.runCount = qBound(1, p.value(QStringLiteral("HEAT_NUMBER"), QStringLiteral("1")).toInt(), kMaxRuns);
    const int inters = p.value(QStringLiteral("INTER_NUMBER")).toInt();
    if (inters > kMaxInter)
        out.warnings << QStringLiteral("The event has %1 intermediate times, only %2 are imported.").arg(inters).arg(kMaxInter);
    ep.type.interCount = qBound(0, inters, kMaxInter);
    ep.type.timePrecision = timePrecision(p.value(QStringLiteral("TIME_ACCURACY"), QStringLiteral("4")).toInt());
    ep.type.speedPrecision = speedPrecision(p.value(QStringLiteral("SPEED_ACCURACY"), QStringLiteral("2")).toInt());
    ep.type.avgSpeedPrecision = speedPrecision(p.value(QStringLiteral("AVG_SPEED_ACCURACY"), QStringLiteral("2")).toInt());
    ep.type.courseLength = p.value(QStringLiteral("COURSE_LENGTH")).toDouble() / 1000.0;
    ep.type.finalType = p.value(QStringLiteral("FINAL_RANKING_TYPE")).toInt() == 1 ? FinalRankingType::BestOfRuns
                                                                                  : FinalRankingType::TotalOfRuns;
    for (int i = 0; i < kMaxRuns; ++i) {
        ep.type.runsUsed[size_t(i)] = p.value(QStringLiteral("FINAL_RANKING_USE_HEAT%1").arg(i + 1), QStringLiteral("1")) != QLatin1String("0");
        const QString name = p.value(QStringLiteral("HEAT%1_NAME").arg(i + 1));
        ep.type.runNames[size_t(i)] = name == QStringLiteral("Run %1").arg(i + 1) ? QString() : name;
    }

    // ---- header
    auto hdr = [&](const QString& key, const QString& skiproKey) {
        const QString v = p.value(QStringLiteral("HEADER_FIS_") + skiproKey);
        if (!v.isEmpty())
            ep.header.setValue(key, v);
    };
    auto person = [&](const QString& key, const QString& skiproKey) {
        hdr(key + QStringLiteral(".name"), skiproKey);
        hdr(key + QStringLiteral(".nation"), skiproKey + QStringLiteral("2"));
        hdr(key + QStringLiteral(".code"), skiproKey + QStringLiteral("C"));
    };
    person(QLatin1String(HeaderKeys::TechnicalDelegate), QStringLiteral("DT"));
    person(QLatin1String(HeaderKeys::Referee), QStringLiteral("REF"));
    person(QLatin1String(HeaderKeys::AssistantReferee), QStringLiteral("AREF"));
    person(QLatin1String(HeaderKeys::ChiefOfRace), QStringLiteral("CHIEFOFRACE"));
    person(QStringLiteral("run1.setter"), QStringLiteral("COURSESETTERA"));
    person(QStringLiteral("run2.setter"), QStringLiteral("COURSESETTERB"));
    const char letters[] = "ABCDEFGHIJ";
    for (int i = 0; i < 10; ++i) {
        const int runIdx = (i % 2) + 1;           // A,C,E,.. run 1 / B,D,F,.. run 2
        const QChar slot = QLatin1Char(char('A' + i / 2));
        person(QStringLiteral("run%1.fore%2").arg(runIdx).arg(slot), QStringLiteral("FORERUNNER") + QLatin1Char(letters[i]));
    }
    for (int r = 1; r <= 2; ++r) {
        hdr(QStringLiteral("run%1.gates").arg(r), QStringLiteral("GATE%1").arg(r));
        hdr(QStringLiteral("run%1.directions").arg(r), QStringLiteral("DIR%1").arg(r));
        hdr(QStringLiteral("run%1.starttime").arg(r), QStringLiteral("STARTTIME%1").arg(r));
    }
    // one course in SkiPro: the course data of both runs
    const QString length = p.value(QStringLiteral("HEADER_FIS_LENGTH"));
    for (int r = 1; r <= 2; ++r) {
        const QString k = QStringLiteral("run%1.").arg(r);
        hdr(k + QLatin1String(HeaderKeys::Course), QStringLiteral("COURSE"));
        hdr(k + QLatin1String(HeaderKeys::Homologation), QStringLiteral("HOMOLOGATION"));
        hdr(k + QLatin1String(HeaderKeys::StartAltitude), QStringLiteral("START"));
        hdr(k + QLatin1String(HeaderKeys::FinishAltitude), QStringLiteral("FINISH"));
        hdr(k + QLatin1String(HeaderKeys::Drop), QStringLiteral("DROP"));
        if (!length.isEmpty() && length != QLatin1String("0"))
            ep.header.setValue(k + QLatin1String(HeaderKeys::Length), length);
    }
    // SkiAlp Pro: first names in their own keys, the course data and the forerunners of each run
    auto fis = [&](const QString& key) { return p.value(QStringLiteral("HEADER_FIS_") + key).trimmed(); };
    auto setPerson = [&](const QString& key, const QString& last, const QString& first, const QString& nation) {
        if (last.isEmpty() && first.isEmpty())
            return;
        ep.header.setValue(key + QStringLiteral(".name"), (last + QLatin1Char(' ') + first).trimmed());
        if (!nation.isEmpty())
            ep.header.setValue(key + QStringLiteral(".nation"), nation);
    };
    const QList<QPair<QString, QString>> juryKeys = { { QLatin1String(HeaderKeys::TechnicalDelegate), QStringLiteral("DT") },
                                                      { QLatin1String(HeaderKeys::Referee), QStringLiteral("REF") },
                                                      { QLatin1String(HeaderKeys::AssistantReferee), QStringLiteral("AREF") },
                                                      { QLatin1String(HeaderKeys::ChiefOfRace), QStringLiteral("CHIEFOFRACE") } };
    for (const auto& j : juryKeys)
        if (!fis(j.second + QStringLiteral("_FIRSTNAME")).isEmpty() || !fis(j.second + QStringLiteral("_NATION")).isEmpty())
            setPerson(j.first, fis(j.second), fis(j.second + QStringLiteral("_FIRSTNAME")), fis(j.second + QStringLiteral("_NATION")));
    for (int r = 1; r <= 2; ++r) {
        const QString k = QStringLiteral("run%1.").arg(r);
        const QString n = QStringLiteral("_%1").arg(r);
        setPerson(k + QStringLiteral("setter"), fis(QStringLiteral("COURSESETTER") + n), fis(QStringLiteral("COURSESETTER_1NAME") + n),
                  fis(QStringLiteral("COURSESETTER_NATION") + n));
        for (char c = 'A'; c <= 'E'; ++c) {
            const QString f = QStringLiteral("FORERUNNER") + QLatin1Char(c);
            setPerson(k + QStringLiteral("fore") + QLatin1Char(c), fis(f + QStringLiteral("LN") + n), fis(f + QStringLiteral("FN") + n),
                      fis(f + QStringLiteral("_NATION") + n));
        }
        const QList<QPair<QString, QString>> runKeys = {
            { QStringLiteral("gates"), QStringLiteral("GATE") },           { QStringLiteral("directions"), QStringLiteral("DIR") },
            { QStringLiteral("starttime"), QStringLiteral("STARTTIME") },  { QLatin1String(HeaderKeys::Course), QStringLiteral("COURSE") },
            { QLatin1String(HeaderKeys::Homologation), QStringLiteral("HOMOLOGATION") },
            { QLatin1String(HeaderKeys::StartAltitude), QStringLiteral("START") },
            { QLatin1String(HeaderKeys::FinishAltitude), QStringLiteral("FINISH") },
            { QLatin1String(HeaderKeys::Drop), QStringLiteral("DROP") },     { QLatin1String(HeaderKeys::Length), QStringLiteral("LENGTH") },
        };
        for (const auto& rk : runKeys) {
            const QString v = fis(rk.second + n);
            if (!v.isEmpty() && !(rk.first == QLatin1String(HeaderKeys::Length) && v == QLatin1String("0")))
                ep.header.setValue(k + rk.first, v);
        }
    }
    hdr(QLatin1String(HeaderKeys::Weather), QStringLiteral("WEATHER"));
    hdr(QLatin1String(HeaderKeys::Snow), QStringLiteral("SNOW"));
    // temperatures: SkiPro in degrees, SkiAlp Pro in tenths of a degree (also per run: "_1")
    for (const auto& t : { qMakePair(QLatin1String(HeaderKeys::TempStart), QStringLiteral("TEMPSTART")),
                           qMakePair(QLatin1String(HeaderKeys::TempFinish), QStringLiteral("TEMPFINISH")) }) {
        QString v = fis(t.second);
        if (v.isEmpty())
            v = fis(t.second + QStringLiteral("_1"));
        if (v.isEmpty())
            continue;
        if (format == Format::SkiAlp11) {
            bool ok = false;
            const int tenths = v.toInt(&ok);
            if (!ok)
                continue;
            v = QString::number(tenths / 10.0, 'f', 1);
        }
        ep.header.setValue(t.first, v);
    }
    const QString list = p.value(QStringLiteral("IMPORT_LIST_NUMBER"));
    if (!list.isEmpty() && list != QLatin1String("0"))
        ep.header.setValue(QLatin1String(HeaderKeys::ListNumber), list);
    // "SVK6534.185": nation, codex and TD number in their own fields
    ep.setFisCode(ep.general.codex);

    // ---- printing
    auto& pr = ep.printing;
    pr.fontTitle1 = skiproFont(e.value(QStringLiteral("FONT_TITLE1")), pr.fontTitle1);
    pr.fontTitle2 = skiproFont(e.value(QStringLiteral("FONT_TITLE2")), pr.fontTitle2);
    pr.fontDocTitle = skiproFont(e.value(QStringLiteral("FONT_TITLE3")), pr.fontDocTitle);
    pr.fontFooter = skiproFont(e.value(QStringLiteral("FONT_FOOTNOTES")), pr.fontFooter);
    pr.fontLabels = skiproFont(e.value(QStringLiteral("FONT_TITLE_ZONE")), pr.fontLabels);
    pr.fontLines = skiproFont(e.value(QStringLiteral("FONT_ZONE")), pr.fontLines);
    if (e.contains(QStringLiteral("DISPLAY_TITLE1")))
        pr.showTitle1 = e.value(QStringLiteral("DISPLAY_TITLE1")) != QLatin1String("0");
    if (e.contains(QStringLiteral("DISPLAY_TITLE2")))
        pr.showTitle2 = e.value(QStringLiteral("DISPLAY_TITLE2")) != QLatin1String("0");
    pr.autoAdjust = e.value(QStringLiteral("AUTO_RESIZE_CELL"), QStringLiteral("1")) != QLatin1String("0");
    pr.showAsTable = e.value(QStringLiteral("DRAW_TAB")) == QLatin1String("1");
    pr.grayRows = e.value(QStringLiteral("DRAW_GREY_LINE"), QStringLiteral("1")) != QLatin1String("0");
    pr.grayLevel = e.value(QStringLiteral("GREY_LINE_DENSITY"), QStringLiteral("10")).toInt();
    auto margin = [&](const char* key, int def) {
        bool ok = false;
        const int v = e.value(QLatin1String(key)).toInt(&ok);
        return ok ? v : def;
    };
    pr.marginsFirst = { margin("MARGIN_PAGE1_TOP", 25), margin("MARGIN_PAGE1_BOTTOM", 25),
                        margin("MARGIN_PAGE1_LEFT", 5), margin("MARGIN_PAGE1_RIGHT", 5) };
    pr.marginsOther = { margin("MARGIN_TOP", 25), margin("MARGIN_BOTTOM", 25), margin("MARGIN_LEFT", 5),
                        margin("MARGIN_RIGHT", 5) };
    pr.titleAlignFirst = margin("TITLES_ALIGN_PAGE1", 1);
    pr.titleAlignOther = margin("TITLES_ALIGN", 1);
    pr.logosFirstPageOnly = e.value(QStringLiteral("LOGO_ONLY_FIRST_PAGE")) == QLatin1String("1");
    for (int i = 0; i < 8; ++i) {
        auto& slot = pr.logos[size_t(i)];
        slot.path = e.value(QStringLiteral("LOGO%1").arg(i + 1));
        const int x1 = e.value(QStringLiteral("LOGO%1_X1").arg(i + 1)).toInt();
        const int y1 = e.value(QStringLiteral("LOGO%1_Y1").arg(i + 1)).toInt();
        const int x2 = e.value(QStringLiteral("LOGO%1_X2").arg(i + 1)).toInt();
        const int y2 = e.value(QStringLiteral("LOGO%1_Y2").arg(i + 1)).toInt();
        if (x2 > x1 && y2 > y1) // SkiPro stores logo positions in 1/1000 of the page
            slot.rect = QRectF(x1 / 1000.0, y1 / 1000.0, (x2 - x1) / 1000.0, (y2 - y1) / 1000.0);
        if (!slot.path.isEmpty() && !QFileInfo::exists(slot.path)) {
            // the path is often from another computer: look for the file in the Logos
            // folder of the SkiPro installation and in the OpenSkiPro2 logos folder
            const QString name = QFileInfo(QDir::fromNativeSeparators(slot.path)).fileName();
            const QDir eventDir = QFileInfo(mainFile).absoluteDir();
            const QStringList candidates = { eventDir.absoluteFilePath(QStringLiteral("../Logos/") + name),
                                             eventDir.absoluteFilePath(QStringLiteral("../../Logos/") + name),
                                             QDir(logosDir).absoluteFilePath(name) };
            QString found;
            for (const QString& c : candidates)
                if (found.isEmpty() && QFileInfo::exists(c))
                    found = QFileInfo(c).canonicalFilePath();
            if (!found.isEmpty())
                slot.path = found;
            else
                out.warnings << QStringLiteral("Logo %1 not found: %2").arg(i + 1).arg(slot.path);
        }
    }

    // ---- timing options
    auto& to = ep.timing;
    auto tflag = [&](const char* key, bool def) {
        const QString v = tp.value(QLatin1String(key));
        return v.isEmpty() ? def : v != QLatin1String("0");
    };
    auto tnum = [&](const char* key, qint64 def) {
        const QString v = tp.value(QLatin1String(key));
        return v.isEmpty() ? def : v.toLongLong();
    };
    to.showNetTime = tflag("TIMING_DISPLAY_NET_TIME", false);
    to.invertGrid = tflag("TIMING_INVERT_GRID", true);
    to.askBibIfUnknown = tflag("TIMING_ASK_NUM", true);
    to.showMarkMustFinish = tflag("TIMING_DISPLAY_SQUARE_NEXT_TIME", true);
    to.confirmInsertStartList = tflag("TIMING_MESSAGE_INSERT_START_LIST", true);
    to.showNumbersToFollow = tflag("TIMING_DISPLAY_READY_GRIDS", true);
    to.showNamesInFollow = tflag("TIMING_DISPLAY_INFO_READY_GRIDS", false);
    to.forerunnerFrom = int(tnum("TIMING_FORERUNNERS_FIRST_BIB", 901));
    to.forerunnerTo = int(tnum("TIMING_FORERUNNERS_LAST_BIB", 999));
    for (int i = 0; i < 4; ++i)
        to.quickPenaltiesMs[size_t(i)] = tnum(QStringLiteral("TIMING_QUICK_PENALTY%1").arg(i + 1).toLatin1().constData(), 0);
    to.delayStartMs = tnum("TIMING_DELAY_START", 0);
    to.delayFinishMs = tnum("TIMING_DELAY_FINISH", 0);
    to.delayInterMs = tnum("TIMING_DELAY_INTER", 0);
    for (int i = 0; i < kMaxInter; ++i)
        to.minTimeInterMs[size_t(i)] = tnum(QStringLiteral("TIMING_MIN_TIME_INTER%1").arg(i + 1).toLatin1().constData(), 0);
    to.minTimeFinishMs = tnum("TIMING_MIN_TIME_FINISH", 0);
    // ranking of the timing window: 1 = run, 2 = event; field in brackets; filter -> by the output groups
    to.rankingEvent = tnum("TIMING_CURRENT_CLASSIFICATION", 2) != 1;
    to.rankingField = fieldFromSource(int(tnum("TIMING_CLASSIFICATION_ADD_FIELD", 0)), format);
    if (!competitorFieldDef(to.rankingField))
        to.rankingField = 0;
    to.rankingByGroups = tflag("TIMING_FILTER_ACTIVE", false);

    // ---- competitor field labels / active flags
    for (const auto& def : competitorFieldDefs()) {
        const int src = fieldToSource(def.id, format);
        if (src <= 0)
            continue;
        const QString name = p.value(QStringLiteral("COMPETITORS_INFO_NAME%1").arg(src));
        const QString active = p.value(QStringLiteral("COMPETITORS_INFO_ACTIVE%1").arg(src));
        if (!name.isEmpty() && name != QCoreApplication::translate("Competitor", def.label) && name != QLatin1String(def.label))
            ep.fields.labels.insert(def.id, name);
        if (active == QLatin1String("1"))
            ep.fields.active.insert(def.id);
        else if (active == QLatin1String("0"))
            ep.fields.active.remove(def.id);
    }
    const int gridCount = p.value(QStringLiteral("COMPETITORS_GRID_COUNT")).toInt();
    if (gridCount > 0) {
        ep.fields.grid.clear();
        for (int i = 1; i <= gridCount; ++i) {
            const int id = fieldFromSource(p.value(QStringLiteral("COMPETITORS_GRID_INFO%1").arg(i)).toInt(), format);
            const int w = p.value(QStringLiteral("COMPETITORS_GRID_WIDTH%1").arg(i)).toInt();
            if (competitorFieldDef(id))
                ep.fields.grid << qMakePair(id, qMax(30, w));
        }
    }

    // ---- output groups: the result groups (filter list 1)
    ep.outputGroups = readFilterList(p, QStringLiteral("FILTERS_LIST1"), format);

    // ---- categories (COMPETITORS_CATEGORYn_NAME / _SEX / _YEAR_MIN / _YEAR_MAX)
    for (int i = 1; i <= 200; ++i) {
        const QString base = QStringLiteral("COMPETITORS_CATEGORY%1_").arg(i);
        const QString name = p.value(base + QStringLiteral("NAME")).trimmed();
        if (name.isEmpty())
            continue;
        CategoryDef c;
        c.name = name;
        const int sex = p.value(base + QStringLiteral("SEX")).toInt();
        c.gender = sex == 1 ? Gender::Male : sex == 2 ? Gender::Female : Gender::Unknown;
        c.yearMin = p.value(base + QStringLiteral("YEAR_MIN")).toInt();
        c.yearMax = p.value(base + QStringLiteral("YEAR_MAX")).toInt();
        out.categories.categories << c;
    }

    // ---- competitors
    {
        QSqlQuery q(main.db());
        if (!q.exec(QStringLiteral("SELECT * FROM TCOMPETITORS ORDER BY C_IDX"))) {
            if (error)
                *error = q.lastError().text();
            return false;
        }
        const QSqlRecord rec = q.record();
        auto col = [&](const char* name) { return rec.indexOf(QLatin1String(name)); };
        const int cNum = col("C_NUM"), cTr = col("C_TRANSPONDER1"), cLast = col("C_LAST_NAME"),
                  cFirst = col("C_FIRST_NAME"), cCode = col("C_CODE"), cSex = col("C_SEX"), cYear = col("C_YEAR"),
                  cCat = col("C_CATEGORY"), cNat = col("C_NATION"), cCom = col("C_COMMITTEE"), cClub = col("C_CLUB"),
                  cTeam = col("C_TEAM");
        // SkiPro 2: list C_I16, points DH..AC C_I21..C_I25, custom C_I32
        // SkiAlp 11: list C_I22, points DH..AC C_I29..C_I33
        const bool skiAlp = format == Format::SkiAlp11;
        const int cList = col(skiAlp ? "C_I22" : "C_I16"), cCustom = skiAlp ? -1 : col("C_I32");
        const int cPts[5] = { col(skiAlp ? "C_I29" : "C_I21"), col(skiAlp ? "C_I30" : "C_I22"), col(skiAlp ? "C_I31" : "C_I23"),
                              col(skiAlp ? "C_I32" : "C_I24"), col(skiAlp ? "C_I33" : "C_I25") };
        auto s = [&](int idx) { return idx >= 0 ? fixEncoding(q.value(idx).toString().trimmed()) : QString(); };
        while (q.next()) {
            Competitor c;
            c.bib = cNum >= 0 ? q.value(cNum).toInt() : 0;
            c.transponder = s(cTr);
            c.lastName = s(cLast);
            c.firstName = s(cFirst);
            c.code = s(cCode);
            const int sex = cSex >= 0 ? q.value(cSex).toInt() : 0;
            c.gender = sex == 1 ? Gender::Male : sex == 2 ? Gender::Female : Gender::Unknown;
            c.year = cYear >= 0 ? q.value(cYear).toInt() : 0;
            c.category = s(cCat);
            c.nation = s(cNat);
            c.region = s(cCom);
            c.club = s(cClub);
            c.team = s(cTeam);
            c.fromList = s(cList);
            c.custom = s(cCustom);
            for (int i = 0; i < 5; ++i)
                c.points[size_t(i)] = cPts[i] >= 0 ? pointsValue(q.value(cPts[i])) : -1;
            out.competitors << c;
        }
    }

    // ---- runs
    std::unique_ptr<ReadDb> ex;
    if (!runsFile.isEmpty()) {
        ex = std::make_unique<ReadDb>(runsFile);
        if (!ex->ok()) {
            out.warnings << QStringLiteral("Cannot open %1: %2").arg(runsFile, ex->error());
            ex.reset();
        }
    }
    auto source = [&](const QString& table) -> const ReadDb* {
        if (ex && ex->hasTable(table))
            return ex.get();
        if (main.hasTable(table))
            return &main;
        return nullptr;
    };

    for (int r = 1; r <= ep.type.runCount; ++r) {
        RunData run;
        run.run = r;
        const QString sl = QStringLiteral("TSTARTLIST_HEAT%1").arg(r);
        if (const ReadDb* db = source(sl)) {
            QSqlQuery q(db->db());
            if (q.exec(QStringLiteral("SELECT C_NUM, C_START FROM %1 ORDER BY C_LINE").arg(sl))) {
                while (q.next()) {
                    StartEntry s;
                    s.bib = q.value(0).toInt();
                    const qint64 st = q.value(1).toLongLong();
                    s.startTimeUs = st > 0 ? st : TimeUtil::kNoTime;
                    if (s.bib > 0)
                        run.startList << s;
                }
            }
        }

        const QString ti = QStringLiteral("TTIMEINFOS_HEAT%1").arg(r);
        if (const ReadDb* db = source(ti)) {
            QSqlQuery q(db->db());
            if (q.exec(QStringLiteral("SELECT C_NUM, C_STATUS, C_TIME, C_PENALTY, C_INTER1, C_INTER2, C_INTER3, C_REASON"
                                      " FROM %1").arg(ti))) {
                while (q.next()) {
                    TimeInfo t;
                    t.bib = q.value(0).toInt();
                    const int st = q.value(1).toInt();
                    t.timeMs = q.value(2).toLongLong();
                    t.penaltyMs = q.value(3).toLongLong();
                    for (int i = 0; i < kMaxInter; ++i) {
                        const qint64 v = q.value(4 + i).toLongLong();
                        t.interMs[size_t(i)] = v > 0 ? v : TimeUtil::kNoTime;
                    }
                    t.reason = q.value(7).toString();
                    if (st >= 1 && st <= 3)
                        t.status = ResultStatus(st);
                    else if (st == 0 && t.timeMs > 0)
                        t.status = ResultStatus::Classified;
                    else
                        t.status = ResultStatus::NotProcessed;
                    if (t.status != ResultStatus::Classified)
                        t.timeMs = TimeUtil::kNoTime;
                    if (t.bib > 0 && t.status != ResultStatus::NotProcessed)
                        run.infos.insert(t.bib, t);
                }
            }
        }

        // raw records
        int nextId = 1;
        auto readRecords = [&](const QString& table, int point) {
            const ReadDb* db = source(table);
            if (!db)
                return;
            QSqlQuery q(db->db());
            if (!q.exec(QStringLiteral("SELECT C_LINE, C_NUM, C_STATUS, C_HOUR2 FROM %1 ORDER BY C_LINE").arg(table)))
                return;
            while (q.next()) {
                TimeRecord t;
                t.id = nextId++;
                t.point = point;
                t.bib = q.value(1).toInt();
                const int st = q.value(2).toInt();
                // status word: low byte 1 = inactive, second byte = 1 DNS, 2 DNF, 3 DSQ
                const int status = (st >> 8) & 0xFF;
                t.flags = status == 1 ? RecordFlag::DNS : status == 2 ? RecordFlag::DNF : status == 3 ? RecordFlag::DSQ : 0;
                if (st & 0x0001)
                    t.flags |= RecordFlag::Inactive;
                t.dayTimeUs = q.value(3).toString().toLongLong();
                t.device = 1;
                run.records << t;
            }
        };
        readRecords(QStringLiteral("TTIMERECORDS_HEAT%1_START").arg(r), Point::Start);
        for (int i = 1; i <= kMaxInter; ++i)
            readRecords(QStringLiteral("TTIMERECORDS_HEAT%1_INTER%2").arg(r).arg(i), i);
        readRecords(QStringLiteral("TTIMERECORDS_HEAT%1_FINISH").arg(r), Point::Finish);

        out.runs << run;
    }
    return true;
}

bool SkiProImporter::apply(const Result& data, Event& ev, QString* error)
{
    if (!ev.isOpen()) {
        if (error)
            *error = QStringLiteral("No event open");
        return false;
    }
    ev.setParams(data.params);
    if (!data.categories.categories.isEmpty())
        ev.setCategories(data.categories);
    if (!ev.replaceCompetitors(data.competitors)) {
        if (error)
            *error = ev.lastError();
        return false;
    }
    for (int r = 1; r <= kMaxRuns; ++r)
        ev.clearRun(r, true, true);
    for (const RunData& run : data.runs) {
        if (!ev.replaceRun(run)) {
            if (error)
                *error = ev.lastError();
            return false;
        }
    }
    return true;
}
