#include "TimingWindow.h"
#include "UiHelpers.h"
#include "app/AppSettings.h"
#include "app/SoundPlayer.h"
#include "core/Event.h"
#include "core/Ranking.h"
#include "CompetitorsPage.h"
#include "dialogs/ImpulseCommentDialog.h"
#include "dialogs/ModifyDataDialog.h"
#include "dialogs/ReportDialog.h"
#include "dialogs/StartListDialog.h"
#include "dialogs/TimingOptionsDialog.h"
#include "timing/Cp540Device.h"
#include "timing/Cp540Simulator.h"
#include "timing/Cp540Protocol.h"
#include "timing/TimingEngine.h"

#include <QAction>
#include <QApplication>
#include <QScrollBar>
#include <QSet>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDockWidget>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QTableWidget>
#include <QTime>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
constexpr int kRecordIdRole = Qt::UserRole + 1;
constexpr int kBibRole = Qt::UserRole + 2;
constexpr int kTypedRowRole = Qt::UserRole + 3;
const QString kClockStyle = QStringLiteral("background:white; border:1px solid #7d93ad; font: bold 12pt 'Courier New'; padding:1px 6px;");
const QString kClockStyleUnsynced =
    QStringLiteral("background:#d93b30; color:white; border:1px solid #9e2018; font: bold 12pt 'Courier New'; padding:1px 6px;");
constexpr int kPointRole = Qt::UserRole + 4;
// background of the Id of an impulse with a comment
const QColor kCommentColor(0xd9, 0xc6, 0xf2);

// SkiPro "Show Mark when a number must finish": square from 4 s before the best time at the
// point, green at the best time, yellow 4 s before / after it, red from 8 s after it.
QIcon mustFinishMark(qint64 diffMs)
{
    if (diffMs <= -4000)
        return QIcon();
    const double f = diffMs > 8000 ? 0.0 : qMax(1.0 - double(qAbs(diffMs)) / 8000.0, 0.0);
    const QColor c = f < 0.5 ? QColor(255, qRound(f * 2.0 * 255.0), 0)
                             : QColor(qRound((1.0 - (f - 0.5) * 2.0) * 255.0), 255, 0);
    QPixmap pm(8, 8);
    pm.fill(c);
    return QIcon(pm);
}
}

TimingWindow::TimingWindow(Event* ev, int run, QWidget* parent)
    : QMainWindow(parent)
    , m_ev(ev)
    , m_run(run)
    , m_engine(new TimingEngine(ev, this))
{
    m_engine->setRun(run);
    setWindowTitle(tr("Timing - %1 / %2").arg(ev->params().type.runName(run)).arg(ev->runCount()));
    setWindowIcon(UiHelpers::icon(QStringLiteral("stopwatch")));
    buildActions();
    buildUi();
    if (m_gridStart)
        m_gridStart->setObjectName(QStringLiteral("start"));
    if (m_gridFinish)
        m_gridFinish->setObjectName(QStringLiteral("finish"));
    for (int i = 0; i < int(m_gridInter.size()); ++i)
        if (m_gridInter[size_t(i)])
            m_gridInter[size_t(i)]->setObjectName(QStringLiteral("inter%1").arg(i + 1));
    m_records->setObjectName(QStringLiteral("records"));
    m_ranking->setObjectName(QStringLiteral("ranking"));
    restoreLayout();
    applyMinimumColumnWidths();

    connect(m_ev, &Event::runChanged, this, [this](int r) {
        if (r == m_run)
            scheduleRefresh();
    });
    connect(m_ev, &Event::paperAppended, this, [this](int r) {
        if (r == m_run)
            scheduleRefresh();
    });
    connect(m_ev, &Event::competitorsChanged, this, &TimingWindow::scheduleRefresh);
    connect(m_ev, &Event::paramsChanged, this, &TimingWindow::scheduleRefresh);
    connect(m_engine, &TimingEngine::message, this, [this](const QString& m) { statusBar()->showMessage(m, 15000); });
    connect(m_engine, &TimingEngine::playSound, this, [](const QString& f) { SoundPlayer::play(f); });
    connect(m_engine, &TimingEngine::unassignedRecord, this, &TimingWindow::onUnassigned);
    connect(m_engine, &TimingEngine::followChanged, this, &TimingWindow::scheduleRefresh);

    auto* clock = new QTimer(this);
    connect(clock, &QTimer::timeout, this, [this] {
        m_clock->setText(TimeUtil::formatDayTime(deviceNowUs() % TimeUtil::kUsPerDay, 1));
        const bool synced = m_engine->isClockSynced();
        if (m_clock->property("synced").toBool() != synced || !m_clock->property("synced").isValid()) {
            m_clock->setProperty("synced", synced);
            m_clock->setStyleSheet(synced ? kClockStyle : kClockStyleUnsynced);
            m_clock->setToolTip(synced ? tr("Time of the timing device")
                                       : tr("PC time: not synchronized with the timing device yet.\n"
                                            "It is synchronized by the next impulse received from the timing device.\n"
                                            "DNS / DNF / DSQ, No time and inserted passages use this time."));
        }
        fillFollow(true);
    });
    clock->start(100);

    reloadDeviceConfig();
    refresh();
}

TimingWindow::~TimingWindow()
{
    for (auto& d : m_devices) {
        if (!d)
            continue;
        d->disconnect(this);
        d->disconnect(m_engine);
        d->close();
        delete d.data();
    }
}

// ---- construction ---------------------------------------------------------------------

void TimingWindow::buildActions()
{
    QMenu* files = menuBar()->addMenu(tr("Files"));
    QMenu* options = menuBar()->addMenu(tr("Options"));
    QMenu* edit = menuBar()->addMenu(tr("Output"));
    QToolBar* tb = addToolBar(tr("Timing"));
    tb->setIconSize(QSize(28, 28));
    tb->setMovable(false);

    auto act = [this](QMenu* menu, const QString& text, const QString& icon, const QKeySequence& key, auto slot) {
        auto* a = new QAction(icon.isEmpty() ? QIcon() : UiHelpers::icon(icon), text, this);
        if (!key.isEmpty()) {
            a->setShortcut(key);
            a->setShortcutContext(Qt::WindowShortcut);
        }
        connect(a, &QAction::triggered, this, slot);
        if (menu)
            menu->addAction(a);
        addAction(a);
        return a;
    };

    QAction* aStartList = act(files, tr("Start List"), QStringLiteral("startlist"), QKeySequence(), [this] {
        StartListDialog dlg(m_ev, m_run, this);
        dlg.exec();
    });
    QAction* aCompetitors = act(files, tr("Competitor management"), QStringLiteral("competitor"), QKeySequence(),
                                &TimingWindow::competitorManagement);
    files->addSeparator();
    act(files, tr("Exit Timing"), QStringLiteral("close"), QKeySequence(), [this] { close(); });

    QAction* aParams = act(options, tr("Sotware Parameters"), QStringLiteral("params"), QKeySequence(),
                           &TimingWindow::softwareParameters);
    options->addSeparator();
    QAction* aDev1 = act(options, tr("Connect / disconnect Timing Device 1"), QStringLiteral("connect"), QKeySequence(),
                         [this] { connectDevice(1, !(m_devices[1] && m_devices[1]->isOpen())); });
    act(options, tr("Connect / disconnect Timing Device 2"), QStringLiteral("connect"), QKeySequence(),
        [this] { connectDevice(2, !(m_devices[2] && m_devices[2]->isOpen())); });
    options->addSeparator();
    QAction* aResync = m_aResync = act(options, tr("Resynchronize the clock with the timing device"), QString(), QKeySequence(), [this] {
        m_engine->resetClockSync();
        statusBar()->showMessage(tr("Clock: PC time until the next impulse of the timing device"), 15000);
    });
    // the action of the paper dock is added to this menu when the dock is created
    m_paperMenu = options;
    options->addSeparator();
    QAction* aLock = m_aLock = act(options, tr("Start/Ints/Finish Automatic Lock/Unlock"), QStringLiteral("lock"), QKeySequence(Qt::Key_F1),
                         [this] { toggleLock(-1); });
    act(options, tr("Start Automatic Lock/Unlock"), QString(), QKeySequence(Qt::Key_F2), [this] { toggleLock(0); });
    act(options, tr("Finish Automatic Lock/Unlock"), QString(), QKeySequence(Qt::Key_F3), [this] { toggleLock(2); });
    options->addSeparator();
    QAction* aActive = m_aActive = act(options, tr("Active passage(s)"), QStringLiteral("time_ok"), QKeySequence(Qt::CTRL | Qt::Key_F4),
                           [this] { setSelectedActive(true); });
    QAction* aInactive = m_aInactive = act(options, tr("Unactive passage(s)"), QStringLiteral("time_x"), QKeySequence(Qt::SHIFT | Qt::Key_F4),
                             [this] { setSelectedActive(false); });
    options->addSeparator();
    QAction* aInsert = m_aInsert = act(options, tr("Insert a passage"), QStringLiteral("time_add"), QKeySequence(Qt::Key_F5),
                           &TimingWindow::insertPassage);
    QAction* aStartNumber = m_aStartNumber = act(options, tr("Insert a number at the start"), QStringLiteral("startlist_add"),
                                                 QKeySequence(Qt::Key_F6), &TimingWindow::insertStartNumber);
    options->addSeparator();
    QAction* aUp = m_aUp = act(options, tr("Number Up"), QStringLiteral("num_up"), QKeySequence(Qt::Key_F7), [this] { shiftNumber(-1); });
    QAction* aDown = m_aDown = act(options, tr("Number Down"), QStringLiteral("num_down"), QKeySequence(Qt::Key_F8), [this] { shiftNumber(1); });
    options->addSeparator();
    m_aTime = act(options, tr("Status : Time"), QStringLiteral("status_time"), QKeySequence(),
                  [this] { setStatusSelected(ResultStatus::Classified); });
    QAction* aDns = m_aDns = act(options, tr("Status : Did not Start"), QStringLiteral("dns"), QKeySequence(Qt::Key_F9),
                        [this] { setStatusSelected(ResultStatus::DNS); });
    QAction* aNps = m_aNps = act(options, tr("Status : Not Permitted to Start"), QStringLiteral("nps"),
                                 QKeySequence(Qt::SHIFT | Qt::Key_F9), [this] { setStatusSelected(ResultStatus::NPS); });
    QAction* aDnf = m_aDnf = act(options, tr("Status : Did not Finish"), QStringLiteral("dnf"), QKeySequence(Qt::Key_F10),
                        [this] { setStatusSelected(ResultStatus::DNF); });
    QAction* aDsq = m_aDsq = act(options, tr("Status : Disqualified"), QStringLiteral("dsq"), QKeySequence(Qt::Key_F11),
                        [this] { setStatusSelected(ResultStatus::DSQ); });
    QAction* aNoTime = m_aNoTime = act(options, tr("No time"), QStringLiteral("notime"), QKeySequence(), &TimingWindow::noTimeSelected);
    act(options, tr("Enter a DNS number"), QString(), QKeySequence(Qt::CTRL | Qt::Key_F9), [this] { enterStatusNumber(ResultStatus::DNS); });
    act(options, tr("Enter a NPS number"), QString(), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F9),
        [this] { enterStatusNumber(ResultStatus::NPS); });
    act(options, tr("Enter a DNF number"), QString(), QKeySequence(Qt::CTRL | Qt::Key_F10), [this] { enterStatusNumber(ResultStatus::DNF); });
    act(options, tr("Enter a DSQ number"), QString(), QKeySequence(Qt::CTRL | Qt::Key_F11), [this] { enterStatusNumber(ResultStatus::DSQ); });
    options->addSeparator();
    QAction* aSearch = m_aSearch = act(options, tr("Search for a Number"), QStringLiteral("search"), QKeySequence(Qt::CTRL | Qt::Key_F),
                           &TimingWindow::searchNumber);
    QAction* aInfo = m_aInfo = act(options, tr("Information / Penalty"), QStringLiteral("info"), QKeySequence(Qt::Key_F12),
                         &TimingWindow::informationPenalty);
    QAction* aComment = m_aComment = act(options, tr("Impulse comment"), QStringLiteral("comment"), QKeySequence(Qt::CTRL | Qt::Key_K),
                                         &TimingWindow::impulseComment);
    for (int i = 0; i < 4; ++i)
        act(nullptr, QString(), QString(), QKeySequence(Qt::CTRL | Qt::Key(Qt::Key_F5 + i)), [this, i] { quickPenalty(i); });

    act(edit, tr("Times records"), QStringLiteral("printer"), QKeySequence(), [this] { openReport(int(ReportType::TimeRecords)); });
    act(edit, tr("Time records (by numbers)"), QStringLiteral("printer"), QKeySequence(),
        [this] { openReport(int(ReportType::TimeRecordsByBib)); });
    act(edit, tr("Timing device paper"), QStringLiteral("printer"), QKeySequence(), [this] { openReport(int(ReportType::TimingPaper)); });
    edit->addSeparator();
    act(edit, tr("Start List"), QStringLiteral("printer"), QKeySequence(), [this] { openReport(int(ReportType::StartList)); });
    act(edit, tr("Run Ranking"), QStringLiteral("printer"), QKeySequence(), [this] { openReport(int(ReportType::RunRanking)); });
    edit->addSeparator();
    act(edit, tr("Event Ranking"), QStringLiteral("printer"), QKeySequence(), [this] { openReport(int(ReportType::RankingAfterRun)); });

    tb->addAction(aStartList);
    tb->addAction(aCompetitors);
    tb->addAction(aParams);
    tb->addSeparator();
    tb->addAction(aDev1);
    tb->addAction(aLock);
    tb->addSeparator();
    tb->addAction(aActive);
    tb->addAction(aInactive);
    tb->addAction(aInsert);
    tb->addAction(aStartNumber);
    tb->addSeparator();
    tb->addAction(aUp);
    tb->addAction(aDown);
    tb->addSeparator();
    tb->addAction(aDns);
    tb->addAction(aNps);
    tb->addAction(aDnf);
    tb->addAction(aDsq);
    tb->addAction(aNoTime);
    tb->addSeparator();
    tb->addAction(aSearch);
    tb->addAction(aInfo);
    tb->addAction(aComment);
}

