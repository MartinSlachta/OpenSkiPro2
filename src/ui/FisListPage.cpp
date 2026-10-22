#include "FisListPage.h"
#include "FisUi.h"
#include "UiHelpers.h"
#include "app/AppSettings.h"
#include "core/Event.h"
#include "db/FisListDb.h"

#include <QApplication>
#include <QCoreApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMap>
#include <QPushButton>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <algorithm>
#include <array>

namespace {

QString esc(const QString& s)
{
    return s.toHtmlEscaped();
}

QString num(double v)
{
    return QString::number(v, 'f', 2);
}

// rows of the open event (category, discipline) and the adder of its level
const char* const kHighlight = "#fff2a8";
const char* const kHighlightCell = "#ffd24d";

QString cellStyle(bool highlight, bool strong = false)
{
    if (strong)
        return QStringLiteral(" style='background-color:%1; font-weight:bold'").arg(QLatin1String(kHighlightCell));
    return highlight ? QStringLiteral(" style='background-color:%1'").arg(QLatin1String(kHighlight)) : QString();
}

// adders are whole numbers on the FIS 1st page
QString adder(double v)
{
    return QString::number(v, 'f', v == int(v) ? 0 : 2);
}

} // namespace

FisListPage::FisListPage(Event* ev, QWidget* parent)
    : QWidget(parent)
    , m_ev(ev)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 4, 6, 4);
    lay->addWidget(UiHelpers::titleBar(tr("FIS list status"), this));
    auto* body = new QHBoxLayout;
    auto* buttons = new QVBoxLayout;
    auto* bImport = UiHelpers::button(QStringLiteral("import"), tr("Import FIS list"), this);
    auto* bReload = UiHelpers::button(QStringLiteral("search"), tr("Refresh"), this);
    buttons->addWidget(bImport);
    buttons->addWidget(bReload);
    buttons->addStretch();
    body->addLayout(buttons);
    m_view = new QTextBrowser(this);
    m_view->setOpenLinks(false);
    body->addWidget(m_view, 1);
    lay->addLayout(body, 1);
    connect(bImport, &QPushButton::clicked, this, &FisListPage::importRequested);
    connect(bReload, &QPushButton::clicked, this, &FisListPage::reload);
    // highlighting of the event category / discipline
    auto reloadVisible = [this] {
        if (isVisible())
            reload();
    };
    connect(ev, &Event::paramsChanged, this, reloadVisible);
    connect(ev, &Event::opened, this, reloadVisible);
    connect(ev, &Event::closed, this, reloadVisible);
}

