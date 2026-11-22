#include "FoldersDialog.h"
#include "app/AppSettings.h"
#include "ui/UiHelpers.h"

#include <QDir>
#include <QFileDialog>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QToolButton>
#include <QVBoxLayout>

FoldersDialog::FoldersDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Folders"));
    auto* lay = new QVBoxLayout(this);
    auto* g = new QGridLayout;
    auto row = [&](int r, const QString& label, const QString& value) {
        auto* e = new QLineEdit(QDir::toNativeSeparators(value), this);
        auto* b = new QToolButton(this);
        b->setIcon(UiHelpers::icon(QStringLiteral("folder")));
        connect(b, &QToolButton::clicked, this, [this, e] {
            const QString d = QFileDialog::getExistingDirectory(this, windowTitle(), e->text());
            if (!d.isEmpty())
                e->setText(QDir::toNativeSeparators(d));
        });
        g->addWidget(new QLabel(label, this), r, 0);
        g->addWidget(e, r, 1);
        g->addWidget(b, r, 2);
        return e;
    };
    m_events = row(0, tr("Events :"), AppSettings::eventsDir());
    m_logos = row(1, tr("Logos :"), AppSettings::logosDir());
    m_export = row(2, tr("Export / backups :"), AppSettings::exportDir());
    {
        // a file, not a folder: the empty SkiAlp Pro event used by the export to SkiAlp Pro
        m_skiAlpTemplate = new QLineEdit(QDir::toNativeSeparators(AppSettings::skiAlpTemplate()), this);
        m_skiAlpTemplate->setPlaceholderText(tr("an empty event exported from SkiAlp Pro (*.spro)"));
        auto* b = new QToolButton(this);
        b->setIcon(UiHelpers::icon(QStringLiteral("folder")));
        connect(b, &QToolButton::clicked, this, [this] {
            const QString f = QFileDialog::getOpenFileName(this, tr("SkiAlp Pro 11 template"), m_skiAlpTemplate->text(),
                                                           tr("SkiAlp Pro event (*.spro)"));
            if (!f.isEmpty())
                m_skiAlpTemplate->setText(QDir::toNativeSeparators(f));
        });
        g->addWidget(new QLabel(tr("SkiAlp Pro 11 template :"), this), 3, 0);
        g->addWidget(m_skiAlpTemplate, 3, 1);
        g->addWidget(b, 3, 2);
    }
    g->setColumnStretch(1, 1);
    lay->addLayout(g);
    lay->addWidget(new QLabel(tr("Configuration file: %1").arg(QDir::toNativeSeparators(AppSettings::iniPath())), this));
    lay->addLayout(UiHelpers::okCancelRow(this));
    resize(640, 0);
}

void FoldersDialog::accept()
{
    AppSettings::setEventsDir(QDir::fromNativeSeparators(m_events->text().trimmed()));
    AppSettings::setLogosDir(QDir::fromNativeSeparators(m_logos->text().trimmed()));
    AppSettings::setExportDir(QDir::fromNativeSeparators(m_export->text().trimmed()));
    AppSettings::setSkiAlpTemplate(QDir::fromNativeSeparators(m_skiAlpTemplate->text().trimmed()));
    QDialog::accept();
}
