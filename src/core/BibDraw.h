#pragma once

#include "Competitor.h"

#include <QSet>
#include <QString>
#include <QVector>

class QRandomGenerator;

// Start order and bib assignment according to the FIS rules (ICR 621):
// - first group of the X (15) best by FIS points drawn, increased when several
//   competitors share the X-th rank (621.3); or reversed (SkiPro "X best (reversed)")
// - the others in the order of their points (equal points drawn)
// - the competitors without points drawn in the last group
// - NC / NJC double draw (621.4): the 15 best in two groups, 1-7 and 8-15
// The functions work on indices into the given competitor list.
namespace BibDraw {

// Points used for the order (hundredths, -1 = none).
inline int pointsOf(const Competitor& c, Discipline d) { return c.pointsFor(d); }

// All competitors by points, equal points in random order, without points drawn at the end.
QVector<int> byPoints(const QVector<Competitor>& list, Discipline d, QRandomGenerator& rng);

// Size of the first group: the `best` first of the points order, increased while the next
// competitor has the same points as the last one; never includes competitors without points.
int firstGroupSize(const QVector<Competitor>& list, const QVector<int>& order, Discipline d, int best);

// X best (draw): first group drawn, the rest as byPoints(). *groupSize = size of the drawn group.
QVector<int> bestDraw(const QVector<Competitor>& list, Discipline d, int best, QRandomGenerator& rng,
                      int* groupSize = nullptr);
// X best (reversed): first group in reverse order of the points, the rest as byPoints().
QVector<int> bestReversed(const QVector<Competitor>& list, Discipline d, int best, QRandomGenerator& rng,
                          int* groupSize = nullptr);

// Double draw (621.4): positions in the points order that make the two groups.
struct DoubleDrawGroups {
    int firstEnd = 0;  // group 1 = order[0 .. firstEnd)
    int secondEnd = 0; // group 2 = order[firstEnd .. secondEnd)
    bool tieAtLimit = false; // same points across a group limit (7/8 or 15/16)
};
// `increase`: a group is increased while the next competitor has the same points as the
// last one of the group (like the first group of 621.3), e.g. 1-8 and 9-15.
DoubleDrawGroups doubleDrawGroups(const QVector<Competitor>& list, const QVector<int>& order, Discipline d,
                                  bool increase = false);
// Random start orders for the double draw: startOrder[i] for order[i], i < secondEnd
// (1..firstEnd within group 1, firstEnd+1..secondEnd within group 2).
QVector<int> randomDoubleDraw(const DoubleDrawGroups& g, QRandomGenerator& rng);
// Applies start orders (1-based, for order[0 .. startOrder.size())) to the points order.
QVector<int> applyStartOrders(const QVector<int>& order, const QVector<int>& startOrder);

// "13, 27, 40-42" -> {13, 27, 40, 41, 42}; *ok = false on a syntax error.
QSet<int> parseBibs(const QString& text, bool* ok = nullptr);
QString formatBibs(const QSet<int>& bibs); // ranges joined: "13, 27, 40-42"
// `count` bibs from `first`, skipping `skip`.
QVector<int> bibNumbers(int first, int count, const QSet<int>& skip);

} // namespace BibDraw
