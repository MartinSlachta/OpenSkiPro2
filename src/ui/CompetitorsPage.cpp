#include "CompetitorsPage.h"
#include "MainWindow.h"
#include "UiHelpers.h"
#include "app/AppSettings.h"
#include "core/Event.h"
#include "db/EventDb.h"
#include "dialogs/AssignBibsDialog.h"
#include "dialogs/CategoriesDialog.h"
#include "dialogs/CompetitorDialog.h"
#include "dialogs/CompetitorFieldsDialog.h"
#include "dialogs/CsvImportDialog.h"
#include "dialogs/FormatFieldDialog.h"
#include "dialogs/SelectEventDialog.h"

#include <QClipboard>
#include <memory>
#include <QComboBox>
#include <QGuiApplication>
#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>

// ---- model -----------------------------------------------------------------

CompetitorTableModel::CompetitorTableModel(Event* ev, QObject* parent)
    : QAbstractTableModel(parent)
    , m_ev(ev)
{
}

void CompetitorTableModel::reload()
{
    beginResetModel();
    m_columns.clear();
    for (const auto& g : m_ev->params().fields.grid)
        if (m_ev->params().fields.isActive(g.first) || g.first == CompField::Bib)
            m_columns << g;
    endResetModel();
}

int CompetitorTableModel::rowCount(const QModelIndex&) const
{
    return m_ev->isOpen() ? int(m_ev->competitors().size()) : 0;
}

int CompetitorTableModel::columnCount(const QModelIndex&) const
{
    return int(m_columns.size());
}

int CompetitorTableModel::fieldAt(int column) const
{
    return column >= 0 && column < m_columns.size() ? m_columns.at(column).first : 0;
}

int CompetitorTableModel::competitorId(int row) const
{
    return row >= 0 && row < m_ev->competitors().size() ? m_ev->competitors().at(row).id : 0;
}

QVariant CompetitorTableModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_ev->competitors().size())
        return QVariant();
    const Competitor& c = m_ev->competitors().at(index.row());
    const int field = fieldAt(index.column());
    if (role == Qt::DisplayRole)
        return c.value(field);
    if (role == Qt::UserRole)
        return c.sortValue(field);
    if (role == Qt::TextAlignmentRole) {
        const auto* d = competitorFieldDef(field);
        return int((d && d->numeric ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter);
    }
    if (role == Qt::FontRole && field >= CompField::PointsDH && field <= CompField::PointsAC
        && field == pointsFieldFor(m_ev->params().type.discipline)) {
        QFont f;
        f.setBold(true);
        return f;
    }
    return QVariant();
}

QVariant CompetitorTableModel::headerData(int section, Qt::Orientation o, int role) const
{
    if (o == Qt::Horizontal && role == Qt::DisplayRole)
        return m_ev->params().fields.label(fieldAt(section));
    return QVariant();
}

void CompetitorFilterModel::setFilter(const QString& bib, const QString& name, int field, const QString& value)
{
    m_bib = bib.trimmed();
    m_name = name.trimmed();
    m_field = field;
    m_value = value.trimmed();
    invalidateFilter();
}

bool CompetitorFilterModel::filterAcceptsRow(int row, const QModelIndex&) const
{
    auto* model = static_cast<CompetitorTableModel*>(sourceModel());
    Q_UNUSED(model);
    // data is read through the source columns of the event directly
    const QModelIndex any = sourceModel()->index(row, 0);
    if (!any.isValid())
        return true;
    // Use the bib / name of the competitor
    const QAbstractItemModel* m = sourceModel();
    const int cols = m->columnCount();
    QString bib, name;
    QString fieldValue;
    for (int c = 0; c < cols; ++c) {
        const int f = static_cast<const CompetitorTableModel*>(m)->fieldAt(c);
        const QString v = m->index(row, c).data().toString();
        if (f == CompField::Bib)
            bib = v;
        if (f == CompField::LastName || f == CompField::FirstName || f == CompField::Name)
            name += v + QLatin1Char(' ');
        if (f == m_field)
            fieldValue = v;
    }
    if (!m_bib.isEmpty() && !bib.startsWith(m_bib))
        return false;
    if (!m_name.isEmpty() && !name.contains(m_name, Qt::CaseInsensitive))
        return false;
    if (m_field > 0 && !m_value.isEmpty() && !fieldValue.contains(m_value, Qt::CaseInsensitive))
        return false;
    return true;
}

