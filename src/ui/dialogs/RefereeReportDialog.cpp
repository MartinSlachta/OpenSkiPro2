#include "RefereeReportDialog.h"
#include "PreviewDialog.h"
#include "app/AppSettings.h"
#include "core/Event.h"
#include "report/RefereeReport.h"
#include "report/ReportRenderer.h"
#include "ui/UiHelpers.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPrintDialog>
#include <QPrinter>
#include <QPushButton>
#include <QRadioButton>
#include <QTimeEdit>
#include <QVBoxLayout>

RefereeReportDialog::RefereeReportDialog(Event* ev, int run, QWidget* parent)
    : QDialog(parent)
    , m_ev(ev)
{
    setWindowTitle(tr("Referee Report"));
    auto* lay = new QHBoxLayout(this);
    auto* left = new QVBoxLayout;

    auto* form = new QFormLayout;
    if (ev->runCount() > 1) {
        m_run = new QComboBox(this);
        for (int r = 1; r <= ev->runCount(); ++r)
            m_run->addItem(ev->params().type.runName(r), r);
        m_run->setCurrentIndex(qBound(0, run - 1, m_run->count() - 1));
        form->addRow(tr("Run :"), m_run);
    }
    left->addLayout(form);

    auto* order = new QGroupBox(tr("Order"), this);
    auto* ol = new QVBoxLayout(order);
    m_order = new QButtonGroup(this);
    auto* byBib = new QRadioButton(tr("Bib"), order);
    auto* byStart = new QRadioButton(tr("Start List"), order);
    m_order->addButton(byBib, int(RefereeReport::Order::Bib));
    m_order->addButton(byStart, int(RefereeReport::Order::StartList));
    (AppSettings::value(QStringLiteral("refereeReport/order"), 0).toInt() == 1 ? byStart : byBib)->setChecked(true);
    ol->addWidget(byBib);
    ol->addWidget(byStart);
    left->addWidget(order);

    // the protests against a disqualification: within 15 minutes after the posting
    auto* times = new QGroupBox(this);
    auto* tl = new QFormLayout(times);
    m_published = new QTimeEdit(QTime::currentTime(), times);
    m_published->setDisplayFormat(QStringLiteral("HH:mm"));
    m_deadline = new QTimeEdit(RefereeReport::deadline(m_published->time()), times);
    m_deadline->setDisplayFormat(QStringLiteral("HH:mm"));
    m_deadline->setToolTip(tr("Protests against a disqualification: 15 minutes after the posting"));
    tl->addRow(tr("Time published :"), m_published);
    tl->addRow(tr("Deadline :"), m_deadline);
    // the referee may write the times on the printed form by hand
    m_noTimes = new QCheckBox(tr("Do not print the times"), times);
    m_noTimes->setChecked(AppSettings::value(QStringLiteral("refereeReport/noTimes"), false).toBool());
    tl->addRow(m_noTimes);
    auto timesEnabled = [this] {
        m_published->setEnabled(!m_noTimes->isChecked());
        m_deadline->setEnabled(!m_noTimes->isChecked());
    };
    timesEnabled();
    connect(m_noTimes, &QCheckBox::toggled, this, [timesEnabled](bool on) {
        AppSettings::setValue(QStringLiteral("refereeReport/noTimes"), on);
        timesEnabled();
    });
    left->addWidget(times);
    m_info = new QLabel(this);
    m_info->setWordWrap(true);
    left->addWidget(m_info);
    left->addStretch();
    lay->addLayout(left, 1);

    auto* buttons = new QVBoxLayout;
    auto* bClose = UiHelpers::button(QStringLiteral("close"), tr("Close"), this);
    auto* bPreview = UiHelpers::button(QStringLiteral("preview"), tr("Preview"), this);
    auto* bPrint = UiHelpers::button(QStringLiteral("print"), tr("Print"), this);
    auto* bPdf = UiHelpers::button(QStringLiteral("pdf"), tr("PDF"), this);
    for (auto* b : { bClose, bPreview, bPrint, bPdf }) {
        b->setMinimumWidth(130);
        buttons->addWidget(b);
    }
    buttons->addStretch();
    lay->addLayout(buttons);

    connect(bClose, &QPushButton::clicked, this, &QDialog::accept);
    connect(bPreview, &QPushButton::clicked, this, &RefereeReportDialog::preview);
    connect(bPrint, &QPushButton::clicked, this, &RefereeReportDialog::print);
    connect(bPdf, &QPushButton::clicked, this, &RefereeReportDialog::exportPdf);
    connect(m_published, &QTimeEdit::timeChanged, this,
            [this](const QTime& t) { m_deadline->setTime(RefereeReport::deadline(t)); });
    connect(m_order, &QButtonGroup::idClicked, this,
            [](int id) { AppSettings::setValue(QStringLiteral("refereeReport/order"), id); });
    if (m_run)
        connect(m_run, &QComboBox::currentIndexChanged, this, &RefereeReportDialog::updateInfo);
    updateInfo();
}

int RefereeReportDialog::run() const
{
    return m_run ? m_run->currentData().toInt() : 1;
}

ReportDocument RefereeReportDialog::document() const
{
    ReportDocument doc;
    doc.run = run();
    doc.docTitle = tr("Report by the Referee - %1").arg(m_ev->params().type.runName(run()));
    const bool times = !m_noTimes->isChecked();
    doc.referee = std::make_shared<RefereeReportData>(RefereeReport::build(*m_ev, run(), RefereeReport::Order(m_order->checkedId()),
                                                                           times ? m_published->time() : QTime(),
                                                                           times ? m_deadline->time() : QTime()));
    return doc;
}

void RefereeReportDialog::updateInfo()
{
    const RefereeReportData d =
        RefereeReport::build(*m_ev, run(), RefereeReport::Order::Bib, m_published->time(), m_deadline->time());
    m_info->setText(tr("DSQ: %1   DNS: %2   NPS: %3   DNF: %4")
                        .arg(d.disqualified.size())
                        .arg(d.didNotStart.size())
                        .arg(d.notPermitted.size())
                        .arg(d.didNotFinish.size()));
}

void RefereeReportDialog::preview()
{
    PreviewDialog dlg(m_ev, document(), this);
    dlg.exec();
}

void RefereeReportDialog::print()
{
    const ReportDocument doc = document();
    ReportRenderer renderer(*m_ev, doc);
    QPrinter printer(QPrinter::HighResolution);
    printer.setFullPage(true);
    printer.setDocName(doc.docTitle);
    QPrintDialog dlg(&printer, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    if (!renderer.print(&printer))
        QMessageBox::warning(this, windowTitle(), tr("Printing failed."));
}

void RefereeReportDialog::exportPdf()
{
    const ReportDocument doc = document();
    QDir().mkpath(AppSettings::exportDir());
    const QString name = tr("Referee report run %1").arg(run()).replace(QLatin1Char(' '), QLatin1Char('_'));
    const QString file = QFileDialog::getSaveFileName(this, tr("PDF"), QDir(AppSettings::exportDir()).absoluteFilePath(name + QStringLiteral(".pdf")),
                                                      tr("PDF (*.pdf)"));
    if (file.isEmpty())
        return;
    ReportRenderer renderer(*m_ev, doc);
    QString err;
    if (!renderer.exportPdf(file, &err))
        QMessageBox::warning(this, windowTitle(), err);
}
