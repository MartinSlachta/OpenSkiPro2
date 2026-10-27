#include "ReportDialog.h"
#include "OutputGroupsDialog.h"
#include "PreviewDialog.h"
#include "SelectEventDialog.h"
#include "ui/MainWindow.h"
#include "app/AppSettings.h"
#include "core/Event.h"
#include "report/ReportBuilder.h"
#include "report/ReportRenderer.h"
#include "ui/UiHelpers.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPrintDialog>
#include <QPrinter>
#include <QPushButton>
#include <QRegularExpression>
#include <QShortcut>
#include <QSpinBox>
#include <QStringConverter>
#include <QTableWidget>
#include <QTextStream>
#include <QToolButton>
#include <QVBoxLayout>

ReportDialog::ReportDialog(Event* ev, ReportType type, int run, QWidget* parent)
    : QDialog(parent)
    , m_ev(ev)
    , m_type(type)
{
    setWindowTitle(ReportFormat::typeName(type));
    auto* lay = new QHBoxLayout(this);

    auto* left = new QVBoxLayout;
    auto* doc = new QGroupBox(tr("Document Format"), this);
    auto* dl = new QGridLayout(doc);
    const bool perRun = type == ReportType::StartList || type == ReportType::RunRanking || type == ReportType::RankingAfterRun
        || type == ReportType::TimeRecords || type == ReportType::TimeRecordsByBib || type == ReportType::TimingPaper;
    int row = 0;
    if (perRun) {
        m_run = new QComboBox(doc);
        for (int r = 1; r <= ev->runCount(); ++r)
            m_run->addItem(ev->params().type.runName(r), r);
        m_run->setCurrentIndex(qBound(0, run - 1, m_run->count() - 1));
        dl->addWidget(new QLabel(tr("Run :"), doc), row, 0);
        dl->addWidget(m_run, row, 1);
        ++row;
    }
    m_title = new QPlainTextEdit(doc);
    m_title->setMaximumHeight(46);
    dl->addWidget(new QLabel(tr("Title :"), doc), row, 0, Qt::AlignTop);
    dl->addWidget(m_title, row, 1, 1, 3);
    ++row;
    m_printHeader = new QCheckBox(tr("Print Header"), doc);
    m_columns = new QSpinBox(doc);
    m_columns->setRange(1, 4);
    dl->addWidget(m_printHeader, row, 1);
    dl->addWidget(new QLabel(tr("Number of Columns :"), doc), row, 2, Qt::AlignRight);
    dl->addWidget(m_columns, row, 3);
    ++row;
    m_landscape = new QCheckBox(tr("Landscape"), doc);
    m_linesFirst = new QSpinBox(doc);
    m_linesFirst->setRange(0, 200);
    m_linesFirst->setSpecialValueText(tr("auto"));
    dl->addWidget(m_landscape, row, 1);
    dl->addWidget(new QLabel(tr("Number of lines on 1st page :"), doc), row, 2, Qt::AlignRight);
    dl->addWidget(m_linesFirst, row, 3);
    ++row;

    auto* fieldsRow = new QHBoxLayout;
    auto* tools = new QVBoxLayout;
    auto tool = [this](const QString& icon) {
        auto* b = new QToolButton(this);
        b->setIcon(UiHelpers::icon(icon));
        b->setIconSize(QSize(22, 22));
        return b;
    };
    auto* add = tool(QStringLiteral("add"));
    auto* del = tool(QStringLiteral("remove"));
    auto* up = tool(QStringLiteral("up"));
    auto* down = tool(QStringLiteral("down"));
    tools->addWidget(add);
    tools->addWidget(del);
    tools->addSpacing(8);
    tools->addWidget(up);
    tools->addWidget(down);
    tools->addStretch();
    fieldsRow->addLayout(tools);
    m_fields = new QTableWidget(doc);
    m_fields->setColumnCount(6);
    m_fields->setHorizontalHeaderLabels({ tr("Fields"), tr("2nd line"), tr("Alignment"), tr("Width"), tr("Bold"), tr("Title") });
    m_fields->horizontalHeaderItem(1)->setToolTip(tr("Field printed under the first one in the same cell (e.g. the club under the name)"));
    m_fields->horizontalHeaderItem(5)->setToolTip(tr("Title of the column, empty = the name of the field"));
    UiHelpers::styleGrid(m_fields);
    m_fields->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_fields->setSelectionMode(QAbstractItemView::SingleSelection);
    m_fields->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    m_fields->setColumnWidth(0, 170);
    m_fields->setColumnWidth(1, 150);
    m_fields->setColumnWidth(2, 90);
    m_fields->setColumnWidth(3, 60);
    m_fields->setColumnWidth(4, 45);
    fieldsRow->addWidget(m_fields, 1);
    dl->addLayout(fieldsRow, row, 0, 1, 4);
    dl->setRowStretch(row, 1);
    ++row;
    m_tableWidth = new QSpinBox(doc);
    m_tableWidth->setRange(30, 100);
    m_tableWidth->setSuffix(QStringLiteral(" %"));
    m_tableWidth->setToolTip(tr("Width of the table in % of the printable width of the page, the table is centred"));
    dl->addWidget(new QLabel(tr("Table width :"), doc), row, 2, Qt::AlignRight);
    dl->addWidget(m_tableWidth, row, 3);
    ++row;
    m_notes = new QPlainTextEdit(doc);
    m_notes->setMaximumHeight(60);
    dl->addWidget(new QLabel(tr("Notes"), doc), row, 0, Qt::AlignTop);
    dl->addWidget(m_notes, row, 1, 1, 3);
    ++row;
    auto* sig = new QGroupBox(tr("Signatures"), doc);
    auto* sl = new QGridLayout(sig);
    const QStringList pos = { tr("Left"), tr("Center"), tr("Right") };
    for (int i = 0; i < 3; ++i) {
        sl->addWidget(new QLabel(pos.at(i), sig), 0, i, Qt::AlignCenter);
        m_signatures[size_t(i)] = new QComboBox(sig);
        m_signatures[size_t(i)]->setEditable(true);
        m_signatures[size_t(i)]->addItems(signatureRoles());
        sl->addWidget(m_signatures[size_t(i)], 1, i);
    }
    dl->addWidget(sig, row, 0, 1, 4);
    left->addWidget(doc, 1);

    auto* options = new QGroupBox(tr("Options"), this);
    auto* ol = new QVBoxLayout(options);
    m_notClassified = new QCheckBox(tr("Print those not classified"), options);
    ol->addWidget(m_notClassified);
    m_notClassified->setVisible(type == ReportType::RunRanking || type == ReportType::RankingAfterRun
                                || type == ReportType::FinalRanking || type == ReportType::FinalRankingPoints);
    auto* groupsRow = new QHBoxLayout;
    m_byGroups = new QCheckBox(options);
    auto* groupsBtn = UiHelpers::button(QStringLiteral("filter"), tr("Output groups..."), options);
    auto groupsText = [this] {
        m_byGroups->setText(tr("Print by output groups (%1)").arg(m_ev->params().outputGroups.size()));
    };
    groupsText();
    groupsRow->addWidget(m_byGroups);
    groupsRow->addStretch();
    groupsRow->addWidget(groupsBtn);
    ol->addLayout(groupsRow);
    options->setVisible(reportSupportsGroups(type));
    connect(m_byGroups, &QCheckBox::toggled, this, &ReportDialog::updateInfo);
    connect(groupsBtn, &QPushButton::clicked, this, [this, groupsText] {
        OutputGroupsDialog dlg(m_ev, this);
        if (dlg.exec() == QDialog::Accepted) {
            groupsText();
            m_byGroups->setChecked(!m_ev->params().outputGroups.isEmpty());
            updateInfo();
        }
    });
    left->addWidget(options);

    auto* loadSave = new QHBoxLayout;
    // like SkiPro: the layout of the document from a file, another event or the default one
    auto* loadBtn = UiHelpers::button(QStringLiteral("folder"), tr("Load"), this);
    auto* loadMenu = new QMenu(loadBtn);
    loadMenu->addAction(tr("Load from a File..."), this, &ReportDialog::loadFromFile);
    loadMenu->addAction(tr("Load from an Event..."), this, &ReportDialog::loadFromEvent);
    loadMenu->addSeparator();
    loadMenu->addAction(tr("Default format"), this, &ReportDialog::resetDefaults);
    loadBtn->setMenu(loadMenu);
    auto* saveBtn = UiHelpers::button(QStringLiteral("save"), tr("Save"), this);
    auto* saveMenu = new QMenu(saveBtn);
    saveMenu->addAction(tr("Save in this Event"), this, &ReportDialog::save);
    saveMenu->addAction(tr("Save to a File..."), this, &ReportDialog::saveToFile);
    saveBtn->setMenu(saveMenu);
    loadBtn->setMinimumWidth(110);
    saveBtn->setMinimumWidth(110);
    loadSave->addWidget(loadBtn);
    loadSave->addWidget(saveBtn);
    loadSave->addStretch();
    left->addLayout(loadSave);
    lay->addLayout(left, 1);

    auto* right = new QVBoxLayout;
    auto* close = UiHelpers::button(QStringLiteral("close"), tr("Close"), this);
    m_info = UiHelpers::infoBox(this);
    m_info->setWordWrap(true);
    auto* prev = UiHelpers::button(QStringLiteral("preview"), tr("Preview (F1)"), this);
    auto* printBtn = UiHelpers::button(QStringLiteral("print"), tr("Print (F2)"), this);
    auto* exportBtn = UiHelpers::button(QStringLiteral("export"), tr("Export"), this);
    for (auto* w : { static_cast<QWidget*>(close), static_cast<QWidget*>(m_info), static_cast<QWidget*>(prev),
                     static_cast<QWidget*>(printBtn), static_cast<QWidget*>(exportBtn) }) {
        w->setMinimumWidth(180);
        right->addWidget(w);
    }
    right->addStretch();
    lay->addLayout(right);

    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    connect(prev, &QPushButton::clicked, this, &ReportDialog::preview);
    connect(printBtn, &QPushButton::clicked, this, &ReportDialog::print);
    connect(exportBtn, &QPushButton::clicked, this, &ReportDialog::exportFile);
    connect(add, &QToolButton::clicked, this, &ReportDialog::addField);
    connect(del, &QToolButton::clicked, this, &ReportDialog::removeField);
    connect(up, &QToolButton::clicked, this, [this] { moveField(-1); });
    connect(down, &QToolButton::clicked, this, [this] { moveField(1); });
    if (m_run)
        connect(m_run, &QComboBox::currentIndexChanged, this, [this] {
            // each run has its own layout: the shown one is kept, the one of the new run is loaded
            if (ReportFormat::layoutPerRun(m_type) && m_formatRun != this->run()) {
                save();
                loadStored();
            }
            updateInfo();
        });
    connect(new QShortcut(QKeySequence(Qt::Key_F1), this), &QShortcut::activated, this, &ReportDialog::preview);
    connect(new QShortcut(QKeySequence(Qt::Key_F2), this), &QShortcut::activated, this, &ReportDialog::print);

    loadStored();
    resize(1000, 740);
}

