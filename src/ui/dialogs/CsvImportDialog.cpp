#include "CsvImportDialog.h"
#include "core/Event.h"
#include "app/AppSettings.h"
#include "ui/UiHelpers.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>

CsvImportDialog::CsvImportDialog(Event* ev, const QString& file, QWidget* parent)
    : QDialog(parent)
    , m_ev(ev)
    , m_file(file)
{
    setWindowTitle(tr("Import Competitors from a File"));
    auto* lay = new QVBoxLayout(this);
    lay->addWidget(new QLabel(tr("File : <b>%1</b>").arg(QFileInfo(file).fileName()), this));
    auto* opts = new QHBoxLayout;
    m_encoding = new QComboBox(this);
    m_encoding->addItem(tr("Automatic"), int(CsvImporter::Encoding::Auto));
    m_encoding->addItem(QStringLiteral("UTF-8"), int(CsvImporter::Encoding::Utf8));
    m_encoding->addItem(tr("Windows (ANSI)"), int(CsvImporter::Encoding::System));
    m_encoding->addItem(QStringLiteral("ISO-8859-1"), int(CsvImporter::Encoding::Latin1));
    m_delimiter = new QComboBox(this);
    m_delimiter->addItem(tr("Automatic"), QString());
    m_delimiter->addItem(QStringLiteral(";"), QStringLiteral(";"));
    m_delimiter->addItem(QStringLiteral(","), QStringLiteral(","));
    m_delimiter->addItem(tr("Tab"), QStringLiteral("\t"));
    m_delimiter->addItem(QStringLiteral("|"), QStringLiteral("|"));
    m_header = new QCheckBox(tr("The first row contains the column names"), this);
    m_header->setChecked(true);
    const bool xlsx = QFileInfo(file).suffix().compare(QLatin1String("xlsx"), Qt::CaseInsensitive) == 0;
    m_encoding->setEnabled(!xlsx);
    m_delimiter->setEnabled(!xlsx);
    opts->addWidget(new QLabel(tr("Encoding :"), this));
    opts->addWidget(m_encoding);
    opts->addWidget(new QLabel(tr("Separator :"), this));
    opts->addWidget(m_delimiter);
    opts->addWidget(m_header);
    opts->addStretch();
    lay->addLayout(opts);

    lay->addWidget(new QLabel(tr("Select the competitor field of each column:"), this));
    m_preview = new QTableWidget(this);
    UiHelpers::styleGrid(m_preview);
    m_preview->setEditTriggers(QAbstractItemView::NoEditTriggers);
    lay->addWidget(m_preview, 1);

    auto* modeRow = new QHBoxLayout;
    m_mode = new QComboBox(this);
    m_mode->addItem(tr("Add to the existing competitors"));
    m_mode->addItem(tr("Replace all the competitors"));
    m_mode->addItem(tr("Update existing competitors (same code), add the others"));
    modeRow->addWidget(new QLabel(tr("Import :"), this));
    modeRow->addWidget(m_mode, 1);
    lay->addLayout(modeRow);
    lay->addLayout(UiHelpers::okCancelRow(this));

    connect(m_encoding, &QComboBox::currentIndexChanged, this, &CsvImportDialog::load);
    connect(m_delimiter, &QComboBox::currentIndexChanged, this, &CsvImportDialog::load);
    connect(m_header, &QCheckBox::toggled, this, &CsvImportDialog::rebuildPreview);
    load();
    restoreMode();
    resize(1000, 600);
}

CsvImportDialog* CsvImportDialog::fromText(Event* ev, const QString& text, QWidget* parent)
{
    auto* dlg = new CsvImportDialog(ev, QString(), parent);
    dlg->setWindowTitle(tr("Import Competitors from the Clipboard"));
    if (auto* label = dlg->findChild<QLabel*>())
        label->setText(tr("Cells copied in Excel (or another table), separated by tabs"));
    dlg->m_text = text;
    dlg->m_fromText = true;
    dlg->restoreMode();
    dlg->m_encoding->setEnabled(false);
    {
        const QSignalBlocker block(dlg->m_delimiter);
        dlg->m_delimiter->setCurrentIndex(qMax(0, dlg->m_delimiter->findData(QStringLiteral("\t"))));
    }
    dlg->load();
    return dlg;
}

void CsvImportDialog::load()
{
    QString err;
    const QString d = m_delimiter->currentData().toString();
    if (m_fromText) {
        CsvImporter::parseText(m_text, m_table, d.isEmpty() ? QChar() : d.at(0));
        rebuildPreview();
        return;
    }
    if (m_file.isEmpty()) {
        m_table = CsvImporter::Table();
        rebuildPreview();
        return;
    }
    if (!CsvImporter::readFile(m_file, m_table, &err, d.isEmpty() ? QChar() : d.at(0),
                               CsvImporter::Encoding(m_encoding->currentData().toInt()))) {
        QMessageBox::warning(this, windowTitle(), err);
    }
    rebuildPreview();
}

