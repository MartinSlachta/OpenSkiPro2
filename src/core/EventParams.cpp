#include "EventParams.h"
#include "Competitor.h"

#include <QCoreApplication>
#include <QStringList>

QString EventTypeParams::runName(int run) const
{
    if (run >= 1 && run <= kMaxRuns && !runNames[size_t(run - 1)].isEmpty())
        return runNames[size_t(run - 1)];
    return QCoreApplication::translate("Event", "Run %1").arg(run);
}

static QFont makeFont(int pt, bool bold)
{
    QFont f(QStringLiteral("Arial"));
    f.setPointSize(pt);
    f.setBold(bold);
    return f;
}

PrintingParams::PrintingParams()
    : fontTitle1(makeFont(14, true))
    , fontTitle2(makeFont(12, true))
    , fontDocTitle(makeFont(12, true))
    , fontFooter(makeFont(9, true))
    , fontLabels(makeFont(10, true))
    , fontLines(makeFont(9, false))
{
    logos[0].rect = QRectF(0.02, 0.015, 0.28, 0.065);  // top left
    logos[1].rect = QRectF(0.70, 0.015, 0.28, 0.065);  // top right
    logos[2].rect = QRectF(0.03, 0.915, 0.94, 0.065);  // bottom band
    for (size_t i = 3; i < logos.size(); ++i)
        logos[i].rect = QRectF(0.35, 0.015, 0.30, 0.065);
}

QString CompetitorFieldConfig::label(int fieldId) const
{
    const QString l = labels.value(fieldId);
    if (!l.isEmpty())
        return l;
    if (const auto* d = competitorFieldDef(fieldId))
        return QCoreApplication::translate("Competitor", d->label);
    return QString();
}

CompetitorFieldConfig CompetitorFieldConfig::defaults()
{
    CompetitorFieldConfig c;
    for (const auto& d : competitorFieldDefs())
        if (d.defaultActive)
            c.active.insert(d.id);
    c.grid = {
        { CompField::Bib, 55 }, { CompField::LastName, 120 }, { CompField::FirstName, 110 },
        { CompField::Code, 75 }, { CompField::Gender, 55 }, { CompField::Year, 55 },
        { CompField::Category, 75 }, { CompField::Nation, 60 }, { CompField::Club, 170 },
        { CompField::FromList, 70 }, { CompField::PointsDH, 75 }, { CompField::PointsSG, 75 },
        { CompField::PointsGS, 75 }, { CompField::PointsSL, 75 }, { CompField::PointsAC, 75 },
    };
    return c;
}

QString HeaderParams::runValue(int run, const QString& key) const
{
    const QString v = values.value(QStringLiteral("run%1.%2").arg(run).arg(key));
    return v.isEmpty() ? values.value(key) : v;
}

QString HeaderParams::dropOf(const QString& start, const QString& finish)
{
    bool a = false, b = false;
    const double s = QString(start).trimmed().replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&a);
    const double f = QString(finish).trimmed().replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&b);
    if (!a || !b)
        return QString();
    const double d = s - f;
    return d == qRound(d) ? QString::number(qRound(d)) : QString::number(d, 'f', 1);
}

QString disciplineName(Discipline d)
{
    switch (d) {
    case Discipline::Downhill: return QCoreApplication::translate("Event", "Downhill");
    case Discipline::SuperG: return QCoreApplication::translate("Event", "Super-G");
    case Discipline::Giant: return QCoreApplication::translate("Event", "Giant Slalom");
    case Discipline::Slalom: return QCoreApplication::translate("Event", "Slalom");
    case Discipline::AlpineCombined: return QCoreApplication::translate("Event", "Alpine Combined");
    }
    return QString();
}

QString fontToString(const QFont& f)
{
    return QStringLiteral("%1;%2;%3;%4")
        .arg(f.family())
        .arg(f.pointSize())
        .arg(f.bold() ? 1 : 0)
        .arg(f.italic() ? 1 : 0);
}

QFont fontFromString(const QString& s, const QFont& fallback)
{
    const QStringList p = s.split(QLatin1Char(';'));
    if (p.size() < 2)
        return fallback;
    QFont f(p.at(0));
    f.setPointSize(qMax(4, p.at(1).toInt()));
    f.setBold(p.value(2) == QLatin1String("1"));
    f.setItalic(p.value(3) == QLatin1String("1"));
    return f;
}

// ---- serialization ---------------------------------------------------------

static QString joinInts(const QList<int>& v)
{
    QStringList s;
    for (int i : v)
        s << QString::number(i);
    return s.join(QLatin1Char(','));
}

