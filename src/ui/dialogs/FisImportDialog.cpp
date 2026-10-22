#include "FisImportDialog.h"
#include "app/AppSettings.h"
#include "db/FisListDb.h"
#include "ui/FisUi.h"
#include "ui/UiHelpers.h"

#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

FisImportDialog::FisImportDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Import FIS list"));
    setMinimumWidth(620);
    auto* lay = new QVBoxLayout(this);

    m_current = new QLabel(this);
    m_current->setWordWrap(true);
    FisListDb::loadHeader(AppSettings::fisListPath(), m_existing, nullptr, nullptr);
    m_current->setText(m_existing.isEmpty()
                           ? tr("Current FIS list: none")
                           : tr("Current FIS list: %1, %2").arg(FisUi::listTitle(m_existing.header),
                                                                FisUi::validityText(m_existing.header)));
    lay->addWidget(m_current);

    auto* src = new QGroupBox(tr("FIS points list (ZIP archive from the FIS website or folder with its files)"), this);
    auto* sl = new QHBoxLayout(src);
    m_path = new QLineEdit(src);
    m_path->setPlaceholderText(tr("e.g. ALFP1627F.zip"));
    auto* bFile = new QPushButton(UiHelpers::icon(QStringLiteral("folder")), tr("Archive..."), src);
    auto* bFolder = new QPushButton(UiHelpers::icon(QStringLiteral("folder")), tr("Folder..."), src);
    sl->addWidget(m_path, 1);
    sl->addWidget(bFile);
    sl->addWidget(bFolder);
    lay->addWidget(src);

    m_info = new QLabel(this);
    m_info->setObjectName(QStringLiteral("infoBox"));
    m_info->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_info->setWordWrap(true);
    m_info->setMinimumHeight(110);
    m_info->setTextFormat(Qt::RichText);
    lay->addWidget(m_info);

    lay->addLayout(UiHelpers::okCancelRow(this));
    for (QPushButton* b : findChildren<QPushButton*>())
        if (b->isDefault())
            m_ok = b;
    if (m_ok)
        m_ok->setText(tr("Import"));

    connect(bFile, &QPushButton::clicked, this, &FisImportDialog::browseFile);
    connect(bFolder, &QPushButton::clicked, this, &FisImportDialog::browseFolder);
    connect(m_path, &QLineEdit::editingFinished, this, &FisImportDialog::readSource);
    readSource();
}

void FisImportDialog::setSource(const QString& path)
{
    m_path->setText(QDir::toNativeSeparators(path));
    readSource();
}

void FisImportDialog::browseFile()
{
    const QString start = m_path->text().isEmpty() ? AppSettings::value(QStringLiteral("folders/fis")).toString() : m_path->text();
    const QString file = QFileDialog::getOpenFileName(this, tr("Import FIS list"), start,
                                                      tr("FIS points list (*.zip *.csv);;All files (*)"));
    if (file.isEmpty())
        return;
    m_path->setText(QDir::toNativeSeparators(file));
    readSource();
}

void FisImportDialog::browseFolder()
{
    const QString start = m_path->text().isEmpty() ? AppSettings::value(QStringLiteral("folders/fis")).toString() : m_path->text();
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Folder of the FIS list files"), start);
    if (dir.isEmpty())
        return;
    m_path->setText(QDir::toNativeSeparators(dir));
    readSource();
}

void FisImportDialog::readSource()
{
    const QString path = QDir::fromNativeSeparators(m_path->text().trimmed());
    if (path == m_readPath && !path.isEmpty())
        return;
    m_readPath = path;
    m_list = FisList();
    if (m_ok)
        m_ok->setEnabled(false);
    if (path.isEmpty()) {
        m_info->setText(tr("Select the archive of the FIS points list downloaded from the FIS website "
                           "(Alpine / FIS points, file ALFP&lt;list&gt;&lt;season&gt;F.zip)."));
        return;
    }
    QString err;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = FisListReader::read(path, m_list, &err);
    QApplication::restoreOverrideCursor();
    if (!ok) {
        m_list = FisList();
        m_info->setText(QStringLiteral("<span style='color:#c00000'>%1</span>").arg(err.toHtmlEscaped()));
        return;
    }
    const FisListHeader& h = m_list.header;
    QString text = QStringLiteral("<b>%1</b><br>%2<br>%3")
                       .arg(FisUi::listTitle(h).toHtmlEscaped(), FisUi::validityText(h).toHtmlEscaped(),
                            tr("%n competitor(s)", nullptr, int(m_list.competitors.size())).toHtmlEscaped());
    QStringList warnings;
    if (h.isExpired())
        warnings << tr("The validity of this list is over.");
    if (!m_existing.isEmpty() && h.listId < m_existing.header.listId)
        warnings << tr("This list is older than the current FIS list.");
    if (!m_existing.isEmpty() && h.listId == m_existing.header.listId)
        warnings << tr("This list is already imported.");
    if (m_list.penalties.isEmpty() || m_list.disciplines.isEmpty())
        warnings << tr("The penalty parameters (cat.csv, dis.csv) are missing.");
    for (const QString& w : warnings)
        text += QStringLiteral("<br><span style='color:#c00000'>%1</span>").arg(w.toHtmlEscaped());
    m_info->setText(text);
    if (m_ok)
        m_ok->setEnabled(true);
}

void FisImportDialog::accept()
{
    readSource();
    if (m_list.isEmpty())
        return;
    if (!m_existing.isEmpty() && m_list.header.listId < m_existing.header.listId
        && QMessageBox::question(this, windowTitle(),
                                 tr("This list is older than the current FIS list %1. Replace it anyway?")
                                     .arg(FisUi::listTitle(m_existing.header)))
            != QMessageBox::Yes)
        return;
    QString err;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = FisListDb::save(AppSettings::fisListPath(), m_list, &err);
    QApplication::restoreOverrideCursor();
    if (!ok) {
        QMessageBox::warning(this, windowTitle(), tr("Cannot save the FIS list:\n%1").arg(err));
        return;
    }
    const QFileInfo fi(m_readPath);
    AppSettings::setValue(QStringLiteral("folders/fis"), fi.isDir() ? fi.absoluteFilePath() : fi.absolutePath());
    m_imported = int(m_list.competitors.size());
    QDialog::accept();
}
