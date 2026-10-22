#pragma once

#include "Types.h"

#include <QDate>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QVector>
#include <array>

// Race category (Catcode) with its race level, FIS Alpine Data Booklet section 1.
struct FisRaceCategory {
    const char* code;
    const char* description; // untranslated
    int level;
};

// All alpine race categories in display order (built in, like the booklet table).
const QList<FisRaceCategory>& fisRaceCategories();
const FisRaceCategory* fisRaceCategory(const QString& code);

// Header of a FIS points list (AL<list><season>hdr.csv).
struct FisListHeader {
    int listId = 0;
    int season = 0;     // 2027 = season 2026/27
    int number = 0;     // list number in the season
    QString name;       // "16th FIS points list 2026/27"
    QDate calculationDate;
    QDate startRaceDate;
    QDate endRaceDate;
    QDate validFrom;
    QDate validTo;
    QString lastUpdate;

    // "1627": list number + season, as in the archive names and SkiPro's "From list"
    QString code() const;
    QString seasonText() const; // "2026/2027"
    bool isExpired(const QDate& today = QDate::currentDate()) const { return validTo.isValid() && today > validTo; }
};

// Minimum / maximum penalty of a race category (AL...cat.csv).
struct FisCategoryPenalty {
    QString catcode;
    double minPenalty = 0.0;
    double maxPenalty = 0.0;
};

// Penalty calculation constants of a discipline and gender (AL...dis.csv).
struct FisDisciplineParams {
    QString discipline; // DH, SG, GS, SL, AC
    Gender gender = Gender::Unknown;
    double zValue = 0.0;
    double fValue = 0.0;
    double maxPoints = 0.0;
    std::array<double, 5> adders{ {0, 0, 0, 0, 0} }; // per race level 0..4
};

// Competitor of the list with the points of each discipline.
struct FisCompetitor {
    int competitorId = 0;
    QString fisCode;
    QString lastName;
    QString firstName;
    Gender gender = Gender::Unknown;
    int year = 0;
    QString nation;
    std::array<int, kDisciplineCount> points{ {-1, -1, -1, -1, -1} }; // hundredths, -1 = none (order of Discipline)
};

struct FisList {
    FisListHeader header;
    QList<FisCategoryPenalty> penalties;
    QList<FisDisciplineParams> disciplines;
    QVector<FisCompetitor> competitors;
    QString source;       // imported archive / folder
    QDateTime importedAt;

    bool isEmpty() const { return header.listId == 0 && competitors.isEmpty(); }
    const FisCategoryPenalty* penalty(const QString& catcode) const;
    const FisDisciplineParams* disciplineParams(const QString& discipline, Gender g) const;
};

// FIS discipline code of a discipline (Discipline::Giant -> "GS").
QString fisDisciplineCode(Discipline d);
