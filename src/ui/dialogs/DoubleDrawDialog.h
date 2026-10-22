#pragma once

#include "core/BibDraw.h"

#include <QDialog>

class QCheckBox;
class QLabel;
class QSpinBox;
class QTableWidget;

// Double draw for NC / NJC (ICR 621.4): the 15 best by points in two groups (1-7, 8-15).
// The start order of each competitor drawn at the team captains' meeting is typed in
// (first ball: points rank, second ball: start order), or drawn by the computer.
class DoubleDrawDialog : public QDialog {
    Q_OBJECT
public:
    DoubleDrawDialog(const QVector<Competitor>& list, const QVector<int>& order, Discipline d, QWidget* parent = nullptr);

    // points order (possibly changed with Up / Down) and the start order of its first rows
    QVector<int> order() const { return m_order; }
    QVector<int> startOrders() const;

    void accept() override;

private:
    void fillTable();
    void updateGroups(); // groups (increased or not), rows of the table
    void updateRanges();
    void validate();
    void moveRow(int delta);
    void draw();
    void clear();
    int groupEnd(int row) const; // last start order allowed for a row

    const QVector<Competitor>& m_list;
    QVector<int> m_order;
    Discipline m_discipline;
    BibDraw::DoubleDrawGroups m_groups;
    QTableWidget* m_table;
    QLabel* m_help;
    QCheckBox* m_increase;
    QLabel* m_info;
    QVector<QSpinBox*> m_spins;
};