void CsvImportDialog::rebuildPreview()
{
    const int cols = m_table.columnCount();
    const bool header = m_header->isChecked();
    const QList<int> guess = header && !m_table.rows.isEmpty() ? CsvImporter::guessMapping(m_table.rows.first()) : QList<int>();
    // the fields chosen last time in this window: by the column name, otherwise by the position
    QList<int> lastFields;
    for (const QString& f : AppSettings::value(settingsKey() + QStringLiteral("/fields")).toStringList())
        lastFields << f.toInt();
    const QStringList lastHeaders = AppSettings::value(settingsKey() + QStringLiteral("/headers")).toStringList();
    auto lastFieldFor = [&](int c, const QString& name) {
        if (header && !name.trimmed().isEmpty()) {
            for (int i = 0; i < lastHeaders.size() && i < lastFields.size(); ++i)
                if (lastHeaders.at(i).trimmed().compare(name.trimmed(), Qt::CaseInsensitive) == 0)
                    return lastFields.at(i);
        }
        if (lastFields.size() == cols && (!header || lastHeaders.isEmpty()))
            return lastFields.at(c);
        return -1;
    };
    m_preview->clear();
    m_preview->setColumnCount(cols);
    const int dataRows = qMin(int(m_table.rows.size()) - (header ? 1 : 0), 200);
    m_preview->setRowCount(qMax(0, dataRows) + 1);
    m_columnFields.clear();
    QStringList headers;
    for (int c = 0; c < cols; ++c) {
        headers << (header && !m_table.rows.isEmpty() ? m_table.rows.first().value(c) : tr("Column %1").arg(c + 1));
        auto* cb = new QComboBox(m_preview);
        cb->addItem(tr("(ignore)"), 0);
        for (const auto& d : competitorFieldDefs())
            cb->addItem(QCoreApplication::translate("Competitor", d.label), d.id);
        const int last = lastFieldFor(c, headers.last());
        const int f = last >= 0 ? last : guess.value(c);
        cb->setCurrentIndex(qMax(0, cb->findData(f)));
        m_preview->setCellWidget(0, c, cb);
        m_columnFields << cb;
    }
    m_preview->setHorizontalHeaderLabels(headers);
    for (int r = 0; r < dataRows; ++r) {
        const QStringList& row = m_table.rows.at(r + (header ? 1 : 0));
        for (int c = 0; c < cols; ++c)
            m_preview->setItem(r + 1, c, new QTableWidgetItem(row.value(c)));
    }
    m_preview->resizeColumnsToContents();
    for (int c = 0; c < cols; ++c)
        m_preview->setColumnWidth(c, qMax(m_preview->columnWidth(c), 130));
}

QString CsvImportDialog::settingsKey() const
{
    return m_fromText ? QStringLiteral("import/clipboard") : QStringLiteral("import/file");
}

void CsvImportDialog::restoreMode()
{
    m_mode->setCurrentIndex(qBound(0, AppSettings::value(settingsKey() + QStringLiteral("/mode"), 0).toInt(), m_mode->count() - 1));
}

QList<int> CsvImportDialog::mapping() const
{
    QList<int> m;
    for (auto* cb : m_columnFields)
        m << cb->currentData().toInt();
    return m;
}

void CsvImportDialog::accept()
{
    const QList<int> map = mapping();
    bool any = false;
    for (int f : map)
        if (f > 0)
            any = true;
    if (!any) {
        QMessageBox::warning(this, windowTitle(), tr("Select the field of at least one column."));
        return;
    }
    // kept for the next import in this window
    QStringList fields, headers;
    for (int c = 0; c < map.size(); ++c) {
        fields << QString::number(map.at(c));
        if (m_header->isChecked() && !m_table.rows.isEmpty())
            headers << m_table.rows.first().value(c);
    }
    AppSettings::setValue(settingsKey() + QStringLiteral("/fields"), fields);
    AppSettings::setValue(settingsKey() + QStringLiteral("/headers"), headers);
    AppSettings::setValue(settingsKey() + QStringLiteral("/mode"), m_mode->currentIndex());
    QVector<Competitor> list = CsvImporter::toCompetitors(m_table, map, m_header->isChecked() ? 1 : 0);
    for (auto& c : list) {
        c.lastName = c.lastName.toUpper();
        if (c.category.isEmpty())
            c.category = m_ev->categories().categoryFor(c.gender, c.year);
    }
    int added = 0, updated = 0;
    switch (m_mode->currentIndex()) {
    case 1:
        if (QMessageBox::question(this, windowTitle(), tr("All the existing competitors will be deleted. Continue?")) != QMessageBox::Yes)
            return;
        m_ev->replaceCompetitors(list);
        added = int(list.size());
        break;
    case 2: {
        QVector<Competitor> toAdd;
        m_ev->beginBatch();
        for (const auto& c : list) {
            const Competitor* existing = nullptr;
            if (!c.code.isEmpty())
                for (const auto& e : m_ev->competitors())
                    if (e.code == c.code)
                        existing = &e;
            if (existing) {
                Competitor u = c;
                u.id = existing->id;
                if (u.bib <= 0)
                    u.bib = existing->bib;
                m_ev->updateCompetitor(u);
                ++updated;
            } else {
                toAdd << c;
            }
        }
        m_ev->addCompetitors(toAdd);
        m_ev->commitBatch();
        added = int(toAdd.size());
        break;
    }
    default:
        m_ev->addCompetitors(list);
        added = int(list.size());
        break;
    }
    QMessageBox::information(this, windowTitle(), tr("%1 competitor(s) added, %2 updated.").arg(added).arg(updated));
    QDialog::accept();
}
