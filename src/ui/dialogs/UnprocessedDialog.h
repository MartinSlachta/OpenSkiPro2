#pragma once

#include "core/Types.h"

#include <QDialog>

class Event;
class QTableWidget;

// "Manage un-processed" (SkiPro): the competitors of the start list who are not classified,
// with their status DNS / NPS / DNF / DSQ (the gate for a DSQ). A click in a column sets the status,
// a second click removes it; the competitors without a result get a proposal.
// Closing without Ok after a change asks whether to save it.
class UnprocessedDialog : public QDialog {
    Q_OBJECT
public:
    UnprocessedDialog(Event* ev, int run, QWidget* parent = nullptr);
    void accept() override;
    void reject() override;

private:
    void setRowStatus(int row, ResultStatus st, const QString& reason);
    void onCellClicked(int row, int column);

    Event* m_ev;
    int m_run;
    QTableWidget* m_table;
    bool m_changed = false; // statuses changed by the user
};
