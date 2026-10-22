#include "TimingOptionsDialog.h"
#include "OutputGroupsDialog.h"
#include "core/Event.h"
#include "core/TimeUtil.h"
#include "ui/UiHelpers.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSignalBlocker>
#include <QPushButton>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

static QString msText(qint64 ms)
{
    return ms != 0 ? (ms < 0 ? QStringLiteral("-") : QString()) + TimeUtil::formatResult(qAbs(ms), Precision::Thousandth) : QString();
}

// seconds with thousandths, "0.000 s" (always with a decimal point)
static QDoubleSpinBox* secondsBox(qint64 ms, double maxSeconds, QWidget* parent)
{
    auto* b = new QDoubleSpinBox(parent);
    b->setLocale(QLocale::c());
    b->setDecimals(3);
    b->setRange(0.0, maxSeconds);
    b->setSingleStep(0.1);
    b->setSuffix(QStringLiteral(" s"));
    b->setValue(qMax<qint64>(0, ms) / 1000.0);
    b->setAccelerated(true);
    return b;
}

static qint64 secondsMs(const QDoubleSpinBox* b)
{
    return qRound64(b->value() * 1000.0);
}

static qint64 parseMs(const QString& s)
{
    QString t = s.trimmed();
    if (t.isEmpty())
        return 0;
    const bool neg = t.startsWith(QLatin1Char('-'));
    if (neg)
        t = t.mid(1);
    const qint64 v = TimeUtil::parseResult(t);
    return v < 0 ? 0 : (neg ? -v : v);
}

