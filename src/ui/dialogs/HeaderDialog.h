#pragma once

#include "core/EventParams.h"

#include <QDialog>
#include <QMap>

class QComboBox;
class QLineEdit;

// Jury / course setters / forerunners / course data of each run (printed on the documents).
class HeaderDialog : public QDialog {
    Q_OBJECT
public:
    HeaderDialog(const EventParams& p, QWidget* parent = nullptr);
    void apply(EventParams& p) const;

private:
    QLineEdit* edit(const QString& key, int width = 0);
    // editable list of values (weather, snow), another text can be typed
    QComboBox* choice(const QString& key, const QStringList& values);
    // course data of a run: key, value in meters
    static QList<QPair<const char*, bool>> courseKeys();
    const EventParams& m_params;
    QMap<QString, QLineEdit*> m_edits;
};
