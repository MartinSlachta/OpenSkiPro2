#include "RunsPage.h"
#include "UiHelpers.h"
#include "core/Event.h"
#include "core/Ranking.h"
#include "dialogs/ModifyDataDialog.h"
#include "dialogs/StartListDialog.h"
#include "dialogs/UnprocessedDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

RunsPage::RunsPage(Event* ev, QWidget* parent)
    : QWidget(parent)
    , m_ev(ev)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 4, 6, 4);
    lay->addWidget(UiHelpers::titleBar(tr("Manage Runs"), this));

    auto* nav = new QHBoxLayout;
    auto* prev = new QToolButton(this);
    prev->setIcon(UiHelpers::icon(QStringLiteral("left")));
    prev->setIconSize(QSize(24, 24));
    prev->setToolTip(tr("Previous Run (Preceeding Page)"));
    prev->setAutoRaise(true);
    auto* next = new QToolButton(this);
    next->setIcon(UiHelpers::icon(QStringLiteral("right")));
    next->setIconSize(QSize(24, 24));
    next->setToolTip(tr("Next Run (Following Page)"));
    next->setAutoRaise(true);
    m_runLabel = new QLabel(this);
    QFont f = m_runLabel->font();
    f.setPointSize(16);
    f.setBold(true);
    m_runLabel->setFont(f);
    nav->addWidget(prev);
    nav->addWidget(next);
    nav->addWidget(m_runLabel);
    nav->addStretch();
    lay->addLayout(nav);

    auto* inq = new QGroupBox(tr("Inquiries"), this);
    auto* ig = new QGridLayout(inq);
    const QStringList labels = { tr("List"), tr("Classified"), tr("Did Not Start"), tr("Not Permitted"), tr("Did Not Finish"),
                                 tr("Disqualified"), tr("Not Processed") };
    QLabel** boxes[] = { &m_cList, &m_cClassified, &m_cDns, &m_cNps, &m_cDnf, &m_cDsq, &m_cNotProcessed };
    for (int i = 0; i < 7; ++i) {
        auto* l = new QLabel(labels.at(i), inq);
        l->setAlignment(Qt::AlignCenter);
        ig->addWidget(l, 0, i);
        *boxes[i] = UiHelpers::counterBox(inq);
        ig->addWidget(*boxes[i], 1, i, Qt::AlignHCenter);
    }
    ig->setColumnStretch(7, 1);
    lay->addWidget(inq);

    auto* body = new QHBoxLayout;
    auto* buttons = new QVBoxLayout;
    auto* bStart = UiHelpers::button(QStringLiteral("startlist"), tr("Start List"), this);
    auto* bTiming = UiHelpers::button(QStringLiteral("stopwatch"), tr("Timing"), this);
    bTiming->setMinimumHeight(48);
    auto* bModify = UiHelpers::button(QStringLiteral("modify"), tr("Modify Data"), this);
    auto* bUnproc = UiHelpers::button(QStringLiteral("check"), tr("Manage un-processed"), this);
    auto* bDelete = UiHelpers::button(QStringLiteral("delete"), tr("Delete Data"), this);
    for (auto* b : { bStart, bTiming, bModify, bUnproc, bDelete }) {
        b->setMinimumWidth(190);
        buttons->addWidget(b);
    }
    buttons->addStretch();
    body->addLayout(buttons);

    auto* right = new QVBoxLayout;
    auto* rtitle = new QHBoxLayout;
    rtitle->addWidget(UiHelpers::titleBar(tr("Ranking"), this), 1);
    m_rankingMode = new QComboBox(this);
    m_rankingMode->addItem(tr("Run"));
    m_rankingMode->addItem(tr("Event"));
    rtitle->addWidget(m_rankingMode);
    right->addLayout(rtitle);
    m_ranking = new QTableWidget(this);
    m_ranking->setColumnCount(4);
    m_ranking->setHorizontalHeaderLabels({ tr("Rank"), tr("Num"), tr("Competitor"), tr("Time") });
    m_ranking->setEditTriggers(QAbstractItemView::NoEditTriggers);
    UiHelpers::styleGrid(m_ranking, true);
    m_ranking->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_ranking->setColumnWidth(0, 80);
    m_ranking->setColumnWidth(1, 80);
    m_ranking->setColumnWidth(3, 140);
    right->addWidget(m_ranking, 1);
    body->addLayout(right, 1);
    lay->addLayout(body, 1);

    connect(prev, &QToolButton::clicked, this, [this] { setRun(m_run - 1); });
    connect(next, &QToolButton::clicked, this, [this] { setRun(m_run + 1); });
    connect(bStart, &QPushButton::clicked, this, &RunsPage::editStartList);
    connect(bTiming, &QPushButton::clicked, this, [this] { Q_EMIT timingRequested(m_run); });
    connect(bModify, &QPushButton::clicked, this, &RunsPage::modifyData);
    connect(bUnproc, &QPushButton::clicked, this, &RunsPage::manageUnprocessed);
    connect(bDelete, &QPushButton::clicked, this, &RunsPage::deleteData);
    connect(m_rankingMode, &QComboBox::currentIndexChanged, this, &RunsPage::refresh);
    connect(ev, &Event::runChanged, this, &RunsPage::refresh);
    connect(ev, &Event::competitorsChanged, this, &RunsPage::refresh);
    connect(ev, &Event::paramsChanged, this, [this] { setRun(m_run); });
    connect(ev, &Event::opened, this, [this] { setRun(1); });
    setRun(1);
}

