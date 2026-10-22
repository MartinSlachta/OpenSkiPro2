#include "FisList.h"

#include <QCoreApplication>

const QList<FisRaceCategory>& fisRaceCategories()
{
    // FIS Alpine Data Booklet v1.16, section 1 (CORP from the 1st page of the points list)
    static const QList<FisRaceCategory> list = {
        { "OWG", QT_TRANSLATE_NOOP("FisList", "Olympic Winter Games"), 0 },
        { "WSC", QT_TRANSLATE_NOOP("FisList", "FIS World Ski Championships"), 0 },
        { "WC", QT_TRANSLATE_NOOP("FisList", "FIS World Cup"), 0 },
        { "WQUA", QT_TRANSLATE_NOOP("FisList", "World Cup Qualification"), 0 },
        { "COM", QT_TRANSLATE_NOOP("FisList", "World Cup Speed Event"), 0 },
        { "ANC", QT_TRANSLATE_NOOP("FisList", "Australian New Zealand Cup"), 1 },
        { "EC", QT_TRANSLATE_NOOP("FisList", "European Cup"), 1 },
        { "ECOM", QT_TRANSLATE_NOOP("FisList", "European Cup Speed Event"), 1 },
        { "EQUA", QT_TRANSLATE_NOOP("FisList", "European Cup Qualification"), 1 },
        { "FEC", QT_TRANSLATE_NOOP("FisList", "Far East Cup"), 1 },
        { "NAC", QT_TRANSLATE_NOOP("FisList", "Nor-Am Cup"), 1 },
        { "SAC", QT_TRANSLATE_NOOP("FisList", "South American Cup"), 1 },
        { "UVS", QT_TRANSLATE_NOOP("FisList", "Universiade"), 1 },
        { "WJC", QT_TRANSLATE_NOOP("FisList", "FIS Junior World Ski Championships"), 1 },
        { "YOG", QT_TRANSLATE_NOOP("FisList", "Youth Olympic Winter Games"), 1 },
        { "NC", QT_TRANSLATE_NOOP("FisList", "National Championships"), 2 },
        { "AWG", QT_TRANSLATE_NOOP("FisList", "Asian Winter Games"), 3 },
        { "CISM", QT_TRANSLATE_NOOP("FisList", "Military and Police"), 3 },
        { "CIT", QT_TRANSLATE_NOOP("FisList", "CIT Race"), 3 },
        { "CITWC", QT_TRANSLATE_NOOP("FisList", "CIT Race Arnold Lunn World Cup"), 3 },
        { "CORP", QT_TRANSLATE_NOOP("FisList", "Corporate"), 3 },
        { "FIS", QT_TRANSLATE_NOOP("FisList", "FIS Race"), 3 },
        { "FQUA", QT_TRANSLATE_NOOP("FisList", "FIS Qualification"), 3 },
        { "JUN", QT_TRANSLATE_NOOP("FisList", "Junior Race"), 3 },
        { "NJC", QT_TRANSLATE_NOOP("FisList", "National Junior Championships"), 3 },
        { "NJR", QT_TRANSLATE_NOOP("FisList", "National Junior Race"), 3 },
        { "UNI", QT_TRANSLATE_NOOP("FisList", "University Race"), 3 },
        { "EYOF", QT_TRANSLATE_NOOP("FisList", "European Youth Olympic Festival"), 3 },
        { "PARA", QT_TRANSLATE_NOOP("FisList", "PARA events"), 3 },
        { "ENL", QT_TRANSLATE_NOOP("FisList", "Entry League FIS races"), 4 },
    };
    return list;
}

const FisRaceCategory* fisRaceCategory(const QString& code)
{
    for (const auto& c : fisRaceCategories())
        if (code.compare(QLatin1String(c.code), Qt::CaseInsensitive) == 0)
            return &c;
    return nullptr;
}

QString FisListHeader::code() const
{
    if (number <= 0)
        return QString();
    return QStringLiteral("%1%2").arg(number).arg(season % 100, 2, 10, QLatin1Char('0'));
}

QString FisListHeader::seasonText() const
{
    if (season <= 0)
        return QString();
    return QStringLiteral("%1/%2").arg(season - 1).arg(season);
}

const FisCategoryPenalty* FisList::penalty(const QString& catcode) const
{
    for (const auto& p : penalties)
        if (p.catcode.compare(catcode, Qt::CaseInsensitive) == 0)
            return &p;
    return nullptr;
}

const FisDisciplineParams* FisList::disciplineParams(const QString& discipline, Gender g) const
{
    for (const auto& d : disciplines)
        if (d.gender == g && d.discipline.compare(discipline, Qt::CaseInsensitive) == 0)
            return &d;
    return nullptr;
}

QString fisDisciplineCode(Discipline d)
{
    switch (d) {
    case Discipline::Downhill: return QStringLiteral("DH");
    case Discipline::SuperG: return QStringLiteral("SG");
    case Discipline::Giant: return QStringLiteral("GS");
    case Discipline::Slalom: return QStringLiteral("SL");
    case Discipline::AlpineCombined: return QStringLiteral("AC");
    }
    return QString();
}
