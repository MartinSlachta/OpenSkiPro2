#include "FisCompetitors.h"
#include "Event.h"

#include <QHash>

namespace FisCompetitors {

QString normalizedCode(const QString& code)
{
    QString c = code.trimmed().toUpper();
    c.remove(QLatin1Char(' '));
    if (c.startsWith(QLatin1String("FIS")) && c.size() > 3)
        c = c.mid(3);
    return c;
}

Competitor fromFis(const FisCompetitor& f, const FisListHeader& h, const CategoryGrid& categories)
{
    Competitor c;
    c.lastName = f.lastName;
    c.firstName = f.firstName;
    c.code = f.fisCode;
    c.gender = f.gender;
    c.year = f.year;
    c.nation = f.nation;
    c.points = f.points;
    c.fromList = h.code();
    c.category = categories.categoryFor(c.gender, c.year);
    return c;
}

QList<int> updatableFields()
{
    return { CompField::LastName, CompField::FirstName, CompField::Gender, CompField::Year, CompField::Nation,
             CompField::PointsDH, CompField::PointsSG, CompField::PointsGS, CompField::PointsSL, CompField::PointsAC,
             CompField::FromList };
}

UpdateResult update(Event* ev, const FisList& list, const QList<int>& fields)
{
    UpdateResult r;
    QHash<QString, int> byCode;
    for (int i = 0; i < list.competitors.size(); ++i)
        byCode.insert(normalizedCode(list.competitors.at(i).fisCode), i);
    const QVector<Competitor> competitors = ev->competitors();
    ev->beginBatch();
    for (const Competitor& c : competitors) {
        const int idx = c.code.trimmed().isEmpty() ? -1 : byCode.value(normalizedCode(c.code), -1);
        if (idx < 0) {
            QString who = c.fullName();
            if (c.bib > 0)
                who = QString::number(c.bib) + QLatin1Char(' ') + who;
            if (!c.code.trimmed().isEmpty())
                who += QStringLiteral(" (%1)").arg(c.code);
            r.notUpdated << who;
            continue;
        }
        const FisCompetitor& f = list.competitors.at(idx);
        Competitor n = c;
        for (int field : fields) {
            switch (field) {
            case CompField::LastName: n.lastName = f.lastName; break;
            case CompField::FirstName: n.firstName = f.firstName; break;
            case CompField::Gender: n.gender = f.gender; break;
            case CompField::Year: n.year = f.year; break;
            case CompField::Nation: n.nation = f.nation; break;
            case CompField::FromList: n.fromList = list.header.code(); break;
            default:
                if (field >= CompField::PointsDH && field <= CompField::PointsAC)
                    n.points[size_t(field - CompField::PointsDH)] = f.points[size_t(field - CompField::PointsDH)];
                break;
            }
        }
        if (fields.contains(CompField::Gender) || fields.contains(CompField::Year)) {
            // the category follows a new gender / year when it was the automatic one
            const QString oldAuto = ev->categories().categoryFor(c.gender, c.year);
            if (c.category.isEmpty() || c.category == oldAuto)
                n.category = ev->categories().categoryFor(n.gender, n.year);
        }
        ev->updateCompetitor(n);
        ++r.updated;
    }
    ev->commitBatch();
    return r;
}

} // namespace FisCompetitors
