#include "DoubleDrawDialog.h"
#include "core/EventParams.h"
#include "core/TimeUtil.h"
#include "app/AppSettings.h"
#include "ui/UiHelpers.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
enum Column { ColRank, ColGroup, ColName, ColNation, ColPoints, ColStart, ColCount };
const QColor kGroup1(0xe8, 0xf0, 0xfa);
const QColor kGroup2(0xf4, 0xf4, 0xe4);
const QColor kTie(0xff, 0xd2, 0x8a);
const QColor kDuplicate(0xff, 0xa0, 0xa0);
} // namespace

DoubleDrawDialog::DoubleDrawDialog(const QVector<Competitor>& list, const QVector<int>& order, Discipline d,
                                   QWidget* parent)
    : QDialog(parent)
    , m_list(list)
    , m_order(order)
    , m_discipline(d)
{
    setWindowTitle(tr("Double draw (NC / NJC)"));
    auto* lay = new QVBoxLayout(this);
    m_help = new QLabel(this);
    m_help->setWordWrap(true);
    lay->addWidget(m_help);
    m_increase = new QCheckBox(tr("Increase the group with equal points"), this);
    m_increase->setToolTip(tr("When competitors share the 7th or the 15th rank, they all belong to the group "
                              "(e.g. 1-8 and 9-15), like the first group of ICR 621.3"));
    m_increase->setChecked(AppSettings::value(QStringLiteral("assignBibs/increaseGroups"), false).toBool());
    lay->addWidget(m_increase);

    auto* body = new QHBoxLayout;
    m_table = new QTableWidget(0, ColCount, this);
    m_table->setHorizontalHeaderLabels({ tr("Points rank"), tr("Group"), tr("Name"), tr("Nation"), tr("Points"),
                                         tr("Start order") });
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setSectionResizeMode(ColName, QHeaderView::Stretch);
    const int widths[] = { 85, 55, 0, 60, 70, 110 };
    for (int c = 0; c < ColCount; ++c)
        if (widths[c] > 0)
            m_table->setColumnWidth(c, widths[c]);
    m_table->verticalHeader()->setDefaultSectionSize(26);
    body->addWidget(m_table, 1);

    auto* buttons = new QVBoxLayout;
    auto* bDraw = UiHelpers::button(QStringLiteral("calculator"), tr("Draw"), this);
    bDraw->setToolTip(tr("Random draw of the start orders within each group"));
    auto* bClear = UiHelpers::button(QStringLiteral("delete"), tr("Clear"), this);
    auto* bUp = UiHelpers::button(QStringLiteral("up"), tr("Up"), this);
    auto* bDown = UiHelpers::button(QStringLiteral("down"), tr("Down"), this);
    bUp->setToolTip(tr("Changes the points rank (equal points decided by the Jury)"));
    bDown->setToolTip(bUp->toolTip());
    buttons->addWidget(bDraw);
    buttons->addWidget(bClear);
    buttons->addSpacing(20);
    buttons->addWidget(bUp);
    buttons->addWidget(bDown);
    buttons->addStretch();
    body->addLayout(buttons);
    lay->addLayout(body, 1);

    m_info = new QLabel(this);
    m_info->setWordWrap(true);
    lay->addWidget(m_info);
    lay->addLayout(UiHelpers::okCancelRow(this));

    connect(bDraw, &QPushButton::clicked, this, &DoubleDrawDialog::draw);
    connect(bClear, &QPushButton::clicked, this, &DoubleDrawDialog::clear);
    connect(bUp, &QPushButton::clicked, this, [this] { moveRow(-1); });
    connect(bDown, &QPushButton::clicked, this, [this] { moveRow(1); });
    connect(m_increase, &QCheckBox::toggled, this, [this](bool on) {
        AppSettings::setValue(QStringLiteral("assignBibs/increaseGroups"), on);
        updateGroups();
    });

    updateGroups();
    resize(780, 640);
}

int DoubleDrawDialog::groupEnd(int row) const
{
    return row < m_groups.firstEnd ? m_groups.firstEnd : m_groups.secondEnd;
}

void DoubleDrawDialog::updateGroups()
{
    m_groups = BibDraw::doubleDrawGroups(m_list, m_order, m_discipline, m_increase->isChecked());
    m_help->setText(tr("The 15 best by %1 points in two groups: 1 to %2 and %3 to %4 (ICR 621.4).\n"
                       "First ball: points rank of the competitor, second ball: its start order in the group.")
                        .arg(disciplineName(m_discipline))
                        .arg(m_groups.firstEnd)
                        .arg(m_groups.firstEnd + 1)
                        .arg(m_groups.secondEnd));
    // rows of the table, the start orders already typed stay with their row
    while (m_spins.size() > m_groups.secondEnd)
        m_spins.removeLast(); // the cell widget is deleted by setRowCount()
    m_table->setRowCount(m_groups.secondEnd);
    for (int r = int(m_spins.size()); r < m_groups.secondEnd; ++r) {
        auto* sp = new QSpinBox(m_table);
        sp->setSpecialValueText(QStringLiteral(" "));
        sp->setAlignment(Qt::AlignCenter);
        m_spins << sp;
        m_table->setCellWidget(r, ColStart, sp);
        connect(sp, &QSpinBox::valueChanged, this, &DoubleDrawDialog::validate);
    }
    fillTable();
}

