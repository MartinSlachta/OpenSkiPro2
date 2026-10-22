#pragma once

#include "db/CsvImporter.h"

#include <QDialog>
#include <QList>

class Event;
class QCheckBox;
class QComboBox;
class QSpinBox;
class QTableWidget;

// Import of competitors from CSV / TXT / XLSX or from the clipboard (cells copied in Excel,
// separated by tabs) with a column mapping.
class CsvImportDialog : public QDialog {
    Q_OBJECT
public:
    CsvImportDialog(Event* ev, const QString& file, QWidget* parent = nullptr);
    // from a text (the clipboard) instead of a file
    static CsvImportDialog* fromText(Event* ev, const QString& text, QWidget* parent = nullptr);
    void accept() override;

private:
    void load();
    void rebuildPreview();
    QList<int> mapping() const;
    QString settingsKey() const; // the mapping chosen last time is kept per import window
    void restoreMode();

    Event* m_ev;
    QString m_file;
    QString m_text;         // clipboard text (m_file empty)
    bool m_fromText = false;
    CsvImporter::Table m_table;
    QComboBox* m_encoding;
    QComboBox* m_delimiter;
    QCheckBox* m_header;
    QComboBox* m_mode;
    QTableWidget* m_preview;
    QList<QComboBox*> m_columnFields;
};