TimingOptionsDialog::TimingOptionsDialog(Event* ev, QWidget* parent)
    : QDialog(parent)
    , m_ev(ev)
{
    const TimingOptions& o = ev->params().timing;
    setWindowTitle(tr("Sotware Parameters"));
    auto* lay = new QVBoxLayout(this);
    auto* tabs = new QTabWidget(this);

    auto* opt = new QWidget(tabs);
    auto* ol = new QVBoxLayout(opt);
    auto* timing = new QGroupBox(tr("Timing Options"), opt);
    auto* tl = new QVBoxLayout(timing);
    m_follow = new QCheckBox(tr("Show windows \"numbers to follow\""), timing);
    m_mark = new QCheckBox(tr("Show Mark when a number must finish"), timing);
    m_names = new QCheckBox(tr("Show Competitor names in \"numbers to follow\" Windows"), timing);
    m_confirm = new QCheckBox(tr("Ask for Confirmation before inserting a Competitor in the Start List"), timing);
    m_invert = new QCheckBox(tr("Inverse timing grid records"), timing);
    m_askBib = new QCheckBox(tr("Ask for bib if it is unknown"), timing);
    m_seq = new QCheckBox(tr("Show the sequence number (Seq) in the timing records"), timing);
    m_seq->setChecked(o.showSeqColumn);
    m_follow->setChecked(o.showNumbersToFollow);
    m_mark->setChecked(o.showMarkMustFinish);
    m_names->setChecked(o.showNamesInFollow);
    m_confirm->setChecked(o.confirmInsertStartList);
    m_invert->setChecked(o.invertGrid);
    m_askBib->setChecked(o.askBibIfUnknown);
    for (auto* c : { m_follow, m_mark, m_names, m_confirm, m_invert, m_askBib, m_seq })
        tl->addWidget(c);
    ol->addWidget(timing);

    auto* fore = new QGroupBox(tr("Forerunner numbers"), opt);
    auto* fl = new QHBoxLayout(fore);
    m_foreFrom = new QSpinBox(fore);
    m_foreTo = new QSpinBox(fore);
    for (auto* s : { m_foreFrom, m_foreTo })
        s->setRange(0, 9999);
    m_foreFrom->setValue(o.forerunnerFrom);
    m_foreTo->setValue(o.forerunnerTo);
    fl->addWidget(new QLabel(tr("From :"), fore));
    fl->addWidget(m_foreFrom);
    fl->addWidget(new QLabel(tr("To :"), fore));
    fl->addWidget(m_foreTo);
    fl->addStretch();
    ol->addWidget(fore);

    auto* quick = new QGroupBox(tr("Quick penalties"), opt);
    auto* ql = new QGridLayout(quick);
    for (int i = 0; i < 4; ++i) {
        m_quick[size_t(i)] = new QLineEdit(msText(o.quickPenaltiesMs[size_t(i)]), quick);
        ql->addWidget(new QLabel(QStringLiteral("Ctrl+F%1 :").arg(5 + i), quick), i / 2, (i % 2) * 2, Qt::AlignRight);
        ql->addWidget(m_quick[size_t(i)], i / 2, (i % 2) * 2 + 1);
    }
    ol->addWidget(quick);
    ol->addStretch();
    tabs->addTab(opt, tr("Options"));

    auto* tim = new QWidget(tabs);
    auto* tml = new QVBoxLayout(tim);
    // SkiPro TIMING_DELAY_*: an impulse quicker than this after the previous one is ignored
    auto* delay = new QGroupBox(tr("Timing Impulse Delay: minimum interval between two impulses"), tim);
    delay->setToolTip(tr("An impulse received sooner than this after the previous one of the same point is ignored\n"
                         "(double impulse of a photocell). 0 = no check."));
    auto* dl = new QFormLayout(delay);
    m_delayStart = secondsBox(o.delayStartMs, 60.0, delay);
    m_delayFinish = secondsBox(o.delayFinishMs, 60.0, delay);
    m_delayInter = secondsBox(o.delayInterMs, 60.0, delay);
    dl->addRow(tr("Start :"), m_delayStart);
    dl->addRow(tr("Finish :"), m_delayFinish);
    dl->addRow(tr("Inter 1/2/.. :"), m_delayInter);
    tml->addWidget(delay);
    auto* minimum = new QGroupBox(tr("False Impulse if time received is below minimum time allowed..."), tim);
    auto* ml = new QFormLayout(minimum);
    for (int i = 0; i < kMaxInter; ++i) {
        m_minInter[size_t(i)] = secondsBox(o.minTimeInterMs[size_t(i)], 86399.999, minimum);
        ml->addRow(tr("Inter %1 :").arg(i + 1), m_minInter[size_t(i)]);
    }
    m_minFinish = secondsBox(o.minTimeFinishMs, 86399.999, minimum);
    ml->addRow(tr("Finish :"), m_minFinish);
    tml->addWidget(minimum);
    tml->addStretch();
    tabs->addTab(tim, tr("Timing"));

    // ranking of the timing window (SkiPro: the "Display Pro / Live pro" part is not supported)
    auto* rank = new QWidget(tabs);
    auto* rkl = new QVBoxLayout(rank);
    auto* rg = new QGroupBox(tr("Ranking"), rank);
    auto* rf = new QFormLayout(rg);
    m_rankType = new QComboBox(rg);
    m_rankType->addItem(tr("Event"), true);
    m_rankType->addItem(tr("Run"), false);
    m_rankType->setCurrentIndex(o.rankingEvent ? 0 : 1);
    m_rankType->setToolTip(tr("Event: ranking of the event after this run, Run: ranking of this run"));
    rf->addRow(tr("Display this ranking :"), m_rankType);
    m_rankField = new QComboBox(rg);
    m_rankField->addItem(tr("-"), 0);
    for (const auto& d : competitorFieldDefs()) {
        if (d.id == CompField::Name || d.id == CompField::LastName || d.id == CompField::FirstName)
            continue;
        const QString custom = ev->params().fields.labels.value(d.id);
        m_rankField->addItem(custom.isEmpty() ? QCoreApplication::translate("Competitor", d.label) : custom, d.id);
    }
    m_rankField->setCurrentIndex(qMax(0, m_rankField->findData(o.rankingField)));
    m_rankField->setToolTip(tr("Shown in brackets after the name of the competitor"));
    rf->addRow(tr("Display this field :"), m_rankField);
    // like the reports: by the output groups of the event, ranked inside each group
    auto* groupsRow = new QHBoxLayout;
    m_byGroups = new QCheckBox(rg);
    m_byGroups->setChecked(o.rankingByGroups);
    auto* groupsBtn = UiHelpers::button(QStringLiteral("filter"), tr("Output groups..."), rg);
    auto groupsText = [this] {
        m_byGroups->setText(tr("Ranking by output groups (%1)").arg(m_ev->params().outputGroups.size()));
    };
    groupsText();
    groupsRow->addWidget(m_byGroups);
    groupsRow->addStretch();
    groupsRow->addWidget(groupsBtn);
    rf->addRow(QString(), groupsRow);
    rkl->addWidget(rg);
    rkl->addStretch();
    tabs->addTab(rank, tr("Ranking"));
    connect(groupsBtn, &QPushButton::clicked, this, [this, groupsText] {
        OutputGroupsDialog dlg(m_ev, this);
        if (dlg.exec() == QDialog::Accepted) {
            groupsText();
            m_byGroups->setChecked(!m_ev->params().outputGroups.isEmpty());
        }
    });

    lay->addWidget(tabs, 1);
    lay->addLayout(UiHelpers::okCancelRow(this));
    resize(620, 560);
}

TimingOptions TimingOptionsDialog::options(const TimingOptions& base) const
{
    TimingOptions o = base;
    o.rankingEvent = m_rankType->currentData().toBool();
    o.rankingField = m_rankField->currentData().toInt();
    o.rankingByGroups = m_byGroups->isChecked();
    o.showNumbersToFollow = m_follow->isChecked();
    o.showMarkMustFinish = m_mark->isChecked();
    o.showNamesInFollow = m_names->isChecked();
    o.confirmInsertStartList = m_confirm->isChecked();
    o.invertGrid = m_invert->isChecked();
    o.askBibIfUnknown = m_askBib->isChecked();
    o.showSeqColumn = m_seq->isChecked();
    o.forerunnerFrom = m_foreFrom->value();
    o.forerunnerTo = m_foreTo->value();
    for (int i = 0; i < 4; ++i)
        o.quickPenaltiesMs[size_t(i)] = qMax<qint64>(0, parseMs(m_quick[size_t(i)]->text()));
    o.delayStartMs = secondsMs(m_delayStart);
    o.delayFinishMs = secondsMs(m_delayFinish);
    o.delayInterMs = secondsMs(m_delayInter);
    for (int i = 0; i < kMaxInter; ++i)
        o.minTimeInterMs[size_t(i)] = secondsMs(m_minInter[size_t(i)]);
    o.minTimeFinishMs = secondsMs(m_minFinish);
    return o;
}
