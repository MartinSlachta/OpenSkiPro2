#pragma once

#include "core/Types.h"
#include "timing/TimingDevice.h"

#include <QMainWindow>
#include <QMap>
#include <QPointer>
#include <QVector>
#include <array>

class Event;
class TimingEngine;
class TimingDevice;
class Cp540Simulator;
class QLabel;
class QListWidget;
class QSplitter;
class QMenu;
class QDockWidget;
class QTableWidget;
class QToolButton;
class QWidget;
class QAction;

// Timing window of one run ("Timing - Run 1 / 2" in SkiPro).
class TimingWindow : public QMainWindow {
    Q_OBJECT
public:
    void showWithSavedGeometry(); // last position / size, maximized the first time
    TimingWindow(Event* ev, int run, QWidget* parent = nullptr);
    ~TimingWindow() override;

    int run() const { return m_run; }
    void reloadDeviceConfig();
    TimingEngine* engine() const { return m_engine; }
    QVector<TimingDeviceState> deviceStates() const;
    void setApiState(const QString& text); // state of the API server in the status bar

Q_SIGNALS:
    void devicesChanged(); // a timing device connected / disconnected / reconfigured

protected:
    void closeEvent(QCloseEvent* e) override;
    bool eventFilter(QObject* obj, QEvent* e) override;

private:
    void buildUi();
    void buildActions();
    void showGridMenu(QTableWidget* g, const QPoint& pos);
    QString layoutKey(const QString& name) const;
    QList<QTableWidget*> layoutGrids() const;
    void restoreLayout();
    void saveLayout();
    QTableWidget* makeGrid(const QStringList& headers, QWidget* parent);
    QWidget* makePointPanel(int point, QWidget* parent);
    QTableWidget* makeFollowGrid(int point, QWidget* parent);
    void fillFollow(bool timesOnly);
    void onFollowTyped(int point, int index, const QString& text);
    void onRecordEdited(bool typedRow, int point, int recordId, int column, const QString& text);
    qint64 deviceNowUs() const;
    void scheduleRefresh();
    void refresh();
    void fillPointGrid(int point);
    void fillPaper();
    void fillInfo();
    QString nameWithField(int bib) const; // name + the field of "Display this field" in brackets
    void updateLockIndicators();
    void updateDeviceIndicators();

    // current selection
    int selectedRecordId() const;
    int selectedBib() const;
    bool selectedFollow(int* point, int* bib) const;

    // actions
    void toggleLock(int group); // -1 all
    void setSelectedActive(bool active);
    void insertPassage();
    void shiftNumber(int direction);
    void setStatusSelected(ResultStatus st);
    void noTimeSelected();
    void insertStartNumber();
    void competitorManagement();
    void onPaperLineSelected(int row);
    void applyMinimumColumnWidths();
    void enterStatusNumber(ResultStatus st);
    void searchNumber();
    void informationPenalty();
    void impulseComment();
    void quickPenalty(int index);
    void editRecordBib(int recordId);
    void connectDevice(int slot, bool on);
    void onUnassigned(int run, int recordId);
    void softwareParameters();
    void openReport(int type);

    Event* m_ev;
    int m_run;
    TimingEngine* m_engine;
    std::array<QPointer<TimingDevice>, 3> m_devices;
    bool m_refreshPending = false;

    QMap<int, QTableWidget*> m_followGrids; // numbers to follow / expected, by point
    bool m_fillingFollow = false;
    bool m_fillingRecords = false;
    bool m_searching = false;                // "Search" selects the number in all the grids
    QList<QTableWidget*> m_selectGrids;      // grids whose selection is the target of the actions
    void gridSelected(QTableWidget* g);
    QToolButton* m_startLock = nullptr;
    QToolButton* m_finishLock = nullptr;
    QTableWidget* m_gridStart = nullptr;
    std::array<QTableWidget*, kMaxInter> m_gridInter{ {nullptr, nullptr, nullptr} };
    QTableWidget* m_gridFinish = nullptr;
    QTableWidget* m_paper = nullptr;
    QTableWidget* m_records = nullptr;
    QTableWidget* m_ranking = nullptr;
    QLabel* m_rankingTitle = nullptr;
    QTableWidget* m_activeGrid = nullptr;
    QWidget* m_paperPanel = nullptr;
    std::array<QLabel*, 7> m_counters{};
    std::array<QToolButton*, 3> m_deviceButtons{ {nullptr, nullptr, nullptr} };
    QLabel* m_clock = nullptr;
    QLabel* m_apiState = nullptr;
    QWidget* m_simBar = nullptr;
    QAction* m_actPaper = nullptr;
    QMenu* m_paperMenu = nullptr;
    QDockWidget* m_paperDock = nullptr;
    QAction* m_aLock = nullptr;
    QAction* m_aActive = nullptr;
    QAction* m_aInactive = nullptr;
    QAction* m_aInsert = nullptr;
    QAction* m_aStartNumber = nullptr;
    QAction* m_aResync = nullptr;
    QAction* m_aUp = nullptr;
    QAction* m_aDown = nullptr;
    QAction* m_aTime = nullptr;
    QAction* m_aDns = nullptr;
    QAction* m_aNps = nullptr;
    QAction* m_aDnf = nullptr;
    QAction* m_aDsq = nullptr;
    QAction* m_aNoTime = nullptr;
    QAction* m_aSearch = nullptr;
    QAction* m_aInfo = nullptr;
    QAction* m_aComment = nullptr;
    QSplitter* m_split = nullptr;
    QSplitter* m_midSplit = nullptr;
    QList<QSplitter*> m_pointSplits;
    int m_paperRecordId = 0;        // passage of the line selected on the paper
    bool m_scrollToPaperRecord = false;
    int m_editRecordId = 0; // inserted passage to edit after the refill
};
