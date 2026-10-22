#include "ModifyDataDialog.h"
#include "app/AppSettings.h"
#include "core/Event.h"
#include "timing/TimingEngine.h"
#include "ui/UiHelpers.h"

#include <QComboBox>
#include <QDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

ModifyDataDialog::ModifyDataDialog(Event* ev, int run, int bib, QWidget* parent)
    : QDialog(parent)
    , m_ev(ev)
    , m_run(run)
    , m_engine(new TimingEngine(ev, this))
{
    m_engine->setRun(run);
    for (int s = 1; s <= 2; ++s)
        m_engine->setDeviceConfig(s, AppSettings::timingDevice(s));
    setWindowTitle(tr("Modify Data - %1").arg(ev->params().type.runName(run)));
    auto* lay = new QVBoxLayout(this);

    auto* comp = new QGroupBox(tr("Competitor"), this);
    auto* cl = new QVBoxLayout(comp);
    auto* row = new QHBoxLayout;
    m_bib = new QSpinBox(comp);
    m_bib->setRange(0, 9999);
    auto* prev = new QToolButton(comp);
    prev->setArrowType(Qt::LeftArrow);
    auto* next = new QToolButton(comp);
    next->setArrowType(Qt::RightArrow);
    prev->setToolTip(tr("Previous competitor in the start order of the run"));
    next->setToolTip(tr("Next competitor in the start order of the run"));
    m_bib->setToolTip(tr("Bib number (the arrows change it by one)"));
    row->addWidget(new QLabel(tr("Bib. :"), comp));
    row->addWidget(m_bib);
    row->addWidget(prev);
    row->addWidget(next);
    row->addStretch();
    cl->addLayout(row);
    m_name = UiHelpers::infoBox(comp);
    cl->addWidget(m_name);
    lay->addWidget(comp);

    auto* inq = new QGroupBox(tr("Inquiries"), this);
    auto* form = new QFormLayout(inq);
    auto* timeRow = new QHBoxLayout;
    m_time = new QLineEdit(inq);
    auto* calc = new QToolButton(inq);
    calc->setIcon(UiHelpers::icon(QStringLiteral("calculator")));
    calc->setIconSize(QSize(24, 24));
    calc->setToolTip(tr("Compute the time from a start and a finish hour"));
    timeRow->addWidget(m_time);
    timeRow->addWidget(calc);
    form->addRow(tr("Time :"), timeRow);
    m_penalty = new QLineEdit(inq);
    form->addRow(tr("Penalty :"), m_penalty);
    m_status = new QComboBox(inq);
    for (ResultStatus s : { ResultStatus::Classified, ResultStatus::DNS, ResultStatus::NPS, ResultStatus::DNF, ResultStatus::DSQ,
                            ResultStatus::NotProcessed })
        m_status->addItem(UiHelpers::statusText(s), int(s));
    form->addRow(tr("Status :"), m_status);
    m_reason = new QLineEdit(inq);
    form->addRow(tr("Gate :"), m_reason);
    for (int i = 0; i < kMaxInter; ++i) {
        m_inter[size_t(i)] = new QLineEdit(inq);
        m_inter[size_t(i)]->setEnabled(i < ev->interCount());
        form->addRow(tr("Inter %1 :").arg(i + 1), m_inter[size_t(i)]);
    }
    m_source = new QLabel(inq);
    m_source->setWordWrap(true);
    form->addRow(m_source);
    lay->addWidget(inq);

    auto* buttons = new QHBoxLayout;
    auto* reset = new QPushButton(tr("Back to the times records"), this);
    reset->setToolTip(tr("Forget the manual data and compute the result from the times records again"));
    auto* cancel = UiHelpers::button(QStringLiteral("cancel"), tr("Cancel"), this);
    auto* close = UiHelpers::button(QStringLiteral("close"), tr("Close"), this);
    buttons->addWidget(reset);
    buttons->addStretch();
    buttons->addWidget(cancel);
    buttons->addWidget(close);
    lay->addLayout(buttons);

    connect(prev, &QToolButton::clicked, this, [this] { step(-1); });
    connect(next, &QToolButton::clicked, this, [this] { step(1); });
    // every change of the number: the data of the previous competitor saved, the new one loaded
    connect(m_bib, &QSpinBox::valueChanged, this, [this](int bib) {
        if (bib == m_loaded.bib)
            return;
        if (save()) {
            load(bib);
            return;
        }
        m_bib->blockSignals(true); // invalid data: stay on the competitor
        m_bib->setValue(m_loaded.bib);
        m_bib->blockSignals(false);
    });
    connect(calc, &QToolButton::clicked, this, &ModifyDataDialog::calculator);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(close, &QPushButton::clicked, this, [this] {
        if (save())
            accept();
    });
    connect(reset, &QPushButton::clicked, this, [this] {
        m_engine->clearManual(m_loaded.bib);
        load(m_loaded.bib);
    });

    int first = bib;
    if (first <= 0 && !ev->run(run).startList.isEmpty())
        first = ev->run(run).startList.first().bib;
    load(first);
    resize(460, 0);
}

