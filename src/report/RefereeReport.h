#pragma once

#include <QString>
#include <QStringList>
#include <QTime>
#include <QVector>

class Event;

// "Report by the Referee" of a run (FIS form 2022): disqualifications with the gate,
// competitors who did not start / were not permitted to start / did not finish; posted by
// the Referee after each run (ICR), protests against a disqualification within 15 minutes.
struct RefereeReportData {
    QString place, country, codex;
    QString eventName;   // "Run 1 - National Championship"
    QString date;        // 30.03.2026
    QString category, gender, event; // NC, L, SL
    QString referee;
    QString published, deadline; // 10:05, 10:20

    struct Disqualified {
        QString bib, name, nation, gate, notes;
    };
    QVector<Disqualified> disqualified;
    QStringList didNotStart, notPermitted, didNotFinish; // bibs
};

namespace RefereeReport {

enum class Order { Bib, StartList };

// The reason of a DSQ split into the gate (its leading number) and the notes.
void splitReason(const QString& reason, QString* gate, QString* notes);

// Deadline of the protests against a disqualification: 15 minutes after the posting.
QTime deadline(const QTime& published);

RefereeReportData build(const Event& ev, int run, Order order, const QTime& published, const QTime& deadline);

} // namespace RefereeReport
