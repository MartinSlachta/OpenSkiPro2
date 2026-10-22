#pragma once

#include "report/ReportFormat.h"

#include <QMainWindow>
#include <QPointer>

class ApiServer;
class Event;
class HeaderBanner;
class NavPanel;
class QAction;
class QLabel;
class QStackedWidget;
class CompetitorsPage;
class FisCompetitorsWindow;
class FisListPage;
class RunsPage;
class TimingWindow;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    Event* event() const { return m_event; }
    static QString eventPath(int slot);
    bool openEventFile(const QString& path);
    void openTiming(int run);
    void showRunsPage();
    void showFisStatus();

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    void createActions();
    void createMenus();
    void updateState();
    void showPage(QWidget* page);

    // Files
    void openEvent();
    void exitEvent();
    void duplicateEvent();
    void saveEventCopy();
    void restoreEvent();
    void deleteEvent();
    void importEvent(bool skiAlp); // SkiPro 2.x event or SkiAlp Pro 11 archive
    // Event
    void editGeneralParameters();
    void editEventType();
    void editPrintingParameters();
    void editHeader();
    void openReport(ReportType type);
    // FIS
    void importFisList();
    void importFisCompetitors();
    void updateFisCompetitors();
    void editFisPenalty();
    void exportFisXml();
    // Local configuration
    void editFolders();
    void editTimingDevice(int slot);
    void editApiServer();
    void selectLanguage(const QString& lang);
    void about();

    Event* m_event;
    HeaderBanner* m_banner;
    NavPanel* m_nav;
    QStackedWidget* m_stack;
    QWidget* m_emptyPage;
    CompetitorsPage* m_competitorsPage;
    RunsPage* m_runsPage;
    FisListPage* m_fisPage;
    QPointer<TimingWindow> m_timing;
    ApiServer* m_api = nullptr;
    QLabel* m_apiState = nullptr;
    QPointer<FisCompetitorsWindow> m_fisWindow;

    QList<QAction*> m_eventActions; // enabled only with an open event
    QAction* m_actOpen = nullptr;
    QAction* m_actExitEvent = nullptr;
    QAction* m_actDuplicate = nullptr;
    QAction* m_actSaveCopy = nullptr;
    QAction* m_actRestore = nullptr;
    QAction* m_actDelete = nullptr;
    QAction* m_actImportSkiPro = nullptr;
    QAction* m_actImportSkiAlp = nullptr;
    QAction* m_actOutputGroups = nullptr;
    QAction* m_actCompetitorFields = nullptr;
    QAction* m_actCategories = nullptr;
    QAction* m_actQuit = nullptr;
    QAction* m_actGeneral = nullptr;
    QAction* m_actType = nullptr;
    QAction* m_actPrinting = nullptr;
    QAction* m_actHeader = nullptr;
    QAction* m_actCompetitors = nullptr;
    QAction* m_actRuns = nullptr;
    QList<QPair<ReportType, QAction*>> m_reportActions;
    QAction* m_actFisImport = nullptr;
    QAction* m_actFisDownload = nullptr;
    QAction* m_actFisStatus = nullptr;
    QAction* m_actFisCompetitors = nullptr;
    QAction* m_actFisUpdate = nullptr;
    QAction* m_actFisPenalty = nullptr;
    QAction* m_actFisXml = nullptr;
    QAction* m_actRefereeReport = nullptr;
    QAction* m_actFolders = nullptr;
    QAction* m_actDevice1 = nullptr;
    QAction* m_actDevice2 = nullptr;
    QAction* m_actApi = nullptr;
    QAction* m_actAbout = nullptr;
};
