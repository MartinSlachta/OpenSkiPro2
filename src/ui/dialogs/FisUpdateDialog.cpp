#include "FisUpdateDialog.h"
#include "app/AppSettings.h"
#include "core/Event.h"
#include "core/FisCompetitors.h"
#include "ui/FisUi.h"
#include "ui/UiHelpers.h"

#include <QApplication>
#include <QCheckBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

static QList<int> defaultFields()
{
    return { CompField::PointsDH, CompField::PointsSG, CompField::PointsGS, CompField::PointsSL, CompField::PointsAC,
             CompField::FromList };
}

FisUpdateDialog::FisUpdateDialog(Event* ev, const FisList& list, QWidget* parent)
    : QDialog(parent)
    , m_ev(ev)
    , m_list(list)
{
    setWindowTitle(tr("Update Competitors from the FIS list"));
    auto* lay = new QVBoxLayout(this);
    auto* info = new QLabel(this);
    info->setTextFormat(Qt::RichText);
    info->setText(QStringLiteral("<b>%1</b><br>%2%3")
                      .arg(FisUi::listTitle(list.header).toHtmlEscaped(), FisUi::validityText(list.header).toHtmlEscaped(),
                           list.header.isExpired()
                               ? QStringLiteral(" &nbsp; <b style='color:#c00000'>[%1]</b>").arg(tr("Expired").toHtmlEscaped())
                               : QString()));
    lay->addWidget(info);
    lay->addWidget(new QLabel(tr("The competitors are found in the FIS list by their code (%1).")
                                  .arg(ev->params().fields.label(CompField::Code)),
                              this));

    auto* box = new QGroupBox(tr("Fields to update"), this);
    auto* grid = new QGridLayout(box);
    QList<int> checked;
    const QVariant saved = AppSettings::value(QStringLiteral("fis/updateFields"));
    if (saved.isValid()) {
        for (const QString& s : saved.toString().split(QLatin1Char(','), Qt::SkipEmptyParts))
            checked << s.toInt();
    } else {
        checked = defaultFields();
    }
    const QList<int> fields = FisCompetitors::updatableFields();
    for (int i = 0; i < fields.size(); ++i) {
        auto* cb = new QCheckBox(ev->params().fields.label(fields.at(i)), box);
        cb->setChecked(checked.contains(fields.at(i)));
        m_fields.insert(fields.at(i), cb);
        // identity on the left, points / list on the right
        const bool right = fields.at(i) >= CompField::FromList;
        const int row = right ? (fields.at(i) == CompField::FromList ? 5 : fields.at(i) - CompField::PointsDH) : i;
        grid->addWidget(cb, row, right ? 1 : 0);
    }
    auto* bPoints = new QPushButton(tr("Points only"), box);
    connect(bPoints, &QPushButton::clicked, this, [this] {
        const QList<int> def = defaultFields();
        for (auto it = m_fields.cbegin(); it != m_fields.cend(); ++it)
            it.value()->setChecked(def.contains(it.key()));
    });
    grid->addWidget(bPoints, 6, 1, Qt::AlignLeft);
    lay->addWidget(box);
    lay->addLayout(UiHelpers::okCancelRow(this));
}

void FisUpdateDialog::accept()
{
    QList<int> fields;
    QStringList saved;
    for (auto it = m_fields.cbegin(); it != m_fields.cend(); ++it)
        if (it.value()->isChecked()) {
            fields << it.key();
            saved << QString::number(it.key());
        }
    if (fields.isEmpty()) {
        QMessageBox::information(this, windowTitle(), tr("Select the fields to update."));
        return;
    }
    AppSettings::setValue(QStringLiteral("fis/updateFields"), saved.join(QLatin1Char(',')));
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const FisCompetitors::UpdateResult r = FisCompetitors::update(m_ev, m_list, fields);
    QApplication::restoreOverrideCursor();
    if (r.notUpdated.isEmpty()) {
        QMessageBox::information(this, windowTitle(), tr("All the competitors have been updated."));
    } else {
        QMessageBox box(QMessageBox::Warning, windowTitle(),
                        tr("%n competitor(s) updated.", nullptr, r.updated) + QStringLiteral("\n")
                            + tr("%n competitor(s) have been not updated (not found in the FIS list):", nullptr,
                                 int(r.notUpdated.size())),
                        QMessageBox::Ok, this);
        const int shown = 20;
        QString list = QStringList(r.notUpdated.mid(0, shown)).join(QLatin1Char('\n'));
        if (r.notUpdated.size() > shown)
            list += QStringLiteral("\n...");
        box.setInformativeText(list);
        box.setDetailedText(r.notUpdated.join(QLatin1Char('\n')));
        box.exec();
    }
    QDialog::accept();
}
