#include "Competitor.h"
#include "TimeUtil.h"

#include <QVariant>

QString Competitor::fullName() const
{
    if (firstName.isEmpty())
        return lastName;
    if (lastName.isEmpty())
        return firstName;
    return lastName + QLatin1Char(' ') + firstName;
}

static int pointsIndex(int fieldId)
{
    if (fieldId >= CompField::PointsDH && fieldId <= CompField::PointsAC)
        return fieldId - CompField::PointsDH;
    return -1;
}

QString Competitor::value(int fieldId) const
{
    switch (fieldId) {
    case CompField::Bib: return bib > 0 ? QString::number(bib) : QString();
    case CompField::Transponder: return transponder;
    case CompField::LastName: return lastName;
    case CompField::FirstName: return firstName;
    case CompField::Code: return code;
    case CompField::Gender: return genderText(gender);
    case CompField::Year: return year > 0 ? QString::number(year) : QString();
    case CompField::Category: return category;
    case CompField::Nation: return nation;
    case CompField::Region: return region;
    case CompField::Club: return club;
    case CompField::Team: return team;
    case CompField::FromList: return fromList;
    case CompField::Custom: return custom;
    case CompField::Name: return fullName();
    default: break;
    }
    const int pi = pointsIndex(fieldId);
    if (pi >= 0)
        return TimeUtil::formatPoints(points[size_t(pi)]);
    return QString();
}

void Competitor::setValue(int fieldId, const QString& v)
{
    const QString t = v.trimmed();
    switch (fieldId) {
    case CompField::Bib: bib = t.toInt(); return;
    case CompField::Transponder: transponder = t; return;
    case CompField::LastName: lastName = t; return;
    case CompField::FirstName: firstName = t; return;
    case CompField::Code: code = t; return;
    case CompField::Gender: gender = parseGender(t); return;
    case CompField::Year: year = t.toInt(); return;
    case CompField::Category: category = t; return;
    case CompField::Nation: nation = t; return;
    case CompField::Region: region = t; return;
    case CompField::Club: club = t; return;
    case CompField::Team: team = t; return;
    case CompField::FromList: fromList = t; return;
    case CompField::Custom: custom = t; return;
    case CompField::Name: {
        // "LAST First Second": first token(s) in upper case are the last name
        const QStringList parts = t.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        QStringList last, first;
        for (const QString& p : parts) {
            if (first.isEmpty() && p == p.toUpper())
                last << p;
            else
                first << p;
        }
        if (last.isEmpty() && !first.isEmpty())
            last << first.takeFirst();
        lastName = last.join(QLatin1Char(' '));
        firstName = first.join(QLatin1Char(' '));
        return;
    }
    default: break;
    }
    const int pi = pointsIndex(fieldId);
    if (pi >= 0)
        points[size_t(pi)] = TimeUtil::parsePoints(t);
}

QVariant Competitor::sortValue(int fieldId) const
{
    switch (fieldId) {
    case CompField::Bib: return bib > 0 ? bib : -1;
    case CompField::Year: return year > 0 ? year : -1;
    default: break;
    }
    const int pi = pointsIndex(fieldId);
    if (pi >= 0)
        return points[size_t(pi)] < 0 ? -1 : points[size_t(pi)];
    return value(fieldId).toLower();
}

const QList<CompetitorFieldDef>& competitorFieldDefs()
{
    static const QList<CompetitorFieldDef> defs = {
        { CompField::Bib, QT_TRANSLATE_NOOP("Competitor", "Bib."), true, true },
        { CompField::Transponder, QT_TRANSLATE_NOOP("Competitor", "Transponder"), false, false },
        { CompField::LastName, QT_TRANSLATE_NOOP("Competitor", "Last Name"), false, true },
        { CompField::FirstName, QT_TRANSLATE_NOOP("Competitor", "First Name"), false, true },
        { CompField::Name, QT_TRANSLATE_NOOP("Competitor", "Name"), false, true },
        { CompField::Code, QT_TRANSLATE_NOOP("Competitor", "Code"), false, true },
        { CompField::Gender, QT_TRANSLATE_NOOP("Competitor", "Gender"), false, true },
        { CompField::Year, QT_TRANSLATE_NOOP("Competitor", "Year"), true, true },
        { CompField::Category, QT_TRANSLATE_NOOP("Competitor", "Category"), false, true },
        { CompField::Nation, QT_TRANSLATE_NOOP("Competitor", "Nation"), false, true },
        { CompField::Region, QT_TRANSLATE_NOOP("Competitor", "Region"), false, true },
        { CompField::Club, QT_TRANSLATE_NOOP("Competitor", "Club"), false, true },
        { CompField::Team, QT_TRANSLATE_NOOP("Competitor", "Team"), false, false },
        { CompField::FromList, QT_TRANSLATE_NOOP("Competitor", "From list"), false, true },
        { CompField::PointsDH, QT_TRANSLATE_NOOP("Competitor", "Downhill"), true, true },
        { CompField::PointsSG, QT_TRANSLATE_NOOP("Competitor", "Super-G"), true, true },
        { CompField::PointsGS, QT_TRANSLATE_NOOP("Competitor", "Giant"), true, true },
        { CompField::PointsSL, QT_TRANSLATE_NOOP("Competitor", "Slalom"), true, true },
        { CompField::PointsAC, QT_TRANSLATE_NOOP("Competitor", "Alpine Combined"), true, true },
        { CompField::Custom, QT_TRANSLATE_NOOP("Competitor", "Custom"), false, false },
    };
    return defs;
}

const CompetitorFieldDef* competitorFieldDef(int id)
{
    for (const auto& d : competitorFieldDefs())
        if (d.id == id)
            return &d;
    return nullptr;
}

QString genderText(Gender g)
{
    switch (g) {
    case Gender::Male: return QCoreApplication::translate("Competitor", "M");
    case Gender::Female: return QCoreApplication::translate("Competitor", "F");
    default: return QString();
    }
}

Gender parseGender(const QString& textIn)
{
    const QString t = textIn.trimmed().toUpper();
    if (t.isEmpty())
        return Gender::Unknown;
    if (t == QLatin1String("1") || t.startsWith(QLatin1Char('M')) || t.startsWith(QLatin1Char('H'))
        || t.startsWith(QLatin1String("MU")))
        return Gender::Male;
    if (t == QLatin1String("2") || t.startsWith(QLatin1Char('F')) || t.startsWith(QLatin1Char('W'))
        || t.startsWith(QLatin1Char('L')) || t.startsWith(QLatin1Char('D')) || t.startsWith(QLatin1Char('Z'))
        || t.startsWith(QStringLiteral("Ž")))
        return Gender::Female;
    return Gender::Unknown;
}

int pointsFieldFor(Discipline d)
{
    return CompField::PointsDH + int(d);
}
