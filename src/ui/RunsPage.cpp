#include "RunsPage.h"
#include "UiHelpers.h"
#include "core/Event.h"
#include "core/Ranking.h"
#include "dialogs/ModifyDataDialog.h"
#include "dialogs/StartListDialog.h"
#include "dialogs/UnprocessedDialog.h"
#include "timing/TimingEngine.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
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
    m_ranking->setContextMenuPolicy(Qt::CustomContextMenu);
    m_ranking->setToolTip(tr("Right click: disqualify the competitor (the gate of the referee's report), Modify Data.\n"
                             "Double click: Modify Data."));
    right->addWidget(m_ranking, 1);
    body->addLayout(right, 1);
    lay->addLayout(body, 1);

    connect(prev, &QToolButton::clicked, this, [this] { setRun(m_run - 1); });
    connect(next, &QToolButton::clicked, this, [this] { setRun(m_run + 1); });
    connect(bStart, &QPushButton::clicked, this, &RunsPage::editStartList);
    connect(bTiming, &QPushButton::clicked, this, [this] { Q_EMIT timingRequested(m_run); });
    connect(bModify, &QPushButton::clicked, this, &RunsPage::modifyData);
    connect(m_ranking, &QTableWidget::customContextMenuRequested, this, &RunsPage::rankingMenu);
    connect(m_ranking, &QTableWidget::cellDoubleClicked, this, [this](int row) {
        if (const int bib = bibAt(row); bib > 0)
            modifyBib(bib);
    });
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
    modifyBib(0);
}

void RunsPage::modifyBib(int bib)
{
    if (!m_ev->isOpen())
        return;
    ModifyDataDialog dlg(m_ev, m_run, bib, this);
    dlg.exec();
}

int RunsPage::bibAt(int row) const
{
    const QTableWidgetItem* it = m_ranking->item(row, 1);
    return it ? it->text().toInt() : 0;
}

// After the run the referee goes through the reports of the gate judges: a DSQ given
// directly in the ranking, also to a competitor with a time (status of the current run).
void RunsPage::rankingMenu(const QPoint& pos)
{
    const int bib = bibAt(m_ranking->rowAt(pos.y()));
    if (bib <= 0 || !m_ev->isOpen())
        return;
    const TimeInfo info = m_ev->run(m_run).info(bib);
    const bool dsq = info.status == ResultStatus::DSQ;
    const QString who = QStringLiteral("%1 - %2").arg(bib).arg(m_ev->competitorName(bib));
    const QString run = m_ev->params().type.runName(m_run);
    QMenu menu(this);
    QAction* title = menu.addAction(who);
    title->setEnabled(false);
    menu.addSeparator();
    QAction* aDsq = menu.addAction(UiHelpers::icon(QStringLiteral("dsq")),
                                   dsq ? tr("Gate of the disqualification (%1)...").arg(run) : tr("Disqualified (%1)...").arg(run));
    QAction* aUndo = dsq ? menu.addAction(tr("Remove the disqualification")) : nullptr;
    menu.addSeparator();
    QAction* aModify = menu.addAction(UiHelpers::icon(QStringLiteral("modify")), tr("Modify Data..."));
    QAction* chosen = menu.exec(m_ranking->viewport()->mapToGlobal(pos));
    if (!chosen)
        return;
    if (chosen == aModify) {
        modifyBib(bib);
        return;
    }
    TimingEngine engine(m_ev);
    engine.setRun(m_run);
    if (chosen == aDsq) {
        bool ok = false;
        const QString gate = QInputDialog::getText(this, tr("Disqualified"),
                                                   tr("Disqualification of %1 (%2)\nGate (or reason) :").arg(who, run),
                                                   QLineEdit::Normal, info.reason, &ok);
        if (!ok)
            return;
        engine.setStatus(bib, ResultStatus::DSQ, gate.trimmed());
    } else if (chosen == aUndo) {
        if (QMessageBox::question(this, tr("Disqualified"), tr("Remove the disqualification of %1 (%2)?").arg(who, run))
            != QMessageBox::Yes)
            return;
        engine.setStatus(bib, ResultStatus::Classified); // the result of the times records again
    }
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