void ReportDialog::loadStored()
{
    m_formatRun = run();
    QString stored = m_ev->reportFormatData(ReportFormat::storageId(m_type, m_formatRun));
    // a run without its own layout takes the one of the nearest run that has one: the
    // runs before, the layout of an older version (one for all the runs, as run 1), the
    // runs after - not the defaults, the timekeeper's columns are not lost
    int from = m_formatRun;
    if (stored.isEmpty() && ReportFormat::layoutPerRun(m_type)) {
        QList<QPair<int, QString>> candidates;
        for (int r = m_formatRun - 1; r >= 1; --r)
            candidates << qMakePair(r, ReportFormat::storageId(m_type, r));
        candidates << qMakePair(1, ReportFormat::storageId(m_type));
        for (int r = m_formatRun + 1; r <= m_ev->runCount(); ++r)
            candidates << qMakePair(r, ReportFormat::storageId(m_type, r));
        for (const auto& c : candidates) {
            stored = m_ev->reportFormatData(c.second);
            if (!stored.isEmpty()) {
                from = c.first;
                break;
            }
        }
    }
    ReportFormat f = stored.isEmpty() ? ReportFormat::defaults(m_type, m_formatRun)
                                      : ReportFormat::forRun(ReportFormat::fromString(stored, m_type), m_type, from, m_formatRun);
    // final ranking: one column per run of the event by default
    if (stored.isEmpty() && (m_type == ReportType::FinalRanking || m_type == ReportType::FinalRankingPoints)
        && m_ev->runCount() != 2) {
        QList<ReportColumn> cols;
        for (const auto& c : f.fields) {
            if (c.field > ReportField::RunTimeRank && c.field <= ReportField::RunTimeRank + kMaxRuns)
                continue;
            if (c.field == ReportField::Time)
                for (int r = 1; r <= m_ev->runCount(); ++r)
                    cols << ReportColumn{ ReportField::RunTimeRank + r, 2, 12 };
            cols << c;
        }
        f.fields = cols;
    }
    setFormat(f);
}