QTableWidget* TimingWindow::makeGrid(const QStringList& headers, QWidget* parent)
{
    auto* g = new QTableWidget(parent);
    g->setColumnCount(int(headers.size()));
    g->setHorizontalHeaderLabels(headers);
    // Num and Hour Cell. are edited in place (double click, F2 or typing)
    g->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed | QAbstractItemView::AnyKeyPressed);
    g->setSelectionMode(QAbstractItemView::ExtendedSelection);
    UiHelpers::styleGrid(g, true);
    g->horizontalHeader()->setStretchLastSection(true);
    g->installEventFilter(this);
    g->setToolTip(tr("Double click or type to change the number or the hour.\n"
                     "Click the status icon to activate / deactivate a passage.\n"
                     "Double click the Id to comment the impulse.\n"
                     "The line 'next' holds the numbers typed for the next impulses."));
    m_selectGrids << g;
    connect(g, &QTableWidget::itemSelectionChanged, this, [this, g] { gridSelected(g); });
    g->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(g, &QWidget::customContextMenuRequested, this, [this, g](const QPoint& pos) { showGridMenu(g, pos); });
    connect(g, &QTableWidget::cellClicked, this, [this, g](int row, int col) {
        auto* it = g->item(row, 0);
        if (col != 1 || !it || it->data(kTypedRowRole).toBool())
            return;
        const TimeRecord* r = m_engine->data().record(it->data(kRecordIdRole).toInt());
        if (r)
            m_engine->setRecordActive(r->id, !r->isActive());
    });
    connect(g, &QTableWidget::cellDoubleClicked, this, [this, g](int row, int col) {
        auto* it = g->item(row, 0);
        if (col != g->columnCount() - 1 || !it || it->data(kTypedRowRole).toBool())
            return;
        m_activeGrid = g;
        impulseComment();
    });
    connect(g, &QTableWidget::itemChanged, this, [this, g](QTableWidgetItem* it) {
        if (m_fillingRecords)
            return;
        auto* first = g->item(it->row(), 0);
        if (!first)
            return;
        const bool typedRow = first->data(kTypedRowRole).toBool();
        const int point = first->data(kPointRole).toInt();
        const int recordId = first->data(kRecordIdRole).toInt();
        const int col = it->column();
        const QString text = it->text();
        QTimer::singleShot(0, this, [this, typedRow, point, recordId, col, text] { onRecordEdited(typedRow, point, recordId, col, text); });
    });
    return g;
}

void TimingWindow::onRecordEdited(bool typedRow, int point, int recordId, int column, const QString& text)
{
    if (typedRow) {
        // numbers typed for the next impulses of the point
        QVector<int> bibs;
        for (const QString& t : text.split(QRegularExpression(QStringLiteral("[\\s,;]+")), Qt::SkipEmptyParts)) {
            const int b = t.toInt();
            if (b > 0)
                bibs << b;
        }
        m_engine->setTypedQueue(point, bibs);
        if (!bibs.isEmpty())
            statusBar()->showMessage(tr("%1: the next impulse is given to %2 - %3")
                                         .arg(UiHelpers::pointName(point))
                                         .arg(bibs.first())
                                         .arg(m_ev->competitorName(bibs.first())),
                                     15000);
        scheduleRefresh();
        return;
    }
    if (column == 2) {
        bool ok = false;
        const int bib = text.trimmed().isEmpty() ? 0 : text.trimmed().toInt(&ok);
        if (text.trimmed().isEmpty() || ok)
            m_engine->setRecordBib(recordId, bib);
    } else if (column == 3) {
        QString t = text.trimmed();
        if (t.startsWith(QLatin1String("M ")))
            t = t.mid(2);
        const qint64 us = TimeUtil::parseDayTime(t);
        if (us >= 0)
            m_engine->setRecordTime(recordId, us);
        else
            QMessageBox::warning(this, tr("Hour Cell."), tr("Invalid hour: %1").arg(text));
    }
    scheduleRefresh();
}

QWidget* TimingWindow::makePointPanel(int point, QWidget* parent)
{
    auto* w = new QWidget(parent);
    auto* lay = new QVBoxLayout(w);
    lay->setContentsMargins(2, 2, 2, 2);
    lay->setSpacing(2);
    lay->addWidget(UiHelpers::titleBar(UiHelpers::pointName(point), w));

    // numbers to follow (top) / impulses (bottom), the operator moves the separation
    auto* split = new QSplitter(Qt::Vertical, w);
    split->setChildrenCollapsible(false);
    split->setHandleWidth(6);
    split->setStyleSheet(QStringLiteral("QSplitter::handle:vertical { background: #c8d4e6; margin: 1px 30px; border-radius: 2px; }"));
    split->setObjectName(point == Point::Start ? QStringLiteral("start") : point == Point::Finish ? QStringLiteral("finish")
                                                                                                  : QStringLiteral("inter%1").arg(point));
    m_pointSplits << split;
    auto* top = new QWidget(split);
    auto* tl = new QVBoxLayout(top);
    tl->setContentsMargins(0, 0, 0, 0);
    tl->setSpacing(2);
    tl->addWidget(makeFollowGrid(point, top), 1);
    auto* bottom = new QWidget(split);
    auto* bl = new QVBoxLayout(bottom);
    bl->setContentsMargins(0, 0, 0, 0);
    bl->setSpacing(0);
    split->addWidget(top);
    split->addWidget(bottom);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    lay->addWidget(split, 1);

    QTableWidget* g = nullptr;
    if (point == Point::Start) {
        m_startLock = new QToolButton(top);
        m_startLock->setAutoRaise(true);
        m_startLock->setIconSize(QSize(22, 22));
        m_startLock->setToolTip(tr("Start Automatic Lock/Unlock (F2)"));
        connect(m_startLock, &QToolButton::clicked, this, [this] { toggleLock(0); });
        tl->addWidget(m_startLock);
        g = m_gridStart = makeGrid({ tr("Seq"), QString(), tr("Num"), tr("Hour Cell."), tr("Id") }, bottom);
        g->setColumnWidth(3, 130);
        g->setColumnWidth(4, 60);
    } else if (point == Point::Finish) {
        m_finishLock = new QToolButton(top);
        m_finishLock->setAutoRaise(true);
        m_finishLock->setIconSize(QSize(22, 22));
        m_finishLock->setToolTip(tr("Finish Automatic Lock/Unlock (F3)"));
        connect(m_finishLock, &QToolButton::clicked, this, [this] { toggleLock(2); });
        tl->addWidget(m_finishLock);
        g = m_gridFinish = makeGrid({ tr("Seq"), QString(), tr("Num"), tr("Hour Cell."), tr("Time"), tr("Id") }, bottom);
        g->setColumnWidth(3, 130);
        g->setColumnWidth(4, 60);
    } else {
        g = makeGrid({ tr("Seq"), QString(), tr("Num"), tr("Hour Cell."), tr("Time"), tr("Id") }, bottom);
        g->setColumnWidth(3, 130);
        g->setColumnWidth(4, 60);
        m_gridInter[size_t(point - 1)] = g;
    }
    g->setColumnWidth(0, 34);
    g->setColumnWidth(1, 22);
    g->setColumnWidth(2, 44);
    bl->addWidget(g, 1);
    const int rowH = m_followGrids.value(point)->verticalHeader()->defaultSectionSize();
    split->setSizes({ 5 * rowH + 34, 600 });
    return w;
}

