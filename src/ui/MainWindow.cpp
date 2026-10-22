#include "MainWindow.h"
#include "CompetitorsPage.h"
#include "FisCompetitorsWindow.h"
#include "FisListPage.h"
#include "FisUi.h"
#include "HeaderBanner.h"
#include "NavPanel.h"
#include "RunsPage.h"
#include "TimingWindow.h"
#include "UiHelpers.h"
#include "api/ApiServer.h"
#include "app/AppSettings.h"
#include "core/Event.h"
#include "core/FisXml.h"
#include "db/SkiAlpImporter.h"
#include "dialogs/OutputGroupsDialog.h"
#include "db/SkiProImporter.h"
#include "dialogs/AboutDialog.h"
#include "dialogs/ApiSettingsDialog.h"
#include "dialogs/EventTypeDialog.h"
#include "dialogs/FisImportDialog.h"
#include "dialogs/FisPenaltyDialog.h"
#include "dialogs/RefereeReportDialog.h"
#include "dialogs/FisUpdateDialog.h"
#include "dialogs/FoldersDialog.h"
#include "dialogs/GeneralParametersDialog.h"
#include "dialogs/HeaderDialog.h"
#include "dialogs/PrintingParametersDialog.h"
#include "dialogs/ReportDialog.h"
#include "dialogs/SelectEventDialog.h"
#include "dialogs/TimingDeviceDialog.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QLocale>
#include <QMenuBar>
#include <QMessageBox>
#include <QScrollArea>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_event(new Event(this))
{
    setWindowTitle(tr("OpenSkiPro2 (version %1)").arg(QStringLiteral(OSP_VERSION)));
    setWindowIcon(UiHelpers::icon(QStringLiteral("app")));

    createActions();
    createMenus();

    auto* central = new QWidget(this);
    auto* lay = new QVBoxLayout(central);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    m_banner = new HeaderBanner(central);
    connect(m_banner, &HeaderBanner::openEventRequested, this, &MainWindow::openEvent);
    lay->addWidget(m_banner);

    auto* split = new QSplitter(Qt::Horizontal, central);
    m_nav = new NavPanel(split);
    m_nav->addGroup(tr("Parameters"));
    m_nav->addAction(m_actGeneral);
    m_nav->addAction(m_actType, 1);
    m_nav->addAction(m_actPrinting);
    m_nav->addAction(m_actHeader, 1);
    m_nav->addGroup(tr("Competitors"));
    m_nav->addAction(m_actCompetitors);
    m_nav->addAction(m_actCompetitorFields, 1);
    m_nav->addAction(m_actCategories, 1);
    m_nav->addGroup(tr("FIS"));
    m_nav->addAction(m_actFisImport);
    m_nav->addAction(m_actFisDownload);
    m_nav->addAction(m_actFisStatus);
    m_nav->addAction(m_actFisCompetitors);
    m_nav->addAction(m_actFisUpdate);
    m_nav->addAction(m_actFisPenalty);
    m_nav->addAction(m_actFisXml);
    m_nav->addAction(m_actRefereeReport);
    m_nav->addGroup(tr("Timing"));
    m_nav->addAction(m_actRuns);
    m_nav->addGroup(tr("Output"));
    m_nav->addAction(m_actOutputGroups);
    m_nav->addSeparator();
    for (int i = 0; i < m_reportActions.size(); ++i) {
        const ReportType t = m_reportActions.at(i).first;
        if (t == ReportType::StartList || t == ReportType::RankingAfterRun || t == ReportType::TimeRecords)
            m_nav->addSeparator();
        const bool indent = t == ReportType::RunRanking || t == ReportType::FinalRanking || t == ReportType::FinalRankingPoints
            || t == ReportType::PenaltyCalculation;
        m_nav->addAction(m_reportActions.at(i).second, indent ? 1 : 0);
    }
    m_nav->finish();
    auto* navScroll = new QScrollArea(split);
    navScroll->setWidget(m_nav);
    navScroll->setWidgetResizable(true);
    navScroll->setFrameShape(QFrame::NoFrame);
    navScroll->setMinimumWidth(240);
    navScroll->setMaximumWidth(320);

    m_stack = new QStackedWidget(split);
    m_emptyPage = new QWidget(m_stack);
    m_competitorsPage = new CompetitorsPage(m_event, m_stack);
    m_runsPage = new RunsPage(m_event, m_stack);
    m_fisPage = new FisListPage(m_event, m_stack);
    m_stack->addWidget(m_emptyPage);
    m_stack->addWidget(m_competitorsPage);
    m_stack->addWidget(m_runsPage);
    m_stack->addWidget(m_fisPage);
    connect(m_fisPage, &FisListPage::importRequested, this, &MainWindow::importFisList);
    split->addWidget(navScroll);
    split->addWidget(m_stack);
    split->setStretchFactor(1, 1);
    lay->addWidget(split, 1);
    setCentralWidget(central);

    connect(m_runsPage, &RunsPage::timingRequested, this, &MainWindow::openTiming);
    connect(m_event, &Event::paramsChanged, this, &MainWindow::updateState);
    connect(m_event, &Event::opened, this, &MainWindow::updateState);
    connect(m_event, &Event::closed, this, &MainWindow::updateState);

    // API for the other applications (docs/api.md), state in the status bar
    m_api = new ApiServer(m_event, this);
    m_apiState = new QLabel(this);
    statusBar()->addPermanentWidget(m_apiState);
    connect(m_api, &ApiServer::stateChanged, this, [this] {
        m_apiState->setText(m_api->stateText());
        if (m_timing)
            m_timing->setApiState(m_api->stateText());
    });
    m_api->applySettings();
    m_apiState->setText(m_api->stateText());

    const QByteArray geo = AppSettings::value(QStringLiteral("MainWindow/geometry")).toByteArray();
    if (geo.isEmpty() || !restoreGeometry(geo))
        resize(1400, 900);
    updateState();

    const QString last = AppSettings::value(QStringLiteral("general/lastEvent")).toString();
    if (!last.isEmpty() && QFileInfo::exists(last))
        openEventFile(last);
}

