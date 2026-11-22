#include "CompetitorDialog.h"
#include "core/Event.h"
#include "ui/UiHelpers.h"

#include <QCheckBox>
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
    m_upperLastName = new QCheckBox(tr("Last name in capitals"), this);
    m_upperLastName->setToolTip(tr("The last name is saved in capitals (also in the import of a file), "
                                   "a setting of the event"));
    m_upperLastName->setChecked(fields.upperLastName);
    lf->addRow(QString(), m_upperLastName);
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
    addLine(lf, CompField::Category, 180);
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
    for (int i = 0; i < CompField::kCustomCount; ++i)
        addLine(rf, CompField::customField(i));
    cols->addWidget(right, 1);
    lay->addLayout(cols);
    lay->addLayout(UiHelpers::okCancelRow(this));
    // the category follows the gender and the year while it is the one they gave
    connect(gender, &QComboBox::currentIndexChanged, this, &CompetitorDialog::updateCategory);
    if (auto* year = qobject_cast<QLineEdit*>(m_editors.value(CompField::Year)))
        connect(year, &QLineEdit::textChanged, this, &CompetitorDialog::updateCategory);
    setCompetitor(c);
    resize(820, 0);
}

QString CompetitorDialog::autoCategory() const
{
    const auto* gender = qobject_cast<QComboBox*>(m_editors.value(CompField::Gender));
    const auto* year = qobject_cast<QLineEdit*>(m_editors.value(CompField::Year));
    if (!gender || !year)
        return QString();
    return m_ev->categories().categoryFor(Gender(gender->currentData().toInt()), year->text().trimmed().toInt());
}

// Empty category = computed when saved: the form shows it as "auto: ...". A category that
// the old gender / year gave is cleared when they change, so it is computed again.
void CompetitorDialog::updateCategory()
{
    auto* category = qobject_cast<QLineEdit*>(m_editors.value(CompField::Category));
    if (!category || m_loading)
        return;
    const QString now = autoCategory();
    if (!m_lastAuto.isEmpty() && category->text().trimmed() == m_lastAuto)
        category->clear();
    m_lastAuto = now;
    category->setPlaceholderText(m_ev->categories().categories.isEmpty()
                                     ? QString()
                                     : tr("auto: %1").arg(now.isEmpty() ? QStringLiteral("-") : now));
}

void CompetitorDialog::setCompetitor(const Competitor& c)
{
    m_loading = true;
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
    m_loading = false;
    m_lastAuto.clear(); // the category of the competitor stays as it is
    updateCategory();
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
    if (m_upperLastName->isChecked())
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
    // the times, the start list and the results belong to the bib: it must be unique
    // (to swap two bibs, give one of them a free bib first, e.g. +1000)
    if (c.bib > 0) {
        for (const auto& other : m_ev->competitors()) {
            if (other.bib == c.bib && other.id != c.id) {
                QMessageBox::warning(this, windowTitle(), tr("The bib %1 is already used by %2.").arg(c.bib).arg(other.fullName()));
                if (auto* e = qobject_cast<QLineEdit*>(m_editors.value(CompField::Bib))) {
                    e->setFocus();
                    e->selectAll();
                }
                return;
            }
        }
    }
    if (m_ev->params().fields.upperLastName != m_upperLastName->isChecked()) {
        EventParams p = m_ev->params();
        p.fields.upperLastName = m_upperLastName->isChecked();
        m_ev->setParams(p);
    }
    QDialog::accept();
}
