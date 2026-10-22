#include "FisCompetitorsWindow.h"
#include "FisUi.h"
#include "UiHelpers.h"
#include "app/AppSettings.h"
#include "core/Event.h"
#include "core/FisCompetitors.h"
#include "core/TimeUtil.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <QTableView>
#include <QVBoxLayout>

// ---- model -----------------------------------------------------------------

FisCompetitorModel::FisCompetitorModel(const FisList* list, QObject* parent)
    : QAbstractTableModel(parent)
    , m_list(list)
{
}

void FisCompetitorModel::setExisting(const QSet<QString>& codes)
{
    m_existing = codes;
    if (rowCount() > 0)
        Q_EMIT dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1));
}

void FisCompetitorModel::setBoldDiscipline(int discipline)
{
    m_boldDiscipline = discipline;
}

bool FisCompetitorModel::isExisting(int row) const
{
    return row >= 0 && row < m_list->competitors.size()
        && m_existing.contains(FisCompetitors::normalizedCode(m_list->competitors.at(row).fisCode));
}

int FisCompetitorModel::rowCount(const QModelIndex&) const
{
    return int(m_list->competitors.size());
}

int FisCompetitorModel::columnCount(const QModelIndex&) const
{
    return PointsFirst + kDisciplineCount;
}

QVariant FisCompetitorModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_list->competitors.size())
        return QVariant();
    const FisCompetitor& c = m_list->competitors.at(index.row());
    const int col = index.column();
    if (role == Qt::DisplayRole) {
        switch (col) {
        case LastName: return c.lastName;
        case FirstName: return c.firstName;
        case Code: return c.fisCode;
        case Gender: return genderText(c.gender);
        case Year: return c.year > 0 ? QString::number(c.year) : QString();
        case Nation: return c.nation;
        default: return TimeUtil::formatPoints(c.points[size_t(col - PointsFirst)]);
        }
    }
    if (role == Qt::UserRole) {
        if (col == Year)
            return c.year > 0 ? c.year : -1;
        if (col >= PointsFirst)
            return c.points[size_t(col - PointsFirst)]; // -1 = no points
        return data(index, Qt::DisplayRole).toString().toLower();
    }
    if (role == Qt::TextAlignmentRole)
        return int((col == Year || col >= PointsFirst ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter);
    const bool existing = isExisting(index.row());
    if (role == Qt::ForegroundRole && existing)
        return QColor(Qt::gray);
    if (role == Qt::FontRole && (existing || col == PointsFirst + m_boldDiscipline)) {
        QFont f;
        f.setItalic(existing);
        f.setBold(col == PointsFirst + m_boldDiscipline);
        return f;
    }
    if (role == Qt::ToolTipRole && existing)
        return FisCompetitorsWindow::tr("Already in the list of competitors");
    return QVariant();
}

QVariant FisCompetitorModel::headerData(int section, Qt::Orientation o, int role) const
{
    if (o != Qt::Horizontal || role != Qt::DisplayRole)
        return QVariant();
    switch (section) {
    case LastName: return QCoreApplication::translate("Competitor", "Last Name");
    case FirstName: return QCoreApplication::translate("Competitor", "First Name");
    case Code: return FisCompetitorsWindow::tr("FIS Code");
    case Gender: return QCoreApplication::translate("Competitor", "Gender");
    case Year: return FisCompetitorsWindow::tr("YoB");
    case Nation: return QCoreApplication::translate("Competitor", "Nation");
    default: return fisDisciplineCode(Discipline(section - PointsFirst));
    }
}

void FisCompetitorFilter::setFilter(const QString& name, const QString& code, const QString& nation, int gender)
{
    m_name = name.trimmed();
    m_code = code.trimmed();
    m_nation = nation.trimmed();
    m_gender = gender;
    invalidateFilter();
}

bool FisCompetitorFilter::filterAcceptsRow(int row, const QModelIndex&) const
{
    const QAbstractItemModel* m = sourceModel();
    auto text = [&](int col) { return m->index(row, col).data().toString(); };
    if (!m_name.isEmpty()) {
        const QString name = text(FisCompetitorModel::LastName) + QLatin1Char(' ') + text(FisCompetitorModel::FirstName);
        if (!name.contains(m_name, Qt::CaseInsensitive))
            return false;
    }
    if (!m_code.isEmpty() && !text(FisCompetitorModel::Code).startsWith(m_code, Qt::CaseInsensitive))
        return false;
    if (!m_nation.isEmpty() && !text(FisCompetitorModel::Nation).startsWith(m_nation, Qt::CaseInsensitive))
        return false;
    if (m_gender != 0 && text(FisCompetitorModel::Gender) != genderText(Gender(m_gender)))
        return false;
    return true;
}

