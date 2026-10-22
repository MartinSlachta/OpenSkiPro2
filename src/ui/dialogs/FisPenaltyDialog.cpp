#include "FisPenaltyDialog.h"
#include "app/AppSettings.h"
#include "core/Event.h"
#include "core/FisList.h"
#include "core/FisPenalty.h"
#include "core/TimeUtil.h"
#include "db/FisListDb.h"
#include "ui/FisUi.h"
#include "ui/UiHelpers.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

QDoubleSpinBox* pointsSpin(QWidget* parent, double max = 9999.99)
{
    auto* s = new QDoubleSpinBox(parent);
    s->setDecimals(2);
    s->setRange(0, max);
    s->setAlignment(Qt::AlignRight);
    return s;
}

QString pts(int hundredths)
{
    return TimeUtil::formatPoints(hundredths);
}

} // namespace

FisPenaltyDialog::FisPenaltyDialog(Event* ev, QWidget* parent)
    : QDialog(parent)
    , m_ev(ev)
{
    setWindowTitle(tr("FIS points / Penalty"));
    auto* lay = new QVBoxLayout(this);

    const auto& type = ev->params().type;
    const FisRaceCategory* rc = fisRaceCategory(type.fisCategory);
    auto* race = new QLabel(this);
    race->setText(type.regulation == Regulation::FIS
                      ? tr("%1, category %2 (level %3)")
                            .arg(disciplineName(type.discipline), type.fisCategory)
                            .arg(rc ? QString::number(rc->level) : QStringLiteral("?"))
                      : tr("%1, national race").arg(disciplineName(type.discipline)));
    lay->addWidget(race);

    auto* top = new QHBoxLayout;
    top->addWidget(new QLabel(tr("FIS list :"), this));
    m_list = new QLineEdit(ev->params().fisPoints.list, this);
    m_list->setMaximumWidth(80);
    top->addWidget(m_list);
    auto* bLoad = UiHelpers::button(QStringLiteral("import"), tr("Load from FIS list"), this);
    bLoad->setToolTip(tr("F value, maximum value, Z, adder of the race level and min / max penalty of the category "
                         "from the imported FIS list"));
    top->addWidget(bLoad);
    top->addStretch();
    lay->addLayout(top);
    m_info = new QLabel(this);
    m_info->setWordWrap(true);
    lay->addWidget(m_info);

    QVector<Gender> genders = FisPenalty::genders(*ev);
    if (genders.isEmpty())
        genders << Gender::Male << Gender::Female;
    auto* box = new QGroupBox(tr("Constants and calculation"), this);
    auto* grid = new QGridLayout(box);
    const QStringList labels = { tr("F value :"), tr("Maximum value :"), tr("Correction value (Z) :"),
                                 tr("Category adder :"), tr("Minimum penalty :"), tr("Maximum penalty :") };
    for (int i = 0; i < labels.size(); ++i)
        grid->addWidget(new QLabel(labels.at(i), box), i + 1, 0);
    grid->addWidget(new QLabel(tr("Calculation :"), box), 7, 0, Qt::AlignTop);
    grid->addWidget(new QLabel(tr("Applied penalty :"), box), 8, 0);
    for (int g = 0; g < genders.size(); ++g) {
        Column c;
        c.gender = genders.at(g);
        const int col = g + 1;
        auto* title = new QLabel(QStringLiteral("<b>%1</b>").arg(c.gender == Gender::Female ? tr("Women") : tr("Men")), box);
        title->setAlignment(Qt::AlignCenter);
        grid->addWidget(title, 0, col);
        c.f = pointsSpin(box);
        c.maxValue = pointsSpin(box);
        c.z = pointsSpin(box);
        c.z->setRange(-999.99, 999.99);
        c.adder = pointsSpin(box);
        c.minPenalty = pointsSpin(box);
        c.maxPenalty = pointsSpin(box);
        int row = 1;
        for (QDoubleSpinBox* s : { c.f, c.maxValue, c.z, c.adder, c.minPenalty, c.maxPenalty }) {
            grid->addWidget(s, row++, col);
            connect(s, &QDoubleSpinBox::valueChanged, this, &FisPenaltyDialog::recalculate);
        }
        c.result = new QLabel(box);
        c.result->setTextFormat(Qt::RichText);
        c.result->setObjectName(QStringLiteral("infoBox"));
        c.result->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        c.result->setMinimumWidth(260);
        grid->addWidget(c.result, 7, col);
        auto* ov = new QHBoxLayout;
        c.override = new QCheckBox(tr("typed by the TD"), box);
        c.applied = pointsSpin(box);
        ov->addWidget(c.override);
        ov->addWidget(c.applied, 1);
        grid->addLayout(ov, 8, col);
        connect(c.override, &QCheckBox::toggled, this, &FisPenaltyDialog::recalculate);
        connect(c.applied, &QDoubleSpinBox::valueChanged, this, &FisPenaltyDialog::recalculate);
        m_columns << c;
    }
    lay->addWidget(box);

    auto* bottom = UiHelpers::okCancelRow(this);
    auto* bReport = UiHelpers::button(QStringLiteral("printer"), tr("Penalty calculation"), this);
    bottom->insertWidget(0, bReport);
    lay->addLayout(bottom);

    connect(bLoad, &QPushButton::clicked, this, [this] { loadFromList(false); });
    bReport->setToolTip(tr("Saves the constants and opens the penalty calculation sheet"));
    connect(bReport, &QPushButton::clicked, this, [this] {
        m_reportWanted = true;
        accept();
    });

    m_loading = true;
    for (Column& c : m_columns)
        setParams(c, ev->params().fisPoints.forGender(c.gender));
    m_loading = false;
    // first opening: the constants of the current list
    bool anySet = false;
    for (const Column& c : m_columns)
        anySet = anySet || ev->params().fisPoints.forGender(c.gender).isSet();
    if (!anySet)
        loadFromList(true);
    recalculate();
}