MainWindow::~MainWindow() = default;

QString MainWindow::eventPath(int slot)
{
    return QDir(AppSettings::eventsDir()).absoluteFilePath(QStringLiteral("Event%1.%2")
                                                               .arg(slot, 3, 10, QLatin1Char('0'))
                                                               .arg(QLatin1String(Event::kFileSuffix)));
}

void MainWindow::createActions()
{
    auto mk = [this](const QString& text, const QString& icon, auto slot, bool needsEvent) {
        auto* a = new QAction(UiHelpers::icon(icon), text, this);
        connect(a, &QAction::triggered, this, slot);
        if (needsEvent)
            m_eventActions << a;
        return a;
    };
    m_actOpen = mk(tr("Open an Event"), QStringLiteral("folder"), &MainWindow::openEvent, false);
    m_actOpen->setShortcut(QKeySequence::Open);
    m_actExitEvent = mk(tr("Exit Event"), QStringLiteral("close"), &MainWindow::exitEvent, true);
    m_actDuplicate = mk(tr("Duplicate Event"), QStringLiteral("new"), &MainWindow::duplicateEvent, true);
    m_actSaveCopy = mk(tr("Save Event"), QStringLiteral("save"), &MainWindow::saveEventCopy, true);
    m_actRestore = mk(tr("Restore an Event"), QStringLiteral("folder"), &MainWindow::restoreEvent, false);
    m_actDelete = mk(tr("Delete Event"), QStringLiteral("delete"), &MainWindow::deleteEvent, false);
    m_actImportSkiPro = mk(tr("Import a SkiPro Event..."), QStringLiteral("import"), [this] { importEvent(false); }, false);
    m_actImportSkiAlp = mk(tr("Import a SkiAlp Pro 11 Event..."), QStringLiteral("import"), [this] { importEvent(true); }, false);
    m_actQuit = mk(tr("Exit Program"), QStringLiteral("close"), &QWidget::close, false);

    m_actGeneral = mk(tr("General Parameters"), QStringLiteral("params"), &MainWindow::editGeneralParameters, true);
    m_actType = mk(tr("Event type"), QStringLiteral("params"), &MainWindow::editEventType, true);
    m_actPrinting = mk(tr("Printing Parameters"), QStringLiteral("params"), &MainWindow::editPrintingParameters, true);
    m_actHeader = mk(tr("Header"), QStringLiteral("params"), &MainWindow::editHeader, true);
    m_actCompetitors = mk(tr("Manage Competitors"), QStringLiteral("competitor"), [this] { showPage(m_competitorsPage); }, true);
    m_actRuns = mk(tr("Manage Runs"), QStringLiteral("stopwatch"), [this] { showPage(m_runsPage); }, true);
    // competitor fields and categories: in the navigation, under the competitors
    m_actCompetitorFields = mk(tr("Competitor Fields"), QStringLiteral("modify"), [this] {
        showPage(m_competitorsPage);
        m_competitorsPage->editFields();
    }, true);
    m_actCategories = mk(tr("Categories"), QStringLiteral("competitor"), [this] {
        showPage(m_competitorsPage);
        m_competitorsPage->editCategories();
    }, true);
    m_actOutputGroups = mk(tr("Output groups"), QStringLiteral("filter"), [this] {
        OutputGroupsDialog dlg(m_event, this);
        dlg.exec();
    }, true);

    const QList<ReportType> reports = { ReportType::Competitors, ReportType::CompetitorsPoints, ReportType::StartList,
                                        ReportType::RunRanking, ReportType::RankingAfterRun, ReportType::FinalRanking,
                                        ReportType::FinalRankingPoints, ReportType::PenaltyCalculation,
                                        ReportType::TimeRecords, ReportType::TimingPaper };
    for (ReportType t : reports) {
        auto* a = mk(ReportFormat::typeName(t), QStringLiteral("printer"), [this, t] { openReport(t); }, true);
        m_reportActions << qMakePair(t, a);
    }

    m_actFisImport = mk(tr("Import FIS list"), QStringLiteral("import"), &MainWindow::importFisList, false);
    m_actFisDownload = mk(tr("Download FIS list"), QStringLiteral("arrow_down"), [] {}, false);
    m_actFisDownload->setEnabled(false);
    m_actFisDownload->setToolTip(tr("Not available yet"));
    m_actFisStatus = mk(tr("List status"), QStringLiteral("info"), &MainWindow::showFisStatus, false);
    m_actFisCompetitors = mk(tr("Import Competitors"), QStringLiteral("competitor"), &MainWindow::importFisCompetitors, true);
    m_actFisUpdate = mk(tr("Update Competitors"), QStringLiteral("check"), &MainWindow::updateFisCompetitors, true);
    m_actFisPenalty = mk(tr("FIS points / Penalty"), QStringLiteral("calculator"), &MainWindow::editFisPenalty, true);
    m_actFisXml = mk(tr("FIS XML export"), QStringLiteral("export"), &MainWindow::exportFisXml, true);
    m_actRefereeReport = mk(tr("Referee Report"), QStringLiteral("printer"), [this] {
        RefereeReportDialog dlg(m_event, m_runsPage->currentRun(), this);
        dlg.exec();
    }, true);

    m_actFolders = mk(tr("Folders"), QStringLiteral("folder"), &MainWindow::editFolders, false);
    m_actDevice1 = mk(tr("Timing Device 1"), QStringLiteral("connect"), [this] { editTimingDevice(1); }, false);
    m_actDevice2 = mk(tr("Timing Device 2"), QStringLiteral("connect"), [this] { editTimingDevice(2); }, false);
    m_actApi = mk(tr("API Server"), QStringLiteral("connect"), &MainWindow::editApiServer, false);
    m_actAbout = mk(tr("About..."), QStringLiteral("info"), &MainWindow::about, false);
}