QTableWidget* TimingWindow::makeFollowGrid(int point, QWidget* parent)
{
    auto* g = new QTableWidget(0, 4, parent);
    QFont f(QStringLiteral("Courier New"));
    f.setPointSize(10);
    g->setFont(f);
    const int rowH = QFontMetrics(f).height() + 6;
    g->horizontalHeader()->setVisible(false);
    g->verticalHeader()->setVisible(false);
    g->verticalHeader()->setMinimumSectionSize(rowH);
    g->verticalHeader()->setDefaultSectionSize(rowH);
    g->setShowGrid(false);
    g->setFocusPolicy(Qt::ClickFocus);
    g->setSelectionMode(QAbstractItemView::SingleSelection);
    g->setSelectionBehavior(QAbstractItemView::SelectRows);
    g->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::AnyKeyPressed | QAbstractItemView::EditKeyPressed);
    g->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    g->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    g->setFrameShape(QFrame::NoFrame);
    g->setMinimumHeight(rowH + 2);
    g->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    g->setColumnWidth(0, 72);
    g->setColumnWidth(2, 80);
    g->setColumnWidth(3, 16);
    g->setColumnHidden(2, point == Point::Start); // no running time before the start
    g->installEventFilter(this);
    g->setToolTip(point == Point::Start
                      ? tr("Numbers to follow at the start (the next one at the bottom).\n"
                           "F7 / F8 move the selected number, F9 gives DNS, Shift+F9 NPS.\n"
                           "Type a number on a line to put it there; a number not in the start list is inserted into it.")
                      : tr("Competitors expected at this point (the next one at the bottom).\n"
                           "F7 / F8 move the selected number, F10 DNF, F11 DSQ, 'No time' if it passed without time.\n"
                           "Type a number on a line to put it there."));
    m_selectGrids << g;
    connect(g, &QTableWidget::itemSelectionChanged, this, [this, g] { gridSelected(g); });
    g->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(g, &QWidget::customContextMenuRequested, this, [this, g](const QPoint& pos) { showGridMenu(g, pos); });
    connect(g, &QTableWidget::itemChanged, this, [this, point, g](QTableWidgetItem* it) {
        if (m_fillingFollow || it->column() != 0)
            return;
        const int index = g->rowCount() - 1 - it->row(); // the next one is at the bottom
        const QString text = it->text();
        QTimer::singleShot(0, this, [this, point, index, text] { onFollowTyped(point, index, text); });
    });
    m_followGrids.insert(point, g);
    return g;
}

qint64 TimingWindow::deviceNowUs() const
{
    return m_engine->nowUs();
}

void TimingWindow::onFollowTyped(int point, int index, const QString& text)
{
    bool ok = false;
    const int bib = text.trimmed().toInt(&ok);
    if (!ok || bib <= 0) {
        fillFollow(false);
        return;
    }
    if (m_engine->placeInFollow(point, bib, index)) {
        fillFollow(false);
        return;
    }
    if (point == Point::Start) {
        if (m_engine->activeRecord(bib, Point::Start)) {
            QMessageBox::warning(this, tr("Start"), tr("%1 - %2 has already started.").arg(bib).arg(m_ev->competitorName(bib)));
            fillFollow(false);
            return;
        }
        if (m_engine->data().startPosition(bib) < 0) {
            if (m_ev->params().timing.confirmInsertStartList
                && QMessageBox::question(this, tr("Start List"),
                                         tr("%1 - %2 is not in the start list.\nInsert this competitor into the start list?")
                                             .arg(bib)
                                             .arg(m_ev->competitorName(bib)))
                    != QMessageBox::Yes) {
                fillFollow(false);
                return;
            }
            m_engine->insertIntoStartList(bib, index);
        } else {
            // in the start list but with a status (DNS ...): it must be cleared first
            statusBar()->showMessage(tr("%1 - %2 has a status (%3).").arg(bib).arg(m_ev->competitorName(bib),
                                         UiHelpers::statusText(m_engine->data().info(bib).status)), 15000);
        }
    } else {
        // not expected here: the next impulse of this point is given to this number
        m_engine->queueNumber(point, bib);
        statusBar()->showMessage(tr("%1: the next impulse is given to %2 - %3")
                                     .arg(UiHelpers::pointName(point))
                                     .arg(bib)
                                     .arg(m_ev->competitorName(bib)),
                                 15000);
    }
    scheduleRefresh();
}

void TimingWindow::fillFollow(bool timesOnly)
{
    const auto& to = m_ev->params().timing;
    m_fillingFollow = true;
    for (auto it = m_followGrids.cbegin(); it != m_followGrids.cend(); ++it) {
        const int point = it.key();
        QTableWidget* g = it.value();
        g->setVisible(to.showNumbersToFollow);
        QWidget* focus = QApplication::focusWidget();
        const bool editing = focus && focus != g && g->isAncestorOf(focus); // the operator is typing a number
        if (!to.showNumbersToFollow || editing)
            continue;
        QVector<int> bibs = m_engine->follow(point);
        if (bibs.isEmpty())
            bibs << 0; // an editable line to type a number
        std::reverse(bibs.begin(), bibs.end()); // the next one at the bottom, next to the grid
        bool same = g->rowCount() == bibs.size();
        for (int r = 0; same && r < bibs.size(); ++r)
            same = g->item(r, 0) && g->item(r, 0)->data(kBibRole).toInt() == bibs.at(r);
        if (!timesOnly || !same) {
            int selectedBib = -1;
            if (!g->selectedItems().isEmpty())
                selectedBib = g->item(g->selectedItems().first()->row(), 0)->data(kBibRole).toInt();
            g->clearSelection();
            g->setRowCount(int(bibs.size()));
            for (int r = 0; r < bibs.size(); ++r) {
                const int bib = bibs.at(r);
                const bool next = r == bibs.size() - 1;
                auto* num = new QTableWidgetItem(UiHelpers::icon(next ? QStringLiteral("ball_green") : QStringLiteral("arrow_down")),
                                                 bib > 0 ? QString::number(bib) : QString());
                num->setData(kBibRole, bib);
                num->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
                auto* name = new QTableWidgetItem(to.showNamesInFollow && bib > 0 ? m_ev->competitorName(bib) : QString());
                name->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
                auto* time = new QTableWidgetItem;
                time->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                time->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
                auto* flag = new QTableWidgetItem;
                flag->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
                if (next) {
                    QFont bold = g->font();
                    bold.setBold(true);
                    for (auto* x : { num, name, time })
                        x->setFont(bold);
                }
                g->setItem(r, 0, num);
                g->setItem(r, 1, name);
                g->setItem(r, 2, time);
                g->setItem(r, 3, flag);
                if (bib > 0 && bib == selectedBib)
                    g->selectRow(r);
            }
            if (selectedBib < 0)
                g->scrollToBottom();
        }
        if (point == Point::Start)
            continue;
        // running time since the start
        const qint64 now = deviceNowUs();
        // SkiPro: best time at the point, the reference of the "must finish" mark
        qint64 bestMs = 0;
        if (to.showMarkMustFinish) {
            for (const TimeInfo& info : m_engine->data().infos) {
                const qint64 t = point == Point::Finish ? (info.timeMs >= 0 ? info.timeMs - info.penaltyMs : -1)
                                                        : info.interMs[size_t(point - 1)];
                if (t > 0 && (bestMs == 0 || t < bestMs))
                    bestMs = t;
            }
        }
        for (int r = 0; r < g->rowCount(); ++r) {
            const TimeRecord* start = m_engine->activeTimeRecord(g->item(r, 0)->data(kBibRole).toInt(), Point::Start);
            QString text;
            QIcon mark;
            if (start) {
                qint64 running = now - start->dayTimeUs;
                while (running < 0)
                    running += TimeUtil::kUsPerDay;
                text = TimeUtil::formatResult(running / 1000, Precision::Tenth);
                if (bestMs > 0)
                    mark = mustFinishMark(running / 1000 - bestMs);
            }
            g->item(r, 2)->setText(text);
            g->item(r, 3)->setIcon(mark);
        }
    }
    m_fillingFollow = false;
}