int ReportDialog::run() const
{
    return m_run ? m_run->currentData().toInt() : m_ev->runCount();
}

QComboBox* ReportDialog::fieldCombo(int current, bool withNone)
{
    auto* c = new QComboBox(m_fields);
    if (withNone)
        c->addItem(tr("-"), 0);
    for (const auto& d : availableReportFields(m_type, m_ev->runCount(), m_ev->interCount())) {
        QString label = d.label.isEmpty() ? tr("(dotted line)") : d.label;
        if (d.field > 0 && d.field < 100 && !m_ev->params().fields.labels.value(d.field).isEmpty())
            label = m_ev->params().fields.labels.value(d.field);
        // runs by their names (Event type), what the column contains in the list
        const auto& type = m_ev->params().type;
        if (d.field > ReportField::RunTime && d.field <= ReportField::RunTime + kMaxRuns)
            label = tr("%1 - time").arg(type.runName(d.field - ReportField::RunTime));
        else if (d.field > ReportField::RunTimeRank && d.field <= ReportField::RunTimeRank + kMaxRuns)
            label = tr("%1 - time (rank)").arg(type.runName(d.field - ReportField::RunTimeRank));
        else if (d.field > ReportField::RunRank && d.field <= ReportField::RunRank + kMaxRuns)
            label = tr("%1 - rank").arg(type.runName(d.field - ReportField::RunRank));
        c->addItem(label, d.field);
    }
    int idx = c->findData(current);
    if (idx < 0 && current > 0) {
        // a field no longer offered (e.g. a run removed from the event): keep it
        c->addItem(reportFieldLabel(current, m_type, run()).isEmpty() ? QString::number(current)
                                                                      : reportFieldLabel(current, m_type, run()),
                   current);
        idx = c->count() - 1;
    }
    c->setCurrentIndex(qMax(0, idx));
    connect(c, &QComboBox::currentIndexChanged, this, &ReportDialog::updateInfo);
    return c;
}