static QList<int> splitInts(const QString& s)
{
    QList<int> out;
    for (const QString& p : s.split(QLatin1Char(','), Qt::SkipEmptyParts))
        out << p.toInt();
    return out;
}

static QString rectToString(const QRectF& r)
{
    return QStringLiteral("%1,%2,%3,%4").arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height());
}

static QRectF rectFromString(const QString& s, const QRectF& fallback)
{
    const QStringList p = s.split(QLatin1Char(','));
    if (p.size() != 4)
        return fallback;
    return QRectF(p[0].toDouble(), p[1].toDouble(), p[2].toDouble(), p[3].toDouble());
}

QVariantMap EventParams::toMap() const
{
    QVariantMap m;
    // general
    m[QStringLiteral("general.title1")] = general.title1;
    m[QStringLiteral("general.title2")] = general.title2;
    m[QStringLiteral("general.date")] = general.date.toString(Qt::ISODate);
    m[QStringLiteral("general.location")] = general.location;
    m[QStringLiteral("general.nation")] = general.nation;
    m[QStringLiteral("general.organisation")] = general.organisation;
    m[QStringLiteral("general.timingBy")] = general.timingBy;
    m[QStringLiteral("general.codex")] = general.codex;
    m[QStringLiteral("general.freeHeader")] = general.useFreeHeader;
    // FIS points / penalty
    m[QStringLiteral("fis.list")] = fisPoints.list;
    for (int g = 0; g < 2; ++g) {
        const FisPointsParams& f = fisPoints.gender[size_t(g)];
        const QString k = g == 0 ? QStringLiteral("fis.m.") : QStringLiteral("fis.w.");
        m[k + QStringLiteral("f")] = f.fValue;
        m[k + QStringLiteral("max")] = f.maxValue;
        m[k + QStringLiteral("z")] = f.zValue;
        m[k + QStringLiteral("adder")] = f.adder;
        m[k + QStringLiteral("minPenalty")] = f.minPenalty;
        m[k + QStringLiteral("maxPenalty")] = f.maxPenalty;
        m[k + QStringLiteral("applied")] = f.appliedOverride;
    }
    // type
    m[QStringLiteral("type.regulation")] = int(type.regulation);
    m[QStringLiteral("type.fisCategory")] = type.fisCategory;
    m[QStringLiteral("type.sport")] = int(type.sport);
    m[QStringLiteral("type.discipline")] = int(type.discipline);
    m[QStringLiteral("type.runCount")] = type.runCount;
    m[QStringLiteral("type.interCount")] = type.interCount;
    m[QStringLiteral("type.timePrecision")] = int(type.timePrecision);
    m[QStringLiteral("type.speedPrecision")] = int(type.speedPrecision);
    m[QStringLiteral("type.avgSpeedPrecision")] = int(type.avgSpeedPrecision);
    m[QStringLiteral("type.courseLength")] = type.courseLength;
    m[QStringLiteral("type.finalType")] = int(type.finalType);
    QList<int> used;
    for (int i = 0; i < kMaxRuns; ++i)
        used << (type.runsUsed[size_t(i)] ? 1 : 0);
    m[QStringLiteral("type.runsUsed")] = joinInts(used);
    for (int i = 0; i < kMaxRuns; ++i)
        m[QStringLiteral("type.runName%1").arg(i + 1)] = type.runNames[size_t(i)];
    // header
    for (auto it = header.values.cbegin(); it != header.values.cend(); ++it)
        m[QStringLiteral("header.") + it.key()] = it.value();
    // printing
    m[QStringLiteral("print.fontTitle1")] = fontToString(printing.fontTitle1);
    m[QStringLiteral("print.fontTitle2")] = fontToString(printing.fontTitle2);
    m[QStringLiteral("print.fontDocTitle")] = fontToString(printing.fontDocTitle);
    m[QStringLiteral("print.fontFooter")] = fontToString(printing.fontFooter);
    m[QStringLiteral("print.fontLabels")] = fontToString(printing.fontLabels);
    m[QStringLiteral("print.fontLines")] = fontToString(printing.fontLines);
    m[QStringLiteral("print.showTitle1")] = printing.showTitle1;
    m[QStringLiteral("print.showTitle2")] = printing.showTitle2;
    m[QStringLiteral("print.autoAdjust")] = printing.autoAdjust;
    m[QStringLiteral("print.showAsTable")] = printing.showAsTable;
    m[QStringLiteral("print.grayRows")] = printing.grayRows;
    m[QStringLiteral("print.grayLevel")] = printing.grayLevel;
    m[QStringLiteral("print.marginsFirst")] = joinInts({ printing.marginsFirst[0], printing.marginsFirst[1],
                                                         printing.marginsFirst[2], printing.marginsFirst[3] });
    m[QStringLiteral("print.marginsOther")] = joinInts({ printing.marginsOther[0], printing.marginsOther[1],
                                                         printing.marginsOther[2], printing.marginsOther[3] });
    m[QStringLiteral("print.titleAlignFirst")] = printing.titleAlignFirst;
    m[QStringLiteral("print.titleAlignOther")] = printing.titleAlignOther;
    for (size_t i = 0; i < printing.logos.size(); ++i) {
        m[QStringLiteral("print.logo%1.path").arg(i + 1)] = printing.logos[i].path;
        m[QStringLiteral("print.logo%1.rect").arg(i + 1)] = rectToString(printing.logos[i].rect);
    }
    m[QStringLiteral("print.logosFirstPageOnly")] = printing.logosFirstPageOnly;
    m[QStringLiteral("print.showAppLogo")] = printing.showAppLogo;
    // timing
    m[QStringLiteral("timing.showNumbersToFollow")] = timing.showNumbersToFollow;
    m[QStringLiteral("timing.showMarkMustFinish")] = timing.showMarkMustFinish;
    m[QStringLiteral("timing.showNamesInFollow")] = timing.showNamesInFollow;
    m[QStringLiteral("timing.confirmInsertStartList")] = timing.confirmInsertStartList;
    m[QStringLiteral("timing.invertGrid")] = timing.invertGrid;
    m[QStringLiteral("timing.showNetTime")] = timing.showNetTime;
    m[QStringLiteral("timing.askBibIfUnknown")] = timing.askBibIfUnknown;
    m[QStringLiteral("timing.showSeqColumn")] = timing.showSeqColumn;
    m[QStringLiteral("timing.forerunnerFrom")] = timing.forerunnerFrom;
    m[QStringLiteral("timing.forerunnerTo")] = timing.forerunnerTo;
    for (size_t i = 0; i < timing.quickPenaltiesMs.size(); ++i)
        m[QStringLiteral("timing.quickPenalty%1").arg(i + 1)] = timing.quickPenaltiesMs[i];
    m[QStringLiteral("timing.delayStart")] = timing.delayStartMs;
    m[QStringLiteral("timing.delayFinish")] = timing.delayFinishMs;
    m[QStringLiteral("timing.delayInter")] = timing.delayInterMs;
    for (int i = 0; i < kMaxInter; ++i)
        m[QStringLiteral("timing.minTimeInter%1").arg(i + 1)] = timing.minTimeInterMs[size_t(i)];
    m[QStringLiteral("timing.minTimeFinish")] = timing.minTimeFinishMs;
    m[QStringLiteral("timing.reverseCount")] = timing.reverseCount;
    m[QStringLiteral("timing.rankingEvent")] = timing.rankingEvent;
    m[QStringLiteral("timing.rankingField")] = timing.rankingField;
    m[QStringLiteral("timing.rankingByGroups")] = timing.rankingByGroups;
    // competitor fields
    QStringList labels;
    for (auto it = fields.labels.cbegin(); it != fields.labels.cend(); ++it)
        labels << QStringLiteral("%1=%2").arg(it.key()).arg(QString(it.value()).replace(QLatin1Char('|'), QLatin1Char('/')));
    m[QStringLiteral("fields.labels")] = labels.join(QLatin1Char('|'));
    QList<int> active(fields.active.cbegin(), fields.active.cend());
    std::sort(active.begin(), active.end());
    m[QStringLiteral("fields.active")] = joinInts(active);
    QStringList grid;
    for (const auto& g : fields.grid)
        grid << QStringLiteral("%1:%2").arg(g.first).arg(g.second);
    m[QStringLiteral("fields.grid")] = grid.join(QLatin1Char(','));
    m[QStringLiteral("output.groups")] = outputGroupsToString(outputGroups);
    return m;
}

