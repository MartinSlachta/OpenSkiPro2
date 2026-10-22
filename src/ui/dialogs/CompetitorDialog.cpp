#include "CompetitorDialog.h"
#include "core/Event.h"
#include "ui/UiHelpers.h"

#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QSet>
#include <QVBoxLayout>

CompetitorDialog::CompetitorDialog(Event* ev, const Competitor& c, bool isNew, QWidget* parent)
    : QDialog(parent)
    , m_ev(ev)
    , m_original(c)
    , m_new(isNew)
{
    setWindowTitle(isNew ? tr("New (F1)") : tr("Modify (F2)"));
    const auto& fields = ev->params().fields;
    auto* lay = new QVBoxLayout(this);
    auto* cols = new QHBoxLayout;

    auto values = [ev](int field) {
        QSet<QString> set;
        for (const auto& x : ev->competitors()) {
            const QString v = x.value(field);
            if (!v.isEmpty())
                set.insert(v);
        }
        QStringList l(set.cbegin(), set.cend());
        l.sort(Qt::CaseInsensitive);
        return l;
    };
    auto addLine = [&](QFormLayout* form, int field, int maxWidth = 0) {
        auto* e = new QLineEdit(this);
        if (maxWidth)
            e->setMaximumWidth(maxWidth);
        e->setEnabled(fields.isActive(field) || field == CompField::Bib);
        form->addRow(fields.label(field) + QStringLiteral(" :"), e);
        m_editors.insert(field, e);
    };
    auto addCombo = [&](QFormLayout* form, int field) {
        auto* cb = new QComboBox(this);
        cb->setEditable(true);
        cb->addItems(values(field));
        cb->setEnabled(fields.isActive(field));
        form->addRow(fields.label(field) + QStringLiteral(" :"), cb);
        m_editors.insert(field, cb);
    };

    auto* left = new QGroupBox(tr("Competitor"), this);
    auto* lf = new QFormLayout(left);
    addLine(lf, CompField::Bib, 70);
    addLine(lf, CompField::Transponder, 160);
    addLine(lf, CompField::LastName);
    addLine(lf, CompField::FirstName);
    addLine(lf, CompField::Code, 120);
    auto* gender = new QComboBox(this);
    gender->addItem(QString(), int(Gender::Unknown));
    gender->addItem(tr("Male"), int(Gender::Male));
    gender->addItem(tr("Female"), int(Gender::Female));
    gender->setMaximumWidth(120);
    lf->addRow(fields.label(CompField::Gender) + QStringLiteral(" :"), gender);
    m_editors.insert(CompField::Gender, gender);
    addLine(lf, CompField::Year, 70);
    addLine(lf, CompField::Category, 120);
    addCombo(lf, CompField::Nation);
    addCombo(lf, CompField::Region);
    addCombo(lf, CompField::Club);
    addCombo(lf, CompField::Team);
    cols->addWidget(left, 1);

    auto* right = new QGroupBox(tr("Custom"), this);
    auto* rf = new QFormLayout(right);
    for (int f = CompField::PointsDH; f <= CompField::PointsAC; ++f)
        addLine(rf, f, 100);
    addLine(rf, CompField::FromList, 100);
    addLine(rf, CompField::Custom);
    cols->addWidget(right, 1);
    lay->addLayout(cols);
    lay->addLayout(UiHelpers::okCancelRow(this));
    setCompetitor(c);
    resize(820, 0);
}

void CompetitorDialog::setCompetitor(const Competitor& c)
{
    m_original = c;
    for (auto it = m_editors.cbegin(); it != m_editors.cend(); ++it) {
        if (auto* e = qobject_cast<QLineEdit*>(it.value())) {
            QString v = c.value(it.key());
            if (it.key() >= CompField::PointsDH && it.key() <= CompField::PointsAC && v == QLatin1String("-"))
                v.clear();
            e->setText(v);
        } else if (auto* cb = qobject_cast<QComboBox*>(it.value())) {
            if (it.key() == CompField::Gender)
                cb->setCurrentIndex(qMax(0, cb->findData(int(c.gender))));
            else
                cb->setCurrentText(c.value(it.key()));
        }
    }
    if (auto* last = qobject_cast<QLineEdit*>(m_editors.value(CompField::LastName)))
        last->setFocus();
}

Competitor CompetitorDialog::competitor() const
{
    Competitor c = m_original;
    for (auto it = m_editors.cbegin(); it != m_editors.cend(); ++it) {
        if (auto* e = qobject_cast<QLineEdit*>(it.value()))
            c.setValue(it.key(), e->text());
        else if (auto* cb = qobject_cast<QComboBox*>(it.value())) {
            if (it.key() == CompField::Gender)
                c.gender = Gender(cb->currentData().toInt());
            else
                c.setValue(it.key(), cb->currentText());
        }
    }
    c.lastName = c.lastName.toUpper();
    if (c.category.isEmpty())
        c.category = m_ev->categories().categoryFor(c.gender, c.year);
    return c;
}

void CompetitorDialog::accept()
{
    const Competitor c = competitor();
    if (c.lastName.isEmpty() && c.firstName.isEmpty()) {
        QMessageBox::warning(this, windowTitle(), tr("Enter the name of the competitor."));
        return;
    }
    if (c.bib > 0) {
        for (const auto& other : m_ev->competitors()) {
            if (other.bib == c.bib && other.id != c.id) {
                if (QMessageBox::question(this, windowTitle(),
                                          tr("The bib %1 is already used by %2. Keep it anyway?").arg(c.bib).arg(other.fullName()))
                    != QMessageBox::Yes)
                    return;
                break;
            }
        }
    }
    QDialog::accept();
}