void MainWindow::createMenus()
{
    QMenu* files = menuBar()->addMenu(tr("Files"));
    files->addAction(m_actOpen);
    files->addAction(m_actExitEvent);
    QMenu* manage = files->addMenu(tr("Manage Events"));
    manage->addAction(m_actDuplicate);
    manage->addSeparator();
    manage->addAction(m_actSaveCopy);
    manage->addAction(m_actRestore);
    manage->addAction(m_actDelete);
    manage->addSeparator();
    manage->addAction(m_actImportSkiPro);
    manage->addAction(m_actImportSkiAlp);
    files->addSeparator();
    files->addAction(m_actQuit);

    QMenu* ev = menuBar()->addMenu(tr("Event"));
    ev->addAction(m_actGeneral);
    ev->addAction(m_actType);
    ev->addAction(m_actPrinting);
    ev->addAction(m_actHeader);
    ev->addSeparator();
    ev->addAction(m_actCompetitors);
    ev->addAction(m_actCompetitorFields);
    ev->addAction(m_actCategories);
    ev->addAction(m_actRuns);
    ev->addSeparator();
    QMenu* edit = ev->addMenu(tr("Output"));
    edit->addAction(m_actOutputGroups);
    edit->addSeparator();
    for (const auto& p : m_reportActions) {
        if (p.first == ReportType::StartList || p.first == ReportType::RankingAfterRun || p.first == ReportType::TimeRecords)
            edit->addSeparator();
        edit->addAction(p.second);
    }

    QMenu* fis = menuBar()->addMenu(tr("FIS"));
    fis->addAction(m_actFisImport);
    fis->addAction(m_actFisDownload);
    fis->addAction(m_actFisStatus);
    fis->addSeparator();
    fis->addAction(m_actFisCompetitors);
    fis->addAction(m_actFisUpdate);
    fis->addSeparator();
    fis->addAction(m_actFisPenalty);
    fis->addAction(m_actFisXml);
    fis->addAction(m_actRefereeReport);

    QMenu* cfg = menuBar()->addMenu(tr("Local configuration"));
    cfg->addAction(m_actFolders);
    cfg->addSeparator();
    cfg->addAction(m_actDevice1);
    cfg->addAction(m_actDevice2);
    cfg->addAction(m_actApi);
    cfg->addSeparator();
    // languages: English is built in, further ones are found as translations in the resources
    auto* group = new QActionGroup(this);
    QList<QPair<QString, QString>> langs = { { QStringLiteral("en"), QStringLiteral("English") } };
    QDirIterator it(QStringLiteral(":/i18n"), { QStringLiteral("openskipro2_*.qm") });
    while (it.hasNext()) {
        const QString code = QFileInfo(it.next()).completeBaseName().mid(12);
        if (code != QLatin1String("en"))
            langs << qMakePair(code, QLocale(code).nativeLanguageName());
    }
    const QString current = AppSettings::language();
    for (const auto& l : langs) {
        QAction* a = cfg->addAction(l.second);
        a->setCheckable(true);
        a->setChecked(l.first == current);
        group->addAction(a);
        const QString code = l.first;
        connect(a, &QAction::triggered, this, [this, code] { selectLanguage(code); });
    }

    QMenu* help = menuBar()->addMenu(tr("Help"));
    help->addAction(m_actAbout);
}

