#include "GeneralParametersDialog.h"
#include "app/AppSettings.h"
#include "ui/UiHelpers.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QVBoxLayout>

GeneralParametersDialog::GeneralParametersDialog(const EventParams& p, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("General Parameters"));
    auto* lay = new QVBoxLayout(this);

    auto* titles = new QGroupBox(tr("Titles"), this);
    auto* tf = new QFormLayout(titles);
    m_title = new QPlainTextEdit(p.general.title1, titles);
    m_discipline = new QPlainTextEdit(p.general.title2, titles);
    m_title->setMaximumHeight(52);
    m_discipline->setMaximumHeight(52);
    tf->addRow(tr("Title :"), m_title);
    tf->addRow(tr("Discipline :"), m_discipline);
    lay->addWidget(titles);

    auto* foot = new QGroupBox(tr("Foot of Page"), this);
    auto* ff = new QFormLayout(foot);
    m_date = new QDateEdit(p.general.date.isValid() ? p.general.date : QDate::currentDate(), foot);
    m_date->setCalendarPopup(true);
    m_location = new QComboBox(foot);
    m_location->setEditable(true);
    const QStringList recent = AppSettings::value(QStringLiteral("general/locations")).toStringList();
    m_location->addItems(recent);
    m_location->setCurrentText(p.general.location);
    m_nation = new QLineEdit(p.general.nation, foot);
    m_nation->setMaximumWidth(60);
    auto* locRow = new QHBoxLayout;
    locRow->addWidget(m_location, 1);
    locRow->addWidget(new QLabel(tr("Nation :"), foot));
    locRow->addWidget(m_nation);
    m_organisation = new QLineEdit(p.general.organisation, foot);
    m_timingBy = new QLineEdit(p.general.timingBy, foot);
    m_freeHeader = new QCheckBox(tr("Use a free header (no jury / technical data block)"), foot);
    m_freeHeader->setChecked(p.general.useFreeHeader);
    ff->addRow(tr("Date :"), m_date);
    ff->addRow(tr("Location :"), locRow);
    ff->addRow(tr("Organisation :"), m_organisation);
    ff->addRow(tr("Timing service provider :"), m_timingBy);
    ff->addRow(QString(), m_freeHeader);
    lay->addWidget(foot);

    auto* fis = new QGroupBox(tr("FIS"), this);
    auto* fisf = new QFormLayout(fis);
    m_codex = new QLineEdit(p.general.codex, fis);
    fisf->addRow(tr("Codex :"), m_codex);
    lay->addWidget(fis);

    lay->addLayout(UiHelpers::okCancelRow(this));
    resize(560, 0);
}

void GeneralParametersDialog::apply(EventParams& p) const
{
    p.general.title1 = m_title->toPlainText().trimmed();
    p.general.title2 = m_discipline->toPlainText().trimmed();
    p.general.date = m_date->date();
    p.general.location = m_location->currentText().trimmed();
    p.general.nation = m_nation->text().trimmed().toUpper();
    p.general.organisation = m_organisation->text().trimmed();
    p.general.timingBy = m_timingBy->text().trimmed();
    p.general.useFreeHeader = m_freeHeader->isChecked();
    p.general.codex = m_codex->text().trimmed();
    QStringList recent = AppSettings::value(QStringLiteral("general/locations")).toStringList();
    if (!p.general.location.isEmpty()) {
        recent.removeAll(p.general.location);
        recent.prepend(p.general.location);
        AppSettings::setValue(QStringLiteral("general/locations"), recent.mid(0, 30));
    }
}