FisPointsParams FisPenaltyDialog::params(const Column& c) const
{
    FisPointsParams p;
    p.fValue = c.f->value();
    p.maxValue = qRound(c.maxValue->value() * 100);
    p.zValue = qRound(c.z->value() * 100);
    p.adder = qRound(c.adder->value() * 100);
    p.minPenalty = qRound(c.minPenalty->value() * 100);
    p.maxPenalty = qRound(c.maxPenalty->value() * 100);
    p.appliedOverride = c.override->isChecked() ? qRound(c.applied->value() * 100) : -1;
    return p;
}

void FisPenaltyDialog::setParams(Column& c, const FisPointsParams& p)
{
    c.f->setValue(p.fValue);
    c.maxValue->setValue(p.maxValue / 100.0);
    c.z->setValue(p.zValue / 100.0);
    c.adder->setValue(p.adder / 100.0);
    c.minPenalty->setValue(p.minPenalty / 100.0);
    c.maxPenalty->setValue(p.maxPenalty / 100.0);
    c.override->setChecked(p.appliedOverride >= 0);
    if (p.appliedOverride >= 0)
        c.applied->setValue(p.appliedOverride / 100.0);
}

bool FisPenaltyDialog::loadFromList(bool quiet)
{
    FisList list;
    QString err;
    if (!FisListDb::loadHeader(AppSettings::fisListPath(), list, nullptr, &err) || list.isEmpty()) {
        if (!quiet)
            QMessageBox::information(this, windowTitle(),
                                     err.isEmpty() ? tr("No FIS list has been imported yet. Import it first (FIS / Import FIS list).")
                                                   : err);
        return false;
    }
    const auto& type = m_ev->params().type;
    QStringList errors;
    m_loading = true;
    for (Column& c : m_columns) {
        FisPointsParams p = params(c);
        QString e;
        if (FisPenalty::loadFromList(list, type.discipline, type.fisCategory, c.gender, p, &e))
            setParams(c, p);
        else
            errors << e;
    }
    m_loading = false;
    m_list->setText(list.header.code());
    QString info = tr("Loaded from %1.").arg(FisUi::listTitle(list.header));
    const QDate day = m_ev->params().general.date;
    if (day.isValid() && ((list.header.validFrom.isValid() && day < list.header.validFrom)
                          || (list.header.validTo.isValid() && day > list.header.validTo)))
        info += QStringLiteral(" <span style='color:#c00000'>%1</span>")
                    .arg(tr("The date of the race is not in the validity of this list (%1).")
                             .arg(FisUi::validityText(list.header))
                             .toHtmlEscaped());
    if (!errors.isEmpty())
        info += QStringLiteral(" <span style='color:#c00000'>%1</span>").arg(errors.join(QLatin1Char(' ')).toHtmlEscaped());
    m_info->setText(info);
    recalculate();
    return errors.isEmpty();
}

void FisPenaltyDialog::recalculate()
{
    if (m_loading)
        return;
    for (Column& c : m_columns) {
        c.applied->setEnabled(c.override->isChecked());
        const FisPenalty::Result r = FisPenalty::compute(*m_ev, c.gender, params(c));
        if (!r.valid) {
            c.result->setText(r.error.toHtmlEscaped());
            if (!c.override->isChecked())
                c.applied->setValue(0);
            continue;
        }
        QStringList lines;
        lines << tr("(A) best 5 of the top 10 : %1").arg(pts(r.sumA));
        lines << tr("(B) best 5 at start : %1").arg(pts(r.sumB));
        lines << tr("(C) race points of A : %1").arg(pts(r.sumC));
        lines << tr("Calculated penalty : %1").arg(pts(r.calculated));
        lines << tr("+ Z %1 + adder %2").arg(pts(r.zValue), pts(r.adder));
        QString minmax = tr("min %1 / max %2").arg(pts(r.minPenalty), pts(r.maxPenalty));
        if (r.doubledMinimum)
            minmax += QStringLiteral(" (%1)").arg(tr("2 x maximum value"));
        lines << minmax;
        lines << QStringLiteral("<b>%1</b>").arg(tr("Applied penalty : %1").arg(pts(r.computedApplied)).toHtmlEscaped());
        for (int i = 0; i < lines.size() - 1; ++i)
            lines[i] = lines.at(i).toHtmlEscaped();
        c.result->setText(lines.join(QStringLiteral("<br>")));
        if (!c.override->isChecked()) {
            const QSignalBlocker b(c.applied);
            c.applied->setValue(r.computedApplied / 100.0);
        }
    }
}

void FisPenaltyDialog::accept()
{
    EventParams p = m_ev->params();
    p.fisPoints.list = m_list->text().trimmed();
    for (const Column& c : m_columns)
        p.fisPoints.forGender(c.gender) = params(c);
    if (p.header.value(QLatin1String(HeaderKeys::ListNumber)).isEmpty() && !p.fisPoints.list.isEmpty())
        p.header.setValue(QLatin1String(HeaderKeys::ListNumber), p.fisPoints.list);
    m_ev->setParams(p);
    QDialog::accept();
}