void MainWindow::updateState()
{
    const bool open = m_event->isOpen();
    for (QAction* a : m_eventActions)
        a->setEnabled(open);
    if (open) {
        m_banner->setTitles(m_event->params().general.title1, m_event->params().general.title2);
        setWindowTitle(tr("OpenSkiPro2 (version %1) - %2")
                           .arg(QStringLiteral(OSP_VERSION), QFileInfo(m_event->path()).completeBaseName()));
    } else {
        m_banner->setTitles(tr("[No event]"), QString());
        setWindowTitle(tr("OpenSkiPro2 (version %1)").arg(QStringLiteral(OSP_VERSION)));
        // the FIS list status does not need an event
        if (m_stack->currentWidget() != m_fisPage)
            m_stack->setCurrentWidget(m_emptyPage);
    }
}

void MainWindow::showRunsPage()
{
    showPage(m_runsPage);
}

void MainWindow::showPage(QWidget* page)
{
    m_stack->setCurrentWidget(page);
}

bool MainWindow::openEventFile(const QString& path)
{
    if (m_timing)
        m_timing->close();
    QString err;
    if (!m_event->open(path, &err)) {
        QMessageBox::warning(this, tr("Open an Event"), tr("Cannot open the event:\n%1").arg(err));
        return false;
    }
    AppSettings::setValue(QStringLiteral("general/lastEvent"), path);
    showPage(m_competitorsPage);
    return true;
}

