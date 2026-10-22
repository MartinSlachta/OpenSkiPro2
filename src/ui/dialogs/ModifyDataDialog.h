#pragma once

#include "core/RunData.h"

#include <QDialog>
#include <array>

class Event;
class TimingEngine;
class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;

// "Modify Data" / "Information / Penalty": manual result of one competitor.
class ModifyDataDialog : public QDialog {
    Q_OBJECT
public:
    ModifyDataDialog(Event* ev, int run, int bib, QWidget* parent = nullptr);

private:
    void load(int bib);
    bool save();
    void step(int delta);
    void calculator();

    Event* m_ev;
    int m_run;
    TimingEngine* m_engine;
    TimeInfo m_loaded;
    QSpinBox* m_bib;
    QLabel* m_name;
    QLabel* m_source;
    QLineEdit* m_time;
    QLineEdit* m_penalty;
    QComboBox* m_status;
    QLineEdit* m_reason;
    std::array<QLineEdit*, kMaxInter> m_inter{};
};
