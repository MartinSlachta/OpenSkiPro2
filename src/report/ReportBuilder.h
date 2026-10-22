#pragma once

#include "ReportFormat.h"

#include <QStringList>
#include <QVector>
#include <memory>

class Event;
struct RefereeReportData;

struct Competitor;

struct ReportSection {
    QString heading;              // e.g. "Did Not Start - Run 1 (2)", empty for the main table
    bool groupHeading = false;    // heading of an output group (printed as a label)
    QVector<QStringList> rows;
    QVector<const Competitor*> owners; // competitor of each row (lists split by groups)
    QStringList notes;                 // text printed at the right end of each row (gate of a DSQ)
    // rows of [label, value, style, column] across the table (penalty sheet): the value in the cell
    // of the given column index (or at the right end), style "" / "box" (white on dark grey) / "black"
    bool textRows = false;
};

// Report content ready for rendering: one string per cell.
struct ReportDocument {
    ReportFormat format;
    QString docTitle;             // resolved document title
    QStringList columnLabels;
    QVector<ReportSection> sections;
    QString summary;              // e.g. "30 competitors"
    QString infoText;             // e.g. "26 competitors classified" (dialog)
    int run = 1;                  // run the report belongs to (header: runs shown)
    int rowCount = 0;
    // "Report by the Referee": a form of its own instead of the table (see RefereeReport)
    std::shared_ptr<const RefereeReportData> referee;
};

namespace ReportBuilder {

// run: run number for start list / run ranking / records; for "ranking after a run" the last run.
ReportDocument build(const Event& ev, const ReportFormat& format, int run);

} // namespace ReportBuilder
