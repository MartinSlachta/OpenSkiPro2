#pragma once

#include "core/EventParams.h"

#include <QDialog>
#include <array>

class Event;
class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;

// "FIS points / Penalty": constants of the race points and penalty calculation per gender
// (copied from the FIS list, may be changed), the calculation (A, B, C, calculated and
// applied penalty) and the applied penalty typed by the TD.
class FisPenaltyDialog : public QDialog {
    Q_OBJECT
public:
    FisPenaltyDialog(Event* ev, QWidget* parent = nullptr);
    void accept() override;
    // fills the constants of the present genders from the imported FIS list
    bool loadFromList(bool quiet);
    // closed with "Penalty calculation": the sheet is to be shown
    bool reportWanted() const { return m_reportWanted; }

private:
    struct Column {
        Gender gender = Gender::Unknown;
        QDoubleSpinBox* f = nullptr;
        QDoubleSpinBox* maxValue = nullptr;
        QDoubleSpinBox* z = nullptr;
        QDoubleSpinBox* adder = nullptr;
        QDoubleSpinBox* minPenalty = nullptr;
        QDoubleSpinBox* maxPenalty = nullptr;
        QLabel* result = nullptr;
        QCheckBox* override = nullptr;
        QDoubleSpinBox* applied = nullptr;
    };
    FisPointsParams params(const Column& c) const;
    void setParams(Column& c, const FisPointsParams& p);
    void recalculate();

    Event* m_ev;
    QLineEdit* m_list;
    QLabel* m_info;
    QVector<Column> m_columns;
    bool m_loading = false;
    bool m_reportWanted = false;
};
