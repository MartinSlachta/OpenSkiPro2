#pragma once

#include "Types.h"

#include <QCoreApplication>
#include <QList>
#include <QString>
#include <QVariant>
#include <array>

// Competitor field identifiers. Values follow SkiPro's COMPETITORS_INFO_n
// numbering so field configurations can be imported.
namespace CompField {
constexpr int Bib = 1;
constexpr int Transponder = 2;
constexpr int LastName = 4;
constexpr int FirstName = 5;
constexpr int Code = 6;
constexpr int Gender = 8;
constexpr int Year = 9;
constexpr int Category = 10;
constexpr int Nation = 12;
constexpr int Region = 13;
constexpr int Club = 14;
constexpr int Team = 15;
constexpr int FromList = 16;
constexpr int PointsDH = 21;
constexpr int PointsSG = 22;
constexpr int PointsGS = 23;
constexpr int PointsSL = 24;
constexpr int PointsAC = 25;
constexpr int Custom = 32;
constexpr int Name = 38; // "LAST First"
}

struct Competitor {
    int id = 0;          // database key
    int bib = 0;
    QString transponder;
    QString lastName;
    QString firstName;
    QString code;        // federation code (FIS code)
    Gender gender = Gender::Unknown;
    int year = 0;
    QString category;
    QString nation;
    QString region;
    QString club;
    QString team;
    QString fromList;    // points list number the data comes from
    std::array<int, kDisciplineCount> points{ {-1, -1, -1, -1, -1} }; // hundredths, -1 = none
    QString custom;

    QString fullName() const; // "LAST First"
    int pointsFor(Discipline d) const { return points[size_t(d)]; }

    QString value(int fieldId) const;
    void setValue(int fieldId, const QString& value);
    // Sort key for numeric fields (bib, year, points; -1 = empty); falls back to text.
    QVariant sortValue(int fieldId) const;
};

struct CompetitorFieldDef {
    int id;
    const char* label; // untranslated, use QCoreApplication::translate("Competitor", label)
    bool numeric;
    bool defaultActive;
};

// All known competitor fields in display order.
const QList<CompetitorFieldDef>& competitorFieldDefs();
const CompetitorFieldDef* competitorFieldDef(int id);
QString genderText(Gender g);
Gender parseGender(const QString& text);
int pointsFieldFor(Discipline d);
