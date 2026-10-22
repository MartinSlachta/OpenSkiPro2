#pragma once

#include <QList>
#include <QString>

struct Competitor;

// Condition on a competitor field (SkiPro / SkiAlp "filters").
struct GroupCriterion {
    enum Op : int { Equal = 0, NotEqual = 1, BeginsWith = 2, Between = 3, Contains = 4, Greater = 5, Less = 6 };
    int field = 10; // CompField::Category
    int op = Equal;
    QString value;
    QString value2; // upper bound of Between

    bool matches(const Competitor& c) const;
};

// Output group: the competitors printed together under one heading in the reports
// (e.g. one category), the rankings are computed inside the group.
struct OutputGroup {
    QString label;
    bool anyMode = false; // true: one condition is enough, false: all of them
    QList<GroupCriterion> criteria;

    bool matches(const Competitor& c) const;
    QString describe() const; // "Category = U12 and Gender = F"
};

QString outputGroupsToString(const QList<OutputGroup>& groups);
QList<OutputGroup> outputGroupsFromString(const QString& s);
QString groupOperatorText(int op);
