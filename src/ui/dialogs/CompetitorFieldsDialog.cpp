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

namespace {
enum Col { ColField, ColActive, ColLabel, ColGrid, ColPublic, ColCount };
}

CompetitorFieldsDialog::CompetitorFieldsDialog(const CompetitorFieldConfig& cfg, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Competitor Fields"));
    auto* lay = new QVBoxLayout(this);
    lay->addWidget(new QLabel(tr("Active fields, their labels and the columns shown in the competitors grid "
                                 "(the order of the rows is the order of the columns). Public fields are shown "
                                 "to the spectators in the same order."), this));
    auto* row = new QHBoxLayout;
    m_table = new QTableWidget(this);
    // the width of a grid column is set with the mouse in the competitors grid, it is kept here
    m_table->setColumnCount(ColCount);
    m_table->setHorizontalHeaderLabels({ tr("Field"), tr("Active"), tr("Label"), tr("In grid"), tr("Public") });
    m_table->horizontalHeaderItem(ColPublic)->setToolTip(tr("Shown to the spectators: screens and results on the internet"));
    UiHelpers::styleGrid(m_table);
    m_table->horizontalHeader()->setSectionResizeMode(ColLabel, QHeaderView::Stretch);

    // order: grid columns first, then the public fields, then the other fields
    const QList<int> shown = cfg.shownSet ? cfg.shown : cfg.publicFields();
    QList<int> order;
    for (const auto& g : cfg.grid)
        order << g.first;
    for (int id : shown)
        if (!order.contains(id))
            order << id;
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
        bool inGridNow = false;
        for (const auto& g : cfg.grid)
            if (g.first == id) {
                inGridNow = true;
                width = g.second;
            }
        inGrid->setCheckState(inGridNow ? Qt::Checked : Qt::Unchecked);
        name->setData(Qt::UserRole + 1, width);
        // Public: Name always, Bib is its own column everywhere, the rest is not for spectators
        auto* pub = new QTableWidgetItem;
        pub->setFlags(Qt::ItemIsSelectable);
        if (CompetitorFieldConfig::canBePublic(id))
            pub->setCheckState(id == CompField::Name || shown.contains(id) ? Qt::Checked : Qt::Unchecked);
        m_table->setItem(r, ColField, name);
        m_table->setItem(r, ColActive, active);
        m_table->setItem(r, ColLabel, label);
        m_table->setItem(r, ColGrid, inGrid);
        m_table->setItem(r, ColPublic, pub);
        updatePublic(r);
    }
    m_table->resizeColumnToContents(ColField);
    // a field that is not active is never public: its checkbox is kept, but greyed
    connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* it) {
        if (it->column() == ColActive)
            updatePublic(it->row());
    });
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
    resize(700, 640);
}

void CompetitorFieldsDialog::updatePublic(int r)
{
    QTableWidgetItem* pub = m_table->item(r, ColPublic);
    QTableWidgetItem* active = m_table->item(r, ColActive);
    if (!pub || !active)
        return;
    const int id = m_table->item(r, ColField)->data(Qt::UserRole).toInt();
    if (!CompetitorFieldConfig::canBePublic(id))
        return;
    const bool editable = id != CompField::Name && active->checkState() == Qt::Checked;
    Qt::ItemFlags f = Qt::ItemIsSelectable | Qt::ItemIsUserCheckable;
    if (editable)
        f |= Qt::ItemIsEnabled;
    if (pub->flags() != f)
        pub->setFlags(f);
}

void CompetitorFieldsDialog::move(int delta)
{
    const int r = m_table->currentRow();
    const int t = r + delta;
    if (r < 0 || t < 0 || t >= m_table->rowCount())
        return;
    const QSignalBlocker block(m_table);
    for (int c = 0; c < ColCount; ++c) {
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
    c.shownSet = true;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        const int id = m_table->item(r, ColField)->data(Qt::UserRole).toInt();
        if (m_table->item(r, ColActive)->checkState() == Qt::Checked || id == CompField::Bib)
            c.active.insert(id);
        const QString label = m_table->item(r, ColLabel)->text().trimmed();
        if (!label.isEmpty())
            c.labels.insert(id, label);
        if (m_table->item(r, ColGrid)->checkState() == Qt::Checked)
            c.grid << qMakePair(id, m_table->item(r, ColField)->data(Qt::UserRole + 1).toInt());
        // Name takes the place of the first of Name / Last Name / First Name
        if (id == CompField::Name || id == CompField::LastName || id == CompField::FirstName) {
            if (!c.shown.contains(CompField::Name))
                c.shown << CompField::Name;
        } else if (CompetitorFieldConfig::canBePublic(id) && m_table->item(r, ColPublic)->checkState() == Qt::Checked) {
            c.shown << id;
        }
    }
    return c;
}