bool FisCompetitorFilter::lessThan(const QModelIndex& a, const QModelIndex& b) const
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

// ---- window -----------------------------------------------------------------

FisCompetitorsWindow::FisCompetitorsWindow(Event* ev, const FisList& list, QWidget* parent)
    : QWidget(parent, Qt::Window)
    , m_ev(ev)
    , m_list(list)
{
    setWindowTitle(tr("Import Competitors - %1").arg(FisUi::listTitle(m_list.header)));
    setWindowIcon(UiHelpers::icon(QStringLiteral("app")));
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 4, 6, 4);
    lay->addWidget(UiHelpers::titleBar(tr("Import Competitors from the FIS list"), this));

    auto* listInfo = new QLabel(this);
    listInfo->setTextFormat(Qt::RichText);
    listInfo->setText(QStringLiteral("<b>%1</b> &nbsp; %2%3")
                          .arg(FisUi::listTitle(m_list.header).toHtmlEscaped(),
                               FisUi::validityText(m_list.header).toHtmlEscaped(),
                               m_list.header.isExpired()
                                   ? QStringLiteral(" &nbsp; <b style='color:#c00000'>[%1]</b>").arg(tr("Expired").toHtmlEscaped())
                                   : QString()));
    lay->addWidget(listInfo);

    auto* search = new QGroupBox(tr("Search"), this);
    auto* sg = new QGridLayout(search);
    m_name = new QLineEdit(search);
    m_name->setMaximumWidth(220);
    m_code = new QLineEdit(search);
    m_code->setMaximumWidth(100);
    m_nation = new QLineEdit(search);
    m_nation->setMaximumWidth(60);
    m_gender = new QComboBox(search);
    m_gender->addItem(tr("All"), 0);
    m_gender->addItem(genderText(Gender::Male), int(Gender::Male));
    m_gender->addItem(genderText(Gender::Female), int(Gender::Female));
    sg->addWidget(new QLabel(tr("Name :"), search), 0, 0, Qt::AlignRight);
    sg->addWidget(m_name, 0, 1);
    sg->addWidget(new QLabel(tr("FIS Code :"), search), 0, 2, Qt::AlignRight);
    sg->addWidget(m_code, 0, 3);
    sg->addWidget(new QLabel(tr("Nation :"), search), 0, 4, Qt::AlignRight);
    sg->addWidget(m_nation, 0, 5);
    sg->addWidget(new QLabel(tr("Gender :"), search), 0, 6, Qt::AlignRight);
    sg->addWidget(m_gender, 0, 7);
    sg->setColumnStretch(8, 1);
    lay->addWidget(search);

    auto* body = new QHBoxLayout;
    auto* buttons = new QVBoxLayout;
    m_count = UiHelpers::infoBox(this);
    m_count->setMinimumWidth(140);
    buttons->addWidget(m_count);
    auto* bInsert = UiHelpers::button(QStringLiteral("add"), tr("Insert (Enter)"), this);
    bInsert->setToolTip(tr("Insert the selected competitors into Manage Competitors (or double click)"));
    auto* bClose = UiHelpers::button(QStringLiteral("close"), tr("Close"), this);
    buttons->addWidget(bInsert);
    buttons->addSpacing(20);
    buttons->addWidget(bClose);
    buttons->addStretch();
    body->addLayout(buttons);

    m_model = new FisCompetitorModel(&m_list, this);
    m_model->setBoldDiscipline(int(ev->params().type.discipline));
    m_proxy = new FisCompetitorFilter(this);
    m_proxy->setSourceModel(m_model);
    m_proxy->setSortRole(Qt::UserRole);
    m_view = new QTableView(this);
    m_view->setModel(m_proxy);
    m_view->setSortingEnabled(true);
    m_view->sortByColumn(FisCompetitorModel::LastName, Qt::AscendingOrder);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    UiHelpers::styleGrid(m_view);
    const int widths[] = { 150, 150, 80, 55, 55, 55 };
    for (int c = 0; c < FisCompetitorModel::PointsFirst; ++c)
        m_view->setColumnWidth(c, widths[c]);
    for (int c = FisCompetitorModel::PointsFirst; c < m_model->columnCount(); ++c)
        m_view->setColumnWidth(c, 70);
    body->addWidget(m_view, 1);
    lay->addLayout(body, 1);

    connect(m_name, &QLineEdit::textChanged, this, &FisCompetitorsWindow::applyFilter);
    connect(m_code, &QLineEdit::textChanged, this, &FisCompetitorsWindow::applyFilter);
    connect(m_nation, &QLineEdit::textChanged, this, &FisCompetitorsWindow::applyFilter);
    connect(m_gender, &QComboBox::currentIndexChanged, this, &FisCompetitorsWindow::applyFilter);
    connect(m_view, &QTableView::doubleClicked, this, &FisCompetitorsWindow::insertSelected);
    connect(bInsert, &QPushButton::clicked, this, &FisCompetitorsWindow::insertSelected);
    connect(bClose, &QPushButton::clicked, this, &QWidget::close);
    for (int key : { int(Qt::Key_Return), int(Qt::Key_Enter) }) {
        auto* sc = new QShortcut(QKeySequence(key), m_view);
        sc->setContext(Qt::WidgetShortcut);
        connect(sc, &QShortcut::activated, this, &FisCompetitorsWindow::insertSelected);
    }
    auto* esc = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(esc, &QShortcut::activated, this, &QWidget::close);
    connect(ev, &Event::competitorsChanged, this, &FisCompetitorsWindow::refreshExisting);
    connect(ev, &Event::paramsChanged, this, [this] {
        m_model->setBoldDiscipline(int(m_ev->params().type.discipline));
        m_view->viewport()->update();
    });
    connect(ev, &Event::closed, this, &QWidget::close);

    const QByteArray geo = AppSettings::value(QStringLiteral("FisCompetitorsWindow/geometry")).toByteArray();
    if (geo.isEmpty() || !restoreGeometry(geo))
        resize(1050, 700);
    refreshExisting();
    applyFilter();
    m_name->setFocus();
}

