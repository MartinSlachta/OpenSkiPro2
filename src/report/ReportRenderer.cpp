#include "ReportRenderer.h"
#include "RefereeReport.h"
#include "core/FisPenalty.h"
#include <QRegularExpression>
#include "core/Event.h"

#include <QCoreApplication>
#include <QSet>
#include <functional>
#include <QDateTime>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QHash>
#include <QLocale>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>

namespace {
struct Tr {
    Q_DECLARE_TR_FUNCTIONS(Report)
};

constexpr double kRefDpi = 1200.0;

const QImage& refDevice()
{
    static const QImage img = [] {
        QImage i(4, 4, QImage::Format_ARGB32);
        const int dpm = int(kRefDpi / 0.0254);
        i.setDotsPerMeterX(dpm);
        i.setDotsPerMeterY(dpm);
        return i;
    }();
    return img;
}

QImage cachedImage(const QString& path)
{
    static QHash<QString, QImage> cache;
    auto it = cache.find(path);
    if (it == cache.end())
        it = cache.insert(path, QImage(path));
    return *it;
}

QFont scaled(const QFont& f, int pt, bool bold)
{
    QFont x = f;
    x.setPointSize(pt);
    x.setBold(bold);
    return x;
}
} // namespace

ReportRenderer::ReportRenderer(const Event& ev, const ReportDocument& doc)
    : m_ev(ev)
    , m_doc(doc)
{
    const PrintingParams& pp = ev.params().printing;
    m_pageMm = doc.format.landscape ? QSizeF(297, 210) : QSizeF(210, 297);
    m_fonts[FTitle1] = pp.fontTitle1;
    m_fonts[FTitle2] = pp.fontTitle2;
    m_fonts[FDocTitle] = pp.fontDocTitle;
    m_fonts[FFooter] = pp.fontFooter;
    m_fonts[FLabels] = pp.fontLabels;
    m_fonts[FLines] = pp.fontLines;
    m_fonts[FLinesBold] = pp.fontLines;
    m_fonts[FLinesBold].setBold(true);
    m_fonts[FHeader] = scaled(pp.fontLines, 7, false);
    m_fonts[FHeaderBold] = scaled(pp.fontLines, 7, true);
    m_fonts[FSection] = scaled(pp.fontLabels, pp.fontLines.pointSize(), true);
    m_fonts[FBar] = scaled(pp.fontLines, 7, true);
    for (auto& f : m_fonts)
        f.setHintingPreference(QFont::PreferNoHinting);
    layout();
}

QString ReportRenderer::footerProgramText()
{
    return QStringLiteral("OpenSkiPro2 %1").arg(QStringLiteral(OSP_VERSION));
}

double ReportRenderer::textWidth(int font, const QString& s) const
{
    return QFontMetricsF(m_fonts[size_t(font)], &refDevice()).horizontalAdvance(s) / (kRefDpi / 25.4);
}

double ReportRenderer::lineHeight(int font) const
{
    return QFontMetricsF(m_fonts[size_t(font)], &refDevice()).height() / (kRefDpi / 25.4);
}

void ReportRenderer::addText(Page& page, const QRectF& r, const QString& text, int font, int flags, const QColor& c) const
{
    if (text.isEmpty())
        return;
    Op op;
    op.kind = Op::Text;
    op.r = r;
    op.text = text;
    op.font = font;
    op.flags = flags;
    op.color = c;
    page << op;
}

void ReportRenderer::addLine(Page& page, double x1, double y1, double x2, double y2, double w) const
{
    Op op;
    op.kind = Op::Line;
    op.r = QRectF(QPointF(x1, y1), QPointF(x2, y2));
    op.width = w;
    page << op;
}

std::array<int, 4> ReportRenderer::margins(bool first) const
{
    const auto& pp = m_ev.params().printing;
    return first ? pp.marginsFirst : pp.marginsOther;
}

double ReportRenderer::layoutTitles(Page& page, double y, bool first) const
{
    const auto& pp = m_ev.params().printing;
    const auto& g = m_ev.params().general;
    const auto m = margins(first);
    const double x = m[2];
    const double w = m_pageMm.width() - m[2] - m[3];
    const int align = first ? pp.titleAlignFirst : pp.titleAlignOther;
    const int h = align == 0 ? Qt::AlignLeft : align == 2 ? Qt::AlignRight : Qt::AlignHCenter;
    auto title = [&](const QString& text, int font) {
        if (text.trimmed().isEmpty())
            return;
        for (const QString& line : text.split(QLatin1Char('\n'))) {
            const double lh = lineHeight(font);
            addText(page, QRectF(x, y, w, lh), line.trimmed(), font, h | Qt::AlignVCenter | Qt::TextDontClip);
            y += lh * 1.15;
        }
    };
    if (pp.showTitle1)
        title(g.title1, FTitle1);
    if (pp.showTitle2)
        title(g.title2, FTitle2);
    y += 1.0;
    title(m_doc.docTitle, FDocTitle);
    return y + 2.0;
}

