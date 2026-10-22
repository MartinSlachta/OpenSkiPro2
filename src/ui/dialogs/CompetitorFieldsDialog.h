#pragma once

#include "core/EventParams.h"

#include <QDialog>

class QTableWidget;

// Competitor fields: active fields, labels and the columns of the grid.
class CompetitorFieldsDialog : public QDialog {
    Q_OBJECT
public:
    CompetitorFieldsDialog(const CompetitorFieldConfig& cfg, QWidget* parent = nullptr);
    CompetitorFieldConfig config() const;

private:
    void move(int delta);
    QTableWidget* m_table;
};
