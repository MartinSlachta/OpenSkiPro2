#include "EventTypeDialog.h"
#include "app/AppSettings.h"
#include "db/FisListDb.h"
#include "ui/UiHelpers.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QRadioButton>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QVBoxLayout>

static QGroupBox* precisionBox(const QString& title, QButtonGroup*& group, Precision current, QWidget* parent)
{
    auto* box = new QGroupBox(title, parent);
    auto* l = new QVBoxLayout(box);
    group = new QButtonGroup(box);
    const QStringList labels = { QStringLiteral("1"), QStringLiteral("1/10"), QStringLiteral("1/100"), QStringLiteral("1/1000") };
    for (int i = 0; i < labels.size(); ++i) {
        auto* r = new QRadioButton(labels.at(i), box);
        group->addButton(r, i);
        l->addWidget(r);
        if (i == int(current))
            r->setChecked(true);
    }
    return box;
}

EventTypeDialog::EventTypeDialog(const EventParams& p, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Event type"));
    FisListDb::loadHeader(AppSettings::fisListPath(), m_fisList, nullptr, nullptr);
    auto* lay = new QVBoxLayout(this);

    // regulation / sport / discipline
    auto* top = new QHBoxLayout;
    auto* reg = new QGroupBox(this);
    auto* rl = new QVBoxLayout(reg);
    m_regulation = new QButtonGroup(this);
    auto* fis = new QRadioButton(tr("FIS"), reg);
    auto* nat = new QRadioButton(tr("National"), reg);
    m_regulation->addButton(fis, int(Regulation::FIS));
    m_regulation->addButton(nat, int(Regulation::National));
    (p.type.regulation == Regulation::FIS ? fis : nat)->setChecked(true);
    rl->addWidget(fis);
    // race category of a FIS race (Catcode, level), min / max penalty from the current FIS list
    m_fisCategory = new QComboBox(reg);
    m_fisCategory->setMaxVisibleItems(40);
    // grouped by race level 0..4, each group under a (not selectable) title
    auto* catModel = qobject_cast<QStandardItemModel*>(m_fisCategory->model());
    for (int level = 0; level <= 4; ++level) {
        bool first = true;
        for (const auto& c : fisRaceCategories()) {
            if (c.level != level)
                continue;
            if (first && catModel) {
                auto* title = new QStandardItem(tr("Level %1").arg(level));
                title->setFlags(Qt::NoItemFlags);
                QFont f = title->font();
                f.setBold(true);
                title->setFont(f);
                title->setBackground(QColor(0xdc, 0xe6, 0xf2));
                title->setForeground(QColor(0x1f, 0x3a, 0x5f));
                catModel->appendRow(title);
                first = false;
            }
            m_fisCategory->addItem(QStringLiteral("%1 - %2").arg(QLatin1String(c.code),
                                                                 QCoreApplication::translate("FisList", c.description)),
                                   QLatin1String(c.code));
        }
    }
    const int catIdx = m_fisCategory->findData(p.type.fisCategory);
    if (catIdx < 0 && !p.type.fisCategory.isEmpty())
        m_fisCategory->addItem(p.type.fisCategory, p.type.fisCategory);
    m_fisCategory->setCurrentIndex(qMax(0, m_fisCategory->findData(p.type.fisCategory)));
    m_fisCategory->setToolTip(tr("Category of the FIS race"));
    auto* catRow = new QHBoxLayout;
    catRow->setContentsMargins(20, 0, 0, 0);
    catRow->addWidget(m_fisCategory);
    rl->addLayout(catRow);
    m_penalty = new QLabel(reg);
    m_penalty->setContentsMargins(20, 0, 0, 0);
    rl->addWidget(m_penalty);
    rl->addWidget(nat);
    rl->addStretch();
    top->addWidget(reg);
    connect(m_fisCategory, &QComboBox::currentIndexChanged, this, &EventTypeDialog::updateFisCategory);
    connect(m_regulation, &QButtonGroup::idClicked, this, &EventTypeDialog::updateFisCategory);

    auto* sport = new QGroupBox(this);
    auto* sl = new QVBoxLayout(sport);
    auto* alpin = new QRadioButton(tr("Alpin"), sport);
    alpin->setChecked(true);
    sl->addWidget(alpin);
    for (const QString& s : { tr("Nordic"), tr("Freestyle"), tr("Snowboard") }) {
        auto* r = new QRadioButton(s, sport);
        r->setEnabled(false);
        r->setToolTip(tr("Not available yet"));
        sl->addWidget(r);
    }
    top->addWidget(sport);

    auto* disc = new QGroupBox(this);
    auto* dl = new QGridLayout(disc);
    m_discipline = new QButtonGroup(this);
    const QStringList names = { tr("Downhill"), tr("Super-G"), tr("Giant"), tr("Slalom"), tr("Alpine Combined") };
    for (int i = 0; i < names.size(); ++i) {
        auto* r = new QRadioButton(names.at(i), disc);
        m_discipline->addButton(r, i);
        dl->addWidget(r, i, 0);
        if (i == int(p.type.discipline))
            r->setChecked(true);
    }
    int row = 0;
    for (const QString& s : { tr("Telemark"), tr("Speed"), tr("Parallel (Qualifications)"), tr("Parallel") }) {
        auto* r = new QRadioButton(s, disc);
        r->setEnabled(false);
        r->setToolTip(tr("Not available yet"));
        dl->addWidget(r, row++, 1);
    }
    top->addWidget(disc, 1);
    lay->addLayout(top);

    auto* runs = new QGroupBox(this);
    auto* rf = new QFormLayout(runs);
    m_runs = new QSpinBox(runs);
    m_runs->setRange(1, kMaxRuns);
    m_runs->setValue(p.type.runCount);
    m_inters = new QSpinBox(runs);
    m_inters->setRange(0, kMaxInter);
    m_inters->setValue(p.type.interCount);
    rf->addRow(tr("Run number :"), m_runs);
    rf->addRow(tr("Number of Intermediate times :"), m_inters);
    lay->addWidget(runs);

    auto* prec = new QHBoxLayout;
    prec->addWidget(precisionBox(tr("Timing Precision"), m_timePrecision, p.type.timePrecision, this));
    auto* speed = new QGroupBox(tr("Speed Precision"), this);
    auto* spl = new QGridLayout(speed);
    spl->addWidget(precisionBox(tr("Instantaneous"), m_speedPrecision, p.type.speedPrecision, speed), 0, 0);
    spl->addWidget(precisionBox(tr("Average"), m_avgPrecision, p.type.avgSpeedPrecision, speed), 0, 1);
    m_length = new QDoubleSpinBox(speed);
    m_length->setDecimals(3);
    m_length->setRange(0, 999);
    m_length->setValue(p.type.courseLength);
    auto* lenRow = new QHBoxLayout;
    lenRow->addStretch();
    lenRow->addWidget(new QLabel(tr("Length of course (km or Miles) :"), speed));
    lenRow->addWidget(m_length);
    spl->addLayout(lenRow, 1, 0, 1, 2);
    prec->addWidget(speed, 1);
    lay->addLayout(prec);

    auto* fin = new QGroupBox(tr("Final ranking"), this);
    auto* fl = new QVBoxLayout(fin);
    auto* typeRow = new QHBoxLayout;
    m_finalType = new QComboBox(fin);
    m_finalType->addItem(tr("Total of Runs"), int(FinalRankingType::TotalOfRuns));
    m_finalType->addItem(tr("Best of Runs"), int(FinalRankingType::BestOfRuns));
    m_finalType->setCurrentIndex(m_finalType->findData(int(p.type.finalType)));
    typeRow->addWidget(new QLabel(tr("Type :"), fin));
    typeRow->addWidget(m_finalType, 1);
    fl->addLayout(typeRow);
    // like SkiAlp: taken into consideration + name of each run (empty = "Run n")
    auto* used = new QGroupBox(tr("Runs taken into consideration and their names"), fin);
    auto* ul = new QGridLayout(used);
    for (int i = 0; i < kMaxRuns; ++i) {
        m_runUsed[size_t(i)] = new QCheckBox(used);
        m_runUsed[size_t(i)]->setChecked(p.type.runsUsed[size_t(i)]);
        m_runUsed[size_t(i)]->setToolTip(tr("Run taken into consideration in the final ranking"));
        m_runNames[size_t(i)] = new QLineEdit(p.type.runNames[size_t(i)], used);
        m_runNames[size_t(i)]->setPlaceholderText(tr("Run %1").arg(i + 1));
        m_runNames[size_t(i)]->setToolTip(tr("Name of the run in the reports and the dialogs, empty = \"Run %1\"").arg(i + 1));
        ul->addWidget(m_runUsed[size_t(i)], i % 3, (i / 3) * 2);
        ul->addWidget(m_runNames[size_t(i)], i % 3, (i / 3) * 2 + 1);
        ul->setColumnStretch((i / 3) * 2 + 1, 1);
    }
    fl->addWidget(used);
    lay->addWidget(fin);
    lay->addLayout(UiHelpers::okCancelRow(this));

    connect(m_runs, &QSpinBox::valueChanged, this, &EventTypeDialog::updateRuns);
    updateRuns();
    updateFisCategory();
}