void MainWindow::openEvent()
{
    SelectEventDialog dlg(SelectEventDialog::Mode::Open, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const QString path = eventPath(dlg.selectedSlot());
    if (!QFileInfo::exists(path)) {
        if (QMessageBox::question(this, tr("Open an Event"), tr("This Event does not exist. Do you want to create it?"))
            != QMessageBox::Yes)
            return;
        QString err;
        if (!m_event->create(path, &err)) {
            QMessageBox::warning(this, tr("Open an Event"), err);
            return;
        }
        AppSettings::setValue(QStringLiteral("general/lastEvent"), path);
        showPage(m_competitorsPage);
        editGeneralParameters();
        return;
    }
    openEventFile(path);
}

void MainWindow::exitEvent()
{
    if (m_timing)
        m_timing->close();
    m_event->close();
    AppSettings::setValue(QStringLiteral("general/lastEvent"), QString());
}

void MainWindow::duplicateEvent()
{
    if (!m_event->isOpen())
        return;
    SelectEventDialog dlg(SelectEventDialog::Mode::FreeSlot, this);
    dlg.setWindowTitle(tr("Duplicate Event"));
    if (dlg.exec() != QDialog::Accepted)
        return;
    const QString target = eventPath(dlg.selectedSlot());
    if (!QFile::copy(m_event->path(), target)) {
        QMessageBox::warning(this, tr("Duplicate Event"), tr("Cannot copy the event to %1").arg(target));
        return;
    }
    QMessageBox::information(this, tr("Duplicate Event"), tr("The event was duplicated to slot %1.").arg(dlg.selectedSlot()));
}

void MainWindow::saveEventCopy()
{
    if (!m_event->isOpen())
        return;
    const QString def = QDir(AppSettings::exportDir()).absoluteFilePath(
        QStringLiteral("%1_%2.osp").arg(QFileInfo(m_event->path()).completeBaseName(),
                                        QDate::currentDate().toString(QStringLiteral("yyyyMMdd"))));
    QDir().mkpath(AppSettings::exportDir());
    const QString file = QFileDialog::getSaveFileName(this, tr("Save Event"), def, tr("OpenSkiPro2 event (*.osp)"));
    if (file.isEmpty())
        return;
    QFile::remove(file);
    if (!QFile::copy(m_event->path(), file))
        QMessageBox::warning(this, tr("Save Event"), tr("Cannot write %1").arg(file));
}

void MainWindow::restoreEvent()
{
    const QString file = QFileDialog::getOpenFileName(this, tr("Restore an Event"), AppSettings::exportDir(),
                                                      tr("OpenSkiPro2 event (*.osp)"));
    if (file.isEmpty())
        return;
    SelectEventDialog dlg(SelectEventDialog::Mode::AnySlot, this);
    dlg.setWindowTitle(tr("Location Selection"));
    if (dlg.exec() != QDialog::Accepted)
        return;
    const QString target = eventPath(dlg.selectedSlot());
    if (QFileInfo::exists(target)) {
        if (QMessageBox::question(this, tr("Restore an Event"), tr("This location is already used. Replace the event?"))
            != QMessageBox::Yes)
            return;
        if (m_event->path() == target)
            exitEvent();
        QFile::remove(target);
    }
    if (!QFile::copy(file, target)) {
        QMessageBox::warning(this, tr("Restore an Event"), tr("Cannot copy %1").arg(file));
        return;
    }
    QFile(target).setPermissions(QFile(target).permissions() | QFileDevice::WriteOwner);
    openEventFile(target);
}

void MainWindow::deleteEvent()
{
    SelectEventDialog dlg(SelectEventDialog::Mode::Existing, this);
    dlg.setWindowTitle(tr("Delete Event"));
    if (dlg.exec() != QDialog::Accepted)
        return;
    const QString path = eventPath(dlg.selectedSlot());
    if (QMessageBox::question(this, tr("Delete Event"), tr("Are you sure you want to delete this event?")) != QMessageBox::Yes)
        return;
    if (m_event->path() == path)
        exitEvent();
    if (!QFile::remove(path))
        QMessageBox::warning(this, tr("Delete Event"), tr("Cannot delete %1").arg(path));
}

void MainWindow::importEvent(bool skiAlp)
{
    const QString title = skiAlp ? tr("Import a SkiAlp Pro 11 Event") : tr("Import a SkiPro Event");
    const QString folderKey = skiAlp ? QStringLiteral("folders/skialp") : QStringLiteral("folders/skipro");
    const QString file = QFileDialog::getOpenFileName(this, title, AppSettings::value(folderKey).toString(),
                                                      skiAlp ? tr("SkiAlp Pro event (*.spro)") : tr("SkiPro event (Event*.scdb)"));
    if (file.isEmpty())
        return;
    AppSettings::setValue(folderKey, QFileInfo(file).absolutePath());
    SkiProImporter::Result data;
    QString err;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = skiAlp ? SkiAlpImporter::read(file, data, &err, AppSettings::logosDir())
                           : SkiProImporter::read(file, data, &err, AppSettings::logosDir());
    QApplication::restoreOverrideCursor();
    if (!ok) {
        QMessageBox::warning(this, title, err);
        return;
    }
    SelectEventDialog dlg(SelectEventDialog::Mode::FreeSlot, this);
    dlg.setWindowTitle(tr("Location Selection"));
    if (dlg.exec() != QDialog::Accepted)
        return;
    const QString target = eventPath(dlg.selectedSlot());
    if (m_timing)
        m_timing->close();
    QApplication::setOverrideCursor(Qt::WaitCursor);
    bool done = m_event->create(target, &err) && SkiProImporter::apply(data, *m_event, &err);
    QApplication::restoreOverrideCursor();
    if (!done) {
        QMessageBox::warning(this, title, err);
        return;
    }
    AppSettings::setValue(QStringLiteral("general/lastEvent"), target);
    showPage(m_competitorsPage);
    QString msg = tr("The event was imported: %1 competitors, %2 runs.").arg(data.competitors.size()).arg(data.runs.size());
    if (!data.warnings.isEmpty())
        msg += QStringLiteral("\n\n") + data.warnings.join(QLatin1Char('\n'));
    QMessageBox::information(this, title, msg);
}

void MainWindow::editGeneralParameters()
{
    GeneralParametersDialog dlg(m_event->params(), this);
    if (dlg.exec() == QDialog::Accepted) {
        EventParams p = m_event->params();
        dlg.apply(p);
        m_event->setParams(p);
    }
}

void MainWindow::editEventType()
{
    EventTypeDialog dlg(m_event->params(), this);
    if (dlg.exec() == QDialog::Accepted) {
        EventParams p = m_event->params();
        dlg.apply(p);
        m_event->setParams(p);
    }
}

void MainWindow::editPrintingParameters()
{
    PrintingParametersDialog dlg(m_event, this);
    if (dlg.exec() == QDialog::Accepted) {
        EventParams p = m_event->params();
        dlg.apply(p);
        m_event->setParams(p);
    }
}

void MainWindow::editHeader()
{
    HeaderDialog dlg(m_event->params(), this);
    if (dlg.exec() == QDialog::Accepted) {
        EventParams p = m_event->params();
        dlg.apply(p);
        m_event->setParams(p);
    }
}

void MainWindow::openReport(ReportType type)
{
    int run = m_runsPage->currentRun();
    ReportDialog dlg(m_event, type, run, this);
    dlg.exec();
}

void MainWindow::importFisList()
{
    FisImportDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    // the window of the competitors shows the previous list
    if (m_fisWindow)
        m_fisWindow->close();
    showFisStatus();
    QMessageBox::information(this, tr("Import FIS list"),
                             tr("The FIS list was imported: %n competitor(s).", nullptr, dlg.importedCount()));
}

void MainWindow::showFisStatus()
{
    m_fisPage->reload();
    showPage(m_fisPage);
}

void MainWindow::importFisCompetitors()
{
    if (!m_event->isOpen())
        return;
    if (m_fisWindow) {
        if (m_fisWindow->isMinimized())
            m_fisWindow->showNormal();
        m_fisWindow->raise();
        m_fisWindow->activateWindow();
        return;
    }
    const QString title = tr("Import Competitors");
    FisList list;
    if (!FisUi::loadList(this, title, list, true) || !FisUi::confirmValidity(this, title, list.header))
        return;
    showPage(m_competitorsPage);
    m_fisWindow = new FisCompetitorsWindow(m_event, list, this);
    m_fisWindow->setAttribute(Qt::WA_DeleteOnClose);
    m_fisWindow->show();
}

void MainWindow::updateFisCompetitors()
{
    if (!m_event->isOpen())
        return;
    const QString title = tr("Update Competitors");
    if (m_event->competitors().isEmpty()) {
        QMessageBox::information(this, title, tr("There are no competitors in the event."));
        return;
    }
    FisList list;
    if (!FisUi::loadList(this, title, list, true) || !FisUi::confirmValidity(this, title, list.header))
        return;
    showPage(m_competitorsPage);
    FisUpdateDialog dlg(m_event, list, this);
    dlg.exec();
}

void MainWindow::editFisPenalty()
{
    if (!m_event->isOpen())
        return;
    FisPenaltyDialog dlg(m_event, this);
    if (dlg.exec() == QDialog::Accepted && dlg.reportWanted())
        openReport(ReportType::PenaltyCalculation);
}

// FIS XML result file of the official results (FIS Alpine Data Booklet 17)
void MainWindow::exportFisXml()
{
    if (!m_event->isOpen())
        return;
    const QString title = tr("FIS XML export");
    const QString bullet = QStringLiteral("\n- ");
    const QStringList errors = FisXml::check(*m_event);
    if (!errors.isEmpty()) {
        QMessageBox::warning(this, title,
                             tr("The FIS XML file cannot be created:") + QLatin1Char('\n') + bullet + errors.join(bullet));
        return;
    }
    const QStringList warnings = FisXml::warnings(*m_event);
    if (!warnings.isEmpty()
        && QMessageBox::question(this, title,
                                 tr("Please check:") + QLatin1Char('\n') + bullet + warnings.join(bullet)
                                     + QStringLiteral("\n\n") + tr("Create the file anyway?"))
            != QMessageBox::Yes)
        return;
    QDir().mkpath(AppSettings::exportDir());
    QString file = QFileDialog::getSaveFileName(this, title,
                                                QDir(AppSettings::exportDir()).absoluteFilePath(FisXml::fileName(*m_event)),
                                                tr("FIS XML (*.xml)"));
    if (file.isEmpty())
        return;
    if (!file.endsWith(QLatin1String(".xml"), Qt::CaseInsensitive))
        file += QStringLiteral(".xml");
    QFile f(file);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(FisXml::build(*m_event)) < 0) {
        QMessageBox::warning(this, title, tr("Cannot write %1: %2").arg(file, f.errorString()));
        return;
    }
    QMessageBox::information(this, title,
                             tr("The FIS XML file was created:\n%1\n\nSend it to FIS (results@fisski.com) "
                                "or submit it in the FIS Member Section.")
                                 .arg(QDir::toNativeSeparators(file)));
}

