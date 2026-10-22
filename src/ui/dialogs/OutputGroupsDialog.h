#pragma once

#include "core/OutputGroup.h"

#include <QDialog>

class Event;
class QTableWidget;

// Output groups of the event ("Skupiny výsledkov" in SkiAlp): the competitors printed together
// under one heading in the reports; rankings are computed inside each group.
class OutputGroupsDialog : public QDialog {
    Q_OBJECT
public:
    OutputGroupsDialog(Event* ev, QWidget* parent = nullptr);
    void accept() override;

    // Edits one group; false when cancelled.
    static bool editGroup(const Event* ev, OutputGroup& g, QWidget* parent);

private:
    void reload(int select = -1);
    void newGroup();
    void editCurrent();
    void removeCurrent();
    void createFromField();
    void move(int delta);
    void saveToFile();
    void loadFromFile();
    void loadFromEvent();

    Event* m_ev;
    QList<OutputGroup> m_groups;
    QTableWidget* m_table;
};