void DoubleDrawDialog::fillTable()
{
    for (int r = 0; r < m_groups.secondEnd; ++r) {
        const Competitor& c = m_list.at(m_order.at(r));
        const int pts = c.pointsFor(m_discipline);
        // same points as the neighbour across a group limit
        bool tie = false;
        for (int limit : { m_groups.firstEnd, m_groups.secondEnd }) {
            if (limit <= 0 || limit >= m_order.size())
                continue;
            const int a = m_list.at(m_order.at(limit - 1)).pointsFor(m_discipline);
            const int b = m_list.at(m_order.at(limit)).pointsFor(m_discipline);
            if (a == b && a >= 0 && pts == a)
                tie = true;
        }
        const QStringList texts = { QString::number(r + 1), r < m_groups.firstEnd ? QStringLiteral("1") : QStringLiteral("2"),
                                    c.fullName(), c.nation, TimeUtil::formatPoints(pts) };
        for (int col = 0; col < texts.size(); ++col) {
            auto* it = new QTableWidgetItem(texts.at(col));
            it->setTextAlignment((col == ColName || col == ColNation ? Qt::AlignLeft : Qt::AlignCenter) | Qt::AlignVCenter);
            it->setBackground(tie ? kTie : (r < m_groups.firstEnd ? kGroup1 : kGroup2));
            if (tie)
                it->setToolTip(tr("Equal points across the limit of a group: the Jury decides (Up / Down)"));
            m_table->setItem(r, col, it);
        }
    }
    updateRanges();
}

void DoubleDrawDialog::updateRanges()
{
    for (int r = 0; r < m_spins.size(); ++r) {
        const int from = r < m_groups.firstEnd ? 1 : m_groups.firstEnd + 1;
        QSpinBox* sp = m_spins.at(r);
        const QSignalBlocker block(sp);
        // the minimum (first order of the group - 1) shows as empty (special value)
        const bool empty = sp->value() == sp->minimum();
        const int v = sp->value();
        sp->setRange(from - 1, groupEnd(r));
        sp->setSpecialValueText(QStringLiteral(" "));
        sp->setValue(!empty && v >= from && v <= groupEnd(r) ? v : from - 1);
    }
    validate();
}

void DoubleDrawDialog::validate()
{
    QHash<int, int> count;
    int missing = 0;
    for (int r = 0; r < m_spins.size(); ++r) {
        const int v = m_spins.at(r)->value();
        if (v == m_spins.at(r)->minimum())
            ++missing;
        else
            ++count[v];
    }
    int duplicates = 0;
    for (int r = 0; r < m_spins.size(); ++r) {
        QSpinBox* sp = m_spins.at(r);
        const bool dup = sp->value() != sp->minimum() && count.value(sp->value()) > 1;
        if (dup)
            ++duplicates;
        sp->setStyleSheet(dup ? QStringLiteral("QSpinBox { background: %1; }").arg(kDuplicate.name()) : QString());
    }
    QStringList msgs;
    if (missing > 0)
        msgs << tr("%n competitor(s) without start order.", nullptr, missing);
    if (duplicates > 0)
        msgs << QStringLiteral("<span style='color:#c00000'>%1</span>")
                    .arg(tr("A start order is given several times.").toHtmlEscaped());
    if (m_groups.tieAtLimit)
        msgs << QStringLiteral("<span style='color:#a05000'>%1</span>")
                    .arg(tr("Equal points across the limit of a group (orange): the Jury decides, change the points "
                            "rank with Up / Down.")
                             .toHtmlEscaped());
    if (msgs.isEmpty())
        msgs << tr("All the start orders are given.");
    m_info->setText(msgs.join(QStringLiteral("<br>")));
}

QVector<int> DoubleDrawDialog::startOrders() const
{
    QVector<int> out;
    for (QSpinBox* sp : m_spins)
        out << sp->value();
    return out;
}

void DoubleDrawDialog::moveRow(int delta)
{
    const int r = m_table->currentRow();
    const int to = r + delta;
    if (r < 0 || to < 0 || to >= m_spins.size())
        return;
    m_order.swapItemsAt(r, to);
    // the start orders stay with the rows (balls already drawn for this rank)
    updateGroups();
    m_table->selectRow(to);
}

void DoubleDrawDialog::draw()
{
    bool any = false;
    for (QSpinBox* sp : m_spins)
        any = any || sp->value() != sp->minimum();
    if (any && QMessageBox::question(this, windowTitle(), tr("Replace the start orders already given?")) != QMessageBox::Yes)
        return;
    const QVector<int> so = BibDraw::randomDoubleDraw(m_groups, *QRandomGenerator::global());
    for (int r = 0; r < m_spins.size() && r < so.size(); ++r) {
        const QSignalBlocker block(m_spins.at(r));
        m_spins.at(r)->setValue(so.at(r));
    }
    validate();
}

void DoubleDrawDialog::clear()
{
    for (QSpinBox* sp : m_spins) {
        const QSignalBlocker block(sp);
        sp->setValue(sp->minimum());
    }
    validate();
}

void DoubleDrawDialog::accept()
{
    QSet<int> seen;
    for (QSpinBox* sp : m_spins) {
        if (sp->value() == sp->minimum() || seen.contains(sp->value())) {
            QMessageBox::warning(this, windowTitle(), tr("Give a different start order to each competitor."));
            return;
        }
        seen.insert(sp->value());
    }
    QDialog::accept();
}
