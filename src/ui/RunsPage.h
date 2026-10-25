#pragma once

#include <QWidget>

class Event;
class QComboBox;
class QPoint;
class QLabel;
class QTableWidget;

// "Manage Runs" page: run selector, counters, start list / timing / data
// management and the ranking of the selected run.
class RunsPage : public QWidget {
    Q_OBJECT
public:
    explicit RunsPage(Event* ev, QWidget* parent = nullptr);
    int currentRun() const { return m_run; }

Q_SIGNALS:
    void timingRequested(int run);

private:
    void setRun(int run);
    void refresh();
    void editStartList();
    void modifyData();
    void modifyBib(int bib); // 0: the first of the start list
    int bibAt(int row) const;
    void rankingMenu(const QPoint& pos);
    void deleteData();
    void manageUnprocessed();

    Event* m_ev;
    int m_run = 1;
    QLabel* m_runLabel;
    QLabel* m_cList;
    QLabel* m_cClassified;
    QLabel* m_cDns;
    QLabel* m_cNps;
    QLabel* m_cDnf;
    QLabel* m_cDsq;
    QLabel* m_cNotProcessed;
    QComboBox* m_rankingMode;
    QTableWidget* m_ranking;
};