void ReportDialog::addFieldRow(const ReportColumn& c, int at)
{
    const int r = at < 0 ? m_fields->rowCount() : at;
    m_fields->insertRow(r);
    m_fields->setCellWidget(r, 0, fieldCombo(c.field, false));
    m_fields->setCellWidget(r, 1, fieldCombo(c.field2, true));
    auto* align = new QComboBox(m_fields);
    align->addItems({ tr("Left"), tr("Center"), tr("Right") });
    align->setCurrentIndex(qBound(0, c.align, 2));
    m_fields->setCellWidget(r, 2, align);
    auto* w = new QSpinBox(m_fields);
    w->setRange(1, 99);
    w->setValue(c.width);
    m_fields->setCellWidget(r, 3, w);
    auto* bold = new QTableWidgetItem;
    bold->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
    bold->setCheckState(c.bold ? Qt::Checked : Qt::Unchecked);
    m_fields->setItem(r, 4, bold);
    m_fields->setItem(r, 5, new QTableWidgetItem(c.title));
}

ReportColumn ReportDialog::fieldRow(int r) const
{
    ReportColumn c;
    c.field = static_cast<QComboBox*>(m_fields->cellWidget(r, 0))->currentData().toInt();
    c.field2 = static_cast<QComboBox*>(m_fields->cellWidget(r, 1))->currentData().toInt();
    c.align = static_cast<QComboBox*>(m_fields->cellWidget(r, 2))->currentIndex();
    c.width = static_cast<QSpinBox*>(m_fields->cellWidget(r, 3))->value();
    c.bold = m_fields->item(r, 4) && m_fields->item(r, 4)->checkState() == Qt::Checked;
    c.title = m_fields->item(r, 5) ? m_fields->item(r, 5)->text().trimmed() : QString();
    return c;
}