void FisCompetitorsWindow::closeEvent(QCloseEvent* e)
{
    AppSettings::setValue(QStringLiteral("FisCompetitorsWindow/geometry"), saveGeometry());
    e->accept();
}

void FisCompetitorsWindow::applyFilter()
{
    m_proxy->setFilter(m_name->text(), m_code->text(), m_nation->text(), m_gender->currentData().toInt());
    m_count->setText(QStringLiteral("%1 / %2").arg(m_proxy->rowCount()).arg(m_model->rowCount()));
}

void FisCompetitorsWindow::refreshExisting()
{
    QSet<QString> codes;
    if (m_ev->isOpen())
        for (const auto& c : m_ev->competitors())
            if (!c.code.trimmed().isEmpty())
                codes.insert(FisCompetitors::normalizedCode(c.code));
    m_model->setExisting(codes);
}

void FisCompetitorsWindow::insertSelected()
{
    if (!m_ev->isOpen())
        return;
    QList<int> rows;
    for (const QModelIndex& i : m_view->selectionModel()->selectedRows())
        rows << m_proxy->mapToSource(i).row();
    if (rows.isEmpty() && m_view->currentIndex().isValid())
        rows << m_proxy->mapToSource(m_view->currentIndex()).row();
    std::sort(rows.begin(), rows.end());
    QVector<Competitor> add;
    QStringList skipped;
    QSet<QString> added;
    for (int row : rows) {
        const FisCompetitor& f = m_list.competitors.at(row);
        const QString code = FisCompetitors::normalizedCode(f.fisCode);
        if (m_model->isExisting(row) || added.contains(code)) {
            skipped << QStringLiteral("%1 %2 (%3)").arg(f.lastName, f.firstName, f.fisCode);
            continue;
        }
        added.insert(code);
        add << FisCompetitors::fromFis(f, m_list.header, m_ev->categories());
    }
    if (add.size() == 1)
        m_ev->addCompetitor(add.first());
    else if (!add.isEmpty())
        m_ev->addCompetitors(add);
    if (!skipped.isEmpty()) {
        const int shown = 20;
        QString text = tr("Already in the list of competitors, not inserted:") + QStringLiteral("\n\n")
            + QStringList(skipped.mid(0, shown)).join(QLatin1Char('\n'));
        if (skipped.size() > shown)
            text += QStringLiteral("\n...");
        QMessageBox::warning(this, windowTitle(), text);
    }
}