bool CompetitorFilterModel::lessThan(const QModelIndex& a, const QModelIndex& b) const
{
    const QVariant va = a.data(Qt::UserRole), vb = b.data(Qt::UserRole);
    if (va.typeId() == QMetaType::Int && vb.typeId() == QMetaType::Int) {
        // empty values (-1) at the end in both sort orders
        const bool ea = va.toInt() < 0, eb = vb.toInt() < 0;
        if (ea != eb)
            return sortOrder() == Qt::AscendingOrder ? eb : ea;
        return va.toInt() < vb.toInt();
    }
    return QString::localeAwareCompare(va.toString(), vb.toString()) < 0;
}

// ---- page -------------------------------------------------------------------

CompetitorsPage::CompetitorsPage(Event* ev, QWidget* parent)
    : QWidget(parent)
    , m_ev(ev)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 4, 6, 4);
    lay->addWidget(UiHelpers::titleBar(tr("Manage Competitors"), this));

    auto* search = new QGroupBox(tr("Search"), this);
    auto* sg = new QGridLayout(search);
    m_searchBib = new QLineEdit(search);
    m_searchBib->setMaximumWidth(70);
    m_searchName = new QLineEdit(search);
    m_searchName->setMaximumWidth(220);
    m_searchField = new QComboBox(search);
    m_searchValue = new QLineEdit(search);
    m_searchValue->setMaximumWidth(220);
    sg->addWidget(new QLabel(tr("Bib. :"), search), 0, 0, Qt::AlignRight);
    sg->addWidget(m_searchBib, 0, 1);
    sg->addWidget(new QLabel(tr("Name :"), search), 0, 2, Qt::AlignRight);
    sg->addWidget(m_searchName, 0, 3);
    sg->addWidget(m_searchField, 1, 1, 1, 2);
    sg->addWidget(m_searchValue, 1, 3);
    sg->setColumnStretch(4, 1);
    lay->addWidget(search);

    auto* body = new QHBoxLayout;
    auto* buttons = new QVBoxLayout;
    m_count = UiHelpers::infoBox(this);
    m_count->setMinimumWidth(140);
    buttons->addWidget(m_count);
    auto* bNew = UiHelpers::button(QStringLiteral("new"), tr("New (F1)"), this);
    auto* bModify = UiHelpers::button(QStringLiteral("modify"), tr("Modify (F2)"), this);
    auto* bDelete = UiHelpers::button(QStringLiteral("delete"), tr("Delete"), this);
    auto* bImport = UiHelpers::button(QStringLiteral("import"), tr("Import"), this);
    auto* bOptions = UiHelpers::button(QStringLiteral("options"), tr("Options"), this);
    buttons->addWidget(bNew);
    buttons->addWidget(bModify);
    buttons->addWidget(bDelete);
    buttons->addSpacing(20);
    buttons->addWidget(bImport);
    buttons->addWidget(bOptions);
    buttons->addStretch();
    body->addLayout(buttons);

    m_model = new CompetitorTableModel(ev, this);
    m_proxy = new CompetitorFilterModel(this);
    m_proxy->setSourceModel(m_model);
    m_proxy->setSortRole(Qt::UserRole);
    m_view = new QTableView(this);
    m_view->setModel(m_proxy);
    m_view->setSortingEnabled(true);
    m_view->sortByColumn(0, Qt::AscendingOrder);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    UiHelpers::styleGrid(m_view);
    body->addWidget(m_view, 1);
    // a column resized with the mouse: its width is kept in the event (Competitor Fields / Width)
    m_widthTimer = new QTimer(this);
    m_widthTimer->setSingleShot(true);
    m_widthTimer->setInterval(600);
    connect(m_widthTimer, &QTimer::timeout, this, &CompetitorsPage::saveColumnWidths);
    connect(m_view->horizontalHeader(), &QHeaderView::sectionResized, this, [this] {
        if (!m_settingWidths)
            m_widthTimer->start();
    });
    lay->addLayout(body, 1);

    auto* menu = new QMenu(this);
    menu->addAction(tr("Import Competitors from a File (CSV, TXT, XLSX)"), this, &CompetitorsPage::importFile);
    menu->addAction(tr("Import Competitors from the Clipboard (Excel)"), this, &CompetitorsPage::importClipboard);
    menu->addAction(tr("Import Competitors from another Event"), this, &CompetitorsPage::importFromEvent);
    menu->addAction(tr("Duplicate selected Competitor"), this, &CompetitorsPage::duplicateSelected);
    menu->addAction(tr("Format a Field"), this, &CompetitorsPage::formatField);
    menu->addSeparator();
    menu->addAction(tr("Assign Bibs"), this, &CompetitorsPage::assignBibs);
    menu->addAction(tr("Delete Bibs"), this, &CompetitorsPage::deleteBibs);
    menu->addSeparator();
    menu->addAction(tr("Delete Competitors"), this, &CompetitorsPage::deleteAll);
    connect(bOptions, &QPushButton::clicked, this, [bOptions, menu] { menu->popup(bOptions->mapToGlobal(QPoint(0, bOptions->height()))); });

    connect(bNew, &QPushButton::clicked, this, &CompetitorsPage::newCompetitor);
    connect(bModify, &QPushButton::clicked, this, &CompetitorsPage::modifyCompetitor);
    connect(bDelete, &QPushButton::clicked, this, &CompetitorsPage::deleteCompetitors);
    connect(bImport, &QPushButton::clicked, this, &CompetitorsPage::importFile);
    connect(m_view, &QTableView::doubleClicked, this, &CompetitorsPage::modifyCompetitor);
    auto* f1 = new QShortcut(QKeySequence(Qt::Key_F1), this);
    connect(f1, &QShortcut::activated, this, &CompetitorsPage::newCompetitor);
    auto* f2 = new QShortcut(QKeySequence(Qt::Key_F2), this);
    connect(f2, &QShortcut::activated, this, &CompetitorsPage::modifyCompetitor);
    auto* del = new QShortcut(QKeySequence::Delete, m_view);
    connect(del, &QShortcut::activated, this, &CompetitorsPage::deleteCompetitors);

    connect(m_searchBib, &QLineEdit::textChanged, this, &CompetitorsPage::applyFilter);
    connect(m_searchName, &QLineEdit::textChanged, this, &CompetitorsPage::applyFilter);
    connect(m_searchValue, &QLineEdit::textChanged, this, &CompetitorsPage::applyFilter);
    connect(m_searchField, &QComboBox::currentIndexChanged, this, &CompetitorsPage::applyFilter);
    connect(ev, &Event::competitorsChanged, this, &CompetitorsPage::reload);
    connect(ev, &Event::paramsChanged, this, &CompetitorsPage::reload);
    connect(ev, &Event::opened, this, &CompetitorsPage::reload);
    connect(ev, &Event::closed, this, &CompetitorsPage::reload);
    reload();
}

