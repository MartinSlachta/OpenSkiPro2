#pragma once

#include "core/EventParams.h"
#include "core/FisList.h"

#include <QDialog>
#include <array>

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QSpinBox;

class EventTypeDialog : public QDialog {
    Q_OBJECT
public:
    EventTypeDialog(const EventParams& p, QWidget* parent = nullptr);
    void apply(EventParams& p) const;

private:
    void updateRuns();
    void updateFisCategory();

    QButtonGroup* m_regulation;
    QComboBox* m_fisCategory;
    QLabel* m_penalty;
    FisList m_fisList; // current FIS list (header, penalties)
    QButtonGroup* m_discipline;
    QSpinBox* m_runs;
    QSpinBox* m_inters;
    QButtonGroup* m_timePrecision;
    QButtonGroup* m_speedPrecision;
    QButtonGroup* m_avgPrecision;
    QDoubleSpinBox* m_length;
    QComboBox* m_finalType;
    std::array<QCheckBox*, kMaxRuns> m_runUsed{};
    std::array<QLineEdit*, kMaxRuns> m_runNames{};
};
