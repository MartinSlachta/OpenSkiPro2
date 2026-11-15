#pragma once

#include "core/Competitor.h"

#include <QDialog>
#include <QMap>

class Event;
class QCheckBox;
class QWidget;

// New (F1) / Modify (F2) competitor.
class CompetitorDialog : public QDialog {
    Q_OBJECT
public:
    CompetitorDialog(Event* ev, const Competitor& c, bool isNew, QWidget* parent = nullptr);
    Competitor competitor() const;
    void setCompetitor(const Competitor& c);
    void accept() override;

private:
    QString autoCategory() const;  // from the gender and the year in the form
    void updateCategory();

    Event* m_ev;
    Competitor m_original;
    bool m_new;
    QMap<int, QWidget*> m_editors;
    QCheckBox* m_upperLastName = nullptr;
    QString m_lastAuto;            // category the gender / year gave before their last change
    bool m_loading = false;
};
