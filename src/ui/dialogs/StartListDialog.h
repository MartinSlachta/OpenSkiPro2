#pragma once

#include "core/RunData.h"

#include <QDialog>

class Event;
class QLineEdit;
class QTableWidget;

// Start list of a run: editable order, generation (bib / points / draw /
// previous run with reversal of the N best).
class StartListDialog : public QDialog {
    Q_OBJECT
public:
    StartListDialog(Event* ev, int run, QWidget* parent = nullptr);
    void accept() override;

private:
    void setList(const QVector<StartEntry>& list);
    QVector<StartEntry> list() const;
    void insertRow();
    void removeRow();
    void moveRow(int delta);
    void search();
    void refreshNames();
    void generateFromPrevious();

    Event* m_ev;
    int m_run;
    QTableWidget* m_table;
    QLineEdit* m_searchBib;
    QLineEdit* m_searchName;
    bool m_updating = false;
};
