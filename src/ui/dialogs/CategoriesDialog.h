#pragma once

#include "core/Category.h"

#include <QDialog>

class Event;
class QTableWidget;

// Categories by gender and birth years; assigns them to the competitors.
class CategoriesDialog : public QDialog {
    Q_OBJECT
public:
    CategoriesDialog(Event* ev, QWidget* parent = nullptr);
    void accept() override;

private:
    void setGrid(const CategoryGrid& g);
    CategoryGrid grid() const;
    void addRow(const CategoryDef& c);
    void loadSkiPro();
    void loadFromFile();
    void loadFromEvent();
    void saveToFile();
    void assign();

    Event* m_ev;
    QTableWidget* m_table;
};
