#pragma once

#include "core/Category.h"
#include "core/Competitor.h"
#include "core/EventParams.h"
#include "core/RunData.h"

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

class Event;

// Writes the current state of an event as a SkiAlp Pro 11 event archive (*.spro), which
// SkiAlp Pro loads with its own import: the fallback when a race has to continue in
// SkiAlp Pro (docs/skialp-export-design.md).
//
// The archive is made from a template: an empty event exported from SkiAlp Pro. Only what
// OpenSkiPro2 knows is replaced (header, event type, categories, output groups, competitor
// fields, competitors, start lists, results and the start / finish impulses); the print
// layouts, logos and timing options of the template stay as they are.
class SkiAlpExporter {
public:
    struct Data {
        EventParams params;
        CategoryGrid categories;
        QVector<Competitor> competitors;
        QVector<RunData> runs; // runs 1..params.type.runCount
    };

    struct RunReport {
        int startList = 0; // start list entries
        int results = 0;   // competitors with a result or a status
        int records = 0;   // start / finish impulses
    };

    struct Report {
        int competitors = 0;
        QVector<RunReport> runs;
        QStringList warnings;
    };

    static Data fromEvent(const Event& ev);

    // Checks the template (a SkiAlp Pro archive with empty competitors and runs).
    static bool checkTemplate(const QString& templateFile, QString* error);

    // Writes `target`. On success the archive was read back and checked.
    static bool write(const Data& data, const QString& templateFile, const QString& target, Report* report,
                      QString* error);
    static bool write(const Event& ev, const QString& templateFile, const QString& target, Report* report,
                      QString* error);

    // Default file name: "Event008-SAP-20261122-0930.spro" (eventPath = path of the .osp file)
    static QString defaultFileName(const QString& eventPath, const QDateTime& now);

    // Status word of a SkiAlp Pro time record (TTIMERECORDS_*.C_STATUS) for the record flags.
    static int recordStatusWord(int flags);
};