void RunsPage::setRun(int run)
{
    m_run = qBound(1, run, qMax(1, m_ev->runCount()));
    m_runLabel->setText(tr("%1 / %2").arg(m_ev->params().type.runName(m_run)).arg(m_ev->runCount()));
    refresh();
}

void RunsPage::refresh()
{
    if (!m_ev->isOpen()) {
        m_ranking->setRowCount(0);
        return;
    }
    const RunCounters c = Ranking::counters(*m_ev, m_run);
    m_cList->setText(QString::number(c.list));
    m_cClassified->setText(QString::number(c.classified));
    m_cDns->setText(QString::number(c.dns));
    m_cNps->setText(QString::number(c.nps));
    m_cDnf->setText(QString::number(c.dnf));
    m_cDsq->setText(QString::number(c.dsq));
    m_cNotProcessed->setText(QString::number(c.notProcessed));

    const bool event = m_rankingMode->currentIndex() == 1;
    const RankingResult r = event ? Ranking::finalRanking(*m_ev, m_run) : Ranking::runRanking(*m_ev, m_run);
    int rows = int(r.classified.size());
    for (const auto& g : r.unclassified)
        rows += int(g.entries.size());
    m_ranking->setRowCount(rows);
    int row = 0;
    auto set = [&](int col, const QString& text, Qt::Alignment a) {
        auto* it = new QTableWidgetItem(text);
        it->setTextAlignment(a | Qt::AlignVCenter);
        m_ranking->setItem(row, col, it);
    };
    for (const auto& e : r.classified) {
        set(0, QString::number(e.rank), Qt::AlignHCenter);
        set(1, QString::number(e.bib), Qt::AlignHCenter);
        set(2, m_ev->competitorName(e.bib), Qt::AlignLeft);
        set(3, TimeUtil::formatResult(e.shownMs, m_ev->precision()), Qt::AlignRight);
        ++row;
    }
    for (const auto& g : r.unclassified) {
        for (const auto& e : g.entries) {
            set(0, QString(), Qt::AlignHCenter);
            set(1, QString::number(e.bib), Qt::AlignHCenter);
            set(2, m_ev->competitorName(e.bib), Qt::AlignLeft);
            set(3, UiHelpers::statusText(e.status), Qt::AlignRight);
            ++row;
        }
    }
}

void RunsPage::editStartList()
{
    if (!m_ev->isOpen())
        return;
    StartListDialog dlg(m_ev, m_run, this);
    dlg.exec();
}

void RunsPage::modifyData()
{
    if (!m_ev->isOpen())
        return;
    ModifyDataDialog dlg(m_ev, m_run, 0, this);
    dlg.exec();
}

void RunsPage::manageUnprocessed()
{
    if (!m_ev->isOpen())
        return;
    UnprocessedDialog dlg(m_ev, m_run, this);
    dlg.exec();
}

void RunsPage::deleteData()
{
    if (!m_ev->isOpen())
        return;
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Delete Data"));
    auto* lay = new QVBoxLayout(&dlg);
    lay->addWidget(new QLabel(tr("Delete the data of run %1:").arg(m_run), &dlg));
    auto* timing = new QCheckBox(tr("Timing data (times records, results, timing device paper)"), &dlg);
    timing->setChecked(true);
    auto* startList = new QCheckBox(tr("Start list"), &dlg);
    lay->addWidget(timing);
    lay->addWidget(startList);
    lay->addLayout(UiHelpers::okCancelRow(&dlg));
    if (dlg.exec() != QDialog::Accepted || (!timing->isChecked() && !startList->isChecked()))
        return;
    if (QMessageBox::warning(this, tr("Delete Data"), tr("The selected data of run %1 will be deleted. Continue?").arg(m_run),
                             QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
        != QMessageBox::Yes)
        return;
    m_ev->clearRun(m_run, startList->isChecked(), timing->isChecked());
}