void CompetitorsPage::reload()
{
    const int keepField = m_searchField->currentData().toInt();
    m_settingWidths = true;
    m_model->reload();
    for (int c = 0; c < m_model->columnCount(); ++c) {
        int w = 75;
        for (const auto& g : m_ev->params().fields.grid)
            if (g.first == m_model->fieldAt(c))
                w = g.second;
        m_view->setColumnWidth(c, w);
    }
    m_settingWidths = false;
    m_searchField->blockSignals(true);
    m_searchField->clear();
    for (const auto& d : competitorFieldDefs())
        if (m_ev->params().fields.isActive(d.id) && d.id != CompField::Bib)
            m_searchField->addItem(m_ev->params().fields.label(d.id), d.id);
    const int idx = m_searchField->findData(keepField > 0 ? keepField : CompField::Code);
    m_searchField->setCurrentIndex(qMax(0, idx));
    m_searchField->blockSignals(false);
    m_count->setText(QString::number(m_ev->isOpen() ? m_ev->competitors().size() : 0));
    applyFilter();
}

void CompetitorsPage::saveColumnWidths()
{
    if (!m_ev->isOpen())
        return;
    EventParams p = m_ev->params();
    bool changed = false;
    for (auto& g : p.fields.grid)
        for (int c = 0; c < m_model->columnCount(); ++c)
            if (m_model->fieldAt(c) == g.first) {
                const int w = qMax(30, m_view->columnWidth(c));
                if (w != g.second) {
                    g.second = w;
                    changed = true;
                }
            }
    if (changed)
        m_ev->setParams(p);
}