void MainWindow::editFolders()
{
    FoldersDialog dlg(this);
    dlg.exec();
}

void MainWindow::editTimingDevice(int slot)
{
    TimingDeviceDialog dlg(slot, AppSettings::timingDevice(slot), this);
    if (dlg.exec() == QDialog::Accepted) {
        AppSettings::setTimingDevice(slot, dlg.config());
        if (m_timing)
            m_timing->reloadDeviceConfig();
    }
}

void MainWindow::editApiServer()
{
    ApiSettingsDialog dlg(m_api, this);
    dlg.exec();
}

void MainWindow::selectLanguage(const QString& lang)
{
    if (lang == AppSettings::language())
        return;
    AppSettings::setLanguage(lang);
    QMessageBox::information(this, tr("Language"), tr("The language will be changed after restarting OpenSkiPro2."));
}

void MainWindow::about()
{
    AboutDialog dlg(this);
    dlg.exec();
}

void MainWindow::openTiming(int run)
{
    if (!m_event->isOpen())
        return;
    if (m_timing) {
        if (m_timing->run() != run) {
            QMessageBox::information(this, tr("Timing"), tr("The timing of run %1 is already open.").arg(m_timing->run()));
        }
        if (m_timing->isMinimized())
            m_timing->showNormal();
        m_timing->raise();
        m_timing->activateWindow();
        return;
    }
    m_timing = new TimingWindow(m_event, run, nullptr);
    m_timing->setAttribute(Qt::WA_DeleteOnClose);
    // the event cannot be changed while timing
    m_timing->setWindowModality(Qt::ApplicationModal);
    connect(m_event, &Event::closed, m_timing, &QWidget::close);
    // the main window is minimized while timing and comes back when the timing is closed
    connect(m_timing, &QObject::destroyed, this, [this] {
        setWindowState(windowState() & ~Qt::WindowMinimized);
        show();
        raise();
        activateWindow();
    });
    // the API publishes the live timing while the window is open (detached when it is destroyed)
    m_api->attachTiming(m_timing->engine());
    m_api->setDevices(m_timing->deviceStates());
    connect(m_timing, &TimingWindow::devicesChanged, m_api, [this] {
        if (m_timing)
            m_api->setDevices(m_timing->deviceStates());
    });
    m_timing->setApiState(m_api->stateText());
    m_timing->showWithSavedGeometry();
    setWindowState(windowState() | Qt::WindowMinimized);
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    if (m_timing && !m_timing->close()) {
        e->ignore();
        return;
    }
    AppSettings::setValue(QStringLiteral("MainWindow/geometry"), saveGeometry());
    e->accept();
}