void ReportDialog::setFormat(const ReportFormat& f)
{
    m_title->setPlainText(f.title);
    m_printHeader->setChecked(f.printHeader);
    m_columns->setValue(f.columns);
    m_linesFirst->setValue(f.linesFirstPage);
    m_landscape->setChecked(f.landscape);
    m_notes->setPlainText(f.notes);
    m_notClassified->setChecked(f.printNotClassified);
    m_byGroups->setChecked(f.byGroups);
    m_tableWidth->setValue(f.tableWidth);
    for (int i = 0; i < 3; ++i)
        m_signatures[size_t(i)]->setCurrentText(f.signatures[size_t(i)]);
    m_fields->setRowCount(0);
    for (const auto& c : f.fields)
        addFieldRow(c);
    updateInfo();
}

ReportFormat ReportDialog::format() const
{
    ReportFormat f = ReportFormat::defaults(m_type);
    f.title = m_title->toPlainText().trimmed();
    f.printHeader = m_printHeader->isChecked();
    f.columns = m_columns->value();
    f.linesFirstPage = m_linesFirst->value();
    f.landscape = m_landscape->isChecked();
    f.notes = m_notes->toPlainText();
    f.printNotClassified = m_notClassified->isChecked();
    f.byGroups = m_byGroups->isChecked();
    f.tableWidth = m_tableWidth->value();
    for (int i = 0; i < 3; ++i)
        f.signatures[size_t(i)] = m_signatures[size_t(i)]->currentText().trimmed();
    f.fields.clear();
    for (int r = 0; r < m_fields->rowCount(); ++r)
        f.fields << fieldRow(r);
    return f;
}

void ReportDialog::addField()
{
    const QList<ReportFieldDef> defs = availableReportFields(m_type, m_ev->runCount(), m_ev->interCount());
    QStringList names;
    for (const auto& d : defs)
        names << (d.label.isEmpty() ? tr("(dotted line)") : d.label);
    bool ok = false;
    const QString sel = QInputDialog::getItem(this, tr("Add a field"), tr("Field :"), names, 0, false, &ok);
    if (!ok)
        return;
    const ReportFieldDef& d = defs.at(names.indexOf(sel));
    const int at = m_fields->currentRow() >= 0 ? m_fields->currentRow() + 1 : m_fields->rowCount();
    addFieldRow(ReportColumn{ d.field, d.defaultAlign, d.defaultWidth }, at);
    m_fields->selectRow(at);
    updateInfo();
}