void TimingWindow::buildUi()
{
    auto* split = new QSplitter(Qt::Horizontal, this);
    m_split = split;
    split->addWidget(makePointPanel(Point::Start, split));
    // like SkiPro: the intermediates one under the other in one column
    if (m_ev->interCount() > 0) {
        auto* inters = new QSplitter(Qt::Vertical, split);
        inters->setObjectName(QStringLiteral("inters"));
        inters->setChildrenCollapsible(false);
        inters->setHandleWidth(6);
        for (int i = 1; i <= m_ev->interCount(); ++i)
            inters->addWidget(makePointPanel(i, inters));
        m_pointSplits << inters;
        split->addWidget(inters);
    }
    split->addWidget(makePointPanel(Point::Finish, split));

    // middle: inquiries / records / ranking
    m_midSplit = new QSplitter(Qt::Vertical, split);
    m_midSplit->setChildrenCollapsible(false);
    m_midSplit->setHandleWidth(6);
    m_midSplit->setStyleSheet(QStringLiteral("QSplitter::handle:vertical { background: #c8d4e6; margin: 1px 30px; border-radius: 2px; }"));
    auto* mid = new QWidget(m_midSplit);
    auto* ml = new QVBoxLayout(mid);
    ml->setContentsMargins(2, 2, 2, 2);
    ml->setSpacing(2);
    ml->addWidget(UiHelpers::titleBar(tr("Inquiries"), mid));
    auto* inq = new QWidget(mid);
    auto* ig = new QGridLayout(inq);
    ig->setContentsMargins(4, 2, 4, 2);
    const QStringList labels = { tr("List"), tr("Classified"), tr("Did Not Start"), tr("Not Permitted"), tr("Did Not Finish"),
                                 tr("Disqualified"), tr("Not Processed") };
    for (int i = 0; i < int(m_counters.size()); ++i) {
        auto* l = new QLabel(labels.at(i), inq);
        l->setAlignment(Qt::AlignCenter);
        ig->addWidget(l, 0, i);
        m_counters[size_t(i)] = UiHelpers::counterBox(inq);
        ig->addWidget(m_counters[size_t(i)], 1, i, Qt::AlignHCenter);
    }
    auto* devRow = new QHBoxLayout;
    devRow->addWidget(new QLabel(tr("Timing Device"), inq));
    for (int s = 1; s <= 2; ++s) {
        auto* b = new QToolButton(inq);
        b->setText(QString::number(s));
        b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        b->setIconSize(QSize(14, 14));
        connect(b, &QToolButton::clicked, this, [this, s] { connectDevice(s, !(m_devices[size_t(s)] && m_devices[size_t(s)]->isOpen())); });
        m_deviceButtons[size_t(s)] = b;
        devRow->addWidget(b);
    }
    m_clock = new QLabel(inq);
    m_clock->setStyleSheet(kClockStyleUnsynced);
    m_clock->setContextMenuPolicy(Qt::ActionsContextMenu);
    m_clock->addAction(m_aResync);
    devRow->addWidget(m_clock);
    devRow->addStretch();
    ig->addLayout(devRow, 2, 0, 1, 6);
    ml->addWidget(inq);

    ml->addWidget(UiHelpers::titleBar(tr("Records"), mid));
    m_records = new QTableWidget(mid);
    m_records->setColumnCount(2);
    m_records->setHorizontalHeaderLabels({ tr("Num"), tr("Competitor & Time") });
    m_records->setEditTriggers(QAbstractItemView::NoEditTriggers);
    UiHelpers::styleGrid(m_records, true);
    m_records->verticalHeader()->setDefaultSectionSize(40);
    m_records->setWordWrap(true);
    m_records->horizontalHeader()->setStretchLastSection(true);
    m_records->setColumnWidth(0, 50);
    ml->addWidget(m_records, 1);
    m_midSplit->addWidget(mid);

    auto* rankPanel = new QWidget(m_midSplit);
    auto* rl = new QVBoxLayout(rankPanel);
    rl->setContentsMargins(2, 2, 2, 2);
    rl->setSpacing(2);
    m_rankingTitle = UiHelpers::titleBar(tr("Ranking - Event"), rankPanel);
    rl->addWidget(m_rankingTitle);
    m_ranking = new QTableWidget(rankPanel);
    m_ranking->setColumnCount(4);
    m_ranking->setHorizontalHeaderLabels({ tr("Rank"), tr("Num"), tr("Competitor"), tr("Time") });
    m_ranking->setEditTriggers(QAbstractItemView::NoEditTriggers);
    UiHelpers::styleGrid(m_ranking, true);
    m_ranking->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_ranking->setColumnWidth(0, 40);
    m_ranking->setColumnWidth(1, 50);
    m_ranking->setColumnWidth(3, 100);
    rl->addWidget(m_ranking, 1);
    m_midSplit->addWidget(rankPanel);
    m_midSplit->setSizes({ 330, 600 });
    m_midSplit->setStretchFactor(0, 0);
    m_midSplit->setStretchFactor(1, 1);
    split->addWidget(m_midSplit);

    // timing device paper: a dock, it can be detached (floating window), attached again or hidden
    auto* dock = new QDockWidget(tr("Timing device paper"), this);
    dock->setObjectName(QStringLiteral("paperDock"));
    dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea | Qt::BottomDockWidgetArea);
    dock->setStyleSheet(QStringLiteral("QDockWidget::title { background: #c8d4e6; padding: 3px 6px; font-weight: bold; }"));
    m_paperPanel = new QWidget(dock);
    auto* pl = new QVBoxLayout(m_paperPanel);
    pl->setContentsMargins(2, 2, 2, 2);
    pl->setSpacing(2);
    m_paper = new QTableWidget(m_paperPanel);
    m_paper->setObjectName(QStringLiteral("devicepaper"));
    m_paper->setColumnCount(4);
    m_paper->setHorizontalHeaderLabels({ tr("Seq"), tr("Dev."), tr("Received"), tr("Data") });
    m_paper->setToolTip(tr("Lines received from the timing devices, exactly as received.\n"
                           "They are never modified by the program."));
    {
        QFont mono(QStringLiteral("Courier New"));
        mono.setPointSize(9);
        m_paper->setFont(mono);
    }
    m_paper->setEditTriggers(QAbstractItemView::NoEditTriggers);
    UiHelpers::styleGrid(m_paper, true);
    // the data column has the width of a line: scrolls horizontally when the paper is narrow
    m_paper->horizontalHeader()->setStretchLastSection(false);
    {
        const QFontMetrics fm(m_paper->font());
        m_paper->setColumnWidth(0, fm.horizontalAdvance(QStringLiteral("99999")) + 12);
        m_paper->setColumnWidth(1, fm.horizontalAdvance(QStringLiteral("Dev.")) + 14);
        m_paper->setColumnWidth(2, fm.horizontalAdvance(QStringLiteral("00:00:00")) + 12);
        m_paper->setColumnWidth(3, fm.horizontalAdvance(QStringLiteral("TN  9999  9999 M4 00:00:00.00000  99999  XXXX")) + 16);
    }
    m_paper->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_paper->setWordWrap(false);
    m_paper->setTextElideMode(Qt::ElideNone);
    m_paper->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_paper->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(m_paper, &QTableWidget::currentCellChanged, this, [this](int row) { onPaperLineSelected(row); });
    connect(m_paper, &QTableWidget::cellClicked, this, [this](int row) { onPaperLineSelected(row); });
    pl->addWidget(m_paper, 1);
    dock->setWidget(m_paperPanel);
    addDockWidget(Qt::RightDockWidgetArea, dock);
    resizeDocks({ dock }, { 420 }, Qt::Horizontal);
    m_paperDock = dock;
    m_actPaper = dock->toggleViewAction();
    m_actPaper->setText(tr("Display the timing device paper"));
    m_paperMenu->addAction(m_actPaper);

    QList<int> sizes;
    sizes << 330;
    if (m_ev->interCount() > 0)
        sizes << 360;
    sizes << 420 << 560;
    split->setSizes(sizes);
    for (int i = 0; i < split->count(); ++i)
        split->setStretchFactor(i, i == split->count() - 2 ? 2 : 1);

    auto* central = new QWidget(this);
    auto* cl = new QVBoxLayout(central);
    cl->setContentsMargins(0, 0, 0, 0);
    cl->addWidget(split, 1);

    // simulator bar
    m_simBar = new QWidget(central);
    auto* sl = new QHBoxLayout(m_simBar);
    sl->setContentsMargins(6, 2, 6, 2);
    auto* simLabel = new QLabel(tr("<b>CP540 simulator:</b>"), m_simBar);
    sl->addWidget(simLabel);
    auto simDevice = [this]() -> Cp540Simulator* {
        for (int s = 1; s <= 2; ++s)
            if (auto* sim = qobject_cast<Cp540Simulator*>(m_devices[size_t(s)].data()))
                return sim;
        return nullptr;
    };
    auto addSim = [&](const QString& text, auto fn) {
        auto* b = new QPushButton(text, m_simBar);
        connect(b, &QPushButton::clicked, this, fn);
        sl->addWidget(b);
        return b;
    };
    addSim(tr("Start impulse"), [this, simDevice] {
        if (auto* s = simDevice())
            s->trigger(s->config().startChannel);
    });
    for (int i = 0; i < m_ev->interCount(); ++i)
        addSim(tr("Inter %1 impulse").arg(i + 1), [this, simDevice, i] {
            if (auto* s = simDevice())
                s->trigger(s->config().interChannels[size_t(i)]);
        });
    addSim(tr("Finish impulse"), [this, simDevice] {
        if (auto* s = simDevice())
            s->trigger(s->config().finishChannel);
    });
    auto* autoBtn = addSim(tr("Automatic run"), [] {});
    autoBtn->setCheckable(true);
    connect(autoBtn, &QPushButton::toggled, this, [simDevice](bool on) {
        if (auto* s = simDevice())
            s->setAutoRun(on);
    });
    addSim(tr("Lose next frame"), [simDevice] {
        if (auto* s = simDevice())
            s->dropNextFrame();
    });
    addSim(tr("Replay a log..."), [this, simDevice] {
        auto* s = simDevice();
        if (!s)
            return;
        const QString f = QFileDialog::getOpenFileName(this, tr("Replay a log"), s->config().simReplayFile,
                                                       tr("Text files (*.txt *.log);;All files (*)"));
        QString err;
        if (!f.isEmpty() && !s->startReplay(f, 400, &err))
            QMessageBox::warning(this, tr("Replay a log"), err);
    });
    sl->addStretch();
    m_simBar->setStyleSheet(QStringLiteral("background:#fff3cd;"));
    m_simBar->hide();
    cl->addWidget(m_simBar);
    setCentralWidget(central);
    statusBar()->showMessage(tr("Ready"));
}

// ---- devices ------------------------------------------------------------------------------

void TimingWindow::reloadDeviceConfig()
{
    bool anySim = false;
    for (int s = 1; s <= 2; ++s) {
        if (m_devices[size_t(s)]) {
            m_devices[size_t(s)]->close();
            m_devices[size_t(s)]->deleteLater();
            m_devices[size_t(s)] = nullptr;
        }
        const TimingDeviceConfig cfg = AppSettings::timingDevice(s);
        m_engine->setDeviceConfig(s, cfg);
        TimingDevice* dev = nullptr;
        if (cfg.type == TimingDeviceConfig::Type::CP540)
            dev = new Cp540Device(s, cfg, this);
        else if (cfg.type == TimingDeviceConfig::Type::Simulator) {
            dev = new Cp540Simulator(s, cfg, this);
            anySim = true;
        }
        if (!dev)
            continue;
        m_devices[size_t(s)] = dev;
        connect(dev, &TimingDevice::rawLine, this, [this, s](const QString& line, bool valid) {
            if (line.trimmed().isEmpty())
                return;
            PaperLine p;
            p.device = s;
            p.receivedUs = TimingEngine::currentDayTimeUs();
            p.text = line;
            p.error = !valid;
            m_ev->appendPaper(m_run, p);
        });
        connect(dev, &TimingDevice::impulse, m_engine, &TimingEngine::handleImpulse);
        connect(dev, &TimingDevice::impulse, this, [this](const TimingImpulse& imp) {
            if (imp.type != TimingImpulse::Type::New || imp.dayTimeUs < 0)
                return;
            // the clock shows the time of the timing device, like SkiPro
            qint64 offset = imp.dayTimeUs - TimingEngine::currentDayTimeUs();
            if (offset > TimeUtil::kUsPerDay / 2)
                offset -= TimeUtil::kUsPerDay;
            else if (offset < -TimeUtil::kUsPerDay / 2)
                offset += TimeUtil::kUsPerDay;
            m_engine->setClockOffsetUs(offset);
        });
        connect(dev, &TimingDevice::statusChanged, this, &TimingWindow::updateDeviceIndicators);
        connect(dev, &TimingDevice::deviceMessage, this, [this](const QString& m) { statusBar()->showMessage(m, 15000); });
        connectDevice(s, true);
    }
    m_simBar->setVisible(anySim);
    updateDeviceIndicators();
}