void ModifyDataDialog::load(int bib)
{
    const Precision prec = Precision::Thousandth;
    m_loaded = m_ev->run(m_run).info(bib);
    m_loaded.bib = bib;
    m_bib->blockSignals(true);
    m_bib->setValue(bib);
    m_bib->blockSignals(false);
    m_name->setText(QStringLiteral("%1 - %2").arg(bib).arg(m_ev->competitorName(bib)));
    m_time->setText(m_loaded.timeMs >= 0 ? TimeUtil::formatResult(m_loaded.timeMs, prec) : QString());
    m_penalty->setText(m_loaded.penaltyMs > 0 ? TimeUtil::formatResult(m_loaded.penaltyMs, prec) : QString());
    m_status->setCurrentIndex(qMax(0, m_status->findData(int(m_loaded.status))));
    m_reason->setText(m_loaded.reason);
    for (int i = 0; i < kMaxInter; ++i)
        m_inter[size_t(i)]->setText(m_loaded.interMs[size_t(i)] >= 0 ? TimeUtil::formatResult(m_loaded.interMs[size_t(i)], prec) : QString());
    m_source->setText(m_loaded.manual ? tr("<i>Result entered manually.</i>") : tr("<i>Result computed from the times records.</i>"));
}

bool ModifyDataDialog::save()
{
    const int bib = m_loaded.bib;
    if (bib <= 0)
        return true;
    TimeInfo t = m_loaded;
    const QString timeText = m_time->text().trimmed();
    t.timeMs = timeText.isEmpty() ? TimeUtil::kNoTime : TimeUtil::parseResult(timeText);
    if (!timeText.isEmpty() && t.timeMs < 0) {
        QMessageBox::warning(this, windowTitle(), tr("Invalid time."));
        return false;
    }
    const QString penText = m_penalty->text().trimmed();
    t.penaltyMs = penText.isEmpty() ? 0 : TimeUtil::parseResult(penText);
    if (t.penaltyMs < 0) {
        QMessageBox::warning(this, windowTitle(), tr("Invalid penalty."));
        return false;
    }
    t.status = ResultStatus(m_status->currentData().toInt());
    t.reason = m_reason->text().trimmed();
    for (int i = 0; i < kMaxInter; ++i) {
        const QString v = m_inter[size_t(i)]->text().trimmed();
        t.interMs[size_t(i)] = v.isEmpty() ? TimeUtil::kNoTime : TimeUtil::parseResult(v);
    }
    // time shown includes the penalty: when the penalty changes, the time follows
    const bool timeEdited = t.timeMs != m_loaded.timeMs;
    const bool onlyPenaltyOrReason = !timeEdited && t.status == m_loaded.status && t.interMs == m_loaded.interMs;
    if (t.penaltyMs == m_loaded.penaltyMs && t.reason == m_loaded.reason && !timeEdited && t.status == m_loaded.status
        && t.interMs == m_loaded.interMs)
        return true; // nothing changed
    if (onlyPenaltyOrReason && !m_loaded.manual) {
        if (t.penaltyMs != m_loaded.penaltyMs)
            m_engine->setPenalty(bib, t.penaltyMs);
        if (t.reason != m_loaded.reason) {
            TimeInfo cur = m_ev->run(m_run).info(bib);
            cur.bib = bib;
            cur.reason = t.reason;
            m_ev->saveInfo(m_run, cur);
        }
        return true;
    }
    if (!onlyPenaltyOrReason && !m_loaded.manual && t.status != m_loaded.status && !timeEdited
        && (t.status == ResultStatus::DNS || t.status == ResultStatus::NPS || t.status == ResultStatus::DNF
            || t.status == ResultStatus::DSQ
            || (t.status == ResultStatus::Classified && m_loaded.status != ResultStatus::NotProcessed))) {
        // status change only: keep the result linked to the times records
        m_engine->setStatus(bib, t.status, t.reason);
        return true;
    }
    if (t.status == ResultStatus::Classified && t.timeMs < 0) {
        QMessageBox::warning(this, windowTitle(), tr("Enter a time or choose another status."));
        return false;
    }
    if (timeEdited && t.timeMs >= 0)
        t.status = t.status == ResultStatus::NotProcessed ? ResultStatus::Classified : t.status;
    m_engine->setManualInfo(t);
    return true;
}

void ModifyDataDialog::step(int delta)
{
    if (!save())
        return;
    const RunData& run = m_ev->run(m_run);
    int pos = run.startPosition(m_loaded.bib);
    if (pos < 0)
        pos = delta > 0 ? -1 : int(run.startList.size());
    const int np = pos + delta;
    if (np < 0 || np >= run.startList.size())
        return;
    load(run.startList.at(np).bib);
}

void ModifyDataDialog::calculator()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Calculator"));
    auto* form = new QFormLayout;
    auto* start = new QLineEdit(&dlg);
    auto* finish = new QLineEdit(&dlg);
    start->setPlaceholderText(QStringLiteral("hh:mm:ss.zzz"));
    finish->setPlaceholderText(QStringLiteral("hh:mm:ss.zzz"));
    if (const TimeRecord* s = m_engine->activeRecord(m_loaded.bib, Point::Start))
        start->setText(TimeUtil::formatClock(s->dayTimeUs, 4));
    if (const TimeRecord* f = m_engine->activeRecord(m_loaded.bib, Point::Finish))
        finish->setText(TimeUtil::formatClock(f->dayTimeUs, 4));
    form->addRow(tr("Start hour :"), start);
    form->addRow(tr("Finish hour :"), finish);
    auto* lay = new QVBoxLayout(&dlg);
    lay->addLayout(form);
    lay->addLayout(UiHelpers::okCancelRow(&dlg));
    if (dlg.exec() != QDialog::Accepted)
        return;
    const qint64 s = TimeUtil::parseDayTime(start->text());
    qint64 f = TimeUtil::parseDayTime(finish->text());
    if (s < 0 || f < 0) {
        QMessageBox::warning(this, windowTitle(), tr("Invalid hour."));
        return;
    }
    if (f < s)
        f += TimeUtil::kUsPerDay;
    m_time->setText(TimeUtil::formatResult(TimeUtil::netTimeMs(s, f), Precision::Thousandth));
}
