#include "CompetitorFieldsDialog.h"
#include "core/Competitor.h"
#include "ui/UiHelpers.h"

#include <QCoreApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

CompetitorFieldsDialog::CompetitorFieldsDialog(const CompetitorFieldConfig& cfg, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Competitor Fields"));
    auto* lay = new QVBoxLayout(this);
    lay->addWidget(new QLabel(tr("Active fields, their labels and the columns shown in the competitors grid "
                                 "(the order of the rows is the order of the columns)."), this));
    auto* row = new QHBoxLayout;
    m_table = new QTableWidget(this);
    // the width of a grid column is set with the mouse in the competitors grid, it is kept here
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels({ tr("Field"), tr("Active"), tr("Label"), tr("In grid") });
    UiHelpers::styleGrid(m_table);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);

    // order: grid columns first, then the other fields
    QList<int> order;
    for (const auto& g : cfg.grid)
        order << g.first;
    for (const auto& d : competitorFieldDefs())
        if (!order.contains(d.id))
            order << d.id;
    m_table->setRowCount(int(order.size()));
    for (int r = 0; r < order.size(); ++r) {
        const int id = order.at(r);
        const auto* d = competitorFieldDef(id);
        auto* name = new QTableWidgetItem(d ? QCoreApplication::translate("Competitor", d->label) : QString::number(id));
        name->setData(Qt::UserRole, id);
        name->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        auto* active = new QTableWidgetItem;
        active->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable);
        active->setCheckState(cfg.isActive(id) || id == CompField::Bib ? Qt::Checked : Qt::Unchecked);
        auto* label = new QTableWidgetItem(cfg.labels.value(id));
        auto* inGrid = new QTableWidgetItem;
        inGrid->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable);
        int width = 75;
        bool shown = false;
        for (const auto& g : cfg.grid)
            if (g.first == id) {
                shown = true;
                width = g.second;
            }
        inGrid->setCheckState(shown ? Qt::Checked : Qt::Unchecked);
        name->setData(Qt::UserRole + 1, width);
        m_table->setItem(r, 0, name);
        m_table->setItem(r, 1, active);
        m_table->setItem(r, 2, label);
        m_table->setItem(r, 3, inGrid);
    }
    m_table->resizeColumnToContents(0);
    row->addWidget(m_table, 1);
    auto* moves = new QVBoxLayout;
    auto* up = new QToolButton(this);
    up->setIcon(UiHelpers::icon(QStringLiteral("up")));
    up->setIconSize(QSize(24, 24));
    auto* down = new QToolButton(this);
    down->setIcon(UiHelpers::icon(QStringLiteral("down")));
    down->setIconSize(QSize(24, 24));
    moves->addWidget(up);
    moves->addWidget(down);
    moves->addStretch();
    row->addLayout(moves);
    lay->addLayout(row, 1);
    lay->addLayout(UiHelpers::okCancelRow(this));
    connect(up, &QToolButton::clicked, this, [this] { move(-1); });
    connect(down, &QToolButton::clicked, this, [this] { move(1); });
    resize(640, 640);
}

void CompetitorFieldsDialog::move(int delta)
{
    const int r = m_table->currentRow();
    const int t = r + delta;
    if (r < 0 || t < 0 || t >= m_table->rowCount())
        return;
    for (int c = 0; c < 4; ++c) {
        QTableWidgetItem* a = m_table->takeItem(r, c);
        QTableWidgetItem* b = m_table->takeItem(t, c);
        m_table->setItem(r, c, b);
        m_table->setItem(t, c, a);
    }
    m_table->setCurrentCell(t, 0);
}

CompetitorFieldConfig CompetitorFieldsDialog::config() const
{
    CompetitorFieldConfig c;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        const int id = m_table->item(r, 0)->data(Qt::UserRole).toInt();
        if (m_table->item(r, 1)->checkState() == Qt::Checked || id == CompField::Bib)
            c.active.insert(id);
        const QString label = m_table->item(r, 2)->text().trimmed();
        if (!label.isEmpty())
            c.labels.insert(id, label);
        if (m_table->item(r, 3)->checkState() == Qt::Checked)
            c.grid << qMakePair(id, m_table->item(r, 0)->data(Qt::UserRole + 1).toInt());
    }
    return c;
}
