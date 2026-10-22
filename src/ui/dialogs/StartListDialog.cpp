#include "StartListDialog.h"
#include "core/Event.h"
#include "core/StartListBuilder.h"
#include "ui/UiHelpers.h"

#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

StartListDialog::StartListDialog(Event* ev, int run, QWidget* parent)
    : QDialog(parent)
    , m_ev(ev)
    , m_run(run)
{
    setWindowTitle(tr("Start List - %1").arg(ev->params().type.runName(run)));
    auto* lay = new QVBoxLayout(this);
    auto* search = new QGroupBox(tr("Search"), this);
    auto* sl = new QHBoxLayout(search);
    m_searchBib = new QLineEdit(search);
    m_searchBib->setMaximumWidth(70);
    m_searchName = new QLineEdit(search);
    sl->addWidget(new QLabel(tr("Bib.:"), search));
    sl->addWidget(m_searchBib);
    sl->addWidget(new QLabel(tr("Last Name:"), search));
    sl->addWidget(m_searchName, 1);
    lay->addWidget(search);

    auto* body = new QHBoxLayout;
    auto* side = new QVBoxLayout;
    auto tool = [this](const QString& icon, const QString& tip) {
        auto* b = new QToolButton(this);
        b->setIcon(UiHelpers::icon(icon));
        b->setIconSize(QSize(22, 22));
        b->setToolTip(tip);
        return b;
    };
    auto* add = tool(QStringLiteral("add"), tr("Insert a competitor"));
    auto* del = tool(QStringLiteral("remove"), tr("Remove from the start list"));
    auto* up = tool(QStringLiteral("up"), tr("Move up"));
    auto* down = tool(QStringLiteral("down"), tr("Move down"));
    side->addWidget(add);
    side->addWidget(del);
    side->addSpacing(12);
    side->addWidget(up);
    side->addWidget(down);
    side->addStretch();
    body->addLayout(side);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels({ tr("Order"), tr("Bib."), tr("Name"), tr("Start Time") });
    UiHelpers::styleGrid(m_table);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->setColumnWidth(0, 60);
    m_table->setColumnWidth(1, 70);
    m_table->setColumnWidth(3, 110);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed | QAbstractItemView::AnyKeyPressed);
    body->addWidget(m_table, 1);
    lay->addLayout(body, 1);

    auto* bottom = new QHBoxLayout;
    auto* options = UiHelpers::button(QStringLiteral("options"), tr("Options"), this);
    bottom->addWidget(options);
    bottom->addStretch();
    auto* ok = UiHelpers::button(QStringLiteral("ok"), tr("Ok"), this);
    auto* cancel = UiHelpers::button(QStringLiteral("cancel"), tr("Cancel"), this);
    ok->setMinimumWidth(120);
    cancel->setMinimumWidth(120);
    bottom->addWidget(ok);
    bottom->addWidget(cancel);
    lay->addLayout(bottom);

    auto* menu = new QMenu(this);
    menu->addAction(tr("Order by bib"), this, [this] { setList(StartListBuilder::firstRun(*m_ev, StartListBuilder::FirstRunOrder::ByBib)); });
    menu->addAction(tr("Order by points (best first)"), this,
                    [this] { setList(StartListBuilder::firstRun(*m_ev, StartListBuilder::FirstRunOrder::ByPoints)); });
    menu->addAction(tr("Random draw"), this, [this] { setList(StartListBuilder::firstRun(*m_ev, StartListBuilder::FirstRunOrder::Random)); });
    auto* prevAct = menu->addAction(tr("From the ranking of the previous run..."), this, &StartListDialog::generateFromPrevious);
    prevAct->setEnabled(run > 1);
    menu->addSeparator();
    menu->addAction(tr("Clear the start list"), this, [this] { setList({}); });
    connect(options, &QPushButton::clicked, this, [options, menu] { menu->popup(options->mapToGlobal(QPoint(0, -menu->sizeHint().height()))); });

    connect(add, &QToolButton::clicked, this, &StartListDialog::insertRow);
    connect(del, &QToolButton::clicked, this, &StartListDialog::removeRow);
    connect(up, &QToolButton::clicked, this, [this] { moveRow(-1); });
    connect(down, &QToolButton::clicked, this, [this] { moveRow(1); });
    connect(ok, &QPushButton::clicked, this, &StartListDialog::accept);
    connect(cancel, &QPushButton::clicked, this, &StartListDialog::reject);
    connect(m_searchBib, &QLineEdit::textChanged, this, &StartListDialog::search);
    connect(m_searchName, &QLineEdit::textChanged, this, &StartListDialog::search);
    connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* it) {
        if (!m_updating && (it->column() == 1 || it->column() == 3))
            refreshNames();
    });

    QVector<StartEntry> current = ev->run(run).startList;
    if (current.isEmpty()) {
        if (run == 1)
            current = StartListBuilder::firstRun(*ev, StartListBuilder::FirstRunOrder::ByBib);
        else
            current = StartListBuilder::nextRun(*ev, run, ev->params().timing.reverseCount);
    }
    setList(current);
    resize(640, 820);
}