double ReportRenderer::layoutHeaderBlock(Page& page, double y) const
{
    const auto& hp = m_ev.params().header;
    const auto m = margins(true);
    const double cw = m_pageMm.width() - m[2] - m[3];
    const double bw = qMin(cw * 0.92, 190.0);
    const double x0 = m[2] + (cw - bw) / 2.0;
    const double leftW = bw * 0.56;
    const double rightX = x0 + leftW + bw * 0.02;
    const double rightW = bw - (rightX - x0);
    const double rh = lineHeight(FHeader) * 1.05;
    const double barH = lineHeight(FBar) * 1.1;
    const int vc = Qt::AlignVCenter;

    auto bar = [&](double x, double yy, double w, const QString& text) {
        Op f;
        f.kind = Op::Fill;
        f.r = QRectF(x, yy, w, barH);
        f.color = Qt::black;
        page << f;
        addText(page, QRectF(x + 0.8, yy, w - 1.6, barH), text, FBar, Qt::AlignLeft | vc, Qt::white);
    };
    auto val = [&](const QString& key) { return hp.value(key).trimmed(); };
    auto nat = [&](const QString& key) {
        const QString n = val(key + QStringLiteral(".nation"));
        return n.isEmpty() ? QString() : QStringLiteral("(%1)").arg(n);
    };

    // --- jury / weather (FIS Alpine Data Booklet, appendix)
    bar(x0, y, leftW, Tr::tr("COMPETITION JURY"));
    bar(rightX, y, rightW, Tr::tr("WEATHER"));
    double yl = y + barH + 0.6;
    double yr = yl;
    const double nameX = x0 + leftW * 0.4;
    struct Role {
        const char* key;
        QString label;
        bool always;
    };
    const QList<Role> roles = {
        { HeaderKeys::TechnicalDelegate, Tr::tr("TECHNICAL DELEGATE"), true },
        { HeaderKeys::ChiefOfRace, Tr::tr("CHIEF OF RACE"), true },
        { HeaderKeys::Referee, Tr::tr("REFEREE"), true },
        { HeaderKeys::AssistantReferee, Tr::tr("ASSISTANT REFEREE"), false },
        { HeaderKeys::CourseChief, Tr::tr("COURSE CHIEF"), true },
        { HeaderKeys::StartReferee, Tr::tr("START REFEREE"), true },
        { HeaderKeys::FinishReferee, Tr::tr("FINISH REFEREE"), true },
    };
    for (const auto& r : roles) {
        const QString key = QLatin1String(r.key);
        const QString name = val(key + QStringLiteral(".name"));
        if (!r.always && name.isEmpty())
            continue;
        addText(page, QRectF(x0, yl, nameX - x0, rh), r.label, FHeader, Qt::AlignLeft | vc);
        addText(page, QRectF(nameX, yl, leftW * 0.5, rh), name, FHeader, Qt::AlignLeft | vc);
        addText(page, QRectF(x0, yl, leftW, rh), nat(key), FHeader, Qt::AlignRight | vc);
        yl += rh;
    }
    auto weatherRow = [&](const QString& label, const QString& value) {
        if (value.isEmpty())
            return;
        addText(page, QRectF(rightX, yr, rightW * 0.5, rh), label, FHeader, Qt::AlignLeft | vc);
        addText(page, QRectF(rightX, yr, rightW, rh), value, FHeader, Qt::AlignRight | vc);
        yr += rh;
    };
    auto degrees = [&](const char* key) {
        const QString v = val(QLatin1String(key));
        return v.isEmpty() ? QString() : v + QStringLiteral(" °C");
    };
    weatherRow(Tr::tr("CONDITIONS"), val(QLatin1String(HeaderKeys::Weather)));
    weatherRow(Tr::tr("SNOW"), val(QLatin1String(HeaderKeys::Snow)));
    weatherRow(Tr::tr("TEMPERATURE START"), degrees(HeaderKeys::TempStart));
    weatherRow(Tr::tr("TEMPERATURE FINISH"), degrees(HeaderKeys::TempFinish));
    y = qMax(yl, yr) + 2.0;

    // --- runs: course setters, forerunners and technical data of each run
    const int runs = qMin(2, m_ev.runCount());
    const double run1X = nameX;
    const double run1W = x0 + leftW - run1X;
    const double runX[2] = { run1X, rightX };
    const double runW[2] = { run1W, rightW };
    const auto& runNames = m_ev.params().type.runNames;
    bar(run1X, y, run1W, runNames[0].isEmpty() ? Tr::tr("RUN 1") : runNames[0].toUpper());
    if (runs > 1)
        bar(rightX, y, rightW, runNames[1].isEmpty() ? Tr::tr("RUN 2") : runNames[1].toUpper());
    y += barH + 0.6;
    // "- A -" of the forerunners: run 1 in the labels column, run 2 at the start of its block
    // (the names of run 2 are indented by it)
    const double letterW = 9.0;
    auto runPerson = [&](int r, const QString& prefix, const QString& key) {
        const double x = runX[r - 1], w = runW[r - 1];
        const double nx = r == 1 ? x : x + letterW;
        if (!prefix.isEmpty())
            addText(page, QRectF(nx - letterW, y, letterW, rh), prefix, FHeader, Qt::AlignLeft | vc);
        addText(page, QRectF(nx, y, w * 0.7, rh), val(key + QStringLiteral(".name")), FHeader, Qt::AlignLeft | vc);
        addText(page, QRectF(x, y, w, rh), nat(key), FHeader, Qt::AlignRight | vc);
    };
    addText(page, QRectF(x0, y, run1X - x0, rh), Tr::tr("COURSESETTER"), FHeader, Qt::AlignLeft | vc);
    for (int r = 1; r <= runs; ++r)
        runPerson(r, QString(), QStringLiteral("run%1.setter").arg(r));
    y += rh;
    bool anyForerunner = false;
    for (int r = 1; r <= runs; ++r)
        for (char c = 'A'; c <= 'E'; ++c)
            if (!val(QStringLiteral("run%1.fore%2.name").arg(r).arg(QLatin1Char(c))).isEmpty())
                anyForerunner = true;
    if (anyForerunner) {
        addText(page, QRectF(x0, y, run1X - x0, rh), Tr::tr("FORERUNNERS"), FHeader, Qt::AlignLeft | vc);
        for (char c = 'A'; c <= 'E'; ++c) {
            bool rowUsed = false;
            for (int r = 1; r <= runs; ++r)
                if (!val(QStringLiteral("run%1.fore%2.name").arg(r).arg(QLatin1Char(c))).isEmpty())
                    rowUsed = true;
            if (!rowUsed)
                continue;
            for (int r = 1; r <= runs; ++r)
                runPerson(r, QStringLiteral("- %1 -").arg(QLatin1Char(c)), QStringLiteral("run%1.fore%2").arg(r).arg(QLatin1Char(c)));
            y += rh;
        }
    }
    y += rh * 0.3;
    auto runText = [&](const QString& label, const std::function<QString(int)>& value) {
        addText(page, QRectF(x0, y, run1X - x0, rh), label, FHeader, Qt::AlignLeft | vc);
        for (int r = 1; r <= runs; ++r) {
            const double nx = r == 1 ? runX[0] : runX[1] + letterW;
            addText(page, QRectF(nx, y, runW[r - 1], rh), value(r), FHeader, Qt::AlignLeft | vc);
        }
        y += rh;
    };
    // course data of each run (a run may be on another course)
    auto course = [&](const char* key) {
        return [&hp, key](int r) { return hp.runValue(r, QLatin1String(key)).trimmed(); };
    };
    auto meters = [&](const char* key) {
        return [&hp, key](int r) {
            const QString v = hp.runValue(r, QLatin1String(key)).trimmed();
            return v.isEmpty() ? QString() : v + QStringLiteral(" m");
        };
    };
    auto perRun = [&](const QString& suffix) {
        return [&val, suffix](int r) { return val(QStringLiteral("run%1.%2").arg(r).arg(suffix)); };
    };
    runText(Tr::tr("COURSE"), course(HeaderKeys::Course));
    runText(Tr::tr("START"), meters(HeaderKeys::StartAltitude));
    runText(Tr::tr("FINISH"), meters(HeaderKeys::FinishAltitude));
    // the drop: start - finish when not typed
    runText(Tr::tr("DROP"), [&hp](int r) {
        QString v = hp.runValue(r, QLatin1String(HeaderKeys::Drop)).trimmed();
        if (v.isEmpty())
            v = HeaderParams::dropOf(hp.runValue(r, QLatin1String(HeaderKeys::StartAltitude)),
                                     hp.runValue(r, QLatin1String(HeaderKeys::FinishAltitude)));
        return v.isEmpty() ? QString() : v + QStringLiteral(" m");
    });
    bool anyLength = false;
    for (int r = 1; r <= runs; ++r)
        anyLength = anyLength || !hp.runValue(r, QLatin1String(HeaderKeys::Length)).trimmed().isEmpty();
    if (anyLength)
        runText(Tr::tr("LENGTH"), meters(HeaderKeys::Length));
    runText(Tr::tr("HOMOLOGATION"), course(HeaderKeys::Homologation));
    runText(Tr::tr("GATES"), perRun(QStringLiteral("gates")));
    runText(Tr::tr("DIRECTIONS"), perRun(QStringLiteral("directions")));
    runText(Tr::tr("STARTING TIME"), perRun(QStringLiteral("starttime")));

    // --- list, penalty, F value and number of competitors / nations
    y += 0.8;
    addLine(page, x0, y, x0 + bw, y, 0.25);
    y += 0.4;
    const auto& fp = m_ev.params().fisPoints;
    const ReportType rt = m_doc.format.type;
    const bool fis = m_ev.params().type.regulation == Regulation::FIS;
    const bool ranking = rt == ReportType::RunRanking || rt == ReportType::RankingAfterRun || rt == ReportType::FinalRanking
        || rt == ReportType::FinalRankingPoints || rt == ReportType::PenaltyCalculation;
    const bool withPenalty = rt == ReportType::FinalRankingPoints || rt == ReportType::PenaltyCalculation;
    QVector<FisPenalty::Result> penalties;
    double fValue = 0;
    if (fis && ranking)
        for (Gender g : FisPenalty::genders(m_ev)) {
            if (!fp.forGender(g).isSet())
                continue;
            fValue = fp.forGender(g).fValue;
            const FisPenalty::Result r = FisPenalty::compute(m_ev, g);
            if (r.valid && withPenalty)
                penalties << r;
        }
    QString list = val(QLatin1String(HeaderKeys::ListNumber));
    if (list.isEmpty() && fis)
        list = fp.list;
    QStringList listLine;
    if (!list.isEmpty())
        listLine << Tr::tr("LIST n°%1").arg(list);
    if (penalties.size() == 1)
        listLine << Tr::tr("APPLIED PENALTY %1").arg(TimeUtil::formatPoints(penalties.first().applied))
                 << Tr::tr("CALCULATED PENALTY %1").arg(TimeUtil::formatPoints(penalties.first().calculated));
    if (!listLine.isEmpty() || fValue > 0) {
        addText(page, QRectF(x0, y, bw, rh), listLine.join(QStringLiteral("      ")), FHeader, Qt::AlignLeft | vc);
        if (fValue > 0)
            addText(page, QRectF(x0, y, bw, rh), Tr::tr("F = %1").arg(fValue, 0, 'f', fValue == int(fValue) ? 0 : 2),
                    FHeader, Qt::AlignRight | vc);
        y += rh;
    }
    if (penalties.size() > 1)
        for (const FisPenalty::Result& r : penalties) {
            addText(page, QRectF(x0, y, bw, rh),
                    (r.gender == Gender::Female ? Tr::tr("WOMEN") : Tr::tr("MEN")) + QStringLiteral("  ")
                        + Tr::tr("APPLIED PENALTY %1").arg(TimeUtil::formatPoints(r.applied)) + QStringLiteral("      ")
                        + Tr::tr("CALCULATED PENALTY %1").arg(TimeUtil::formatPoints(r.calculated)),
                    FHeader, Qt::AlignLeft | vc);
            y += rh;
        }
    QSet<QString> nations;
    int competitors = 0;
    for (const auto& c : m_ev.competitors()) {
        if (m_ev.isForerunner(c.bib))
            continue;
        ++competitors;
        if (!c.nation.trimmed().isEmpty())
            nations.insert(c.nation.trimmed().toUpper());
    }
    addText(page, QRectF(x0, y, bw, rh),
            Tr::tr("Number of competitors %1, Number of NSA %2").arg(competitors).arg(nations.size()), FHeader,
            Qt::AlignLeft | vc);
    y += rh;
    return y + 3.0;
}

