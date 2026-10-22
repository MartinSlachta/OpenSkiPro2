#include "BibDraw.h"

#include <QRandomGenerator>
#include <QRegularExpression>
#include <algorithm>

namespace BibDraw {

QVector<int> byPoints(const QVector<Competitor>& list, Discipline d, QRandomGenerator& rng)
{
    QVector<int> with, without;
    for (int i = 0; i < list.size(); ++i)
        (pointsOf(list.at(i), d) >= 0 ? with : without) << i;
    // random order first, then a stable sort: equal points stay drawn
    std::shuffle(with.begin(), with.end(), rng);
    std::stable_sort(with.begin(), with.end(),
                     [&](int a, int b) { return pointsOf(list.at(a), d) < pointsOf(list.at(b), d); });
    std::shuffle(without.begin(), without.end(), rng);
    return with + without;
}

int firstGroupSize(const QVector<Competitor>& list, const QVector<int>& order, Discipline d, int best)
{
    int withPoints = 0;
    while (withPoints < order.size() && pointsOf(list.at(order.at(withPoints)), d) >= 0)
        ++withPoints;
    int n = qMin(qMax(0, best), withPoints);
    if (n == 0)
        return 0;
    const int last = pointsOf(list.at(order.at(n - 1)), d);
    while (n < withPoints && pointsOf(list.at(order.at(n)), d) == last)
        ++n;
    return n;
}

QVector<int> bestDraw(const QVector<Competitor>& list, Discipline d, int best, QRandomGenerator& rng, int* groupSize)
{
    QVector<int> order = byPoints(list, d, rng);
    const int n = firstGroupSize(list, order, d, best);
    std::shuffle(order.begin(), order.begin() + n, rng);
    if (groupSize)
        *groupSize = n;
    return order;
}

QVector<int> bestReversed(const QVector<Competitor>& list, Discipline d, int best, QRandomGenerator& rng, int* groupSize)
{
    QVector<int> order = byPoints(list, d, rng);
    const int n = firstGroupSize(list, order, d, best);
    std::reverse(order.begin(), order.begin() + n);
    if (groupSize)
        *groupSize = n;
    return order;
}

DoubleDrawGroups doubleDrawGroups(const QVector<Competitor>& list, const QVector<int>& order, Discipline d, bool increase)
{
    DoubleDrawGroups g;
    int withPoints = 0;
    while (withPoints < order.size() && pointsOf(list.at(order.at(withPoints)), d) >= 0)
        ++withPoints;
    auto tie = [&](int limit) {
        return limit > 0 && limit < withPoints
            && pointsOf(list.at(order.at(limit - 1)), d) == pointsOf(list.at(order.at(limit)), d);
    };
    g.firstEnd = qMin(7, withPoints);
    while (increase && tie(g.firstEnd))
        ++g.firstEnd;
    g.secondEnd = qMax(g.firstEnd, qMin(15, withPoints));
    while (increase && tie(g.secondEnd))
        ++g.secondEnd;
    g.tieAtLimit = tie(g.firstEnd) || tie(g.secondEnd);
    return g;
}

QVector<int> randomDoubleDraw(const DoubleDrawGroups& g, QRandomGenerator& rng)
{
    QVector<int> first, second;
    for (int i = 1; i <= g.firstEnd; ++i)
        first << i;
    for (int i = g.firstEnd + 1; i <= g.secondEnd; ++i)
        second << i;
    std::shuffle(first.begin(), first.end(), rng);
    std::shuffle(second.begin(), second.end(), rng);
    return first + second;
}

QVector<int> applyStartOrders(const QVector<int>& order, const QVector<int>& startOrder)
{
    QVector<int> out = order;
    for (int i = 0; i < startOrder.size() && i < order.size(); ++i) {
        const int pos = startOrder.at(i) - 1;
        if (pos >= 0 && pos < startOrder.size())
            out[pos] = order.at(i);
    }
    return out;
}

QSet<int> parseBibs(const QString& text, bool* ok)
{
    QSet<int> out;
    bool good = true;
    static const QRegularExpression sep(QStringLiteral("[,;\\s]+"));
    for (const QString& part : text.split(sep, Qt::SkipEmptyParts)) {
        const int dash = part.indexOf(QLatin1Char('-'));
        bool a = false, b = true;
        if (dash > 0) {
            const int from = part.left(dash).toInt(&a);
            const int to = part.mid(dash + 1).toInt(&b);
            if (a && b && from > 0 && to >= from && to - from < 10000) {
                for (int i = from; i <= to; ++i)
                    out.insert(i);
                continue;
            }
        } else {
            const int v = part.toInt(&a);
            if (a && v > 0) {
                out.insert(v);
                continue;
            }
        }
        good = false;
    }
    if (ok)
        *ok = good;
    return out;
}

QString formatBibs(const QSet<int>& bibs)
{
    QList<int> l = bibs.values();
    std::sort(l.begin(), l.end());
    QStringList parts;
    for (int i = 0; i < l.size();) {
        int j = i;
        while (j + 1 < l.size() && l.at(j + 1) == l.at(j) + 1)
            ++j;
        parts << (j > i + 1 ? QStringLiteral("%1-%2").arg(l.at(i)).arg(l.at(j)) : QString::number(l.at(i)));
        if (j == i + 1)
            parts << QString::number(l.at(j));
        i = j + 1;
    }
    return parts.join(QStringLiteral(", "));
}

QVector<int> bibNumbers(int first, int count, const QSet<int>& skip)
{
    QVector<int> out;
    for (int b = qMax(1, first); out.size() < count; ++b)
        if (!skip.contains(b))
            out << b;
    return out;
}

} // namespace BibDraw
