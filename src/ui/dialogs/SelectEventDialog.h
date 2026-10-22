#pragma once

#include <QDialog>

class QTableWidget;

// "Select Event": list of the numbered event slots (Event001 ... Event999).
class SelectEventDialog : public QDialog {
    Q_OBJECT
public:
    enum class Mode {
        Open,     // any slot, an empty slot creates a new event
        FreeSlot, // only empty slots
        AnySlot,  // any slot
        Existing, // only existing events
    };
    SelectEventDialog(Mode mode, QWidget* parent = nullptr);
    int selectedSlot() const { return m_slot; }

    void accept() override;

private:
    Mode m_mode;
    int m_slot = 0;
    QTableWidget* m_table;
};