// FIS form "Report by the Referee" (2022): the same boxes as the PDF of FIS, filled in. The
// grids of numbers and the lines of disqualifications grow when the form has too few of them.
void ReportRenderer::layoutRefereeReport()
{
    const RefereeReportData& d = *m_doc.referee;
    Page page;
    const double x0 = 15.0, w = 180.0, x1 = x0 + w;
    auto X = [&](double px) { return x0 + (px - 89.0) * w / 651.0; }; // columns measured on the FIS form
    // the head (logo, bands) and the foot (bands, federation) are the images of the FIS form
    auto image = [&](const QString& path, const QRectF& r) {
        Op op;
        op.kind = Op::Image;
        op.text = path;
        op.r = r;
        page << op;
    };
    auto box = [&](double l, double t, double r, double b) {
        addLine(page, l, t, r, t, 0.2);
        addLine(page, l, b, r, b, 0.2);
        addLine(page, l, t, l, b, 0.2);
        addLine(page, r, t, r, b, 0.2);
    };
    const int vc = Qt::AlignVCenter;
    auto label = [&](double l, double t, double r, double h, const QString& text) {
        addText(page, QRectF(l + 1.0, t + 0.3, r - l - 2.0, h), text, FHeaderBold, Qt::AlignLeft | Qt::AlignTop);
    };
    auto value = [&](double l, double t, double r, double h, const QString& text, int align = Qt::AlignLeft) {
        addText(page, QRectF(l + 1.0, t, r - l - 2.0, h), text, FLinesBold, align | vc);
    };

    // head: logo, bands, title (placed as on the form, the bands run off the page)
    image(QStringLiteral(":/fis/referee_head.jpg"), QRectF(0.5, 5.0, 217.2, 21.2));
    double y = 26.0;
    addText(page, QRectF(x0, y, w, 7.0), Tr::tr("REPORT BY THE REFEREE"), FSection, Qt::AlignLeft | vc);
    addText(page, QRectF(x0, y, w, 7.0), Tr::tr("Page %1/%2").arg(1).arg(1), FHeader, Qt::AlignRight | vc);
    y += 9.0;

    // event
    const double r1 = 11.5, r2 = 7.0;
    box(x0, y, x1, y + r1);
    addLine(page, X(331), y, X(331), y + r1);
    addLine(page, X(586), y, X(586), y + r1);
    label(x0, y, X(331), 4.5, Tr::tr("Place"));
    label(X(331), y, X(586), 4.5, Tr::tr("Country"));
    label(X(586), y, x1, 4.5, Tr::tr("Codex"));
    value(x0, y + 5.0, X(331), 6.0, d.place);
    value(X(331), y + 5.0, X(586), 6.0, d.country);
    value(X(586), y + 5.0, x1, 6.0, d.codex);
    y += r1;
    box(x0, y, x1, y + r2);
    addLine(page, X(486), y, X(486), y + r2);
    label(x0, y, X(486), r2, Tr::tr("Name of the Event"));
    value(x0 + 27.0, y, X(486), r2, d.eventName);
    label(X(486), y, x1, r2, Tr::tr("Date"));
    value(X(486) + 9.0, y, x1, r2, d.date);
    y += r2;
    box(x0, y, x1, y + r2);
    addLine(page, X(286), y, X(286), y + r2);
    addLine(page, X(486), y, X(486), y + r2);
    label(x0, y, X(286), r2, Tr::tr("Category"));
    value(x0 + 15.0, y, X(286), r2, d.category);
    label(X(286), y, X(486), r2, Tr::tr("Gender"));
    value(X(286) + 13.0, y, X(486), r2, d.gender);
    label(X(486), y, x1, r2, Tr::tr("Event"));
    value(X(486) + 10.0, y, x1, r2, d.event);
    y += r2;
    box(x0, y, x1, y + r2);
    label(x0, y, x1, r2, Tr::tr("The following competitors have been disqualified according to ICR:"));
    y += r2;

    // disqualifications
    const double cols[] = { X(89), X(137), X(333), X(370), X(453), X(601), X(740) };
    const QStringList heads = { Tr::tr("No."), Tr::tr("Name - Surname"), Tr::tr("Nat."), Tr::tr("Gate Number."),
                                Tr::tr("Gate Judge"), Tr::tr("Notes") };
    const double headH = 9.5, rowH = 8.8;
    const int rows = qMax(9, int(d.disqualified.size()));
    const double tableBottom = y + headH + rows * rowH;
    box(x0, y, x1, tableBottom);
    for (int c = 1; c < 6; ++c)
        addLine(page, cols[c], y, cols[c], tableBottom);
    for (int c = 0; c < 6; ++c)
        label(cols[c], y, cols[c + 1], headH, heads.at(c));
    addLine(page, x0, y + headH, x1, y + headH);
    for (int r = 0; r < rows; ++r) {
        const double ry = y + headH + r * rowH;
        if (r > 0)
            addLine(page, x0, ry, x1, ry);
        if (r >= d.disqualified.size())
            continue;
        const auto& q = d.disqualified.at(r);
        value(cols[0], ry, cols[1], rowH, q.bib, Qt::AlignHCenter);
        value(cols[1], ry, cols[2], rowH, q.name);
        value(cols[2], ry, cols[3], rowH, q.nation, Qt::AlignHCenter);
        value(cols[3], ry, cols[4], rowH, q.gate, Qt::AlignHCenter);
        value(cols[5], ry, cols[6], rowH, q.notes);
    }
    y = tableBottom;

    // grids of numbers: first line after the label (10 boxes), the next ones 13 boxes
    const double cellW = (x1 - X(255)) / 10.0;
    const double smallCols[] = { X(89), X(153), X(208), X(255) };
    auto grid = [&](const QString& title, const QStringList& bibs, int minLines) {
        const double h = 8.0;
        const int extra = qMax(minLines - 1, (qMax(0, int(bibs.size()) - 10) + 12) / 13);
        const double bottom = y + h * (1 + extra);
        box(x0, y, x1, bottom);
        addText(page, QRectF(x0 + 1.0, y + 0.3, X(255) - x0 - 2.0, h), title, FHeaderBold, Qt::AlignLeft | Qt::AlignTop);
        int n = 0;
        for (int line = 0; line <= extra; ++line) {
            const double ly = y + line * h;
            if (line > 0)
                addLine(page, x0, ly, x1, ly);
            QVector<QRectF> cells;
            if (line > 0)
                for (int c = 0; c < 3; ++c)
                    cells << QRectF(smallCols[c], ly, smallCols[c + 1] - smallCols[c], h);
            for (int c = 0; c < 10; ++c)
                cells << QRectF(X(255) + c * cellW, ly, cellW, h);
            for (const QRectF& cell : cells) {
                if (cell.left() > x0 + 0.1)
                    addLine(page, cell.left(), ly, cell.left(), ly + h);
                if (n < bibs.size())
                    addText(page, cell, bibs.at(n++), FLinesBold, Qt::AlignHCenter | vc);
            }
        }
        y = bottom;
    };
    grid(Tr::tr("Did not start (N°)"), d.didNotStart, 3);
    grid(Tr::tr("Not permitted to start (N°)"), d.notPermitted, 1);
    grid(Tr::tr("Did not finish (N°)"), d.didNotFinish, 4);

    // posting, deadline of the protests, referee
    const double fh = 10.0;
    box(x0, y, x1, y + fh);
    for (double px : { 265.0, 386.0, 484.0 })
        addLine(page, X(px), y, X(px), y + fh);
    label(x0, y, X(265), 4.5, Tr::tr("Time published"));
    value(x0, y + 4.5, X(265), 5.0, d.published, Qt::AlignHCenter);
    label(X(265), y, X(386), 4.5, Tr::tr("Deadline"));
    value(X(265), y + 4.5, X(386), 5.0, d.deadline, Qt::AlignHCenter);
    label(X(386), y, X(484), 4.5, Tr::tr("Date"));
    value(X(386), y + 4.5, X(484), 5.0, d.date, Qt::AlignHCenter);
    label(X(484), y, x1, 4.5, Tr::tr("The Referee"));
    value(X(484), y + 4.5, x1, 5.0, d.referee);

    // foot: bands and the federation, then the program
    image(QStringLiteral(":/fis/referee_foot.png"), QRectF(0.0, 278.1, 195.3, 9.06));
    addText(page, QRectF(x0, m_pageMm.height() - 8.0, w, 4.0), footerProgramText(), FFooter, Qt::AlignLeft | vc);
    m_pages << page;
}

