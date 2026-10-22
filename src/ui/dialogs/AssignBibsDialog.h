#pragma once

#include "core/Competitor.h"

#include <QDialog>

class Event;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QSpinBox;

// Assigns bib numbers to the competitors (all or a gender / categories) in a chosen order:
// points, FIS rules (ICR 621: X best drawn or reversed, NC / NJC double draw), name,
// random draw or current bibs. The bibs follow the start order from the first bib,
// skipping the given numbers (bibs not returned).
class AssignBibsDialog : public QDialog {
    Q_OBJECT
public:
    enum class Mode { Points, BestDraw, BestReversed, DoubleDraw, Name, Random, CurrentBibs };

    AssignBibsDialog(Event* ev, QWidget* parent = nullptr);
    void accept() override;

private:
    QVector<Competitor> selected() const;
    Mode mode() const;
    Discipline discipline() const;
    void updateState();

    Event* m_ev;
    QComboBox* m_gender;
    QListWidget* m_categories;
    QCheckBox* m_onlyMissing;
    QLabel* m_count;
    QComboBox* m_order;
    QComboBox* m_points;
    QLabel* m_bestLabel;
    QSpinBox* m_best;
    QLabel* m_group;
    QSpinBox* m_first;
    QLineEdit* m_skip;
    QLabel* m_bibs;
};
