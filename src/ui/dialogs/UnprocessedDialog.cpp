#include "UnprocessedDialog.h"
#include "core/Event.h"
#include "core/Ranking.h"
#include "timing/TimingEngine.h"
#include "ui/UiHelpers.h"

#include <QHeaderView>
#include <QInputDialog>
#include <QMessageBox>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace {

constexpr int kStatusRole = Qt::UserRole + 1;   // status chosen in the dialog (on the bib item)
constexpr int kReasonRole = Qt::UserRole + 2;   // gate of a DSQ
constexpr int kOrigStatusRole = Qt::UserRole + 3;
constexpr int kOrigReasonRole = Qt::UserRole + 4;

int columnOf(ResultStatus st)
{
    switch (st) {
    case ResultStatus::DNS: return 2;
    case ResultStatus::NPS: return 3;
    case ResultStatus::DNF: return 4;
    case ResultStatus::DSQ: return 5;
    default: return -1;
    }
}

ResultStatus statusOf(int column)
{
    switch (column) {
    case 2: return ResultStatus::DNS;
    case 3: return ResultStatus::NPS;
    case 4: return ResultStatus::DNF;
    case 5: return ResultStatus::DSQ;
    default: return ResultStatus::NotProcessed;
    }
}

// "12" -> "Gate 12", other texts as they are (like the reports)
QString gateText(const QString& reason)
{
    const QString r = reason.trimmed();
    bool number = false;
    r.toInt(&number);
    return number ? UnprocessedDialog::tr("Gate %1").arg(r) : r;
}

} // namespace

UnprocessedDialog::UnprocessedDialog(Event* ev, int run, QWidget* parent)
    : QDialog(parent)
    , m_ev(ev)
    , m_run(run)
{
    setWindowTitle(tr("Manage un-processed"));
    auto* lay = new QVBoxLayout(this);
    m_table = new QTableWidget(this);
    m_table->setColumnCount(6);
    m_table->setHorizontalHeaderLabels({ tr("Bib."), tr("Competitor"), tr("Did Not Start"), tr("Not Permitted"), tr("Did Not Finish"),
                                         tr("Disqualified") });
    UiHelpers::styleGrid(m_table);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    for (int c = 2; c <= 5; ++c)
        m_table->setColumnWidth(c, 100);
    m_table->setColumnWidth(0, 60);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setToolTip(tr("Click in a column to give the status, click again to remove it.\n"
                           "Disqualified: the gate is asked."));

    TimingEngine engine(ev);
    engine.setRun(run);
    const QVector<int> onCourse = engine.onCourse();
    const RunData& data = ev->run(run);
    QVector<int> bibs;
    for (int bib : Ranking::participants(data)) {
        if (ev->isForerunner(bib))
            continue;
        const TimeInfo info = data.info(bib);
        if (!info.isClassified())
            bibs << bib;
    }
    std::sort(bibs.begin(), bibs.end()); // by bib, like SkiPro
    const QColor yellow(0xff, 0xfb, 0xe0);
    m_table->setRowCount(int(bibs.size()));
    for (int r = 0; r < bibs.size(); ++r) {
        const int bib = bibs.at(r);
        const TimeInfo info = data.info(bib);
        const ResultStatus st = columnOf(info.status) > 0 ? info.status : ResultStatus::NotProcessed;
        auto* b = new QTableWidgetItem(QString::number(bib));
        b->setTextAlignment(Qt::AlignCenter);
        b->setData(Qt::UserRole, bib);
        b->setData(kOrigStatusRole, int(st));
        b->setData(kOrigReasonRole, st == ResultStatus::DSQ ? info.reason : QString());
        auto* name = new QTableWidgetItem(ev->competitorName(bib));
        for (auto* it : { b, name })
            it->setBackground(yellow);
        m_table->setItem(r, 0, b);
        m_table->setItem(r, 1, name);
        for (int c = 2; c <= 5; ++c) {
            auto* it = new QTableWidgetItem;
            it->setTextAlignment(Qt::AlignCenter);
            m_table->setItem(r, c, it);
        }
        if (st != ResultStatus::NotProcessed) {
            setRowStatus(r, st, info.reason);
        } else {
            // without a result: proposal, started -> DNF, not started -> DNS
            setRowStatus(r, onCourse.contains(bib) ? ResultStatus::DNF : ResultStatus::DNS, QString());
            name->setToolTip(tr("No result yet: proposed status"));
        }
    }
    connect(m_table, &QTableWidget::cellClicked, this, &UnprocessedDialog::onCellClicked);
    lay->addWidget(m_table, 1);
    lay->addLayout(UiHelpers::okCancelRow(this));
    resize(780, 800);
}

void UnprocessedDialog::setRowStatus(int row, ResultStatus st, const QString& reason)
{
    QTableWidgetItem* b = m_table->item(row, 0);
    b->setData(kStatusRole, int(st));
    b->setData(kReasonRole, st == ResultStatus::DSQ ? reason : QString());
    for (int c = 2; c <= 5; ++c) {
        QTableWidgetItem* it = m_table->item(row, c);
        const bool on = c == columnOf(st);
        const bool gate = on && st == ResultStatus::DSQ && !reason.trimmed().isEmpty();
        it->setText(gate ? gateText(reason) : QString());
        it->setIcon(on && !gate ? UiHelpers::icon(QStringLiteral("ok")) : QIcon());
    }
}

void UnprocessedDialog::onCellClicked(int row, int column)
{
    if (column < 2 || column > 5)
        return;
    QTableWidgetItem* b = m_table->item(row, 0);
    const ResultStatus cur = ResultStatus(b->data(kStatusRole).toInt());
    const ResultStatus st = statusOf(column);
    if (st == ResultStatus::DSQ) {
        // the gate, also to change it on a competitor already disqualified
        bool ok = false;
        const QString gate = QInputDialog::getText(this, tr("Disqualified"),
                                                   tr("Gate of the disqualification of %1 :").arg(m_table->item(row, 1)->text()),
                                                   QLineEdit::Normal, b->data(kReasonRole).toString(), &ok);
        if (!ok)
            return;
        m_changed |= cur != st || gate.trimmed() != b->data(kReasonRole).toString();
        setRowStatus(row, st, gate.trimmed());
        return;
    }
    setRowStatus(row, cur == st ? ResultStatus::NotProcessed : st, QString());
    m_changed = true;
}

void UnprocessedDialog::reject()
{
    if (m_changed) {
        const auto answer = QMessageBox::question(this, windowTitle(), tr("The statuses were changed. Save the changes?"),
                                                  QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
                                                  QMessageBox::Save);
        if (answer == QMessageBox::Cancel)
            return;
        if (answer == QMessageBox::Save) {
            accept();
            return;
        }
    }
    QDialog::reject();
}

void UnprocessedDialog::accept()
{
    TimingEngine engine(m_ev);
    engine.setRun(m_run);
    m_ev->beginBatch();
    for (int r = 0; r < m_table->rowCount(); ++r) {
        const QTableWidgetItem* b = m_table->item(r, 0);
        const int bib = b->data(Qt::UserRole).toInt();
        const ResultStatus st = ResultStatus(b->data(kStatusRole).toInt());
        const QString reason = b->data(kReasonRole).toString();
        if (st == ResultStatus(b->data(kOrigStatusRole).toInt()) && reason == b->data(kOrigReasonRole).toString())
            continue;
        engine.setStatus(bib, st, reason); // NotProcessed: the status is removed
    }
    m_ev->commitBatch();
    QDialog::accept();
}
