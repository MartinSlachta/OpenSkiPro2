#pragma once

#include <QtGlobal>

// Result status of a competitor in one run. Numeric values match SkiPro
// (TTIMEINFOS_HEATn.C_STATUS) so imported data maps 1:1; NPS (not permitted to start,
// ICR 628) does not exist in SkiPro.
enum class ResultStatus : int {
    NotProcessed = -1,
    Classified = 0,
    DNS = 1,
    DNF = 2,
    DSQ = 3,
    NPS = 4,
};

enum class Gender : int {
    Unknown = 0,
    Male = 1,
    Female = 2,
};

enum class Regulation : int {
    FIS = 0,
    National = 1,
};

enum class Sport : int {
    Alpine = 0,
};

// Order and values match SkiPro's DISCIPLINE parameter for alpine events.
enum class Discipline : int {
    Downhill = 0,
    SuperG = 1,
    Giant = 2,
    Slalom = 3,
    AlpineCombined = 4,
};
constexpr int kDisciplineCount = 5;

// Display / truncation precision of times.
enum class Precision : int {
    Second = 0,
    Tenth = 1,
    Hundredth = 2,
    Thousandth = 3,
};

enum class FinalRankingType : int {
    TotalOfRuns = 0,
    BestOfRuns = 1,
};

// Measuring points of a run. Intermediates are numbered 1..kMaxInter.
namespace Point {
constexpr int Start = 0;
constexpr int Finish = 100;
}
constexpr int kMaxInter = 3;
constexpr int kMaxRuns = 9;

// Flags of a raw time record (impulse). DNS / DNF values match SkiPro.
namespace RecordFlag {
constexpr int None = 0;
constexpr int Inactive = 0x0001;   // ignored for computation (false impulse, cancelled)
constexpr int Manual = 0x0002;     // inserted manually by the operator
constexpr int Cancelled = 0x0004;  // cancelled on the timing device (TC)
constexpr int DeviceKey = 0x0008;  // manual key of the timing device (CP540 M1..M4)
constexpr int NoTime = 0x0010;     // passage without time ("No time")
constexpr int Inserted = 0x0020;   // inserted passage (copy of another record)
constexpr int StatusEntry = 0x0040; // record created only to carry a status (DNS/NPS/DNF/DSQ), no time
constexpr int DNS = 0x0100;
constexpr int DNF = 0x0200;
constexpr int DSQ = 0x0400;
constexpr int NPS = 0x0800;
}
