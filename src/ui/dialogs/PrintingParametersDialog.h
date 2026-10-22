#pragma once

#include "core/EventParams.h"

#include <QDialog>
#include <array>

class Event;
class QCheckBox;
class QComboBox;
class QLineEdit;
class QPushButton;
class QSpinBox;

class PrintingParametersDialog : public QDialog {
    Q_OBJECT
public:
    PrintingParametersDialog(Event* ev, QWidget* parent = nullptr);
    void apply(EventParams& p) const;

private:
    void updateFontButtons();
    void collect(PrintingParams& pp) const;

    Event* m_ev;
    PrintingParams m_pp;
    std::array<QPushButton*, 6> m_fontButtons{};
    QCheckBox* m_showTitle1;
    QCheckBox* m_showTitle2;
    QCheckBox* m_autoAdjust;
    QCheckBox* m_asTable;
    QCheckBox* m_grayRows;
    QSpinBox* m_grayLevel;
    std::array<QSpinBox*, 4> m_marginFirst{};
    std::array<QSpinBox*, 4> m_marginOther{};
    QComboBox* m_alignFirst;
    QComboBox* m_alignOther;
    std::array<QLineEdit*, 8> m_logos{};
    QCheckBox* m_appLogo;
    QCheckBox* m_logosFirstOnly;
};