void ReportRenderer::layoutLogos(Page& page, bool first) const
{
    const auto& pp = m_ev.params().printing;
    if (pp.logosFirstPageOnly && !first)
        return;
    for (const auto& logo : pp.logos) {
        if (logo.path.isEmpty() || !QFileInfo::exists(logo.path) || logo.rect.width() <= 0 || logo.rect.height() <= 0)
            continue;
        Op op;
        op.kind = Op::Image;
        op.text = logo.path;
        op.r = QRectF(logo.rect.x() * m_pageMm.width(), logo.rect.y() * m_pageMm.height(),
                      logo.rect.width() * m_pageMm.width(), logo.rect.height() * m_pageMm.height());
        page.prepend(op);
    }
}

void ReportRenderer::layoutFooter(Page& page, int pageNo, int pageCount) const
{
    const auto& g = m_ev.params().general;
    const auto m = margins(pageNo == 0);
    const double x = m[2];
    const double w = m_pageMm.width() - m[2] - m[3];
    const double lh = lineHeight(FFooter);
    const double footerH = lh * 2.4;
    double y = m_pageMm.height() - m[1] - footerH;

    QStringList left;
    if (g.date.isValid())
        left << QLocale::system().toString(g.date, QLocale::ShortFormat);
    QString loc = g.location;
    if (!g.nation.isEmpty())
        loc += (loc.isEmpty() ? QString() : QStringLiteral(" ")) + QStringLiteral("(%1)").arg(g.nation);
    if (!loc.isEmpty())
        left << loc;
    if (!g.organisation.isEmpty())
        left << g.organisation;
    QStringList right;
    if (!g.codex.isEmpty())
        right << g.codex;
    const QDateTime now = QDateTime::currentDateTime();
    right << Tr::tr("%1 at %2")
                 .arg(QLocale::system().toString(now.date(), QLocale::ShortFormat),
                      QLocale::system().toString(now.time(), QStringLiteral("HH:mm")));
    right << Tr::tr("Page %1/%2").arg(pageNo + 1).arg(pageCount);

    addText(page, QRectF(x, y, w, lh), left.join(QStringLiteral(" / ")), FFooter, Qt::AlignLeft | Qt::AlignVCenter);
    addText(page, QRectF(x, y, w, lh), right.join(QStringLiteral(" / ")), FFooter, Qt::AlignRight | Qt::AlignVCenter);
    y += lh + 0.4;
    addLine(page, x, y, x + w, y, 0.3);
    y += 0.4;
    if (m_ev.params().printing.showAppLogo)
        addText(page, QRectF(x, y, w, lh), footerProgramText(), FFooter, Qt::AlignLeft | Qt::AlignVCenter);
    addText(page, QRectF(x, y, w, lh), g.timingBy, FFooter, Qt::AlignRight | Qt::AlignVCenter);
}

