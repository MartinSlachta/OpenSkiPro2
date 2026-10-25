#include "GeneralParametersDialog.h"
#include "app/AppSettings.h"
#include "ui/UiHelpers.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QRegularExpressionValidator>
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

    // FIS code "SVK6534.185": nation (the nation above unless typed), codex, TD number (Header)
    auto* fis = new QGroupBox(tr("FIS"), this);
    auto* fisf = new QFormLayout(fis);
    auto* codexRow = new QHBoxLayout;
    m_fisNation = new QLineEdit(p.general.fisNation, fis);
    m_fisNation->setMaximumWidth(60);
    m_fisNation->setMaxLength(3);
    m_fisNation->setToolTip(tr("Nation of the codex, empty: the nation of the foot of page"));
    m_codex = new QLineEdit(GeneralParams::normalizedCodex(p.general.codex), fis);
    m_codex->setMaximumWidth(80);
    m_codex->setPlaceholderText(QStringLiteral("0123"));
    m_codex->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("\\d{0,4}")), m_codex));
    m_codex->setToolTip(tr("Codex of the race (4 digits, completed with zeros on the left)"));
    m_td = new QLineEdit(p.header.value(QLatin1String(HeaderKeys::TechnicalDelegate) + QStringLiteral(".code")), fis);
    m_td->setMaximumWidth(80);
    m_td->setReadOnly(true);
    m_td->setPlaceholderText(tr("TD"));
    m_td->setToolTip(tr("FIS number of the Technical Delegate, entered in the Header"));
    m_fullCodex = new QLabel(fis);
    QFont bold = m_fullCodex->font();
    bold.setBold(true);
    m_fullCodex->setFont(bold);
    codexRow->addWidget(m_fisNation);
    codexRow->addWidget(m_codex);
    codexRow->addWidget(new QLabel(QStringLiteral("."), fis));
    codexRow->addWidget(m_td);
    codexRow->addSpacing(12);
    codexRow->addWidget(m_fullCodex);
    codexRow->addStretch();
    fisf->addRow(tr("Codex :"), codexRow);
    lay->addWidget(fis);

    // OpenSkiPro Publish: each race its own token, entered in advance (two races on one day)
    auto* publish = new QGroupBox(tr("Results on the Internet"), this);
    auto* pf = new QFormLayout(publish);
    m_publishToken = new QLineEdit(p.general.publishToken, publish);
    m_publishToken->setClearButtonEnabled(true);
    m_publishToken->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_publishToken->setPlaceholderText(tr("paste the token of this race"));
    m_publishToken->setToolTip(tr("Token from your account on the OpenSkiPro Publish server. "
                                  "OpenSkiPro Connect (module Publish) publishes this race with it; "
                                  "empty: the race is not published."));
    pf->addRow(tr("Publish token :"), m_publishToken);
    lay->addWidget(publish);
    connect(m_nation, &QLineEdit::textChanged, this, &GeneralParametersDialog::updateCodex);
    connect(m_fisNation, &QLineEdit::textChanged, this, &GeneralParametersDialog::updateCodex);
    connect(m_codex, &QLineEdit::textChanged, this, &GeneralParametersDialog::updateCodex);
    connect(m_codex, &QLineEdit::editingFinished, this,
            [this] { m_codex->setText(GeneralParams::normalizedCodex(m_codex->text())); });
    updateCodex();

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
    p.general.codex = GeneralParams::normalizedCodex(m_codex->text());
    // the same nation as the foot of page: follows it
    const QString fisNation = m_fisNation->text().trimmed().toUpper();
    p.general.fisNation = fisNation == p.general.nation ? QString() : fisNation;
    p.general.publishToken = m_publishToken->text().simplified().remove(QLatin1Char(' ')); // pasted with spaces / line breaks
    QStringList recent = AppSettings::value(QStringLiteral("general/locations")).toStringList();
    if (!p.general.location.isEmpty()) {
        recent.removeAll(p.general.location);
        recent.prepend(p.general.location);
        AppSettings::setValue(QStringLiteral("general/locations"), recent.mid(0, 30));
    }
}

void GeneralParametersDialog::updateCodex()
{
    const QString nation = m_nation->text().trimmed().toUpper();
    m_fisNation->setPlaceholderText(nation);
    const QString fisNation = m_fisNation->text().trimmed().toUpper();
    const QString codex = GeneralParams::normalizedCodex(m_codex->text());
    const QString td = m_td->text().trimmed();
    if (codex.isEmpty()) {
        m_fullCodex->clear();
        return;
    }
    m_fullCodex->setText((fisNation.isEmpty() ? nation : fisNation) + codex + (td.isEmpty() ? QString() : QStringLiteral(".") + td));
}