void TimingWindow::connectDevice(int slot, bool on)
{
    TimingDevice* dev = m_devices[size_t(slot)];
    if (!dev) {
        statusBar()->showMessage(tr("Timing device %1 is not configured (Local configuration > Timing Device %1).").arg(slot), 10000);
        return;
    }
    if (!on) {
        // a disconnection by mistake during a race: the impulses are not received anymore
        if (dev->isOpen()
            && QMessageBox::warning(this, tr("Disconnect Timing Device %1").arg(slot),
                                    tr("The timing device %1 is connected.\n"
                                       "While it is disconnected, no impulse is received by the program.\n\n"
                                       "Disconnect the timing device %1?")
                                        .arg(slot),
                                    QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
                != QMessageBox::Yes)
            return;
        dev->close();
    } else {
        QString err;
        if (!dev->open(&err))
            statusBar()->showMessage(tr("Not possible to connect the timing device %1: %2").arg(slot).arg(err), 15000);
    }
    updateDeviceIndicators();
}

void TimingWindow::updateDeviceIndicators()
{
    for (int s = 1; s <= 2; ++s) {
        QToolButton* b = m_deviceButtons[size_t(s)];
        if (!b)
            continue;
        TimingDevice* dev = m_devices[size_t(s)];
        const bool ok = dev && dev->isOpen();
        b->setIcon(UiHelpers::icon(ok ? QStringLiteral("ball_green") : QStringLiteral("ball_red")));
        b->setToolTip(dev ? dev->description() : tr("Not configured"));
        b->setEnabled(dev != nullptr);
    }
    Q_EMIT devicesChanged();
}

QVector<TimingDeviceState> TimingWindow::deviceStates() const
{
    QVector<TimingDeviceState> list;
    for (int s = 1; s <= 2; ++s) {
        const TimingDeviceConfig cfg = m_engine->deviceConfig(s);
        TimingDeviceState st;
        st.slot = s;
        st.type = cfg.type;
        st.link = cfg.link;
        if (TimingDevice* dev = m_devices[size_t(s)]) {
            st.connected = dev->isOpen();
            st.text = dev->description();
        }
        list << st;
    }
    return list;
}

void TimingWindow::setApiState(const QString& text)
{
    if (!m_apiState) {
        m_apiState = new QLabel(this);
        statusBar()->addPermanentWidget(m_apiState);
    }
    m_apiState->setText(text);
}

// ---- refresh ------------------------------------------------------------------------------

void TimingWindow::scheduleRefresh()
{
    if (m_refreshPending)
        return;
    m_refreshPending = true;
    QTimer::singleShot(0, this, [this] {
        m_refreshPending = false;
        refresh();
    });
}

void TimingWindow::refresh()
{
    if (!m_ev->isOpen())
        return;
    {
        // changes made outside of this window (start list, dialogs ...)
        const QSignalBlocker blocker(m_engine);
        m_engine->rebuildFollow();
    }
    applyMinimumColumnWidths();
    fillPointGrid(Point::Start);
    for (int i = 1; i <= m_ev->interCount(); ++i)
        fillPointGrid(i);
    fillPointGrid(Point::Finish);
    fillPaper();
    fillInfo();
    updateLockIndicators();
}

void TimingWindow::fillPointGrid(int point)
{
    QTableWidget* g = point == Point::Start ? m_gridStart : point == Point::Finish ? m_gridFinish : m_gridInter[size_t(point - 1)];
    if (!g)
        return;
    QWidget* focus = QApplication::focusWidget();
    if (focus && focus != g && g->isAncestorOf(focus))
        return; // the operator is editing a cell
    const RunData& run = m_engine->data();
    const bool invert = m_ev->params().timing.invertGrid;
    QVector<const TimeRecord*> recs = run.pointRecords(point);
    if (invert)
        std::reverse(recs.begin(), recs.end());

    QSet<int> selected;
    for (auto* it : g->selectedItems())
        if (auto* first = g->item(it->row(), 0))
            selected.insert(first->data(kRecordIdRole).toInt());

    m_fillingRecords = true;
    g->setUpdatesEnabled(false);
    g->clearSelection();
    g->setRowCount(int(recs.size()) + 1);
    const Precision prec = m_ev->precision();
    // sequence numbers per point in chronological order
    QHash<int, int> seqOf;
    {
        QVector<const TimeRecord*> chrono = recs;
        if (invert)
            std::reverse(chrono.begin(), chrono.end());
        for (int i = 0; i < chrono.size(); ++i)
            seqOf.insert(chrono[i]->id, i + 1);
    }
    const Qt::ItemFlags ro = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    const Qt::ItemFlags rw = ro | Qt::ItemIsEditable;

    // line for the numbers typed for the next impulses (at the top when the grid is inverted)
    const int typedRow = invert ? 0 : int(recs.size());
    {
        QStringList typed;
        for (int b : m_engine->typedQueue(point))
            typed << QString::number(b);
        auto* seq = new QTableWidgetItem;
        seq->setData(kTypedRowRole, true);
        seq->setData(kPointRole, point);
        seq->setFlags(ro);
        auto* st = new QTableWidgetItem(UiHelpers::icon(QStringLiteral("time_add")), QString());
        st->setFlags(ro);
        auto* num = new QTableWidgetItem(typed.join(QLatin1Char(' ')));
        num->setFlags(rw);
        num->setTextAlignment(Qt::AlignCenter);
        auto* h = new QTableWidgetItem(tr("next"));
        h->setFlags(ro);
        h->setForeground(Qt::gray);
        h->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        for (auto* x : { seq, st, num, h })
            x->setBackground(QColor(0xff, 0xf6, 0xc8));
        g->setItem(typedRow, 0, seq);
        g->setItem(typedRow, 1, st);
        g->setItem(typedRow, 2, num);
        g->setItem(typedRow, 3, h);
        for (int c = 4; c < g->columnCount(); ++c) {
            auto* t = new QTableWidgetItem;
            t->setFlags(ro);
            t->setBackground(QColor(0xff, 0xf6, 0xc8));
            g->setItem(typedRow, c, t);
        }
    }

    int paperRow = -1;
    for (int i = 0; i < recs.size(); ++i) {
        const int row = invert ? i + 1 : i;
        const TimeRecord* r = recs.at(i);
        auto* seq = new QTableWidgetItem(QString::number(seqOf.value(r->id)));
        seq->setData(kRecordIdRole, r->id);
        seq->setData(kBibRole, r->bib);
        seq->setData(kPointRole, point);
        seq->setTextAlignment(Qt::AlignCenter);
        seq->setFlags(ro);
        // a passage of a competitor before his start (or a start after one of his passages)
        bool beforeStart = false;
        if (r->bib > 0 && r->isActive() && r->hasTime()) {
            if (point != Point::Start) {
                const TimeRecord* start = m_engine->activeTimeRecord(r->bib, Point::Start);
                beforeStart = start && r->dayTimeUs < start->dayTimeUs;
            } else {
                for (const auto& o : run.records)
                    if (o.bib == r->bib && o.point != Point::Start && o.isActive() && o.hasTime() && o.dayTimeUs < r->dayTimeUs)
                        beforeStart = true;
            }
        }
        const bool duplicate = r->isActive() && r->bib > 0 && m_engine->hasDuplicate(*r);
        auto* st = new QTableWidgetItem;
        st->setFlags(ro);
        if (!r->isActive()) {
            st->setIcon(UiHelpers::icon(QStringLiteral("cancel")));
            st->setToolTip(tr("Inactive passage (click to activate)"));
        } else if (r->bib <= 0) {
            st->setIcon(UiHelpers::icon(QStringLiteral("warning")));
            st->setToolTip(tr("Passage without number"));
        } else if (beforeStart || duplicate) {
            st->setIcon(UiHelpers::icon(QStringLiteral("warning")));
            QStringList why;
            if (beforeStart)
                why << (point == Point::Start ? tr("Another passage of %1 is earlier than this start").arg(r->bib)
                                              : tr("Earlier than the start of %1").arg(r->bib));
            if (duplicate)
                why << tr("%1 has another active passage at this point").arg(r->bib);
            st->setToolTip(why.join(QLatin1Char('\n')));
        } else {
            st->setIcon(UiHelpers::icon(QStringLiteral("check")));
        }
        auto* num = new QTableWidgetItem(r->bib > 0 ? QString::number(r->bib) : QString());
        num->setTextAlignment(Qt::AlignCenter);
        num->setFlags(rw);
        QString hour;
        const ResultStatus rs = r->recordStatus();
        const QString abbr = rs == ResultStatus::DNS ? QStringLiteral("DNS")
                           : rs == ResultStatus::NPS ? QStringLiteral("NPS")
                           : rs == ResultStatus::DNF ? QStringLiteral("DNF")
                           : rs == ResultStatus::DSQ ? QStringLiteral("DSQ")
                                                     : QString();
        if (r->hasTime())
            hour = TimeUtil::formatDayTime(r->dayTimeUs) + (abbr.isEmpty() ? QString() : QStringLiteral(" ") + abbr);
        else if (!abbr.isEmpty())
            hour = abbr;
        else if (r->flags & RecordFlag::NoTime)
            hour = tr("No time");
        else
            hour = TimeUtil::formatDayTime(r->dayTimeUs);
        if (r->flags & (RecordFlag::DeviceKey | RecordFlag::Manual))
            hour = QStringLiteral("M ") + hour;
        auto* h = new QTableWidgetItem(hour);
        h->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        h->setFlags(rw);
        if (!r->isActive()) {
            for (auto* it : { seq, num, h })
                it->setForeground(Qt::gray);
        }
        if (r->flags & RecordFlag::Inserted)
            h->setToolTip(tr("Inserted passage"));
        else if (r->flags & RecordFlag::Manual)
            h->setToolTip(tr("Entered manually"));
        g->setItem(row, 0, seq);
        g->setItem(row, 1, st);
        g->setItem(row, 2, num);
        g->setItem(row, 3, h);
        const int idCol = g->columnCount() - 1;
        if (idCol > 4) {
            // time only on the passage used for the result
            QString t;
            if (beforeStart) {
                t = tr("< start");
            } else if (r->bib > 0 && m_engine->activeTimeRecord(r->bib, point) == r) {
                const TimeInfo info = run.info(r->bib);
                if (point == Point::Finish && info.timeMs >= 0)
                    t = TimeUtil::formatResult(info.timeMs, prec);
                else if (point != Point::Finish && info.interMs[size_t(point - 1)] >= 0)
                    t = TimeUtil::formatResult(info.interMs[size_t(point - 1)], prec);
            }
            auto* ti = new QTableWidgetItem(t);
            ti->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            ti->setFlags(ro);
            g->setItem(row, 4, ti);
        }
        {
            // timing device / sequential number of the device, as on the timing device paper
            QString id;
            if (r->device > 0)
                id = r->deviceSeq > 0 ? QStringLiteral("%1/%2").arg(r->device).arg(r->deviceSeq) : QString::number(r->device);
            auto* di = new QTableWidgetItem(id);
            di->setTextAlignment(Qt::AlignCenter);
            di->setFlags(ro);
            di->setForeground(QColor(0x60, 0x60, 0x60));
            QStringList tip;
            if (r->device > 0)
                tip << tr("Timing device %1, channel %2, sequential number %3")
                           .arg(r->device)
                           .arg(r->deviceChannel)
                           .arg(r->deviceSeq);
            if (!r->note.isEmpty())
                tip << tr("Comment:") + QLatin1Char('\n') + r->note;
            di->setToolTip(tip.join(QStringLiteral("\n\n")));
            g->setItem(row, idCol, di);
        }
        if (r->id == m_paperRecordId) {
            // passage of the line selected on the timing device paper
            for (int c = 0; c < g->columnCount(); ++c)
                if (auto* it = g->item(row, c))
                    it->setBackground(QColor(0x9f, 0xd8, 0xf5));
            paperRow = row;
        } else if (r->isActive() && (beforeStart || duplicate)) {
            const QColor bg = beforeStart ? QColor(0xf6, 0xb8, 0xb8) : QColor(0xfd, 0xdc, 0xa8);
            for (int c = 0; c < g->columnCount(); ++c)
                if (auto* it = g->item(row, c))
                    it->setBackground(bg);
        }
        if (!r->note.isEmpty()) {
            // commented impulse: the Id stays coloured over the highlights of the line
            if (auto* it = g->item(row, g->columnCount() - 1))
                it->setBackground(kCommentColor);
        }
        if (selected.contains(r->id))
            g->selectRow(row);
    }
    g->setUpdatesEnabled(true);
    m_fillingRecords = false;
    if (paperRow >= 0 && m_scrollToPaperRecord) {
        g->scrollToItem(g->item(paperRow, 0), QAbstractItemView::PositionAtCenter);
        m_scrollToPaperRecord = false;
    }
    if (m_editRecordId > 0) {
        for (int row = 0; row < g->rowCount(); ++row) {
            auto* it = g->item(row, 0);
            if (it && !it->data(kTypedRowRole).toBool() && it->data(kRecordIdRole).toInt() == m_editRecordId) {
                m_editRecordId = 0;
                g->setFocus();
                g->selectRow(row);
                g->setCurrentCell(row, 2);
                g->scrollToItem(g->item(row, 2));
                g->editItem(g->item(row, 2));
                break;
            }
        }
    }
}

void TimingWindow::onPaperLineSelected(int row)
{
    // quick visual link: the passage made from the selected line of the timing device paper
    const RunData& run = m_engine->data();
    int found = 0;
    if (row >= 0 && row < run.paper.size()) {
        const PaperLine& p = run.paper.at(row);
        const Cp540Message msg = Cp540Protocol::parse(p.text.toLatin1());
        if (msg.seq > 0 && msg.channel > 0)
            for (const auto& r : run.records)
                if (r.device == p.device && r.deviceSeq == msg.seq && r.deviceChannel == msg.channel)
                    found = r.id;
        if (!found)
            statusBar()->showMessage(tr("No passage for this line"), 5000);
    }
    if (found == m_paperRecordId)
        return;
    m_paperRecordId = found;
    m_scrollToPaperRecord = found > 0;
    for (int point : m_engine->points())
        fillPointGrid(point);
}

void TimingWindow::applyMinimumColumnWidths()
{
    // minimum widths from the contents: Seq 9999, icon, Num 9999, hour to 1/100000 s, Id 9/9999
    QList<QTableWidget*> grids{ m_gridStart, m_gridFinish };
    for (auto* g : m_gridInter)
        grids << g;
    for (QTableWidget* g : grids) {
        if (!g)
            continue;
        const QFontMetrics fm(g->font());
        const QFontMetrics hm(g->horizontalHeader()->font());
        const int pad = 10;
        auto minW = [&](int col, const QString& sample) {
            const QString header = g->horizontalHeaderItem(col) ? g->horizontalHeaderItem(col)->text() : QString();
            return qMax(fm.horizontalAdvance(sample), hm.horizontalAdvance(header)) + pad;
        };
        QList<int> mins;
        mins << minW(0, QStringLiteral("9999"));
        mins << g->style()->pixelMetric(QStyle::PM_SmallIconSize) + 2 * pad;
        mins << minW(2, QStringLiteral("9999"));
        mins << minW(3, QStringLiteral("M 00h00:00.00000"));
        const int idCol = g->columnCount() - 1;
        if (idCol > 4)
            mins << minW(4, QStringLiteral("59:59.99"));
        mins << minW(idCol, QStringLiteral("9/9999"));
        for (int c = 0; c < g->columnCount() && c < mins.size(); ++c) {
            if (c == 1)
                g->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
            if (g->columnWidth(c) < mins.at(c) || c == 1)
                g->setColumnWidth(c, mins.at(c));
        }
        g->horizontalHeader()->setMinimumSectionSize(qMin(mins.at(0), mins.at(2)));
        // the panel cannot be made narrower than the columns
        const bool seq = m_ev->params().timing.showSeqColumn;
        g->setColumnHidden(0, !seq);
        int total = 0;
        for (int i = seq ? 0 : 1; i < mins.size(); ++i)
            total += mins.at(i);
        g->setMinimumWidth(total + g->verticalScrollBar()->sizeHint().width() + 2 * g->frameWidth() + 4);
    }
}

void TimingWindow::fillPaper()
{
    // the paper is only appended: the lines already shown never change
    if (!m_paper)
        return;
    const RunData& run = m_engine->data();
    const int count = int(run.paper.size());
    if (m_paper->rowCount() > count)
        m_paper->setRowCount(0); // run cleared
    const int first = m_paper->rowCount();
    if (first == count)
        return;
    const bool atBottom = m_paper->verticalScrollBar()->value() >= m_paper->verticalScrollBar()->maximum() - 2;
    m_paper->setRowCount(count);
    for (int i = first; i < count; ++i) {
        const PaperLine& p = run.paper.at(i);
        auto set = [&](int col, const QString& text, Qt::Alignment a) {
            auto* it = new QTableWidgetItem(text);
            it->setTextAlignment(a | Qt::AlignVCenter);
            if (p.error) {
                it->setForeground(QColor(0xc0, 0x10, 0x10));
                it->setToolTip(tr("Checksum error"));
            }
            m_paper->setItem(i, col, it);
        };
        set(0, QString::number(p.seq), Qt::AlignCenter);
        set(1, QString::number(p.device), Qt::AlignCenter);
        set(2, p.receivedUs >= 0 ? TimeUtil::formatClock(p.receivedUs, 0) : QString(), Qt::AlignCenter);
        QString text = p.text;
        text.replace(QLatin1Char('	'), QStringLiteral("  "));
        set(3, text, Qt::AlignLeft);
    }
    if (atBottom || first == 0)
        m_paper->scrollToBottom();
}

void TimingWindow::fillInfo()
{
    const RunCounters c = Ranking::counters(*m_ev, m_run);
    const int vals[7] = { c.list, c.classified, c.dns, c.nps, c.dnf, c.dsq, c.notProcessed };
    for (int i = 0; i < 7; ++i)
        m_counters[size_t(i)]->setText(QString::number(vals[i]));

    fillFollow(false);

    // last records
    const RunData& run = m_engine->data();
    const RankingResult runRank = Ranking::runRanking(*m_ev, m_run);
    const RankingResult evRank = Ranking::finalRanking(*m_ev, m_run);
    QHash<int, int> rr, er;
    QHash<int, qint64> et;
    for (const auto& e : runRank.classified)
        rr.insert(e.bib, e.rank);
    for (const auto& e : evRank.classified) {
        er.insert(e.bib, e.rank);
        et.insert(e.bib, e.shownMs);
    }
    QVector<const TimeRecord*> fin;
    for (const TimeRecord* r : run.pointRecords(Point::Finish))
        if (r->bib > 0 && r->isActive())
            fin.prepend(r); // the last one first
    const int nrec = qMin(int(fin.size()), 12);
    m_records->setRowCount(nrec);
    const Precision prec = m_ev->precision();
    for (int i = 0; i < nrec; ++i) {
        const int bib = fin.at(i)->bib;
        const TimeInfo info = run.info(bib);
        QString line2;
        if (info.isClassified()) {
            line2 = QStringLiteral("[%1] %2").arg(rr.value(bib)).arg(TimeUtil::formatResult(info.timeMs, prec));
            if (er.contains(bib))
                line2 += tr(", Event: [%1] %2").arg(er.value(bib)).arg(TimeUtil::formatResult(et.value(bib), prec));
        } else {
            line2 = QStringLiteral("[%1]").arg(UiHelpers::statusText(info.status));
        }
        auto* n = new QTableWidgetItem(QString::number(bib));
        n->setTextAlignment(Qt::AlignCenter);
        m_records->setItem(i, 0, n);
        m_records->setItem(i, 1, new QTableWidgetItem(nameWithField(bib) + QLatin1Char('\n') + line2));
    }

    // ranking (Software Parameters / Ranking): of the event or of the run, by the output groups
    // (ranked inside each group, like the reports), the field in brackets after the name
    const TimingOptions& to = m_ev->params().timing;
    const RankingResult& shown = to.rankingEvent ? evRank : runRank;
    QString title = to.rankingEvent ? tr("Ranking - Event") : tr("Ranking - %1").arg(m_ev->params().type.runName(m_run));
    QVector<QPair<QString, RankingResult>> parts; // (group heading, ranking)
    const QList<OutputGroup>& groups = m_ev->params().outputGroups;
    if (to.rankingByGroups && !groups.isEmpty()) {
        title += tr(" (by groups)");
        for (const OutputGroup& g : groups) {
            RankingResult sub = Ranking::subRanking(shown, [&](int bib) {
                const Competitor* c = m_ev->competitorByBib(bib);
                return c && g.matches(*c);
            }, prec);
            if (!sub.classified.isEmpty())
                parts << qMakePair(g.label, sub);
        }
    } else {
        parts << qMakePair(QString(), shown);
    }
    m_rankingTitle->setText(title);
    int rows = 0;
    for (const auto& p : parts)
        rows += int(p.second.classified.size()) + (p.first.isEmpty() ? 0 : 1);
    m_ranking->clearSpans();
    m_ranking->setRowCount(rows);
    int row = 0;
    for (const auto& p : parts) {
        if (!p.first.isEmpty()) {
            // group heading: white on black, like the printed outputs
            auto* h = new QTableWidgetItem(p.first);
            h->setBackground(Qt::black);
            h->setForeground(Qt::white);
            QFont f = m_ranking->font();
            f.setBold(true);
            h->setFont(f);
            m_ranking->setItem(row, 0, h);
            for (int c = 1; c < m_ranking->columnCount(); ++c)
                m_ranking->setItem(row, c, new QTableWidgetItem);
            m_ranking->setSpan(row, 0, 1, m_ranking->columnCount());
            ++row;
        }
        for (const auto& e : p.second.classified) {
            auto set = [&](int col, const QString& text, Qt::Alignment a) {
                auto* it = new QTableWidgetItem(text);
                it->setTextAlignment(a | Qt::AlignVCenter);
                m_ranking->setItem(row, col, it);
            };
            set(0, QString::number(e.rank), Qt::AlignHCenter);
            set(1, QString::number(e.bib), Qt::AlignHCenter);
            set(2, nameWithField(e.bib), Qt::AlignLeft);
            set(3, TimeUtil::formatResult(e.shownMs, prec), Qt::AlignRight);
            ++row;
        }
    }
}

QString TimingWindow::nameWithField(int bib) const
{
    // Software Parameters / Ranking / "Display this field": in brackets after the name
    QString name = m_ev->competitorName(bib);
    const int field = m_ev->params().timing.rankingField;
    if (field > 0)
        if (const Competitor* c = m_ev->competitorByBib(bib)) {
            const QString extra = c->value(field);
            if (!extra.isEmpty())
                name += QStringLiteral(" (%1)").arg(extra);
        }
    return name;
}

void TimingWindow::updateLockIndicators()
{
    const bool s = m_engine->isLocked(TimingEngine::Group::Start);
    const bool f = m_engine->isLocked(TimingEngine::Group::Finish);
    m_startLock->setIcon(UiHelpers::icon(s ? QStringLiteral("ball_red") : QStringLiteral("ball_green")));
    m_startLock->setText(s ? tr("Locked") : tr("Automatic"));
    m_startLock->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_finishLock->setIcon(UiHelpers::icon(f ? QStringLiteral("ball_red") : QStringLiteral("ball_green")));
    m_finishLock->setText(f ? tr("Locked") : tr("Automatic"));
    m_finishLock->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
}

// ---- selection ---------------------------------------------------------------------------

int TimingWindow::selectedRecordId() const
{
    if (!m_activeGrid || m_followGrids.key(m_activeGrid, INT_MIN) != INT_MIN)
        return 0;
    const auto rows = m_activeGrid->selectionModel()->selectedRows();
    if (rows.isEmpty())
        return 0;
    auto* it = m_activeGrid->item(rows.first().row(), 0);
    return it && !it->data(kTypedRowRole).toBool() ? it->data(kRecordIdRole).toInt() : 0;
}

bool TimingWindow::selectedFollow(int* point, int* bib) const
{
    if (!m_activeGrid)
        return false;
    const int p = m_followGrids.key(m_activeGrid, INT_MIN);
    if (p == INT_MIN)
        return false;
    const auto items = m_activeGrid->selectedItems();
    if (items.isEmpty())
        return false;
    auto* it = m_activeGrid->item(items.first()->row(), 0);
    const int b = it ? it->data(kBibRole).toInt() : 0;
    if (b <= 0)
        return false;
    *point = p;
    *bib = b;
    return true;
}

int TimingWindow::selectedBib() const
{
    int point = 0, bib = 0;
    if (selectedFollow(&point, &bib))
        return bib;
    const TimeRecord* r = m_engine->data().record(selectedRecordId());
    return r ? r->bib : 0;
}

// ---- actions ------------------------------------------------------------------------------

void TimingWindow::toggleLock(int group)
{
    if (group < 0) {
        const bool lock = !(m_engine->isLocked(TimingEngine::Group::Start) && m_engine->isLocked(TimingEngine::Group::Finish));
        m_engine->setLocked(TimingEngine::Group::Start, lock);
        m_engine->setLocked(TimingEngine::Group::Inter, lock);
        m_engine->setLocked(TimingEngine::Group::Finish, lock);
    } else {
        const auto g = TimingEngine::Group(group);
        m_engine->setLocked(g, !m_engine->isLocked(g));
        if (g == TimingEngine::Group::Finish)
            m_engine->setLocked(TimingEngine::Group::Inter, m_engine->isLocked(g));
    }
    updateLockIndicators();
}

void TimingWindow::setSelectedActive(bool active)
{
    if (!m_activeGrid)
        return;
    QSet<int> ids;
    for (const auto& idx : m_activeGrid->selectionModel()->selectedRows())
        if (auto* it = m_activeGrid->item(idx.row(), 0))
            if (!it->data(kTypedRowRole).toBool() && it->data(kRecordIdRole).toInt() > 0)
                ids.insert(it->data(kRecordIdRole).toInt());
    m_ev->beginBatch();
    for (int id : ids)
        m_engine->setRecordActive(id, active);
    m_ev->commitBatch();
}

void TimingWindow::insertPassage()
{
    // SkiPro: a new line in the impulse list of the point; the number and the hour are typed in it
    int point = Point::Finish;
    if (m_activeGrid) {
        const int fp = m_followGrids.key(m_activeGrid, INT_MIN);
        if (fp != INT_MIN)
            point = fp;
        else if (m_activeGrid == m_gridStart)
            point = Point::Start;
        for (int i = 0; i < kMaxInter; ++i)
            if (m_activeGrid == m_gridInter[size_t(i)])
                point = i + 1;
    }
    int id = 0;
    if (const int sel = selectedRecordId())
        id = m_engine->duplicateRecord(sel); // copy of the selected passage (same hour, no number)
    else
        id = m_engine->insertPassage(point, deviceNowUs(), 0);
    if (!id)
        return;
    m_editRecordId = id;
    statusBar()->showMessage(tr("Passage inserted: type its number, the hour can be changed too"), 10000);
    scheduleRefresh();
}

void TimingWindow::insertStartNumber()
{
    // index in the numbers to follow at the start: before the selected number, otherwise as the next one
    int index = 0;
    int p = 0, selBib = 0;
    if (selectedFollow(&p, &selBib) && p == Point::Start)
        index = int(m_engine->follow(Point::Start).indexOf(selBib));
    bool ok = false;
    const int bib = QInputDialog::getInt(this, tr("Insert a number at the start"),
                                         tr("Bib. (inserted into the start list before %1) :")
                                             .arg(m_engine->follow(Point::Start).value(index) > 0
                                                      ? QString::number(m_engine->follow(Point::Start).value(index))
                                                      : tr("the next competitor")),
                                         1, 1, 9999, 1, &ok);
    if (!ok)
        return;
    if (m_engine->activeRecord(bib, Point::Start)) {
        QMessageBox::warning(this, tr("Start"), tr("%1 - %2 has already started.").arg(bib).arg(m_ev->competitorName(bib)));
        return;
    }
    if (m_engine->placeInFollow(Point::Start, bib, qMax(0, index))) {
        scheduleRefresh();
        return;
    }
    if (m_engine->data().startPosition(bib) >= 0) {
        QMessageBox::warning(this, tr("Start"), tr("%1 - %2 has a status (%3).")
                                                    .arg(bib)
                                                    .arg(m_ev->competitorName(bib), UiHelpers::statusText(m_engine->data().info(bib).status)));
        return;
    }
    if (!m_ev->competitorByBib(bib)
        && QMessageBox::question(this, tr("Start"), tr("%1 is not in the competitors list.\nInsert it into the start list anyway?").arg(bib))
            != QMessageBox::Yes)
        return;
    m_engine->insertIntoStartList(bib, qMax(0, index));
    scheduleRefresh();
}

void TimingWindow::competitorManagement()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Competitor management"));
    auto* lay = new QVBoxLayout(&dlg);
    lay->setContentsMargins(4, 4, 4, 4);
    lay->addWidget(new CompetitorsPage(m_ev, &dlg), 1);
    auto* close = new QPushButton(UiHelpers::icon(QStringLiteral("close")), tr("Close"), &dlg);
    connect(close, &QPushButton::clicked, &dlg, &QDialog::accept);
    auto* row = new QHBoxLayout;
    row->addStretch();
    row->addWidget(close);
    lay->addLayout(row);
    const QByteArray geo = AppSettings::value(QStringLiteral("TimingWindow/competitorsGeometry")).toByteArray();
    if (geo.isEmpty() || !dlg.restoreGeometry(geo))
        dlg.resize(1200, 750);
    dlg.exec();
    AppSettings::setValue(QStringLiteral("TimingWindow/competitorsGeometry"), dlg.saveGeometry());
}

