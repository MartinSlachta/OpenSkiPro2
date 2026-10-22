#include "OutputGroup.h"
#include "Competitor.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

// numbers compare as numbers, other values as text
int compareValues(const QString& a, const QString& b)
{
    bool okA = false, okB = false;
    const double da = QString(a).replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&okA);
    const double db = QString(b).replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&okB);
    if (okA && okB)
        return da < db ? -1 : (da > db ? 1 : 0);
    return QString::localeAwareCompare(a.toLower(), b.toLower());
}

QString tr(const char* s)
{
    return QCoreApplication::translate("OutputGroup", s);
}

} // namespace

bool GroupCriterion::matches(const Competitor& c) const
{
    if (field == CompField::Gender && (op == Equal || op == NotEqual)) {
        const bool same = c.gender == parseGender(value);
        return op == Equal ? same : !same;
    }
    const QString v = c.value(field);
    switch (op) {
    case Equal: return v.compare(value, Qt::CaseInsensitive) == 0;
    case NotEqual: return v.compare(value, Qt::CaseInsensitive) != 0;
    case BeginsWith: return v.startsWith(value, Qt::CaseInsensitive);
    case Contains: return v.contains(value, Qt::CaseInsensitive);
    case Between: return !v.isEmpty() && compareValues(v, value) >= 0 && compareValues(v, value2) <= 0;
    case Greater: return !v.isEmpty() && compareValues(v, value) > 0;
    case Less: return !v.isEmpty() && compareValues(v, value) < 0;
    default: return false;
    }
}

bool OutputGroup::matches(const Competitor& c) const
{
    if (criteria.isEmpty())
        return true;
    for (const GroupCriterion& cr : criteria) {
        const bool m = cr.matches(c);
        if (anyMode && m)
            return true;
        if (!anyMode && !m)
            return false;
    }
    return !anyMode;
}

QString groupOperatorText(int op)
{
    switch (op) {
    case GroupCriterion::Equal: return tr("is equal to");
    case GroupCriterion::NotEqual: return tr("is different from");
    case GroupCriterion::BeginsWith: return tr("begins with");
    case GroupCriterion::Between: return tr("is between");
    case GroupCriterion::Contains: return tr("contains");
    case GroupCriterion::Greater: return tr("is greater than");
    case GroupCriterion::Less: return tr("is less than");
    default: return QString();
    }
}

QString OutputGroup::describe() const
{
    QStringList parts;
    for (const GroupCriterion& c : criteria) {
        const CompetitorFieldDef* def = competitorFieldDef(c.field);
        const QString field = def ? QCoreApplication::translate("Competitor", def->label) : QString::number(c.field);
        QString value = c.value;
        if (c.op == GroupCriterion::Between)
            value += QStringLiteral(" - ") + c.value2;
        parts << QStringLiteral("%1 %2 %3").arg(field, groupOperatorText(c.op), value);
    }
    return parts.join(anyMode ? tr(" or ") : tr(" and "));
}

QString outputGroupsToString(const QList<OutputGroup>& groups)
{
    QJsonArray arr;
    for (const OutputGroup& g : groups) {
        QJsonObject o;
        o[QStringLiteral("label")] = g.label;
        o[QStringLiteral("any")] = g.anyMode;
        QJsonArray crit;
        for (const GroupCriterion& c : g.criteria)
            crit.append(QJsonArray{ c.field, c.op, c.value, c.value2 });
        o[QStringLiteral("criteria")] = crit;
        arr.append(o);
    }
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

QList<OutputGroup> outputGroupsFromString(const QString& s)
{
    QList<OutputGroup> groups;
    const QJsonDocument doc = QJsonDocument::fromJson(s.toUtf8());
    for (const auto& v : doc.array()) {
        const QJsonObject o = v.toObject();
        OutputGroup g;
        g.label = o.value(QStringLiteral("label")).toString();
        g.anyMode = o.value(QStringLiteral("any")).toBool();
        for (const auto& cv : o.value(QStringLiteral("criteria")).toArray()) {
            const QJsonArray a = cv.toArray();
            GroupCriterion c;
            c.field = a.at(0).toInt();
            c.op = a.at(1).toInt();
            c.value = a.at(2).toString();
            c.value2 = a.at(3).toString();
            g.criteria << c;
        }
        groups << g;
    }
    return groups;
}
