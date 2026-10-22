#include "Category.h"

#include <QCoreApplication>
#include <QFile>
#include <QMap>
#include <QRegularExpression>
#include <QStringConverter>
#include <QStringList>
#include <QTextStream>

bool CategoryDef::matches(Gender g, int year) const
{
    if (gender != Gender::Unknown && gender != g)
        return false;
    if (yearMin > 0 && year < yearMin)
        return false;
    if (yearMax > 0 && year > yearMax)
        return false;
    return year > 0 || (yearMin == 0 && yearMax == 0);
}

QString CategoryGrid::categoryFor(Gender g, int year) const
{
    for (const auto& c : categories)
        if (c.matches(g, year))
            return c.name;
    return QString();
}

QString CategoryGrid::toString() const
{
    QStringList rows;
    for (const auto& c : categories)
        rows << QStringLiteral("%1;%2;%3;%4")
                    .arg(QString(c.name).remove(QLatin1Char(';')).remove(QLatin1Char('|')))
                    .arg(int(c.gender))
                    .arg(c.yearMin)
                    .arg(c.yearMax);
    return rows.join(QLatin1Char('|'));
}

CategoryGrid CategoryGrid::fromString(const QString& s)
{
    CategoryGrid g;
    for (const QString& row : s.split(QLatin1Char('|'), Qt::SkipEmptyParts)) {
        const QStringList p = row.split(QLatin1Char(';'));
        if (p.size() < 4)
            continue;
        CategoryDef c;
        c.name = p[0];
        c.gender = Gender(p[1].toInt());
        c.yearMin = p[2].toInt();
        c.yearMax = p[3].toInt();
        g.categories << c;
    }
    return g;
}

CategoryGrid CategoryGrid::fromSkiProCatFile(const QString& path, QString* error)
{
    CategoryGrid grid;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return grid;
    }
    const QByteArray raw = f.readAll();
    QString text;
    if (raw.startsWith("\xff\xfe") || raw.startsWith("\xfe\xff")) {
        auto dec = QStringDecoder(QStringDecoder::Utf16);
        text = dec(raw);
    } else {
        auto dec = QStringDecoder(QStringDecoder::System);
        text = dec(raw);
    }
    QMap<int, CategoryDef> byIndex;
    const QStringList lines = text.split(QRegularExpression(QStringLiteral("[\r\n]+")), Qt::SkipEmptyParts);
    for (const QString& lineIn : lines) {
        const QString line = lineIn.trimmed();
        if (!line.startsWith(QLatin1String("COMPETITORS_CATEGORY")))
            continue;
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq < 0)
            continue;
        const QString key = line.left(eq);
        const QString value = line.mid(eq + 1);
        const QString rest = key.mid(int(qstrlen("COMPETITORS_CATEGORY")));
        const int us = rest.indexOf(QLatin1Char('_'));
        if (us < 0)
            continue;
        const int idx = rest.left(us).toInt();
        const QString what = rest.mid(us + 1);
        CategoryDef& c = byIndex[idx];
        if (what == QLatin1String("NAME"))
            c.name = value;
        else if (what == QLatin1String("SEX"))
            c.gender = value.toInt() == 1 ? Gender::Male : value.toInt() == 2 ? Gender::Female : Gender::Unknown;
        else if (what == QLatin1String("YEAR_MIN"))
            c.yearMin = value.toInt();
        else if (what == QLatin1String("YEAR_MAX"))
            c.yearMax = value.toInt();
    }
    for (const auto& c : byIndex)
        if (!c.name.isEmpty())
            grid.categories << c;
    if (grid.categories.isEmpty() && error)
        *error = QCoreApplication::translate("Category", "No category found in the file.");
    return grid;
}