void TimingWindow::shiftNumber(int direction)
{
    int point = 0, bib = 0;
    if (!selectedFollow(&point, &bib)) {
        statusBar()->showMessage(tr("Select a number in the numbers to follow of a point."), 10000);
        return;
    }
    // the next number is at the bottom: "up" means later
    m_engine->moveFollow(point, bib, -direction);
    fillFollow(false);
}

void TimingWindow::noTimeSelected()
{
    int point = 0, bib = 0;
    if (!selectedFollow(&point, &bib)) {
        statusBar()->showMessage(tr("Select a number in the numbers to follow of a point."), 10000);
        return;
    }
    m_engine->noTime(point, bib);
}

static QString askReason(QWidget* parent, const QString& title, const QString& label, const QString& def, bool* ok)
{
    return QInputDialog::getText(parent, title, label, QLineEdit::Normal, def, ok);
}

void TimingWindow::setStatusSelected(ResultStatus st)
{
    int point = 0, bib = 0;
    const bool onFollow = selectedFollow(&point, &bib);
    const int recordId = onFollow ? 0 : selectedRecordId();
    const TimeRecord* rec = m_engine->data().record(recordId);
    if (!onFollow && rec)
        bib = rec->bib;
    if (!onFollow && !rec) {
        enterStatusNumber(st);
        return;
    }
    QString reason;
    if (st == ResultStatus::DSQ && bib > 0) {
        bool ok = false;
        reason = askReason(this, tr("Status : Disqualified"),
                           tr("Disqualification of %1 - %2\nReason (gate...) :").arg(bib).arg(m_ev->competitorName(bib)),
                           m_engine->data().info(bib).reason, &ok);
        if (!ok)
            return;
    }
    if (onFollow) {
        m_engine->statusAtPoint(point, bib, st, reason);
        return;
    }
    m_engine->setRecordStatus(recordId, st);
    if (st == ResultStatus::DSQ && bib > 0) {
        TimeInfo info = m_engine->data().info(bib);
        info.bib = bib;
        info.reason = reason;
        m_ev->saveInfo(m_run, info);
    }
}

