#pragma once

#include "core/EventParams.h"

#include <QDialog>

class QTableWidget;

// Competitor fields: active fields, labels, the columns of the grid and the public fields.
class CompetitorFieldsDialog : public QDialog {
    Q_OBJECT
public:
    CompetitorFieldsDialog(const CompetitorFieldConfig& cfg, QWidget* parent = nullptr);
    CompetitorFieldConfig config() const;

private:
    void move(int delta);
    void updatePublic(int row);
    QTableWidget* m_table;
};
