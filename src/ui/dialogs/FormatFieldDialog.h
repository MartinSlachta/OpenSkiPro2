#pragma once

#include <QDialog>
#include <functional>

class Event;
class QButtonGroup;
class QComboBox;
class QLineEdit;

// "Format a Field" (SkiPro): case of a competitor field, text replacement, copy from another
// field; applied to all the competitors, the dialog stays open until Close.
class FormatFieldDialog : public QDialog {
    Q_OBJECT
public:
    FormatFieldDialog(Event* ev, QWidget* parent = nullptr);

private:
    int field() const;
    QString fieldName(int id) const;
    // applies `change` to the selected field of every competitor, returns the number changed
    int apply(const std::function<QString(const QString&)>& change, int sourceField = 0);
    void formatCase();
    void replaceText();
    void copyField();
    void runOption();
    void clearField();
    void fillSources();

    Event* m_ev;
    QComboBox* m_field;
    QButtonGroup* m_case;
    QLineEdit* m_replace;
    QLineEdit* m_by;
    QComboBox* m_option;
    QComboBox* m_source;
};