void TimingWindow::enterStatusNumber(ResultStatus st)
{
    bool ok = false;
    const int bib = QInputDialog::getInt(this, UiHelpers::statusText(st), tr("Bib. :"), qMax(1, selectedBib()), 1, 9999, 1, &ok);
    if (!ok)
        return;
    QString reason;
    if (st == ResultStatus::DSQ) {
        reason = QInputDialog::getText(this, UiHelpers::statusText(st), tr("Reason (gate...) :"), QLineEdit::Normal, QString(), &ok);
        if (!ok)
            return;
    }
    m_engine->setStatus(bib, st, reason);
}

void TimingWindow::searchNumber()
{
    bool ok = false;
    const int bib = QInputDialog::getInt(this, tr("Search for a Number"), tr("Bib. :"), 1, 1, 9999, 1, &ok);
    if (!ok)
        return;
    bool found = false;
    m_searching = true;
    for (QTableWidget* g : { m_gridStart, m_gridInter[0], m_gridInter[1], m_gridInter[2], m_gridFinish }) {
        if (!g)
            continue;
        g->clearSelection();
        for (int r = 0; r < g->rowCount(); ++r) {
            if (g->item(r, 0) && g->item(r, 0)->data(kBibRole).toInt() == bib) {
                g->selectRow(r);
                g->scrollToItem(g->item(r, 0));
                found = true;
                m_activeGrid = g;
            }
        }
    }
    m_searching = false;
    const TimeInfo info = m_engine->data().info(bib);
    statusBar()->showMessage(tr("%1 - %2 : %3 %4")
                                 .arg(bib)
                                 .arg(m_ev->competitorName(bib), UiHelpers::statusText(info.status),
                                      TimeUtil::formatResult(info.timeMs, m_ev->precision())),
                             20000);
    if (!found)
        QApplication::beep();
}

