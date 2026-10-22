#pragma once

#include "report/ReportFormat.h"

#include <QDialog>
#include <array>

class Event;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;
class QTableWidget;

// "Document Format" dialog of every printed list (fields, alignment, width,
// notes, signatures) with Preview / Print / Export.
class ReportDialog : public QDialog {
    Q_OBJECT
public:
    ReportDialog(Event* ev, ReportType type, int run, QWidget* parent = nullptr);

protected:
    void done(int r) override;

private:
    void setFormat(const ReportFormat& f);
    ReportFormat format() const;
    void addFieldRow(const ReportColumn& c, int at = -1);
    QComboBox* fieldCombo(int current, bool withNone);
    ReportColumn fieldRow(int r) const;
    void addField();
    void removeField();
    void moveField(int delta);
    void updateInfo();
    void preview();
    void print();
    void exportFile();
    void save();
    void resetDefaults();
    void loadFromFile();
    void loadFromEvent();
    void saveToFile();
    int run() const;
    void loadStored();          // layout saved in the event for the document / run shown

    Event* m_ev;
    ReportType m_type;
    QComboBox* m_run = nullptr;
    int m_formatRun = 0;        // run whose layout is shown (layouts per run, see ReportFormat)
    QPlainTextEdit* m_title;
    QCheckBox* m_printHeader;
    QSpinBox* m_columns;
    QSpinBox* m_linesFirst;
    QCheckBox* m_landscape;
    QTableWidget* m_fields;
    QSpinBox* m_tableWidth;
    QPlainTextEdit* m_notes;
    std::array<QComboBox*, 3> m_signatures{};
    QCheckBox* m_notClassified;
    QCheckBox* m_byGroups;
    QLabel* m_info;
};
