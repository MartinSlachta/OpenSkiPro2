#pragma once

#include "core/FisList.h"

#include <QDialog>
#include <QMap>

class Event;
class QCheckBox;

// "Update Competitors" of the FIS section (SkiPro: Update Competitors from a federation file):
// the competitors of the event are matched with the FIS list by their FIS code and the
// chosen fields ("Fields to update") are replaced by the values of the list.
class FisUpdateDialog : public QDialog {
    Q_OBJECT
public:
    FisUpdateDialog(Event* ev, const FisList& list, QWidget* parent = nullptr);
    void accept() override;

private:
    Event* m_ev;
    const FisList& m_list;
    QMap<int, QCheckBox*> m_fields;
};
