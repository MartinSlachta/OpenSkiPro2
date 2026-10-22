// Developer helper: "OpenSkiPro2 --ui-snapshots <outdir> <SkiPro EventNNN.scdb>"
// imports the SkiPro event into a temporary event, builds the main windows and
// dialogs without showing them on screen and saves them as PNG images, plus
// every report as PDF and PNG. Used to check the UI and the printed documents.
#include "Snapshots.h"
#include "MainWindow.h"
#include "TimingWindow.h"
#include "app/AppSettings.h"
#include <QFileInfo>
#include "core/Event.h"
#include "db/SkiAlpImporter.h"
#include "db/SkiProImporter.h"
#include "dialogs/OutputGroupsDialog.h"
#include "dialogs/EventTypeDialog.h"
#include "dialogs/AssignBibsDialog.h"
#include "dialogs/DoubleDrawDialog.h"
#include "core/BibDraw.h"
#include <QRandomGenerator>
#include "dialogs/FisImportDialog.h"
#include "dialogs/FisUpdateDialog.h"
#include "dialogs/FisPenaltyDialog.h"
#include "dialogs/RefereeReportDialog.h"
#include "FisCompetitorsWindow.h"
#include "db/FisListDb.h"
#include "dialogs/GeneralParametersDialog.h"
#include "dialogs/HeaderDialog.h"
#include "dialogs/ModifyDataDialog.h"
#include "dialogs/PrintingParametersDialog.h"
#include "dialogs/ReportDialog.h"
#include "dialogs/StartListDialog.h"
#include "dialogs/TimingDeviceDialog.h"
#include "dialogs/TimingOptionsDialog.h"
#include "dialogs/UnprocessedDialog.h"
#include "report/ReportBuilder.h"
#include "report/ReportRenderer.h"
#include "timing/Cp540Simulator.h"

#include <QListView>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QElapsedTimer>
#include <QHeaderView>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>

static void shot(QWidget* w, const QString& file, QSize size = QSize())
{
    w->setAttribute(Qt::WA_DontShowOnScreen, true);
    if (size.isValid())
        w->resize(size);
    w->show();
    QApplication::processEvents();
    QApplication::processEvents();
    w->grab().save(file);
    w->hide();
}

