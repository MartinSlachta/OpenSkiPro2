#pragma once

#include "report/ReportBuilder.h"

#include <QDialog>

class Event;
class QButtonGroup;
class QCheckBox;
class QComboBox;
class QLabel;
class QTimeEdit;

// "Referee Report" of the FIS section (like SkiAlp Pro): the FIS form "Report by the Referee"
// of a run with its DSQ, DNS, NPS and DNF, the time of the posting and the deadline of the protests.
class RefereeReportDialog : public QDialog {
    Q_OBJECT
public:
    RefereeReportDialog(Event* ev, int run, QWidget* parent = nullptr);

    ReportDocument document() const;

private:
    int run() const;
    void updateInfo();
    void preview();
    void print();
    void exportPdf();

    Event* m_ev;
    QComboBox* m_run = nullptr;
    QButtonGroup* m_order;
    QTimeEdit* m_published;
    QTimeEdit* m_deadline;
    QCheckBox* m_noTimes;
    QLabel* m_info;
};
