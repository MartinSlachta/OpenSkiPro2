#include "CsvImporter.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QStringDecoder>
#include <QXmlStreamReader>
#include <QtCore/private/qzipreader_p.h>

namespace CsvImporter {

int Table::columnCount() const
{
    int n = 0;
    for (const auto& r : rows)
        n = qMax(n, int(r.size()));
    return n;
}

QChar detectDelimiter(const QString& text)
{
    const QString sample = text.left(4096);
    const QChar candidates[] = { QLatin1Char(';'), QLatin1Char('\t'), QLatin1Char(','), QLatin1Char('|') };
    QChar best = QLatin1Char(';');
    int bestCount = -1;
    for (QChar c : candidates) {
        const int n = int(sample.count(c));
        if (n > bestCount) {
            bestCount = n;
            best = c;
        }
    }
    return best;
}

bool parseText(const QString& text, Table& out, QChar delimiter)
{
    out.rows.clear();
    if (delimiter.isNull())
        delimiter = detectDelimiter(text);
    QStringList row;
    QString field;
    bool inQuotes = false;
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (inQuotes) {
            if (c == QLatin1Char('"')) {
                if (i + 1 < text.size() && text.at(i + 1) == QLatin1Char('"')) {
                    field += QLatin1Char('"');
                    ++i;
                } else {
                    inQuotes = false;
                }
            } else {
                field += c;
            }
            continue;
        }
        if (c == QLatin1Char('"') && field.isEmpty()) {
            inQuotes = true;
        } else if (c == delimiter) {
            row << field.trimmed();
            field.clear();
        } else if (c == QLatin1Char('\r') || c == QLatin1Char('\n')) {
            if (c == QLatin1Char('\r') && i + 1 < text.size() && text.at(i + 1) == QLatin1Char('\n'))
                ++i;
            row << field.trimmed();
            field.clear();
            bool empty = true;
            for (const auto& f : row)
                if (!f.isEmpty())
                    empty = false;
            if (!empty)
                out.rows << row;
            row.clear();
        } else {
            field += c;
        }
    }
    if (!field.isEmpty() || !row.isEmpty()) {
        row << field.trimmed();
        out.rows << row;
    }
    return true;
}