void ReportRenderer::layout()
{
    m_pages.clear();
    if (m_doc.referee) {
        layoutRefereeReport();
        return;
    }
    const auto& pp = m_ev.params().printing;
    const ReportFormat& fmt = m_doc.format;
    const int nCols = qBound(1, fmt.columns, 4);
    const double frameGap = 6.0;
    const double footerH = lineHeight(FFooter) * 2.4;
    // cells with a second line (e.g. the club under the name) are two lines high
    const bool twoLines = fmt.hasSecondLines();
    const double headerRowH = lineHeight(FLabels) * (twoLines ? 2.3 : 1.3);
    const double lineH = lineHeight(FLines) * 1.2;
    const double rowH = twoLines ? lineHeight(FLines) * 2.25 : lineH;
    const double sectionH = lineHeight(FSection) * 1.8;

    // column geometry (relative to the frame origin)
    auto columnGeometry = [&](double frameW) {
        QVector<double> widths;
        double total = 0;
        for (const auto& c : fmt.fields)
            total += qMax(1, c.width);
        const double charW = textWidth(FLines, QStringLiteral("0"));
        for (const auto& c : fmt.fields) {
            double w = pp.autoAdjust || total <= 0 ? frameW * qMax(1, c.width) / total : qMax(1, c.width) * charW * 1.15;
            widths << w;
        }
        double sum = 0;
        for (double w : widths)
            sum += w;
        if (sum > frameW && sum > 0)
            for (double& w : widths)
                w *= frameW / sum;
        return widths;
    };

    struct State {
        Page page;
        bool first = true;
        double top = 0;      // top of the frames area
        double bottom = 0;   // content bottom
        double x = 0;        // content left
        double w = 0;        // content width
        int frame = 0;
        double cursor = 0;
        int rowsOnPage = 0;
    } st;
    QVector<double> colW;
    double frameW = 0;

    double frameSlot = 0; // width available for one frame; the table may use less of it
    auto frameX = [&]() { return st.x + st.frame * (frameSlot + frameGap) + (frameSlot - frameW) / 2.0; };
    auto drawColumnHeader = [&]() {
        if (fmt.fields.isEmpty())
            return;
        double cx = frameX();
        for (int i = 0; i < fmt.fields.size(); ++i) {
            const int a = fmt.fields.at(i).align;
            const int h = a == 0 ? Qt::AlignLeft : a == 1 ? Qt::AlignHCenter : Qt::AlignRight;
            const QStringList lines = m_doc.columnLabels.value(i).split(QLatin1Char('\n'));
            const double lh = headerRowH / lines.size();
            for (int l = 0; l < lines.size(); ++l)
                addText(st.page, QRectF(cx + 0.6, st.cursor + l * lh, colW[i] - 1.2, lh), lines.at(l), FLabels,
                        h | Qt::AlignVCenter | Qt::TextDontClip);
            cx += colW[i];
        }
        st.cursor += headerRowH;
        addLine(st.page, frameX(), st.cursor, frameX() + frameW, st.cursor, 0.3);
        st.cursor += 0.8;
    };
    auto startPage = [&]() {
        const bool first = m_pages.isEmpty();
        st.page.clear();
        st.first = first;
        const auto m = margins(first);
        st.x = m[2];
        st.w = m_pageMm.width() - m[2] - m[3];
        st.bottom = m_pageMm.height() - m[1] - footerH - 2.0;
        double y = layoutTitles(st.page, m[0], first);
        if (first && fmt.printHeader && !m_ev.params().general.useFreeHeader)
            y = layoutHeaderBlock(st.page, y);
        st.top = y;
        st.frame = 0;
        st.cursor = st.top;
        st.rowsOnPage = 0;
        frameSlot = (st.w - (nCols - 1) * frameGap) / nCols;
        frameW = frameSlot * qBound(30, fmt.tableWidth, 100) / 100.0;
        colW = columnGeometry(frameW);
        drawColumnHeader();
    };
    auto finishPage = [&]() { m_pages << st.page; };
    auto nextFrame = [&]() {
        if (st.frame + 1 < nCols) {
            ++st.frame;
            st.cursor = st.top;
            drawColumnHeader();
        } else {
            finishPage();
            startPage();
        }
    };
    auto ensure = [&](double h) {
        const bool limit = st.first && fmt.linesFirstPage > 0 && st.rowsOnPage >= fmt.linesFirstPage;
        if (st.cursor + h > st.bottom || limit)
            nextFrame();
    };

    // "time (rank)" cells: the times aligned on the right of a block, the ranks in a column of
    // their own after them, so that one and two digit ranks do not shift the times
    static const QRegularExpression rankRe(QStringLiteral("^(.*\\S) (\\(\\d+\\))$"));
    QVector<double> timeW(fmt.fields.size(), 0.0), rankW(fmt.fields.size(), 0.0);
    for (const auto& section : m_doc.sections)
        for (const auto& row : section.rows)
            for (int i = 0; i < fmt.fields.size(); ++i) {
                const QString first = row.value(i).section(QLatin1Char('\n'), 0, 0);
                const auto m = rankRe.match(first);
                if (!m.hasMatch())
                    continue;
                const int font = fmt.fields.at(i).bold ? FLinesBold : FLines;
                timeW[i] = qMax(timeW[i], textWidth(font, m.captured(1)));
                rankW[i] = qMax(rankW[i], textWidth(font, m.captured(2)));
            }
    auto drawCell = [&](int i, const QRectF& r, const QString& text, int font, int h) {
        const auto m = rankW[i] > 0 ? rankRe.match(text) : QRegularExpressionMatch();
        if (!m.hasMatch()) {
            addText(st.page, r, text, font, h | Qt::AlignVCenter);
            return;
        }
        const double gap = textWidth(font, QStringLiteral(" "));
        const double block = qMin(r.width(), timeW[i] + 0.5 + gap + rankW[i]);
        double x = r.left();
        if (h & Qt::AlignRight)
            x = r.right() - block;
        else if (h & Qt::AlignHCenter)
            x = r.left() + (r.width() - block) / 2.0;
        const double tw = block - gap - rankW[i];
        addText(st.page, QRectF(x, r.top(), tw, r.height()), m.captured(1), font, Qt::AlignRight | Qt::AlignVCenter | Qt::TextDontClip);
        addText(st.page, QRectF(x + tw + gap, r.top(), rankW[i] + 0.5, r.height()), m.captured(2), font,
                Qt::AlignLeft | Qt::AlignVCenter | Qt::TextDontClip);
    };

    startPage();
    const QColor gray = QColor::fromHsvF(0, 0, 1.0 - qBound(0, pp.grayLevel, 100) / 100.0);
    for (const auto& section : m_doc.sections) {
        if (section.groupHeading) {
            // output group: white label on a black band, like SkiPro / SkiAlp
            ensure(sectionH + rowH);
            st.cursor += sectionH * 0.25;
            const double w = qMin(frameW, textWidth(FSection, section.heading) + 6.0);
            Op band;
            band.kind = Op::Fill;
            band.r = QRectF(frameX(), st.cursor, w, sectionH * 0.75);
            band.color = Qt::black;
            st.page << band;
            addText(st.page, QRectF(frameX() + 2.0, st.cursor, w - 3.0, sectionH * 0.75), section.heading, FSection,
                    Qt::AlignLeft | Qt::AlignVCenter, Qt::white);
            st.cursor += sectionH * 0.75 + sectionH * 0.15;
        } else if (!section.heading.isEmpty()) {
            ensure(sectionH + rowH);
            st.cursor += sectionH * 0.35;
            addText(st.page, QRectF(frameX(), st.cursor, frameW, sectionH * 0.65), section.heading, FSection,
                    Qt::AlignLeft | Qt::AlignVCenter);
            st.cursor += sectionH * 0.65;
        }
        if (section.textRows) {
            // label across the table, value in the cell of a column (white on a dark box when asked)
            int alt = 0;
            for (const QStringList& row : section.rows) {
                ensure(rowH);
                const double fx = frameX();
                if (pp.grayRows && (alt++ % 2) == 0 && !row.value(1).isEmpty()) {
                    Op f;
                    f.kind = Op::Fill;
                    f.r = QRectF(fx, st.cursor, frameW, rowH);
                    f.color = gray;
                    st.page << f;
                }
                addText(st.page, QRectF(fx + 0.6, st.cursor, frameW * 0.75, rowH), row.value(0), FLines,
                        Qt::AlignLeft | Qt::AlignVCenter);
                const int col = row.value(3).toInt();
                QRectF cell(fx + frameW * 0.5, st.cursor, frameW * 0.5, rowH);
                if (col >= 0 && col < colW.size()) {
                    double cx = fx;
                    for (int i = 0; i < col; ++i)
                        cx += colW[i];
                    cell = QRectF(cx, st.cursor, colW[col], rowH);
                }
                const QString style = row.value(2);
                const bool box = style == QLatin1String("box") || style == QLatin1String("black");
                if (box) {
                    Op f;
                    f.kind = Op::Fill;
                    f.r = cell.adjusted(0, 0.1, 0, -0.1);
                    f.color = style == QLatin1String("black") ? QColor(Qt::black) : QColor(0x50, 0x50, 0x50);
                    st.page << f;
                }
                addText(st.page, cell.adjusted(0.6, 0, -0.6, 0), row.value(1), box ? FLinesBold : FLines,
                        Qt::AlignRight | Qt::AlignVCenter, box ? QColor(Qt::white) : QColor(Qt::black));
                st.cursor += rowH;
                ++st.rowsOnPage;
            }
            continue;
        }
        int alt = 0;
        for (int ri = 0; ri < section.rows.size(); ++ri) {
            const QStringList& row = section.rows.at(ri);
            ensure(rowH);
            const double fx = frameX();
            if (pp.grayRows && (alt % 2) == 0) {
                Op f;
                f.kind = Op::Fill;
                f.r = QRectF(fx, st.cursor, frameW, rowH);
                f.color = gray;
                st.page << f;
            }
            double cx = fx;
            for (int i = 0; i < fmt.fields.size(); ++i) {
                const int a = fmt.fields.at(i).align;
                const int h = a == 0 ? Qt::AlignLeft : a == 1 ? Qt::AlignHCenter : Qt::AlignRight;
                const QString text = row.value(i);
                const int font = fmt.fields.at(i).bold ? FLinesBold : FLines;
                const int nl = int(text.indexOf(QLatin1Char('\n')));
                if (twoLines) {
                    const double top = st.cursor + (rowH - 2 * lineH) / 2.0;
                    drawCell(i, QRectF(cx + 0.6, top, colW[i] - 1.2, lineH), nl < 0 ? text : text.left(nl), font, h);
                    if (nl >= 0)
                        addText(st.page, QRectF(cx + 0.6, top + lineH, colW[i] - 1.2, lineH), text.mid(nl + 1), FLines,
                                h | Qt::AlignVCenter);
                } else {
                    drawCell(i, QRectF(cx + 0.6, st.cursor, colW[i] - 1.2, rowH), text, font, h);
                }
                if (pp.showAsTable && i > 0)
                    addLine(st.page, cx, st.cursor, cx, st.cursor + rowH, 0.1);
                cx += colW[i];
            }
            const QString note = section.notes.value(ri);
            if (!note.isEmpty())
                addText(st.page, QRectF(fx, st.cursor, frameW - 0.6, rowH), note, FLines, Qt::AlignRight | Qt::AlignVCenter);
            if (pp.showAsTable)
                addLine(st.page, fx, st.cursor + rowH, fx + frameW, st.cursor + rowH, 0.1);
            st.cursor += rowH;
            ++st.rowsOnPage;
            ++alt;
        }
    }

    // summary, notes and signatures use the full content width below the frames
    auto ensureFull = [&](double h) {
        if (st.cursor + h > st.bottom) {
            finishPage();
            startPage();
        }
    };
    if (st.frame > 0)
        st.cursor = st.bottom; // multi-column: continue below is not possible, force page if needed
    if (!m_doc.summary.isEmpty()) {
        ensureFull(rowH * 2);
        st.cursor += rowH * 0.6;
        addText(st.page, QRectF(st.x, st.cursor, st.w, rowH), m_doc.summary, FSection, Qt::AlignLeft | Qt::AlignVCenter);
        st.cursor += rowH;
    }
    if (!fmt.notes.trimmed().isEmpty()) {
        const QStringList lines = fmt.notes.split(QLatin1Char('\n'));
        ensureFull(rowH * (lines.size() + 1));
        st.cursor += rowH * 0.6;
        for (const QString& l : lines) {
            addText(st.page, QRectF(st.x, st.cursor, st.w, rowH), l, FLines, Qt::AlignLeft | Qt::AlignVCenter);
            st.cursor += rowH;
        }
    }
    bool anySignature = false;
    for (const auto& s : fmt.signatures)
        if (!s.isEmpty())
            anySignature = true;
    if (anySignature) {
        const double sigH = 16.0;
        ensureFull(sigH + 4);
        const double y = qMax(st.cursor + 6.0, st.bottom - sigH);
        const double cellW = st.w / 3.0;
        for (int i = 0; i < 3; ++i) {
            const QString role = fmt.signatures[size_t(i)];
            if (role.isEmpty())
                continue;
            const double cx = st.x + i * cellW;
            addText(st.page, QRectF(cx, y, cellW, rowH), role, FSection, Qt::AlignHCenter | Qt::AlignVCenter);
            addLine(st.page, cx + cellW * 0.15, y + sigH - 2, cx + cellW * 0.85, y + sigH - 2, 0.2);
        }
        st.cursor = y + sigH;
    }
    finishPage();

    const int count = int(m_pages.size());
    for (int i = 0; i < count; ++i) {
        layoutLogos(m_pages[i], i == 0);
        layoutFooter(m_pages[i], i, count);
    }
}