void EventTypeDialog::updateFisCategory()
{
    const bool isFis = m_regulation->checkedId() == int(Regulation::FIS);
    m_fisCategory->setEnabled(isFis);
    m_penalty->setEnabled(isFis);
    const FisList& list = m_fisList;
    const QString code = m_fisCategory->currentData().toString();
    const FisRaceCategory* rc = fisRaceCategory(code);
    const QString level = rc ? tr("Level %1").arg(rc->level) : tr("Level ?");
    const FisCategoryPenalty* pen = list.penalty(code);
    if (list.isEmpty())
        m_penalty->setText(tr("%1 - No FIS list imported").arg(level));
    else if (!pen)
        m_penalty->setText(tr("%1 - Not in the FIS list %2").arg(level, list.header.code()));
    else
        m_penalty->setText(tr("%1 - Penalty: min %2, max %3 (list %4)")
                               .arg(level)
                               .arg(pen->minPenalty, 0, 'f', 2)
                               .arg(pen->maxPenalty, 0, 'f', 2)
                               .arg(list.header.code()));
}

void EventTypeDialog::updateRuns()
{
    for (int i = 0; i < kMaxRuns; ++i) {
        m_runUsed[size_t(i)]->setEnabled(i < m_runs->value());
        m_runNames[size_t(i)]->setEnabled(i < m_runs->value());
    }
}

void EventTypeDialog::apply(EventParams& p) const
{
    p.type.regulation = Regulation(m_regulation->checkedId());
    p.type.fisCategory = m_fisCategory->currentData().toString();
    p.type.sport = Sport::Alpine;
    p.type.discipline = Discipline(qMax(0, m_discipline->checkedId()));
    p.type.runCount = m_runs->value();
    p.type.interCount = m_inters->value();
    p.type.timePrecision = Precision(qMax(0, m_timePrecision->checkedId()));
    p.type.speedPrecision = Precision(qMax(0, m_speedPrecision->checkedId()));
    p.type.avgSpeedPrecision = Precision(qMax(0, m_avgPrecision->checkedId()));
    p.type.courseLength = m_length->value();
    p.type.finalType = FinalRankingType(m_finalType->currentData().toInt());
    for (int i = 0; i < kMaxRuns; ++i) {
        p.type.runsUsed[size_t(i)] = m_runUsed[size_t(i)]->isChecked();
        const QString name = m_runNames[size_t(i)]->text().trimmed();
        p.type.runNames[size_t(i)] = name == tr("Run %1").arg(i + 1) ? QString() : name;
    }
}