void CompetitorsPage::applyFilter()
{
    m_proxy->setFilter(m_searchBib->text(), m_searchName->text(), m_searchField->currentData().toInt(), m_searchValue->text());
}

QList<int> CompetitorsPage::selectedIds() const
{
    QList<int> ids;
    for (const QModelIndex& i : m_view->selectionModel()->selectedRows())
        ids << m_model->competitorId(m_proxy->mapToSource(i).row());
    return ids;
}

void CompetitorsPage::newCompetitor()
{
    if (!m_ev->isOpen())
        return;
    Competitor c;
    int maxBib = 0;
    for (const auto& x : m_ev->competitors())
        if (!m_ev->isForerunner(x.bib))
            maxBib = qMax(maxBib, x.bib);
    c.bib = maxBib + 1;
    CompetitorDialog dlg(m_ev, c, true, this);
    while (dlg.exec() == QDialog::Accepted) {
        Competitor n = dlg.competitor();
        m_ev->addCompetitor(n);
        // keep entering: next bib
        Competitor next;
        next.bib = n.bib + 1;
        dlg.setCompetitor(next);
    }
}

void CompetitorsPage::modifyCompetitor()
{
    const QList<int> ids = selectedIds();
    if (ids.isEmpty())
        return;
    const Competitor* c = m_ev->competitorById(ids.first());
    if (!c)
        return;
    CompetitorDialog dlg(m_ev, *c, false, this);
    if (dlg.exec() == QDialog::Accepted)
        m_ev->updateCompetitor(dlg.competitor());
}

void CompetitorsPage::deleteCompetitors()
{
    const QList<int> ids = selectedIds();
    if (ids.isEmpty())
        return;
    if (QMessageBox::question(this, tr("Delete"), tr("Delete %n selected competitor(s)?", nullptr, int(ids.size())))
        != QMessageBox::Yes)
        return;
    m_ev->removeCompetitors(ids);
}

void CompetitorsPage::deleteAll()
{
    if (!m_ev->isOpen() || m_ev->competitors().isEmpty())
        return;
    if (QMessageBox::question(this, tr("Delete Competitors"), tr("Delete all %n competitor(s) of the event?", nullptr,
                                                                 int(m_ev->competitors().size())))
        != QMessageBox::Yes)
        return;
    QList<int> ids;
    for (const auto& c : m_ev->competitors())
        ids << c.id;
    m_ev->removeCompetitors(ids);
}