void ReportRenderer::paintPage(QPainter& p, int page, double pxPerMm) const
{
    if (page < 0 || page >= m_pages.size())
        return;
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    auto px = [pxPerMm](const QRectF& r) {
        return QRectF(r.x() * pxPerMm, r.y() * pxPerMm, r.width() * pxPerMm, r.height() * pxPerMm);
    };
    for (const Op& op : m_pages.at(page)) {
        switch (op.kind) {
        case Op::Fill:
            p.fillRect(px(op.r), op.color);
            break;
        case Op::Line: {
            QPen pen(op.color);
            pen.setWidthF(qMax(0.5, op.width * pxPerMm));
            p.setPen(pen);
            const QRectF r = px(op.r);
            p.drawLine(r.topLeft(), r.bottomRight());
            break;
        }
        case Op::Image: {
            const QImage img = cachedImage(op.text);
            if (img.isNull())
                break;
            const QRectF box = px(op.r);
            QSizeF s = img.size();
            s.scale(box.size(), Qt::KeepAspectRatio);
            const QRectF target(box.x() + (box.width() - s.width()) / 2, box.y() + (box.height() - s.height()) / 2,
                                s.width(), s.height());
            p.drawImage(target, img);
            break;
        }
        case Op::Text: {
            p.setFont(m_fonts[size_t(op.font)]);
            p.setPen(op.color);
            const QRectF r = px(op.r);
            QString text = op.text;
            if (!(op.flags & Qt::TextDontClip))
                text = p.fontMetrics().elidedText(text, Qt::ElideRight, int(r.width()) + 1);
            p.drawText(r, op.flags & ~Qt::TextDontClip, text);
            break;
        }
        }
    }
    p.restore();
}

