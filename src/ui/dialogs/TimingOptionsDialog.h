#pragma once

#include "core/EventParams.h"

#include <QDialog>
#include <array>

class Event;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QPushButton;
class QSpinBox;

// "Sotware Parameters" of the timing window (options, delays, minimum times, ranking).
class TimingOptionsDialog : public QDialog {
    Q_OBJECT
public:
    TimingOptionsDialog(Event* ev, QWidget* parent = nullptr);
    TimingOptions options(const TimingOptions& base) const;

private:
    Event* m_ev;
    QComboBox* m_rankType;
    QComboBox* m_rankField;
    QCheckBox* m_byGroups;
    QCheckBox* m_follow;
    QCheckBox* m_mark;
    QCheckBox* m_names;
    QCheckBox* m_seq;
    QCheckBox* m_confirm;
    QCheckBox* m_invert;
    QCheckBox* m_askBib;
    QSpinBox* m_foreFrom;
    QSpinBox* m_foreTo;
    std::array<QLineEdit*, 4> m_quick{};
    QDoubleSpinBox* m_delayStart;
    QDoubleSpinBox* m_delayFinish;
    QDoubleSpinBox* m_delayInter;
    std::array<QDoubleSpinBox*, kMaxInter> m_minInter{};
    QDoubleSpinBox* m_minFinish;
};
