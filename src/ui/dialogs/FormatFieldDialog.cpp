#include "FormatFieldDialog.h"
#include "core/Event.h"
#include "core/FieldFormat.h"
#include "ui/UiHelpers.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QCoreApplication>
#include <QLabel>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

FormatFieldDialog::FormatFieldDialog(Event* ev, QWidget* parent)
    : QDialog(parent)
    , m_ev(ev)
{
    setWindowTitle(tr("Format a Field"));
    auto* lay = new QVBoxLayout(this);

    auto* sel = new QGroupBox(tr("Selection of Fields"), this);
    auto* sl = new QVBoxLayout(sel);
    m_field = new QComboBox(sel);
    for (const auto& d : competitorFieldDefs())
        if (d.id != CompField::Name && d.id != CompField::Bib && ev->params().fields.isActive(d.id))
            m_field->addItem(fieldName(d.id), d.id);
    m_field->setCurrentIndex(qMax(0, m_field->findData(CompField::LastName)));
    sl->addWidget(m_field);
    lay->addWidget(sel);

    auto okButton = [this](QWidget* parent) {
        auto* b = UiHelpers::button(QStringLiteral("ok"), tr("Ok"), parent);
        b->setMinimumWidth(90);
        return b;
    };

    // case of the text
    auto* fmt = new QGroupBox(tr("Format"), this);
    auto* fl = new QHBoxLayout(fmt);
    auto* radios = new QVBoxLayout;
    m_case = new QButtonGroup(this);
    const QStringList cases = { tr("All in Lowercase"), tr("First Letter as a Capital Letter"), tr("All Capitals") };
    for (int i = 0; i < cases.size(); ++i) {
        auto* r = new QRadioButton(cases.at(i), fmt);
        m_case->addButton(r, i);
        radios->addWidget(r);
    }
    m_case->button(int(FieldFormat::Case::Upper))->setChecked(true);
    auto* fmtOk = okButton(fmt);
    fl->addLayout(radios, 1);
    fl->addWidget(fmtOk);
    lay->addWidget(fmt);

    // replacement of a text
    auto* rep = new QGroupBox(tr("Replace"), this);
    auto* rl = new QHBoxLayout(rep);
    auto* rf = new QFormLayout;
    m_replace = new QLineEdit(rep);
    m_by = new QLineEdit(rep);
    rf->addRow(tr("Replace :"), m_replace);
    rf->addRow(tr("By :"), m_by);
    auto* repOk = okButton(rep);
    rl->addLayout(rf, 1);
    rl->addWidget(repOk);
    lay->addWidget(rep);

    // options: copy from another field
    auto* opt = new QGroupBox(tr("Options"), this);
    auto* ol = new QHBoxLayout(opt);
    auto* og = new QGridLayout;
    m_option = new QComboBox(opt);
    m_option->addItem(tr("Copy from another Field"));
    m_option->addItem(tr("Delete the values of the Field"));
    m_source = new QComboBox(opt);
    m_source->setToolTip(tr("Field whose values are copied to the selected field"));
    og->addWidget(m_option, 0, 0);
    og->addWidget(new QLabel(tr("From :"), opt), 1, 0, Qt::AlignLeft);
    og->addWidget(m_source, 2, 0);
    auto* optOk = okButton(opt);
    ol->addLayout(og, 1);
    ol->addWidget(optOk);
    lay->addWidget(opt);

    auto* bottom = new QHBoxLayout;
    bottom->addStretch();
    auto* close = UiHelpers::button(QStringLiteral("close"), tr("Close"), this);
    close->setMinimumWidth(120);
    bottom->addWidget(close);
    lay->addLayout(bottom);

    connect(fmtOk, &QPushButton::clicked, this, &FormatFieldDialog::formatCase);
    connect(repOk, &QPushButton::clicked, this, &FormatFieldDialog::replaceText);
    connect(optOk, &QPushButton::clicked, this, &FormatFieldDialog::runOption);
    connect(m_option, &QComboBox::currentIndexChanged, this, [this](int i) { m_source->setEnabled(i == 0); });
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_field, &QComboBox::currentIndexChanged, this, &FormatFieldDialog::fillSources);
    fillSources();
    resize(470, 0);
}

QString FormatFieldDialog::fieldName(int id) const
{
    const QString custom = m_ev->params().fields.labels.value(id);
    if (!custom.isEmpty())
        return custom;
    const CompetitorFieldDef* d = competitorFieldDef(id);
    return d ? QCoreApplication::translate("Competitor", d->label) : QString::number(id);
}

int FormatFieldDialog::field() const
{
    return m_field->currentData().toInt();
}

void FormatFieldDialog::fillSources()
{
    const int keep = m_source->currentData().toInt();
    m_source->clear();
    for (const auto& d : competitorFieldDefs())
        if (d.id != field() && d.id != CompField::Bib && m_ev->params().fields.isActive(d.id))
            m_source->addItem(fieldName(d.id), d.id);
    m_source->setCurrentIndex(qMax(0, m_source->findData(keep)));
}

int FormatFieldDialog::apply(const std::function<QString(const QString&)>& change, int sourceField)
{
    const int target = field();
    int n = 0;
    m_ev->beginBatch();
    const QVector<Competitor> list = m_ev->competitors(); // a copy: updated while iterating
    for (Competitor c : list) {
        const QString before = c.value(target);
        const QString after = change(sourceField > 0 ? c.value(sourceField) : before);
        if (after == before)
            continue;
        c.setValue(target, after);
        if (c.value(target) == before)
            continue; // not accepted by the field (e.g. not a number)
        m_ev->updateCompetitor(c);
        ++n;
    }
    m_ev->commitBatch();
    QMessageBox::information(this, windowTitle(), tr("%1: %n competitor(s) modified.", nullptr, n).arg(fieldName(target)));
    return n;
}

void FormatFieldDialog::formatCase()
{
    const auto c = FieldFormat::Case(m_case->checkedId());
    apply([c](const QString& v) { return FieldFormat::applyCase(v, c); });
}

void FormatFieldDialog::replaceText()
{
    const QString what = m_replace->text();
    if (what.isEmpty()) {
        QMessageBox::warning(this, windowTitle(), tr("Enter the text to replace."));
        return;
    }
    const QString by = m_by->text();
    apply([&](const QString& v) { return FieldFormat::replace(v, what, by); });
}

void FormatFieldDialog::runOption()
{
    if (m_option->currentIndex() == 0)
        copyField();
    else
        clearField();
}

void FormatFieldDialog::clearField()
{
    if (QMessageBox::question(this, windowTitle(),
                              tr("Delete the values of the field %1 of all the competitors ?").arg(fieldName(field())))
        != QMessageBox::Yes)
        return;
    apply([](const QString&) { return QString(); });
}

void FormatFieldDialog::copyField()
{
    const int source = m_source->currentData().toInt();
    if (source <= 0)
        return;
    // SkiPro: "Ok to copy field values %s to the fields %s"
    if (QMessageBox::question(this, windowTitle(),
                              tr("Ok to copy field values %1 to the fields %2 ?").arg(fieldName(source), fieldName(field())))
        != QMessageBox::Yes)
        return;
    apply([](const QString& v) { return v; }, source);
}