QImage ReportRenderer::renderPage(int page, double dpi) const
{
    const double pxPerMm = dpi / 25.4;
    QImage img(QSize(int(m_pageMm.width() * pxPerMm), int(m_pageMm.height() * pxPerMm)), QImage::Format_RGB32);
    const int dpm = int(dpi / 0.0254);
    img.setDotsPerMeterX(dpm);
    img.setDotsPerMeterY(dpm);
    img.fill(Qt::white);
    QPainter p(&img);
    paintPage(p, page, pxPerMm);
    return img;
}

bool ReportRenderer::print(QPagedPaintDevice* device) const
{
    device->setPageSize(QPageSize(QPageSize::A4));
    device->setPageOrientation(m_doc.format.landscape ? QPageLayout::Landscape : QPageLayout::Portrait);
    device->setPageMargins(QMarginsF(0, 0, 0, 0), QPageLayout::Millimeter);
    QPainter p;
    if (!p.begin(device))
        return false;
    const double pxPerMm = device->logicalDpiX() / 25.4;
    for (int i = 0; i < pageCount(); ++i) {
        if (i > 0)
            device->newPage();
        paintPage(p, i, pxPerMm);
    }
    p.end();
    return true;
}

bool ReportRenderer::exportPdf(const QString& path, QString* error) const
{
    QPdfWriter writer(path);
    writer.setResolution(600);
    writer.setCreator(footerProgramText());
    writer.setTitle(m_doc.docTitle);
    if (!print(&writer)) {
        if (error)
            *error = Tr::tr("Cannot write %1").arg(path);
        return false;
    }
    return true;
}