static QString decode(const QByteArray& raw, Encoding enc)
{
    if (raw.startsWith("\xEF\xBB\xBF"))
        return QString::fromUtf8(raw.mid(3));
    if (raw.startsWith("\xFF\xFE") || raw.startsWith("\xFE\xFF")) {
        QStringDecoder d(QStringDecoder::Utf16);
        return d(raw);
    }
    switch (enc) {
    case Encoding::Utf8: return QString::fromUtf8(raw);
    case Encoding::Latin1: return QString::fromLatin1(raw);
    case Encoding::System: {
        QStringDecoder d(QStringDecoder::System);
        return d(raw);
    }
    case Encoding::Auto: break;
    }
    QStringDecoder utf8(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    const QString s = utf8(raw);
    if (!utf8.hasError())
        return s;
    QStringDecoder sys(QStringDecoder::System);
    return sys(raw);
}

// Column letters "AB12" -> zero based column index
static int columnIndex(const QString& ref)
{
    int col = 0;
    for (QChar c : ref) {
        if (!c.isLetter())
            break;
        col = col * 26 + (c.toUpper().unicode() - 'A' + 1);
    }
    return col - 1;
}

static bool readXlsx(const QString& path, Table& out, QString* error)
{
    QZipReader zip(path);
    if (!zip.isReadable()) {
        if (error)
            *error = QStringLiteral("Cannot read the XLSX file.");
        return false;
    }
    // shared strings
    QStringList shared;
    {
        QXmlStreamReader x(zip.fileData(QStringLiteral("xl/sharedStrings.xml")));
        QString cur;
        bool inSi = false;
        while (!x.atEnd()) {
            x.readNext();
            if (x.isStartElement() && x.name() == QLatin1String("si")) {
                inSi = true;
                cur.clear();
            } else if (x.isEndElement() && x.name() == QLatin1String("si")) {
                inSi = false;
                shared << cur;
            } else if (inSi && x.isStartElement() && x.name() == QLatin1String("t")) {
                cur += x.readElementText();
            }
        }
    }
    // first worksheet
    QString sheet;
    for (const auto& fi : zip.fileInfoList()) {
        if (fi.filePath.startsWith(QLatin1String("xl/worksheets/sheet")) && fi.filePath.endsWith(QLatin1String(".xml"))) {
            if (sheet.isEmpty() || fi.filePath == QLatin1String("xl/worksheets/sheet1.xml"))
                sheet = fi.filePath;
        }
    }
    const QByteArray data = zip.fileData(sheet);
    if (data.isEmpty()) {
        if (error)
            *error = QStringLiteral("The XLSX file has no worksheet.");
        return false;
    }
    QXmlStreamReader x(data);
    QStringList row;
    int col = 0;
    QString type;
    out.rows.clear();
    while (!x.atEnd()) {
        x.readNext();
        if (x.isStartElement()) {
            if (x.name() == QLatin1String("row")) {
                row.clear();
            } else if (x.name() == QLatin1String("c")) {
                col = columnIndex(x.attributes().value(QLatin1String("r")).toString());
                type = x.attributes().value(QLatin1String("t")).toString();
            } else if (x.name() == QLatin1String("v") || x.name() == QLatin1String("t")) {
                QString v = x.readElementText();
                if (type == QLatin1String("s"))
                    v = shared.value(v.toInt());
                while (row.size() < col)
                    row << QString();
                if (col >= 0) {
                    if (row.size() == col)
                        row << v.trimmed();
                    else
                        row[col] += v.trimmed();
                }
            }
        } else if (x.isEndElement() && x.name() == QLatin1String("row")) {
            bool empty = true;
            for (const auto& f : row)
                if (!f.isEmpty())
                    empty = false;
            if (!empty)
                out.rows << row;
        }
    }
    if (x.hasError() && error)
        *error = x.errorString();
    return !x.hasError();
}

bool readFile(const QString& path, Table& out, QString* error, QChar delimiter, Encoding enc)
{
    if (QFileInfo(path).suffix().compare(QLatin1String("xlsx"), Qt::CaseInsensitive) == 0)
        return readXlsx(path, out, error);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    return parseText(decode(f.readAll(), enc), out, delimiter);
}

static QString norm(const QString& s)
{
    QString n = s.normalized(QString::NormalizationForm_D).toLower();
    n.remove(QRegularExpression(QStringLiteral("[^a-z0-9]")));
    return n;
}

QList<int> guessMapping(const QStringList& header)
{
    static const QList<QPair<QStringList, int>> keys = {
        { { QStringLiteral("bib"), QStringLiteral("num"), QStringLiteral("dossard"), QStringLiteral("startnumber"),
            QStringLiteral("startovecislo"), QStringLiteral("cislo"), QStringLiteral("nr"), QStringLiteral("startnr") },
          CompField::Bib },
        { { QStringLiteral("lastname"), QStringLiteral("surname"), QStringLiteral("nom"), QStringLiteral("priezvisko"),
            QStringLiteral("prijmeni"), QStringLiteral("familyname") },
          CompField::LastName },
        { { QStringLiteral("firstname"), QStringLiteral("prenom"), QStringLiteral("meno"), QStringLiteral("jmeno"),
            QStringLiteral("givenname") },
          CompField::FirstName },
        { { QStringLiteral("name"), QStringLiteral("fullname"), QStringLiteral("menoapriezvisko") }, CompField::Name },
        { { QStringLiteral("code"), QStringLiteral("fiscode"), QStringLiteral("kod"), QStringLiteral("licence"),
            QStringLiteral("license"), QStringLiteral("id") },
          CompField::Code },
        { { QStringLiteral("gender"), QStringLiteral("sex"), QStringLiteral("sexe"), QStringLiteral("pohlavie"),
            QStringLiteral("pohlavi") },
          CompField::Gender },
        { { QStringLiteral("year"), QStringLiteral("yob"), QStringLiteral("birthyear"), QStringLiteral("rocnik"),
            QStringLiteral("rok"), QStringLiteral("annee"), QStringLiteral("birth") },
          CompField::Year },
        { { QStringLiteral("category"), QStringLiteral("cat"), QStringLiteral("kategoria"), QStringLiteral("kategorie") },
          CompField::Category },
        { { QStringLiteral("nation"), QStringLiteral("nat"), QStringLiteral("country"), QStringLiteral("krajina"),
            QStringLiteral("stat"), QStringLiteral("nationcode") },
          CompField::Nation },
        { { QStringLiteral("region"), QStringLiteral("committee"), QStringLiteral("comite") }, CompField::Region },
        { { QStringLiteral("club"), QStringLiteral("klub"), QStringLiteral("oddiel"), QStringLiteral("skiclub") },
          CompField::Club },
        { { QStringLiteral("team"), QStringLiteral("tim"), QStringLiteral("equipe") }, CompField::Team },
        { { QStringLiteral("dh"), QStringLiteral("dhpoints"), QStringLiteral("downhill"), QStringLiteral("zjazd") },
          CompField::PointsDH },
        { { QStringLiteral("sg"), QStringLiteral("sgpoints"), QStringLiteral("superg") }, CompField::PointsSG },
        { { QStringLiteral("gs"), QStringLiteral("gspoints"), QStringLiteral("giant"), QStringLiteral("obrovskyslalom") },
          CompField::PointsGS },
        { { QStringLiteral("sl"), QStringLiteral("slpoints"), QStringLiteral("slalom") }, CompField::PointsSL },
        { { QStringLiteral("sc"), QStringLiteral("scpoints"), QStringLiteral("ac"), QStringLiteral("acpoints"),
            QStringLiteral("supercombined"), QStringLiteral("alpinecombined"), QStringLiteral("combined") },
          CompField::PointsAC },
        { { QStringLiteral("transponder"), QStringLiteral("chip") }, CompField::Transponder },
    };
    QList<int> out;
    QSet<int> used;
    for (const QString& h : header) {
        const QString n = norm(h);
        int field = 0;
        for (const auto& k : keys) {
            if (k.first.contains(n) && !used.contains(k.second)) {
                field = k.second;
                break;
            }
        }
        if (field)
            used.insert(field);
        out << field;
    }
    return out;
}

QVector<Competitor> toCompetitors(const Table& table, const QList<int>& mapping, int firstRow)
{
    QVector<Competitor> out;
    for (int r = qMax(0, firstRow); r < table.rows.size(); ++r) {
        const QStringList& row = table.rows.at(r);
        Competitor c;
        bool any = false;
        for (int col = 0; col < row.size() && col < mapping.size(); ++col) {
            const int field = mapping.at(col);
            if (field <= 0)
                continue;
            const QString v = row.at(col).trimmed();
            if (v.isEmpty())
                continue;
            c.setValue(field, v);
            any = true;
        }
        if (any && (!c.lastName.isEmpty() || !c.firstName.isEmpty() || c.bib > 0))
            out << c;
    }
    return out;
}

} // namespace CsvImporter