void FisListPage::reload()
{
    FisList list;
    QString err;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = FisListDb::load(AppSettings::fisListPath(), list, &err);
    QApplication::restoreOverrideCursor();
    if (!ok) {
        m_view->setHtml(QStringLiteral("<p style='color:#c00000'>%1</p>")
                            .arg(esc(tr("Cannot read the FIS list:\n%1").arg(err))));
        return;
    }
    if (list.isEmpty()) {
        m_view->setHtml(QStringLiteral("<h3>%1</h3><p>%2</p>")
                            .arg(esc(tr("No FIS list imported")),
                                 esc(tr("Download the FIS points list (ZIP archive, e.g. ALFP1627F.zip) from the FIS website "
                                        "(Alpine / FIS points) and import it with \"Import FIS list\"."))));
        return;
    }

    const FisListHeader& h = list.header;
    const QDate today = QDate::currentDate();
    QString state, stateColor;
    if (h.isExpired(today)) {
        state = tr("Expired");
        stateColor = QStringLiteral("#c00000");
    } else if (h.validFrom.isValid() && today < h.validFrom) {
        state = tr("Not valid yet");
        stateColor = QStringLiteral("#c07000");
    } else {
        state = tr("Valid");
        stateColor = QStringLiteral("#007000");
    }

    QString html;
    html += QStringLiteral("<style>"
                           "table.g { border-collapse: collapse; }"
                           "table.g td, table.g th { border: 1px solid #808080; padding: 2px 6px; }"
                           "table.g th { background: #dce6f2; }"
                           "td.n { text-align: right; }"
                           "</style>");
    html += QStringLiteral("<div align='center'><h2>%1</h2><h3>%2</h3><h2>%3</h2>"
                           "<p style='font-size: large'><b>%4</b> &nbsp; <b style='color:%5'>[%6]</b></p></div>")
                .arg(esc(tr("Alpine Skiing")), esc(h.seasonText()), esc(h.name), esc(FisUi::validityText(h)),
                     stateColor, esc(state));

    // the open event: its category (FIS race) and discipline are highlighted
    QString eventCat, eventDisc;
    int eventLevel = -1;
    if (m_ev->isOpen()) {
        const EventTypeParams& t = m_ev->params().type;
        eventDisc = fisDisciplineCode(t.discipline);
        QString text;
        if (t.regulation == Regulation::FIS) {
            eventCat = t.fisCategory;
            const FisRaceCategory* rc = fisRaceCategory(eventCat);
            eventLevel = rc ? rc->level : -1;
            text = rc ? tr("Event: %1 - %2 (level %3), %4")
                            .arg(eventCat, QCoreApplication::translate("FisList", rc->description))
                            .arg(rc->level)
                            .arg(disciplineName(t.discipline))
                      : tr("Event: %1, %2").arg(eventCat, disciplineName(t.discipline));
            if (!list.penalty(eventCat))
                text += QStringLiteral(" - ") + tr("category not in the FIS list");
        } else {
            text = tr("Event: national race, %1").arg(disciplineName(t.discipline));
        }
        html += QStringLiteral("<p align='center'><span style='background-color:%1'>&nbsp;%2&nbsp;</span></p>")
                    .arg(QLatin1String(kHighlight), esc(text));
    }

    // categories grouped by level and min / max penalty (FIS 1st page)
    struct Group {
        int level;
        double minP, maxP;
        QStringList codes;
    };
    QList<Group> groups;
    for (const auto& p : list.penalties) {
        const FisRaceCategory* rc = fisRaceCategory(p.catcode);
        const int level = rc ? rc->level : 99;
        bool found = false;
        for (auto& g : groups)
            if (g.level == level && g.minP == p.minPenalty && g.maxP == p.maxPenalty) {
                g.codes << p.catcode;
                found = true;
            }
        if (!found)
            groups << Group{ level, p.minPenalty, p.maxPenalty, { p.catcode } };
    }
    std::sort(groups.begin(), groups.end(), [](const Group& a, const Group& b) {
        if (a.level != b.level)
            return a.level < b.level;
        if (a.minP != b.minP)
            return a.minP < b.minP;
        return a.maxP < b.maxP;
    });
    html += QStringLiteral("<table class='g' align='center' width='80%'><tr><th>%1</th><th>%2</th><th>%3</th><th>%4</th></tr>")
                .arg(esc(tr("Category")), esc(tr("Race level")), esc(tr("Minimum penalty")), esc(tr("Maximum penalty")));
    for (auto& g : groups) {
        g.codes.sort();
        QStringList codes;
        bool hit = false;
        for (const QString& c : g.codes) {
            if (!eventCat.isEmpty() && c.compare(eventCat, Qt::CaseInsensitive) == 0) {
                codes << QStringLiteral("<b>%1</b>").arg(esc(c));
                hit = true;
            } else {
                codes << esc(c);
            }
        }
        html += QStringLiteral("<tr><td%5>%1</td><td class='n'%5>%2</td><td class='n'%5>%3</td><td class='n'%5>%4</td></tr>")
                    .arg(codes.join(QLatin1Char(',')), g.level == 99 ? QStringLiteral("?") : QString::number(g.level),
                         num(g.minP), num(g.maxP), cellStyle(hit));
    }
    html += QStringLiteral("</table><br>");

    // discipline constants: MEN / GENERAL / WOMEN
    html += QStringLiteral("<table class='g' align='center'><tr><th colspan='7'>%1</th><th colspan='2'>%2</th>"
                           "<th colspan='7'>%3</th></tr><tr>")
                .arg(esc(tr("MEN")), esc(tr("GENERAL")), esc(tr("WOMEN")));
    const QString side = QStringLiteral("<th>%1</th><th>%2 0</th><th>%2 1</th><th>%2 2</th><th>%2 3</th><th>%2 4</th><th>%3</th>")
                             .arg(esc(tr("Disc.")), esc(tr("Level")), esc(tr("Z-Value")));
    html += side + QStringLiteral("<th>%1</th><th>%2</th>").arg(esc(tr("F Value")), esc(tr("Max points"))) + side
        + QStringLiteral("</tr>");
    auto sideCells = [eventLevel](const FisDisciplineParams* d, const QString& code, bool hit) {
        const QString st = cellStyle(hit);
        if (!d)
            return QStringLiteral("<td%2>%1</td>").arg(code, st) + QStringLiteral("<td%1></td>").arg(st).repeated(6);
        QString s = QStringLiteral("<td%2><b>%1</b></td>").arg(code, st);
        for (int i = 0; i < int(d->adders.size()); ++i)
            s += QStringLiteral("<td class='n'%2>%1</td>").arg(adder(d->adders[size_t(i)]), cellStyle(hit, hit && i == eventLevel));
        s += QStringLiteral("<td class='n'%2>%1</td>").arg(num(d->zValue), st);
        return s;
    };
    for (const char* c : { "DH", "SL", "GS", "SG", "AC" }) {
        const QString code = QLatin1String(c);
        const FisDisciplineParams* m = list.disciplineParams(code, Gender::Male);
        const FisDisciplineParams* w = list.disciplineParams(code, Gender::Female);
        const FisDisciplineParams* g = m ? m : w;
        const bool hit = code == eventDisc;
        const QString st = cellStyle(hit);
        html += QStringLiteral("<tr>") + sideCells(m, code, hit);
        html += g ? QStringLiteral("<td class='n'%3>%1</td><td class='n'%3>%2</td>").arg(num(g->fValue), num(g->maxPoints), st)
                  : QStringLiteral("<td%1></td><td%1></td>").arg(st);
        html += sideCells(w, code, hit) + QStringLiteral("</tr>");
    }
    html += QStringLiteral("</table><br>");

    // imported data
    int men = 0, women = 0;
    std::array<int, kDisciplineCount> withPoints{};
    for (const auto& c : list.competitors) {
        if (c.gender == Gender::Male)
            ++men;
        else if (c.gender == Gender::Female)
            ++women;
        for (int i = 0; i < kDisciplineCount; ++i)
            if (c.points[size_t(i)] >= 0)
                ++withPoints[size_t(i)];
    }
    QStringList pts;
    for (int i = 0; i < kDisciplineCount; ++i)
        pts << QStringLiteral("%1: %2").arg(fisDisciplineCode(Discipline(i))).arg(withPoints[size_t(i)]);
    const QLocale loc;
    const QList<QPair<QString, QString>> info = {
        { tr("List number"), QStringLiteral("%1 (%2)").arg(h.number).arg(h.code()) },
        { tr("List ID"), QString::number(h.listId) },
        { tr("Season"), h.seasonText() },
        { tr("Calculation date"), FisUi::formatDate(h.calculationDate) },
        { tr("Races taken into account"), tr("%1 to %2").arg(FisUi::formatDate(h.startRaceDate), FisUi::formatDate(h.endRaceDate)) },
        { tr("Last update (FIS)"), h.lastUpdate },
        { tr("Imported competitors"), tr("%1 (men %2, women %3)").arg(list.competitors.size()).arg(men).arg(women) },
        { tr("Competitors with points"), pts.join(QStringLiteral(", ")) },
        { tr("Imported on"), loc.toString(list.importedAt, QLocale::ShortFormat) },
        { tr("Imported from"), list.source },
    };
    html += QStringLiteral("<table class='g' align='center' width='80%'>");
    for (const auto& kv : info)
        html += QStringLiteral("<tr><th align='left'>%1</th><td>%2</td></tr>").arg(esc(kv.first), esc(kv.second));
    html += QStringLiteral("</table>");
    m_view->setHtml(html);
}
