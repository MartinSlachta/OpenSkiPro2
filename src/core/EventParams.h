#pragma once

#include "OutputGroup.h"
#include "Types.h"

#include <QDate>
#include <QFont>
#include <QList>
#include <QMap>
#include <QPair>
#include <QRectF>
#include <QSet>
#include <QString>
#include <QVariantMap>
#include <algorithm>
#include <array>

struct GeneralParams {
    QString title1;      // event title
    QString title2;      // discipline line
    QDate date;
    QString location;
    QString nation;
    QString organisation;
    QString timingBy;    // timing service provider
    QString codex;
    bool useFreeHeader = false;
};

struct EventTypeParams {
    Regulation regulation = Regulation::FIS;
    QString fisCategory = QStringLiteral("FIS"); // race category (Catcode) of a FIS race, see fisRaceCategories()
    Sport sport = Sport::Alpine;
    Discipline discipline = Discipline::Giant;
    int runCount = 2;
    int interCount = 0;
    Precision timePrecision = Precision::Hundredth;
    Precision speedPrecision = Precision::Hundredth;
    Precision avgSpeedPrecision = Precision::Hundredth;
    double courseLength = 0.0;
    FinalRankingType finalType = FinalRankingType::TotalOfRuns;
    std::array<bool, kMaxRuns> runsUsed{ {true, true, true, true, true, true, true, true, true} };
    std::array<QString, kMaxRuns> runNames;

    QString runName(int run) const; // run is 1-based
};

// Header (jury / technical data). Values are stored by key, see HeaderKeys.
struct HeaderParams {
    QMap<QString, QString> values;
    QString value(const QString& key) const { return values.value(key); }
    void setValue(const QString& key, const QString& v) { values.insert(key, v); }
    // Course data of a run ("run<i>.<key>"), each run may use another course; events of
    // older versions have one value for all the runs (stored without the run prefix).
    QString runValue(int run, const QString& key) const;
    // Drop = start - finish altitude ("1730", "1588" -> "142"), empty when not numbers.
    static QString dropOf(const QString& start, const QString& finish);
};

namespace HeaderKeys {
// person rows: "<role>.code", "<role>.name", "<role>.nation"
inline const char* const TechnicalDelegate = "td";
inline const char* const Referee = "ref";
inline const char* const AssistantReferee = "aref";
inline const char* const ChiefOfRace = "chief";
inline const char* const CourseChief = "coursechief";
inline const char* const StartReferee = "startref";
inline const char* const FinishReferee = "finishref";
// per run (run index 1/2): "run<i>.setter.*", "run<i>.fore<A..E>.*", "run<i>.gates", "run<i>.directions", "run<i>.starttime"
// and the course data "run<i>.course", "run<i>.start", ... (see HeaderParams::runValue)
inline const char* const Course = "course";
inline const char* const StartAltitude = "start";
inline const char* const FinishAltitude = "finish";
inline const char* const Drop = "drop";
inline const char* const Length = "length";
inline const char* const Homologation = "homologation";
inline const char* const Weather = "weather";
inline const char* const Snow = "snow";
inline const char* const TempStart = "temp.start";
inline const char* const TempFinish = "temp.finish";
inline const char* const ListNumber = "list";
}

struct LogoSlot {
    QString path;
    QRectF rect; // relative to the page (0..1), keeps working in portrait and landscape
};

struct PrintingParams {
    QFont fontTitle1;
    QFont fontTitle2;
    QFont fontDocTitle;
    QFont fontFooter;
    QFont fontLabels;
    QFont fontLines;
    bool showTitle1 = true;
    bool showTitle2 = true;
    bool autoAdjust = true;
    bool showAsTable = false;
    bool grayRows = true;
    int grayLevel = 10;          // percent
    // margins in mm: top, bottom, left, right
    std::array<int, 4> marginsFirst{ {25, 25, 5, 5} };
    std::array<int, 4> marginsOther{ {25, 25, 5, 5} };
    int titleAlignFirst = 1;     // 0 left, 1 center, 2 right
    int titleAlignOther = 1;
    std::array<LogoSlot, 8> logos;
    bool logosFirstPageOnly = false;
    bool showAppLogo = true;
    PrintingParams();
};

struct TimingOptions {
    bool showNumbersToFollow = true;
    bool showMarkMustFinish = true;
    bool showNamesInFollow = false;
    bool confirmInsertStartList = true;
    bool invertGrid = true;
    bool showNetTime = false;
    bool askBibIfUnknown = false;
    bool showSeqColumn = true;     // Seq column in the timing records
    int forerunnerFrom = 901;
    int forerunnerTo = 999;
    std::array<qint64, 4> quickPenaltiesMs{ {0, 0, 0, 0} };
    qint64 delayStartMs = 0;
    qint64 delayFinishMs = 0;
    qint64 delayInterMs = 0;
    std::array<qint64, kMaxInter> minTimeInterMs{ {0, 0, 0} };
    qint64 minTimeFinishMs = 0;
    // Start list of run 2+ : number of best ranked competitors whose order is reversed (0 = none)
    int reverseCount = 0;
    // ranking of the timing window (SkiPro "Software Parameters / Ranking")
    bool rankingEvent = true;        // ranking of the event after this run, false: ranking of the run
    int rankingField = 0;            // competitor field shown in brackets after the name (0 = none)
    bool rankingByGroups = false;    // by the output groups of the event, ranked inside each group
};

struct CompetitorFieldConfig {
    QMap<int, QString> labels;   // custom label per field id (empty -> default)
    QSet<int> active;            // fields active in this event
    QList<QPair<int, int>> grid; // (field id, width px) shown in the competitors grid
    QString label(int fieldId) const;
    bool isActive(int fieldId) const { return active.contains(fieldId); }
    static CompetitorFieldConfig defaults();
};

// Constants of the FIS points / penalty calculation of one gender (copied from the FIS
// list, may be changed). Points in hundredths.
struct FisPointsParams {
    double fValue = 0.0;       // F value of the discipline (0 = not set)
    int maxValue = 0;          // maximum value (max points) of the discipline, e.g. SL 16500
    int zValue = 0;            // correction value (Z)
    int adder = 0;             // category adder of the race level
    int minPenalty = 0;        // minimum / maximum penalty of the race category
    int maxPenalty = 99900;
    int appliedOverride = -1;  // applied penalty typed by the TD, -1 = calculated

    bool isSet() const { return fValue > 0.0; }
};

struct FisPointsSettings {
    QString list;                              // FIS points list used ("1627")
    std::array<FisPointsParams, 2> gender;     // 0 men, 1 women
    FisPointsParams& forGender(Gender g) { return gender[g == Gender::Female ? 1 : 0]; }
    const FisPointsParams& forGender(Gender g) const { return gender[g == Gender::Female ? 1 : 0]; }
};

struct EventParams {
    GeneralParams general;
    EventTypeParams type;
    HeaderParams header;
    PrintingParams printing;
    TimingOptions timing;
    CompetitorFieldConfig fields;
    QList<OutputGroup> outputGroups; // groups of the printed outputs (categories, ...)
    FisPointsSettings fisPoints;     // FIS points / penalty calculation

    QVariantMap toMap() const;
    void fromMap(const QVariantMap& map);
    static EventParams defaults();
};

QString disciplineName(Discipline d);
QString fontToString(const QFont& f);
QFont fontFromString(const QString& s, const QFont& fallback);
