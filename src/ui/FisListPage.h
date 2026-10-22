#pragma once

#include <QWidget>

class Event;
class QTextBrowser;

// "List status" page of the FIS section: the imported FIS points list presented like
// the first page of the FIS list (validity, penalties of the categories, discipline
// constants) with the number of imported competitors. The category and the discipline
// of the open event are highlighted.
class FisListPage : public QWidget {
    Q_OBJECT
public:
    explicit FisListPage(Event* ev, QWidget* parent = nullptr);
    void reload();

Q_SIGNALS:
    void importRequested();

private:
    Event* m_ev;
    QTextBrowser* m_view;
};
