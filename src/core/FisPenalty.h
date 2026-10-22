#pragma once

#include "EventParams.h"
#include "Types.h"

#include <QHash>
#include <QString>
#include <QVector>

class Event;
struct FisList;

// Race points and penalty according to the FIS Points Rules (art. 4.4):
// race points P = (Tx / To - 1) x F; penalty = (A + B - C) / 10 where
// A = the 5 best FIS points among the 10 first of the race, B = the 5 best FIS points of the
// competitors who started (not DNS / NPS of run 1), C = race points of the 5 of A. Points above
// the maximum value (or missing) count as the maximum value. Applied penalty = penalty + Z +
// category adder, kept between the minimum and maximum penalty of the category (minimum doubled
// to 2 x maximum value when less than 3 of the 5 of A have FIS points or at least 3 classified
// competitors have none). Men and women are calculated separately. Points in hundredths.
namespace FisPenalty {

struct Line {
    int bib = 0;
    int rank = 0;              // rank in the race (0 = not classified)
    ResultStatus status = ResultStatus::Classified;
    int statusRun = 0;
    int listPoints = -1;       // FIS points of the competitor (-1 = none)
    int usedPoints = 0;        // points used: the maximum value when above it or missing
    int racePoints = -1;       // race points (time only, -1 = not classified)
    int usedRacePoints = -1;   // race points used for C (at most the maximum value)
    bool selected = false;     // one of the 5 of A
};

struct Result {
    Gender gender = Gender::Unknown;
    bool valid = false;        // parameters set and at least one competitor classified
    QString error;
    QVector<Line> top10;       // classified, rank <= 10 (ties at the 10th rank included)
    QVector<Line> best5Start;  // the 5 of B
    int missingA = 0;          // places of A / C filled with the maximum value (less than 5)
    int missingB = 0;
    int sumA = 0, sumB = 0, sumC = 0;
    int calculated = 0;        // (A + B - C) / 10
    int zValue = 0, adder = 0;
    int minPenalty = 0, maxPenalty = 0; // as applied (minimum maybe doubled)
    bool doubledMinimum = false;
    int computedApplied = 0;   // calculated + Z + adder, within min / max
    int applied = 0;           // computedApplied or the value typed by the TD
    bool overridden = false;
    QHash<int, int> racePoints; // bib -> race points (time only)

    int totalPoints(int bib) const; // race points + applied penalty, -1 if not classified
};

// Genders of the event (men first); competitors without gender count as men.
QVector<Gender> genders(const Event& ev);
Gender genderOf(Gender g);

// Race points (hundredths) of time `timeMs` for the winner time `winnerMs`.
int racePoints(qint64 timeMs, qint64 winnerMs, double fValue);
// Rounding of the FIS points rules: x.xx4 down, x.xx5 up. `num / den` in hundredths.
int roundDiv(qint64 num, qint64 den);

Result compute(const Event& ev, Gender g);
// With other constants than the ones of the event (dialog).
Result compute(const Event& ev, Gender g, const FisPointsParams& p);

// Constants of the FIS list for the discipline, category / level and gender of the event.
// Returns false (with *error) when the list has no data for them.
bool loadFromList(const FisList& list, Discipline d, const QString& catcode, Gender g, FisPointsParams& out,
                  QString* error);

} // namespace FisPenalty