void ReportDialog::removeField()
{
    if (m_fields->currentRow() >= 0)
        m_fields->removeRow(m_fields->currentRow());
    updateInfo();
}

void ReportDialog::moveField(int delta)
{
    const int r = m_fields->currentRow();
    const int t = r + delta;
    if (r < 0 || t < 0 || t >= m_fields->rowCount())
        return;
    const ReportColumn c = fieldRow(r);
    m_fields->removeRow(r);
    addFieldRow(c, t);
    m_fields->selectRow(t);
}

void ReportDialog::updateInfo()
{
    const ReportDocument doc = ReportBuilder::build(*m_ev, format(), run());
    m_info->setText(doc.infoText);
}

void ReportDialog::save()
{
    m_ev->setReportFormatData(ReportFormat::storageId(m_type, m_formatRun), format().toString());
}

// File of a report layout: {"report": "<storage id>", "format": {...}}
void ReportDialog::saveToFile()
{
    const QString dir = AppSettings::value(QStringLiteral("folders/reportFormats"), AppSettings::exportDir()).toString();
    QDir().mkpath(dir);
    QString file = QFileDialog::getSaveFileName(this, tr("Save to a File"),
                                                QDir(dir).absoluteFilePath(ReportFormat::storageId(m_type) + QStringLiteral(".osprf")),
                                                tr("OpenSkiPro2 report layout (*.osprf)"));
    if (file.isEmpty())
        return;
    if (!file.endsWith(QLatin1String(".osprf"), Qt::CaseInsensitive))
        file += QStringLiteral(".osprf");
    AppSettings::setValue(QStringLiteral("folders/reportFormats"), QFileInfo(file).absolutePath());
    QJsonObject o;
    o[QStringLiteral("report")] = ReportFormat::storageId(m_type);
    o[QStringLiteral("format")] = QJsonDocument::fromJson(format().toString().toUtf8()).object();
    QFile f(file);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(QJsonDocument(o).toJson()) < 0)
        QMessageBox::warning(this, windowTitle(), tr("Cannot write %1: %2").arg(file, f.errorString()));
}

void ReportDialog::loadFromFile()
{
    const QString dir = AppSettings::value(QStringLiteral("folders/reportFormats"), AppSettings::exportDir()).toString();
    const QString file = QFileDialog::getOpenFileName(this, tr("Load from a File"), dir, tr("OpenSkiPro2 report layout (*.osprf)"));
    if (file.isEmpty())
        return;
    AppSettings::setValue(QStringLiteral("folders/reportFormats"), QFileInfo(file).absolutePath());
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, windowTitle(), tr("Cannot read %1: %2").arg(file, f.errorString()));
        return;
    }
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    const QString id = o.value(QStringLiteral("report")).toString();
    if (!o.value(QStringLiteral("format")).isObject()) {
        QMessageBox::warning(this, windowTitle(), tr("%1 is not a report layout.").arg(file));
        return;
    }
    if (id != ReportFormat::storageId(m_type)) {
        // the fields of another document do not fit this one
        QString other = id;
        for (int t = 0; t <= kReportTypeLast; ++t)
            if (ReportFormat::storageId(ReportType(t)) == id)
                other = ReportFormat::typeName(ReportType(t));
        QMessageBox::warning(this, windowTitle(), tr("The file contains the layout of another document: %1.").arg(other));
        return;
    }
    const QString data = QString::fromUtf8(QJsonDocument(o.value(QStringLiteral("format")).toObject()).toJson(QJsonDocument::Compact));
    setFormat(ReportFormat::fromString(data, m_type));
}