int runSnapshots(const QString& outDir, const QString& skiproFile)
{
    QDir().mkpath(outDir);
    QTemporaryDir tmp;
    AppSettings::setEventsDir(tmp.path());
    const QString path = MainWindow::eventPath(1);

    SkiProImporter::Result data;
    QString err;
    // an OpenSkiPro2 event (*.osp) is used as it is (copy)
    if (skiproFile.endsWith(QLatin1String(".osp"), Qt::CaseInsensitive)) {
        if (!QFile::copy(skiproFile, path)) {
            qWarning("cannot copy %s", qPrintable(skiproFile));
            return 1;
        }
    } else {
    // a SkiAlp Pro archive (*.spro) or a SkiPro event
    const bool skiAlp = skiproFile.endsWith(QLatin1String(".spro"), Qt::CaseInsensitive);
    const QString logos = QFileInfo(skiproFile).absolutePath() + QStringLiteral("/../Logos");
    if (!(skiAlp ? SkiAlpImporter::read(skiproFile, data, &err, logos) : SkiProImporter::read(skiproFile, data, &err, logos))) {
        qWarning("import: %s", qPrintable(err));
        return 1;
    }
    {
        Event ev;
        if (!ev.create(path, &err) || !SkiProImporter::apply(data, ev, &err)) {
            qWarning("create: %s", qPrintable(err));
            return 1;
        }
    }
    }

    MainWindow mw;
    mw.openEventFile(path);
    shot(&mw, outDir + QStringLiteral("/01_main_competitors.png"), QSize(1500, 900));
    mw.showRunsPage();
    shot(&mw, outDir + QStringLiteral("/02_main_runs.png"), QSize(1500, 900));

    Event* ev = mw.event();
    // an OpenSkiPro2 event: its documents as they are, before the changes made for the other snapshots
    if (skiproFile.endsWith(QLatin1String(".osp"), Qt::CaseInsensitive)) {
        const QList<QPair<ReportType, int>> docs = {
            { ReportType::StartList, 1 },     { ReportType::StartList, 2 },          { ReportType::RunRanking, 1 },
            { ReportType::RunRanking, 2 },    { ReportType::FinalRanking, 2 },       { ReportType::FinalRankingPoints, 2 },
            { ReportType::PenaltyCalculation, 2 },
        };
        for (const auto& d : docs) {
            const ReportDocument doc = ReportBuilder::build(*ev, ReportFormat::defaults(d.first, d.second), d.second);
            ReportRenderer renderer(*ev, doc);
            const QString base = QStringLiteral("%1/60_%2_run%3").arg(outDir, ReportFormat::storageId(d.first)).arg(d.second);
            renderer.exportPdf(base + QStringLiteral(".pdf"));
            for (int p = 0; p < renderer.pageCount(); ++p)
                renderer.renderPage(p, 110).save(base + QStringLiteral("_p%1.png").arg(p + 1));
        }
        // Report by the Referee of each run
        for (int r = 1; r <= ev->runCount(); ++r) {
            RefereeReportDialog dlg(ev, r, &mw);
            if (r == 1)
                shot(&dlg, outDir + QStringLiteral("/61_referee_dialog.png"));
            ReportRenderer renderer(*ev, dlg.document());
            const QString base = QStringLiteral("%1/61_referee_run%2").arg(outDir).arg(r);
            renderer.exportPdf(base + QStringLiteral(".pdf"));
            renderer.renderPage(0, 110).save(base + QStringLiteral("_p1.png"));
        }
    }
    {
        EventParams p = ev->params();
        p.type.runNames[0] = QStringLiteral("1.kolo"); // names of the runs in the outputs
        p.type.runNames[1] = QStringLiteral("2.kolo");
        // no dialog asking for the bib of an impulse during the simulated timing (nothing is on screen)
        p.timing.askBibIfUnknown = false;
        ev->setParams(p);
    }
    // FIS list: archive given by OSP_FIS_ARCHIVE (e.g. ALFP1627F.zip), imported into the snapshot settings folder
    const QString fisArchive = qEnvironmentVariable("OSP_FIS_ARCHIVE");
    if (!fisArchive.isEmpty()) {
        {
            FisImportDialog d(&mw);
            d.setSource(fisArchive);
            shot(&d, outDir + QStringLiteral("/40_fis_import.png"));
        }
        FisList list;
        if (FisListReader::read(fisArchive, list, &err) && FisListDb::save(AppSettings::fisListPath(), list, &err)) {
            mw.showFisStatus();
            shot(&mw, outDir + QStringLiteral("/41_fis_status.png"), QSize(1500, 1000));
            FisCompetitorsWindow w(ev, list, &mw);
            shot(&w, outDir + QStringLiteral("/42_fis_competitors.png"), QSize(1100, 700));
            FisUpdateDialog u(ev, list, &mw);
            shot(&u, outDir + QStringLiteral("/43_fis_update.png"));
            EventTypeDialog t(ev->params(), &mw);
            shot(&t, outDir + QStringLiteral("/44_event_type_fis.png"));
            // FIS points / penalty: constants of the list, kept in the event for the reports
            FisPenaltyDialog pd(ev, &mw);
            shot(&pd, outDir + QStringLiteral("/50_fis_penalty.png"));
            pd.accept();
            for (ReportType t : { ReportType::FinalRankingPoints, ReportType::PenaltyCalculation }) {
                const ReportDocument doc = ReportBuilder::build(*ev, ReportFormat::defaults(t), ev->runCount());
                ReportRenderer renderer(*ev, doc);
                const QString base = QStringLiteral("%1/51_%2").arg(outDir, ReportFormat::storageId(t));
                renderer.exportPdf(base + QStringLiteral(".pdf"));
                renderer.renderPage(0, 110).save(base + QStringLiteral("_p1.png"));
            }
            // list of the race categories (popup of the combo box)
            for (auto* combo : t.findChildren<QComboBox*>())
                if (combo->count() > 10) {
                    QListView v;
                    v.setModel(combo->model());
                    shot(&v, outDir + QStringLiteral("/45_event_type_fis_categories.png"),
                         QSize(360, v.sizeHintForRow(1) * combo->count() + 4));
                }
        } else {
            qWarning("FIS list: %s", qPrintable(err));
        }
    }
    {
        AssignBibsDialog d(ev, &mw);
        shot(&d, outDir + QStringLiteral("/46_assign_bibs.png"));
        const QVector<Competitor> list = ev->competitors();
        const Discipline disc = ev->params().type.discipline;
        QRandomGenerator rng(7);
        DoubleDrawDialog dd(list, BibDraw::byPoints(list, disc, rng), disc, &mw);
        shot(&dd, outDir + QStringLiteral("/47_double_draw.png"));
        // equal points at the 7th / 8th rank: marked, then the first group increased
        QVector<Competitor> tied = list;
        QRandomGenerator rng2(7);
        const QVector<int> order = BibDraw::byPoints(tied, disc, rng2);
        if (order.size() > 8) {
            tied[order.at(7)].points[size_t(disc)] = tied.at(order.at(6)).pointsFor(disc);
            DoubleDrawDialog dt(tied, order, disc, &mw);
            shot(&dt, outDir + QStringLiteral("/48_double_draw_tie.png"));
            if (auto* cb = dt.findChild<QCheckBox*>())
                cb->setChecked(true);
            shot(&dt, outDir + QStringLiteral("/49_double_draw_increased.png"));
        }
    }
    {
        GeneralParametersDialog d(ev->params(), &mw);
        shot(&d, outDir + QStringLiteral("/10_general.png"));
    }
    {
        EventTypeDialog d(ev->params(), &mw);
        shot(&d, outDir + QStringLiteral("/11_event_type.png"));
    }
    {
        PrintingParametersDialog d(ev, &mw);
        shot(&d, outDir + QStringLiteral("/12_printing.png"));
    }
    {
        HeaderDialog d(ev->params(), &mw);
        shot(&d, outDir + QStringLiteral("/13_header.png"));
    }
    {
        StartListDialog d(ev, 2, &mw);
        shot(&d, outDir + QStringLiteral("/14_start_list.png"));
    }
    {
        ModifyDataDialog d(ev, 1, 101, &mw);
        shot(&d, outDir + QStringLiteral("/15_modify_data.png"));
    }
    {
        TimingDeviceConfig cfg;
        cfg.type = TimingDeviceConfig::Type::CP540;
        TimingDeviceDialog d(1, cfg, &mw);
        shot(&d, outDir + QStringLiteral("/16_timing_device.png"));
    }
    {
        ReportDialog d(ev, ReportType::FinalRanking, 2, &mw);
        shot(&d, outDir + QStringLiteral("/17_report_final.png"));
    }
    {
        // start list: a layout for each run (run 1 list points, run 2 the time of run 1)
        ReportDialog d(ev, ReportType::StartList, 1, &mw);
        shot(&d, outDir + QStringLiteral("/17b_report_start_run1.png"));
        if (auto* runBox = d.findChild<QComboBox*>())
            runBox->setCurrentIndex(1);
        shot(&d, outDir + QStringLiteral("/17c_report_start_run2.png"));
    }
    {
        TimingWindow tw(ev, 1);
        shot(&tw, outDir + QStringLiteral("/20_timing_run1.png"), QSize(1900, 1000));
    }
    // ranking of the timing window: run ranking filtered by the first output group
    if (!ev->params().outputGroups.isEmpty()) {
        const EventParams before = ev->params();
        EventParams p = before;
        p.timing.rankingEvent = false;
        p.timing.rankingByGroups = true;
        ev->setParams(p);
        {
            TimingOptionsDialog d(ev, &mw);
            if (auto* tabs = d.findChild<QTabWidget*>())
                tabs->setCurrentIndex(2);
            shot(&d, outDir + QStringLiteral("/16b_timing_options_ranking.png"));
            if (auto* tabs = d.findChild<QTabWidget*>())
                tabs->setCurrentIndex(1);
            shot(&d, outDir + QStringLiteral("/16c_timing_options_timing.png"));
        }
        {
            TimingWindow tw(ev, 1);
            shot(&tw, outDir + QStringLiteral("/20b_timing_run1_ranking_filter.png"), QSize(1900, 1000));
        }
        ev->setParams(before);
    }

    // simulated timing session through the real timing window and the CP540 simulator
    {
        TimingDeviceConfig sim;
        sim.type = TimingDeviceConfig::Type::Simulator;
        sim.startChannel = 1;
        sim.interChannels = { 2, 0, 0 };
        sim.finishChannel = 4;
        AppSettings::setTimingDevice(1, sim);
        EventParams p = ev->params();
        p.type.interCount = 1;
        p.timing.confirmInsertStartList = false; // no message box in this mode
        ev->setParams(p);
        ev->clearRun(2, false, true);
        if (ev->run(2).startList.isEmpty())
            ev->setStartList(2, ev->run(1).startList);
        TimingWindow tw(ev, 2);
        tw.setAttribute(Qt::WA_DontShowOnScreen, true);
        tw.resize(1900, 1000);
        tw.show();
        Cp540Simulator* device = tw.findChild<Cp540Simulator*>();
        auto wait = [](int ms) {
            QElapsedTimer t;
            t.start();
            while (t.elapsed() < ms)
                QApplication::processEvents(QEventLoop::AllEvents, 20);
        };
        // the operator types an unknown number in the green line of the start
        for (QTableWidget* g : tw.findChildren<QTableWidget*>()) {
            if (!g->toolTip().startsWith(QStringLiteral("Numbers to follow")) || g->rowCount() == 0 || !g->item(g->rowCount() - 1, 0))
                continue;
            if (g->item(g->rowCount() - 1, 0)->flags() & Qt::ItemIsEditable) {
                g->item(g->rowCount() - 1, 0)->setText(QStringLiteral("150")); // first follow grid = start
                wait(50);
                break;
            }
        }
        if (device) {
            for (int i = 0; i < 4; ++i) {
                device->trigger(1);
                wait(120);
            }
            device->trigger(2);
            wait(1300);
            tw.grab().save(outDir + QStringLiteral("/22_timing_on_course.png"));
            device->trigger(4);
            wait(150);
            device->dropNextFrame(); // lost frame: recovered by the recall mechanism
            device->trigger(4);
            wait(150);
            device->trigger(4);
            wait(800);
        }
        tw.grab().save(outDir + QStringLiteral("/21_timing_simulated.png"));
        // click on a line of the timing device paper: its passage is highlighted
        if (auto* paper = tw.findChild<QTableWidget*>(QStringLiteral("devicepaper")); paper && paper->rowCount() > 4) {
            paper->setCurrentCell(4, 0);
            wait(100);
            tw.grab().save(outDir + QStringLiteral("/23_timing_paper_link.png"));
        }
        const RunData& run = ev->run(2);
        int unassigned = 0;
        for (const auto& r : run.records)
            if (r.bib == 0)
                ++unassigned;
        qInfo("simulated run 2: %d records, %d unassigned, %d results", int(run.records.size()), unassigned,
              int(run.infos.size()));
        // disconnect first: closing with connected devices would ask for a confirmation on screen
        for (TimingDevice* d : tw.findChildren<TimingDevice*>())
            d->close();
        tw.close();
    }

    // three intermediates: one column under the other, like SkiPro
    {
        EventParams p = ev->params();
        const int before = p.type.interCount;
        p.type.interCount = 3;
        ev->setParams(p);
        {
            TimingWindow tw(ev, 1);
            shot(&tw, outDir + QStringLiteral("/24_timing_3_inters.png"), QSize(1900, 1000));
        }
        p.type.interCount = before;
        ev->setParams(p);
    }

    const QList<QPair<ReportType, int>> reports = {
        { ReportType::Competitors, 1 },      { ReportType::CompetitorsPoints, 1 }, { ReportType::StartList, 1 },
        { ReportType::StartList, 2 },        { ReportType::RunRanking, 1 },        { ReportType::RankingAfterRun, 1 },
        { ReportType::FinalRanking, 2 },     { ReportType::TimeRecords, 1 },       { ReportType::TimingPaper, 2 },
        { ReportType::FinalRankingPoints, 2 }, { ReportType::PenaltyCalculation, 2 },
    };
    // a gate on the first disqualification of run 1 (printed at the end of its line)
    for (const TimeInfo& info : ev->run(1).infos)
        if (info.status == ResultStatus::DSQ && info.reason.isEmpty()) {
            TimeInfo t = info;
            t.reason = QStringLiteral("12");
            ev->saveInfo(1, t);
            break;
        }
    {
        UnprocessedDialog d(ev, 1, &mw);
        shot(&d, outDir + QStringLiteral("/25_unprocessed_run1.png"));
    }
    for (const auto& r : reports) {
        ReportFormat f = ReportFormat::defaults(r.first, r.second);
        const ReportDocument doc = ReportBuilder::build(*ev, f, r.second);
        ReportRenderer renderer(*ev, doc);
        const QString base = QStringLiteral("%1/30_%2_run%3").arg(outDir, ReportFormat::storageId(r.first)).arg(r.second);
        renderer.exportPdf(base + QStringLiteral(".pdf"));
        for (int p = 0; p < renderer.pageCount(); ++p)
            renderer.renderPage(p, 110).save(base + QStringLiteral("_p%1.png").arg(p + 1));
    }
    // output groups: the dialog and the final ranking by groups
    {
        OutputGroupsDialog d(ev, &mw);
        shot(&d, outDir + QStringLiteral("/18_output_groups.png"));
        if (!ev->params().outputGroups.isEmpty()) {
            OutputGroup g = ev->params().outputGroups.first();
            QDialog* edit = nullptr;
            QTimer::singleShot(0, &mw, [&] {
                for (QWidget* w : QApplication::topLevelWidgets())
                    if (auto* dlg = qobject_cast<QDialog*>(w); dlg && dlg->isVisible() && dlg != &d)
                        edit = dlg;
                if (edit) {
                    edit->grab().save(outDir + QStringLiteral("/19_output_group_edit.png"));
                    edit->reject();
                }
            });
            OutputGroupsDialog::editGroup(ev, g, &mw);
        }
        // run 1: run 2 was replaced by the simulated session above
        ReportFormat f = ReportFormat::defaults(ReportType::RunRanking);
        f.byGroups = true;
        const ReportDocument doc = ReportBuilder::build(*ev, f, 1);
        ReportRenderer renderer(*ev, doc);
        const QString base = outDir + QStringLiteral("/31_run1_by_groups");
        renderer.exportPdf(base + QStringLiteral(".pdf"));
        for (int p = 0; p < renderer.pageCount() && p < 3; ++p)
            renderer.renderPage(p, 110).save(base + QStringLiteral("_p%1.png").arg(p + 1));
    }
    // report columns: club on a second line under the name, bold time, own title, narrower table
    {
        ReportFormat f = ReportFormat::defaults(ReportType::RunRanking);
        for (auto& col : f.fields) {
            if (col.field == CompField::Name) {
                col.field2 = CompField::Club;
                col.title = QStringLiteral("Competitor / Club");
            }
            if (col.field == ReportField::Time)
                col.bold = true;
        }
        f.tableWidth = 90;
        const ReportDocument doc = ReportBuilder::build(*ev, f, 1);
        ReportRenderer renderer(*ev, doc);
        renderer.renderPage(0, 110).save(outDir + QStringLiteral("/32_columns_two_lines_p1.png"));
    }
    mw.event()->close();
    return 0;
}