void EventParams::fromMap(const QVariantMap& m)
{
    *this = defaults();
    auto str = [&](const char* k, const QString& def = QString()) {
        const auto it = m.constFind(QLatin1String(k));
        return it == m.constEnd() ? def : it->toString();
    };
    auto num = [&](const QString& k, qint64 def) {
        const auto it = m.constFind(k);
        return it == m.constEnd() ? def : it->toLongLong();
    };
    auto flag = [&](const QString& k, bool def) {
        const auto it = m.constFind(k);
        if (it == m.constEnd())
            return def;
        const QString s = it->toString();
        return s == QLatin1String("1") || s.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0;
    };

    general.title1 = str("general.title1", general.title1);
    general.title2 = str("general.title2", general.title2);
    general.date = QDate::fromString(str("general.date"), Qt::ISODate);
    general.location = str("general.location");
    general.nation = str("general.nation");
    general.organisation = str("general.organisation");
    general.timingBy = str("general.timingBy");
    general.codex = str("general.codex");
    general.useFreeHeader = flag(QStringLiteral("general.freeHeader"), false);
    fisPoints.list = str("fis.list");
    for (int g = 0; g < 2; ++g) {
        FisPointsParams& f = fisPoints.gender[size_t(g)];
        const QString k = g == 0 ? QStringLiteral("fis.m.") : QStringLiteral("fis.w.");
        f.fValue = m.value(k + QStringLiteral("f"), 0.0).toDouble();
        f.maxValue = int(num(k + QStringLiteral("max"), 0));
        f.zValue = int(num(k + QStringLiteral("z"), 0));
        f.adder = int(num(k + QStringLiteral("adder"), 0));
        f.minPenalty = int(num(k + QStringLiteral("minPenalty"), 0));
        f.maxPenalty = int(num(k + QStringLiteral("maxPenalty"), 99900));
        f.appliedOverride = int(num(k + QStringLiteral("applied"), -1));
    }

    type.regulation = Regulation(num(QStringLiteral("type.regulation"), 0));
    type.fisCategory = m.value(QStringLiteral("type.fisCategory"), QStringLiteral("FIS")).toString();
    type.sport = Sport(num(QStringLiteral("type.sport"), 0));
    type.discipline = Discipline(qBound(qint64(0), num(QStringLiteral("type.discipline"), qint64(2)), kDisciplineCount - 1));
    type.runCount = int(qBound(qint64(1), num(QStringLiteral("type.runCount"), qint64(2)), kMaxRuns));
    type.interCount = int(qBound(qint64(0), num(QStringLiteral("type.interCount"), qint64(0)), kMaxInter));
    type.timePrecision = Precision(qBound(qint64(0), num(QStringLiteral("type.timePrecision"), qint64(2)), 3));
    type.speedPrecision = Precision(qBound(qint64(0), num(QStringLiteral("type.speedPrecision"), qint64(2)), 3));
    type.avgSpeedPrecision = Precision(qBound(qint64(0), num(QStringLiteral("type.avgSpeedPrecision"), qint64(2)), 3));
    type.courseLength = m.value(QStringLiteral("type.courseLength"), 0.0).toDouble();
    type.finalType = FinalRankingType(num(QStringLiteral("type.finalType"), 0));
    const QList<int> used = splitInts(str("type.runsUsed"));
    for (int i = 0; i < kMaxRuns; ++i)
        type.runsUsed[size_t(i)] = i < used.size() ? used.at(i) != 0 : true;
    for (int i = 0; i < kMaxRuns; ++i)
        type.runNames[size_t(i)] = m.value(QStringLiteral("type.runName%1").arg(i + 1)).toString();

    header.values.clear();
    for (auto it = m.cbegin(); it != m.cend(); ++it)
        if (it.key().startsWith(QLatin1String("header.")))
            header.values.insert(it.key().mid(7), it.value().toString());

    printing.fontTitle1 = fontFromString(str("print.fontTitle1"), printing.fontTitle1);
    printing.fontTitle2 = fontFromString(str("print.fontTitle2"), printing.fontTitle2);
    printing.fontDocTitle = fontFromString(str("print.fontDocTitle"), printing.fontDocTitle);
    printing.fontFooter = fontFromString(str("print.fontFooter"), printing.fontFooter);
    printing.fontLabels = fontFromString(str("print.fontLabels"), printing.fontLabels);
    printing.fontLines = fontFromString(str("print.fontLines"), printing.fontLines);
    printing.showTitle1 = flag(QStringLiteral("print.showTitle1"), true);
    printing.showTitle2 = flag(QStringLiteral("print.showTitle2"), true);
    printing.autoAdjust = flag(QStringLiteral("print.autoAdjust"), true);
    printing.showAsTable = flag(QStringLiteral("print.showAsTable"), false);
    printing.grayRows = flag(QStringLiteral("print.grayRows"), true);
    printing.grayLevel = int(num(QStringLiteral("print.grayLevel"), 10));
    const QList<int> mf = splitInts(str("print.marginsFirst"));
    const QList<int> mo = splitInts(str("print.marginsOther"));
    for (int i = 0; i < 4; ++i) {
        if (mf.size() == 4)
            printing.marginsFirst[size_t(i)] = mf.at(i);
        if (mo.size() == 4)
            printing.marginsOther[size_t(i)] = mo.at(i);
    }
    printing.titleAlignFirst = int(num(QStringLiteral("print.titleAlignFirst"), 1));
    printing.titleAlignOther = int(num(QStringLiteral("print.titleAlignOther"), 1));
    for (size_t i = 0; i < printing.logos.size(); ++i) {
        printing.logos[i].path = m.value(QStringLiteral("print.logo%1.path").arg(i + 1)).toString();
        printing.logos[i].rect = rectFromString(m.value(QStringLiteral("print.logo%1.rect").arg(i + 1)).toString(),
                                                printing.logos[i].rect);
    }
    printing.logosFirstPageOnly = flag(QStringLiteral("print.logosFirstPageOnly"), false);
    printing.showAppLogo = flag(QStringLiteral("print.showAppLogo"), true);

    timing.showNumbersToFollow = flag(QStringLiteral("timing.showNumbersToFollow"), true);
    timing.showMarkMustFinish = flag(QStringLiteral("timing.showMarkMustFinish"), true);
    timing.showNamesInFollow = flag(QStringLiteral("timing.showNamesInFollow"), false);
    timing.confirmInsertStartList = flag(QStringLiteral("timing.confirmInsertStartList"), true);
    timing.invertGrid = flag(QStringLiteral("timing.invertGrid"), true);
    timing.showNetTime = flag(QStringLiteral("timing.showNetTime"), false);
    timing.showSeqColumn = flag(QStringLiteral("timing.showSeqColumn"), true);
    timing.askBibIfUnknown = flag(QStringLiteral("timing.askBibIfUnknown"), false);
    timing.forerunnerFrom = int(num(QStringLiteral("timing.forerunnerFrom"), 901));
    timing.forerunnerTo = int(num(QStringLiteral("timing.forerunnerTo"), 999));
    for (size_t i = 0; i < timing.quickPenaltiesMs.size(); ++i)
        timing.quickPenaltiesMs[i] = num(QStringLiteral("timing.quickPenalty%1").arg(i + 1), 0);
    timing.delayStartMs = num(QStringLiteral("timing.delayStart"), 0);
    timing.delayFinishMs = num(QStringLiteral("timing.delayFinish"), 0);
    timing.delayInterMs = num(QStringLiteral("timing.delayInter"), 0);
    for (int i = 0; i < kMaxInter; ++i)
        timing.minTimeInterMs[size_t(i)] = num(QStringLiteral("timing.minTimeInter%1").arg(i + 1), 0);
    timing.minTimeFinishMs = num(QStringLiteral("timing.minTimeFinish"), 0);
    timing.reverseCount = int(num(QStringLiteral("timing.reverseCount"), 0));
    timing.rankingEvent = flag(QStringLiteral("timing.rankingEvent"), true);
    timing.rankingField = int(num(QStringLiteral("timing.rankingField"), 0));
    timing.rankingByGroups = flag(QStringLiteral("timing.rankingByGroups"), false);

    if (m.contains(QStringLiteral("fields.active"))) {
        fields.labels.clear();
        for (const QString& kv : str("fields.labels").split(QLatin1Char('|'), Qt::SkipEmptyParts)) {
            const int eq = kv.indexOf(QLatin1Char('='));
            if (eq > 0)
                fields.labels.insert(kv.left(eq).toInt(), kv.mid(eq + 1));
        }
        const QList<int> act = splitInts(str("fields.active"));
        fields.active = QSet<int>(act.cbegin(), act.cend());
        fields.grid.clear();
        for (const QString& g : str("fields.grid").split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            const QStringList p = g.split(QLatin1Char(':'));
            if (p.size() == 2)
                fields.grid << qMakePair(p[0].toInt(), p[1].toInt());
        }
    }
    outputGroups = outputGroupsFromString(str("output.groups"));
}

EventParams EventParams::defaults()
{
    EventParams p;
    p.general.title1 = QCoreApplication::translate("Event", "Event Title");
    p.general.title2 = QCoreApplication::translate("Event", "Discipline");
    p.general.date = QDate::currentDate();
    p.fields = CompetitorFieldConfig::defaults();
    return p;
}
