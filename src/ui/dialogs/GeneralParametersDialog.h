#pragma once

#include "core/EventParams.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDateEdit;
class QLineEdit;
class QPlainTextEdit;

class GeneralParametersDialog : public QDialog {
    Q_OBJECT
public:
    GeneralParametersDialog(const EventParams& p, QWidget* parent = nullptr);
    void apply(EventParams& p) const;

private:
    QPlainTextEdit* m_title;
    QPlainTextEdit* m_discipline;
    QDateEdit* m_date;
    QComboBox* m_location;
    QLineEdit* m_nation;
    QLineEdit* m_organisation;
    QLineEdit* m_timingBy;
    QCheckBox* m_freeHeader;
    QLineEdit* m_codex;
};