void StartListDialog::setList(const QVector<StartEntry>& list)
{
    m_updating = true;
    m_table->setRowCount(int(list.size()));
    for (int r = 0; r < list.size(); ++r) {
        auto* order = new QTableWidgetItem(QString::number(r + 1));
        order->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        order->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(r, 0, order);
        m_table->setItem(r, 1, new QTableWidgetItem(QString::number(list.at(r).bib)));
        auto* name = new QTableWidgetItem;
        name->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        m_table->setItem(r, 2, name);
        m_table->setItem(r, 3, new QTableWidgetItem(list.at(r).startTimeUs >= 0 ? TimeUtil::formatClock(list.at(r).startTimeUs, 0) : QString()));
    }
    m_updating = false;
    refreshNames();
}

void StartListDialog::refreshNames()
{
    m_updating = true;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        if (auto* o = m_table->item(r, 0))
            o->setText(QString::number(r + 1));
        const int bib = m_table->item(r, 1) ? m_table->item(r, 1)->text().toInt() : 0;
        if (auto* n = m_table->item(r, 2)) {
            n->setText(bib > 0 ? m_ev->competitorName(bib) : QString());
            n->setBackground(m_ev->competitorByBib(bib) || m_ev->isForerunner(bib) ? QBrush(QColor(0xff, 0xff, 0xe1))
                                                                                  : QBrush(QColor(0xff, 0xd0, 0xd0)));
        }
    }
    m_updating = false;
}

QVector<StartEntry> StartListDialog::list() const
{
    QVector<StartEntry> out;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        StartEntry e;
        e.bib = m_table->item(r, 1) ? m_table->item(r, 1)->text().trimmed().toInt() : 0;
        const QString st = m_table->item(r, 3) ? m_table->item(r, 3)->text().trimmed() : QString();
        e.startTimeUs = st.isEmpty() ? TimeUtil::kNoTime : TimeUtil::parseDayTime(st);
        if (e.bib > 0)
            out << e;
    }
    return out;
}

void StartListDialog::insertRow()
{
    bool ok = false;
    const int bib = QInputDialog::getInt(this, tr("Insert a competitor"), tr("Bib. :"), 1, 1, 9999, 1, &ok);
    if (!ok)
        return;
    QVector<StartEntry> l = list();
    for (const auto& e : l) {
        if (e.bib == bib) {
            QMessageBox::warning(this, windowTitle(), tr("The bib %1 is already in the start list.").arg(bib));
            return;
        }
    }
    const int row = m_table->currentRow() >= 0 ? m_table->currentRow() : int(l.size());
    StartEntry e;
    e.bib = bib;
    l.insert(qBound(0, row, int(l.size())), e);
    setList(l);
    m_table->selectRow(row);
}

void StartListDialog::removeRow()
{
    const int r = m_table->currentRow();
    if (r < 0)
        return;
    m_table->removeRow(r);
    refreshNames();
}

void StartListDialog::moveRow(int delta)
{
    const int r = m_table->currentRow();
    const int t = r + delta;
    if (r < 0 || t < 0 || t >= m_table->rowCount())
        return;
    QVector<StartEntry> l = list();
    if (r >= l.size() || t >= l.size())
        return;
    l.swapItemsAt(r, t);
    setList(l);
    m_table->selectRow(t);
}

void StartListDialog::search()
{
    const QString bib = m_searchBib->text().trimmed();
    const QString name = m_searchName->text().trimmed();
    if (bib.isEmpty() && name.isEmpty())
        return;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        const bool match = (!bib.isEmpty() && m_table->item(r, 1)->text() == bib)
            || (!name.isEmpty() && m_table->item(r, 2)->text().startsWith(name, Qt::CaseInsensitive));
        if (match) {
            m_table->selectRow(r);
            m_table->scrollToItem(m_table->item(r, 0));
            return;
        }
    }
}

void StartListDialog::generateFromPrevious()
{
    bool ok = false;
    const int n = QInputDialog::getInt(this, tr("From the ranking of the previous run"),
                                       tr("Number of best ranked competitors whose order is reversed (0 = no reversal) :"),
                                       m_ev->params().timing.reverseCount, 0, 999, 1, &ok);
    if (!ok)
        return;
    EventParams p = m_ev->params();
    if (p.timing.reverseCount != n) {
        p.timing.reverseCount = n;
        m_ev->setParams(p);
    }
    setList(StartListBuilder::nextRun(*m_ev, m_run, n));
}

void StartListDialog::accept()
{
    const QVector<StartEntry> l = list();
    QSet<int> seen;
    for (const auto& e : l) {
        if (seen.contains(e.bib)) {
            QMessageBox::warning(this, windowTitle(), tr("The bib %1 is twice in the start list.").arg(e.bib));
            return;
        }
        seen.insert(e.bib);
    }
    if (!m_ev->setStartList(m_run, l)) {
        QMessageBox::warning(this, windowTitle(), m_ev->lastError());
        return;
    }
    QDialog::accept();
}