void CompetitorsPage::deleteBibs()
{
    if (!m_ev->isOpen())
        return;
    int count = 0;
    for (const auto& c : m_ev->competitors())
        if (c.bib > 0)
            ++count;
    if (count == 0) {
        QMessageBox::information(this, tr("Delete Bibs"), tr("No competitor has a bib."));
        return;
    }
    QString text = tr("Delete the bibs of %n competitor(s)?", nullptr, count);
    bool runData = false;
    for (int r = 1; r <= m_ev->runCount(); ++r)
        runData = runData || !m_ev->run(r).startList.isEmpty() || !m_ev->run(r).records.isEmpty()
            || !m_ev->run(r).infos.isEmpty();
    if (runData)
        text += QStringLiteral("\n\n") + tr("Warning: the start lists and the timing data of the runs use the bibs.");
    if (QMessageBox::question(this, tr("Delete Bibs"), text, QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
        != QMessageBox::Yes)
        return;
    const QVector<Competitor> list = m_ev->competitors();
    m_ev->beginBatch();
    for (const auto& c : list) {
        if (c.bib <= 0)
            continue;
        Competitor n = c;
        n.bib = 0;
        m_ev->updateCompetitor(n);
    }
    m_ev->commitBatch();
}

void CompetitorsPage::importFile()
{
    if (!m_ev->isOpen())
        return;
    const QString file = QFileDialog::getOpenFileName(this, tr("Import Competitors from a File"),
                                                      AppSettings::value(QStringLiteral("folders/import")).toString(),
                                                      tr("Tables (*.csv *.txt *.xlsx);;All files (*)"));
    if (file.isEmpty())
        return;
    AppSettings::setValue(QStringLiteral("folders/import"), QFileInfo(file).absolutePath());
    CsvImportDialog dlg(m_ev, file, this);
    dlg.exec();
}

void CompetitorsPage::importClipboard()
{
    if (!m_ev->isOpen())
        return;
    const QString text = QGuiApplication::clipboard()->text();
    if (text.trimmed().isEmpty()) {
        QMessageBox::information(this, tr("Import Competitors from the Clipboard"),
                                 tr("The clipboard is empty: copy the cells of the competitors in Excel first."));
        return;
    }
    std::unique_ptr<CsvImportDialog> dlg(CsvImportDialog::fromText(m_ev, text, this));
    dlg->exec();
}

void CompetitorsPage::importFromEvent()
{
    SelectEventDialog dlg(SelectEventDialog::Mode::Existing, this);
    dlg.setWindowTitle(tr("List of Competitors from another Event"));
    if (dlg.exec() != QDialog::Accepted)
        return;
    const QString path = MainWindow::eventPath(dlg.selectedSlot());
    if (path == m_ev->path())
        return;
    EventDb other;
    QString err;
    if (!other.open(path, false, &err)) {
        QMessageBox::warning(this, tr("Import"), err);
        return;
    }
    QVector<Competitor> list = other.loadCompetitors();
    other.close();
    for (auto& c : list)
        c.id = 0;
    m_ev->addCompetitors(list);
    QMessageBox::information(this, tr("Import"), tr("%n competitor(s) imported.", nullptr, int(list.size())));
}

void CompetitorsPage::duplicateSelected()
{
    const QList<int> ids = selectedIds();
    if (ids.isEmpty())
        return;
    const Competitor* c = m_ev->competitorById(ids.first());
    if (!c)
        return;
    Competitor copy = *c;
    copy.id = 0;
    copy.bib = 0;
    m_ev->addCompetitor(copy);
}

void CompetitorsPage::formatField()
{
    if (!m_ev->isOpen())
        return;
    FormatFieldDialog dlg(m_ev, this);
    dlg.exec();
}

void CompetitorsPage::editFields()
{
    if (!m_ev->isOpen())
        return;
    CompetitorFieldsDialog dlg(m_ev->params().fields, this);
    if (dlg.exec() == QDialog::Accepted) {
        EventParams p = m_ev->params();
        p.fields = dlg.config();
        m_ev->setParams(p);
    }
}

void CompetitorsPage::assignBibs()
{
    if (!m_ev->isOpen())
        return;
    AssignBibsDialog dlg(m_ev, this);
    dlg.exec();
}

void CompetitorsPage::editCategories()
{
    if (!m_ev->isOpen())
        return;
    CategoriesDialog dlg(m_ev, this);
    dlg.exec();
}
