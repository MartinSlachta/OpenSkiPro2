#pragma once

#include <QList>
#include <QString>
#include <array>

enum class ReportType {
    Competitors,
    CompetitorsPoints,
    StartList,
    RunRanking,
    RankingAfterRun,
    FinalRanking,
    TimeRecords,
    TimeRecordsByBib,
    TimingPaper,
    FinalRankingPoints,   // final ranking with the race points (FIS points + penalty)
    PenaltyCalculation,   // FIS penalty calculation sheet
};
constexpr int kReportTypeLast = int(ReportType::PenaltyCalculation);

// Report field identifiers. Competitor fields use their own ids (1..38).
namespace ReportField {
constexpr int Rank = 101;          // rank, or order in a start list
constexpr int Time = 102;
constexpr int Gap = 103;
constexpr int Status = 104;
constexpr int Reason = 105;
constexpr int Penalty = 106;
constexpr int Points = 107;        // list points of the event discipline
constexpr int StartTime = 108;     // planned start time
constexpr int Dotted = 109;        // empty column with dots, for hand written notes
constexpr int PreviousRun = 110;   // start list: points (run 1) / time of the previous run
constexpr int RacePoints = 111;    // FIS race points of the time, without the penalty (booklet)
constexpr int Best5 = 112;         // penalty sheet: points used for A / B (best 5)
constexpr int TimePoints = 113;    // penalty sheet: race points of the time only (C)
constexpr int RunTime = 200;       // + run number (201..209): time of run k
constexpr int RunTimeRank = 210;   // + run number (211..219): "time (rank)" of run k
constexpr int RunRank = 220;       // + run number (221..229): rank of run k only
constexpr int Inter = 300;         // + inter number (301..303): intermediate time (rank)
constexpr int RecSeq = 401;
constexpr int RecStatus = 402;
constexpr int RecNum = 403;
constexpr int RecHour = 404;
constexpr int RecTime = 405;
constexpr int RecPoint = 406;
constexpr int PaperSeq = 501;
constexpr int PaperDevice = 502;   // timing device 1 / 2
constexpr int PaperReceived = 503; // PC time of the reception
constexpr int PaperText = 504;     // raw line
}

struct ReportColumn {
    int field = 0;
    int align = 0;   // 0 left, 1 center, 2 right
    int width = 10;  // relative width (characters)
    int field2 = 0;  // field printed on a second line of the cell (0 = none)
    bool bold = false;
    QString title;   // column title, empty = the name of the field
};

struct ReportFieldDef {
    int field;
    QString label;
    int defaultAlign;
    int defaultWidth;
};

struct ReportFormat {
    ReportType type = ReportType::Competitors;
    QString title;
    bool printHeader = false;
    int columns = 1;
    int linesFirstPage = 0;      // 0 = as many as fit
    bool landscape = false;
    QList<ReportColumn> fields;
    QString notes;
    std::array<QString, 3> signatures; // left, center, right (role names)
    bool printNotClassified = true;
    bool byGroups = false;       // printed by the output groups of the event
    int tableWidth = 100;        // width of the table in % of the printable width

    bool hasSecondLines() const;

    QString toString() const;
    static ReportFormat fromString(const QString& s, ReportType type);
    // Default layout; documents of a run (start list, run ranking, ranking after a run) have
    // their own layout for each run, e.g. the start list of run 2 shows the time of run 1.
    static ReportFormat defaults(ReportType type, int run = 1);
    static QString storageId(ReportType type);            // document type
    static QString storageId(ReportType type, int run);   // layout of a run ("start_list_run2")
    static bool layoutPerRun(ReportType type);
    static QString typeName(ReportType type);
};

// Fields that can be used in a report of the given type.
QList<ReportFieldDef> availableReportFields(ReportType type, int runCount, int interCount);
QString reportFieldLabel(int field, ReportType type, int run = 1);
QStringList signatureRoles();
// Reports that can be printed by output groups (the ones listing competitors).
bool reportSupportsGroups(ReportType type);
