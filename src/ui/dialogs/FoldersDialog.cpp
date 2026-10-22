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
    QDialog::accept();
}