void TimingWindow::informationPenalty()
{
    int bib = selectedBib();
    if (bib <= 0) {
        bool ok = false;
        bib = QInputDialog::getInt(this, tr("Information / Penalty"), tr("Bib. :"), 1, 1, 9999, 1, &ok);
        if (!ok)
            return;
    }
    ModifyDataDialog dlg(m_ev, m_run, bib, this);
    dlg.exec();
}

void TimingWindow::impulseComment()
{
    const TimeRecord* r = m_engine->data().record(selectedRecordId());
    if (!r) {
        statusBar()->showMessage(tr("Select an impulse at the start, an intermediate or the finish"), 5000);
        return;
    }
    int seq = 0;
    const auto rows = m_activeGrid->selectionModel()->selectedRows();
    if (!rows.isEmpty())
        if (auto* it = m_activeGrid->item(rows.first().row(), 0))
            seq = it->text().toInt();
    // impulses keep coming while the dialog is open: the record is found again by its id
    const int id = r->id;
    ImpulseCommentDialog dlg(*r, seq, this);
    if (dlg.exec() == QDialog::Accepted)
        m_engine->setRecordNote(id, dlg.comment());
}

void TimingWindow::quickPenalty(int index)
{
    const qint64 ms = m_ev->params().timing.quickPenaltiesMs[size_t(index)];
    const int bib = selectedBib();
    if (ms <= 0 || bib <= 0)
        return;
    const TimeInfo info = m_engine->data().info(bib);
    m_engine->setPenalty(bib, info.penaltyMs + ms);
    statusBar()->showMessage(tr("Penalty %1 added to %2").arg(TimeUtil::formatResult(ms, m_ev->precision())).arg(bib), 10000);
}

void TimingWindow::editRecordBib(int recordId)
{
    const TimeRecord* r = m_engine->data().record(recordId);
    if (!r)
        return;
    bool ok = false;
    const int bib = QInputDialog::getInt(this, tr("Modify the number"),
                                         tr("%1 %2\nBib. (0 = no number) :").arg(UiHelpers::pointName(r->point),
                                                                                 TimeUtil::formatDayTime(r->dayTimeUs)),
                                         r->bib, 0, 9999, 1, &ok);
    if (ok)
        m_engine->setRecordBib(recordId, bib);
}

void TimingWindow::onUnassigned(int run, int recordId)
{
    if (run != m_run || !m_ev->params().timing.askBibIfUnknown)
        return;
    const TimeRecord* r = m_engine->data().record(recordId);
    if (!r || r->bib > 0 || (r->flags & RecordFlag::Manual))
        return;
    // ask asynchronously so that the impulse handling is never blocked
    QTimer::singleShot(0, this, [this, recordId] {
        const TimeRecord* rec = m_engine->data().record(recordId);
        if (!rec || rec->bib > 0 || !rec->isActive())
            return;
        editRecordBib(recordId);
    });
}

void TimingWindow::softwareParameters()
{
    TimingOptionsDialog dlg(m_ev, this);
    if (dlg.exec() == QDialog::Accepted) {
        EventParams p = m_ev->params();
        p.timing = dlg.options(p.timing);
        m_ev->setParams(p);
    }
}

void TimingWindow::openReport(int type)
{
    ReportDialog dlg(m_ev, ReportType(type), m_run, this);
    dlg.exec();
}

// The actions (DNS, DNF, DSQ, ...) apply to the selection of one grid: the one the operator
// selected last. The selection restored when a grid is filled again does not change it, and
// a selection made by the operator clears the other grids (one competitor marked at a time).
void TimingWindow::gridSelected(QTableWidget* g)
{
    if (m_fillingFollow || m_fillingRecords || m_searching)
        return;
    if (g->selectedItems().isEmpty())
        return;
    m_activeGrid = g;
    for (QTableWidget* other : m_selectGrids)
        if (other != g && !other->selectedItems().isEmpty()) {
            const QSignalBlocker block(other);
            other->clearSelection();
            other->viewport()->update();
        }
}

void TimingWindow::showGridMenu(QTableWidget* g, const QPoint& pos)
{
    m_activeGrid = g;
    const int row = g->rowAt(pos.y());
    if (row >= 0 && !g->selectionModel()->isRowSelected(row, QModelIndex()))
        g->selectRow(row);
    const int followPoint = m_followGrids.key(g, INT_MIN);
    const bool onFollow = followPoint != INT_MIN;
    int point = followPoint;
    if (!onFollow) {
        point = g == m_gridStart ? int(Point::Start) : g == m_gridFinish ? int(Point::Finish) : 0;
        for (int i = 0; i < int(m_gridInter.size()); ++i)
            if (g == m_gridInter[size_t(i)])
                point = i + 1;
    }
    int fp = 0, fb = 0;
    const bool followSel = onFollow && selectedFollow(&fp, &fb);
    const int recordId = onFollow ? 0 : selectedRecordId();
    const bool recordSel = recordId > 0;
    const TimeRecord* rec = m_engine->data().record(recordId);
    const bool hasBib = followSel || (rec && rec->bib > 0);

    // SkiPro: the same menu everywhere, the items not applicable are disabled
    m_aLock->setEnabled(onFollow);
    m_aActive->setEnabled(recordSel);
    m_aInactive->setEnabled(recordSel);
    m_aInsert->setEnabled(!onFollow);
    m_aStartNumber->setEnabled(point == Point::Start);
    m_aUp->setEnabled(followSel);
    m_aDown->setEnabled(followSel);
    m_aTime->setEnabled(followSel || recordSel);
    m_aDns->setEnabled(point == Point::Start && (followSel || hasBib));
    m_aNps->setEnabled(point == Point::Start && (followSel || hasBib));
    m_aDnf->setEnabled(point != Point::Start && (followSel || hasBib));
    m_aDsq->setEnabled(followSel || hasBib);
    m_aNoTime->setEnabled(followSel && point != Point::Start);
    m_aComment->setEnabled(recordSel);

    QMenu menu(this);
    menu.addAction(m_aLock);
    menu.addSeparator();
    menu.addAction(m_aActive);
    menu.addAction(m_aInactive);
    menu.addSeparator();
    menu.addAction(m_aInsert);
    menu.addAction(m_aStartNumber);
    menu.addSeparator();
    menu.addAction(m_aUp);
    menu.addAction(m_aDown);
    menu.addSeparator();
    menu.addAction(m_aTime);
    menu.addAction(m_aDns);
    menu.addAction(m_aNps);
    menu.addAction(m_aDnf);
    menu.addAction(m_aDsq);
    menu.addAction(m_aNoTime);
    menu.addSeparator();
    menu.addAction(m_aSearch);
    menu.addAction(m_aInfo);
    menu.addAction(m_aComment);
    menu.exec(g->viewport()->mapToGlobal(pos));
    // the shortcuts stay available outside of the menu
    for (QAction* a : menu.actions())
        a->setEnabled(true);
}

// ---- window layout ------------------------------------------------------------------------

QString TimingWindow::layoutKey(const QString& name) const
{
    // the layout depends on the number of intermediates
    return QStringLiteral("TimingWindow/i%1/%2").arg(m_ev->interCount()).arg(name);
}

QList<QTableWidget*> TimingWindow::layoutGrids() const
{
    QList<QTableWidget*> grids{ m_gridStart, m_gridFinish, m_paper, m_records, m_ranking };
    for (auto* g : m_gridInter)
        grids << g;
    grids.removeAll(nullptr);
    return grids;
}

void TimingWindow::restoreLayout()
{
    if (m_split)
        m_split->restoreState(AppSettings::value(layoutKey(QStringLiteral("splitter2"))).toByteArray());
    if (m_midSplit)
        m_midSplit->restoreState(AppSettings::value(layoutKey(QStringLiteral("middle"))).toByteArray());
    restoreState(AppSettings::value(QStringLiteral("TimingWindow/docks")).toByteArray());
    for (QSplitter* sp : m_pointSplits)
        sp->restoreState(AppSettings::value(layoutKey(QStringLiteral("point_") + sp->objectName())).toByteArray());
    for (QTableWidget* g : layoutGrids()) {
        const QByteArray st = AppSettings::value(layoutKey(QStringLiteral("columns2_") + g->objectName())).toByteArray();
        if (!st.isEmpty())
            g->horizontalHeader()->restoreState(st);
    }
}

void TimingWindow::saveLayout()
{
    AppSettings::setValue(QStringLiteral("TimingWindow/geometry"), saveGeometry());
    if (m_split)
        AppSettings::setValue(layoutKey(QStringLiteral("splitter2")), m_split->saveState());
    if (m_midSplit)
        AppSettings::setValue(layoutKey(QStringLiteral("middle")), m_midSplit->saveState());
    AppSettings::setValue(QStringLiteral("TimingWindow/docks"), saveState());
    for (QSplitter* sp : m_pointSplits)
        AppSettings::setValue(layoutKey(QStringLiteral("point_") + sp->objectName()), sp->saveState());
    for (QTableWidget* g : layoutGrids())
        AppSettings::setValue(layoutKey(QStringLiteral("columns2_") + g->objectName()), g->horizontalHeader()->saveState());
}

void TimingWindow::showWithSavedGeometry()
{
    const QByteArray geo = AppSettings::value(QStringLiteral("TimingWindow/geometry")).toByteArray();
    if (geo.isEmpty() || !restoreGeometry(geo)) {
        showMaximized();
        return;
    }
    show();
}

bool TimingWindow::eventFilter(QObject* obj, QEvent* e)
{
    if (e->type() == QEvent::FocusIn) {
        if (auto* g = qobject_cast<QTableWidget*>(obj))
            m_activeGrid = g;
    }
    return QMainWindow::eventFilter(obj, e);
}

void TimingWindow::closeEvent(QCloseEvent* e)
{
    bool anyOpen = false;
    for (auto& d : m_devices)
        if (d && d->isOpen())
            anyOpen = true;
    if (anyOpen
        && QMessageBox::question(this, tr("Exit Timing"), tr("The timing devices will be disconnected. Exit the timing?"))
            != QMessageBox::Yes) {
        e->ignore();
        return;
    }
    for (auto& d : m_devices)
        if (d)
            d->close();
    saveLayout();
    e->accept();
}
