#pragma once

#include "core/Competitor.h"

#include <QDialog>
#include <QMap>

class Event;
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
    Event* m_ev;
    Competitor m_original;
    bool m_new;
    QMap<int, QWidget*> m_editors;
};