void ReportDialog::loadFromEvent()
{
    SelectEventDialog dlg(SelectEventDialog::Mode::Existing, this);
    dlg.setWindowTitle(tr("Load from an Event"));
    if (dlg.exec() != QDialog::Accepted)
        return;
    const QString path = MainWindow::eventPath(dlg.selectedSlot());
    QString data, err;
    {
        Event other;
        if (!other.open(path, &err)) {
            QMessageBox::warning(this, windowTitle(), err);
            return;
        }
        data = other.reportFormatData(ReportFormat::storageId(m_type, run()));
        if (data.isEmpty())
            data = other.reportFormatData(ReportFormat::storageId(m_type));
        other.close();
    }
    if (data.isEmpty()) {
        QMessageBox::information(this, windowTitle(), tr("No layout of this document is saved in the selected event."));
        return;
    }
    setFormat(ReportFormat::fromString(data, m_type));
}

void ReportDialog::resetDefaults()
{
    setFormat(ReportFormat::defaults(m_type, run()));
}

void ReportDialog::done(int r)
{
    save();
    QDialog::done(r);
}

void ReportDialog::preview()
{
    save();
    const ReportDocument doc = ReportBuilder::build(*m_ev, format(), run());
    PreviewDialog dlg(m_ev, doc, this);
    dlg.exec();
}

void ReportDialog::print()
{
    save();
    const ReportDocument doc = ReportBuilder::build(*m_ev, format(), run());
    ReportRenderer renderer(*m_ev, doc);
    QPrinter printer(QPrinter::HighResolution);
    printer.setFullPage(true);
    printer.setDocName(doc.docTitle);
    QPrintDialog dlg(&printer, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    if (!renderer.print(&printer))
        QMessageBox::warning(this, windowTitle(), tr("Printing failed."));
}

void ReportDialog::exportFile()
{
    save();
    const ReportDocument doc = ReportBuilder::build(*m_ev, format(), run());
    QDir().mkpath(AppSettings::exportDir());
    QString base = doc.docTitle;
    base.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]+")), QStringLiteral("_"));
    QString filter;
    const QString file = QFileDialog::getSaveFileName(this, tr("Export"), QDir(AppSettings::exportDir()).absoluteFilePath(base + QStringLiteral(".pdf")),
                                                      tr("PDF (*.pdf);;CSV (*.csv)"), &filter);
    if (file.isEmpty())
        return;
    if (file.endsWith(QLatin1String(".csv"), Qt::CaseInsensitive)) {
        QFile f(file);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            QMessageBox::warning(this, windowTitle(), f.errorString());
            return;
        }
        QTextStream out(&f);
        out.setEncoding(QStringConverter::Utf8);
        out.setGenerateByteOrderMark(true);
        auto quote = [](QString s) {
            s.replace(QLatin1Char('\n'), QStringLiteral(" / ")); // cell with a second line
            if (s.contains(QLatin1Char(';')) || s.contains(QLatin1Char('"')))
                s = QLatin1Char('"') + s.replace(QLatin1Char('"'), QStringLiteral("\"\"")) + QLatin1Char('"');
            return s;
        };
        QStringList head;
        for (const auto& l : doc.columnLabels)
            head << quote(l);
        out << head.join(QLatin1Char(';')) << '\n';
        for (const auto& s : doc.sections) {
            if (!s.heading.isEmpty())
                out << quote(s.heading) << '\n';
            for (int ri = 0; ri < s.rows.size(); ++ri) {
                QStringList cells;
                for (const auto& c : s.rows.at(ri))
                    cells << quote(c);
                if (!s.notes.value(ri).isEmpty())
                    cells << quote(s.notes.value(ri));
                out << cells.join(QLatin1Char(';')) << '\n';
            }
        }
        return;
    }
    ReportRenderer renderer(*m_ev, doc);
    QString err;
    if (!renderer.exportPdf(file, &err))
        QMessageBox::warning(this, windowTitle(), err);
}
