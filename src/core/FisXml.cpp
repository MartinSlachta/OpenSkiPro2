#include "FisXml.h"
#include "Event.h"
#include "FisList.h"
#include "FisPenalty.h"
#include "Ranking.h"
#include "TimeUtil.h"

#include <QCoreApplication>
#include <QRegularExpression>
#include <QXmlStreamWriter>
#include <algorithm>

namespace {

// "1:41.83", "49.61": FIS times with a dot
QString fisTime(qint64 ms, Precision p)
{
    return TimeUtil::formatResult(ms, p).replace(QLatin1Char(','), QLatin1Char('.'));
}

QString fisPoints(int hundredths)
{
    return QStringLiteral("%1.%2").arg(hundredths / 100).arg(qAbs(hundredths % 100), 2, 10, QLatin1Char('0'));
}

QString number(const QString& s, int decimals)
{
    bool ok = false;
    const double v = QString(s).trimmed().replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&ok);
    return ok ? QString::number(v, 'f', decimals) : QString();
}

// "KUPČO Martin" -> ("KUPČO", "Martin"), the words in capitals are the last name
QPair<QString, QString> splitName(const QString& name)
{
    Competitor c;
    c.setValue(CompField::Name, name);
    return { c.lastName, c.firstName };
}

// list number of the season: "1627" -> "16", "2026" -> "20"
QString listNumber(const QString& code)
{
    const QString c = code.trimmed();
    return c.size() > 2 ? c.left(c.size() - 2) : c;
}

QString listCode(const Event& ev)
{
    const QString l = ev.params().fisPoints.list.trimmed();
    return l.isEmpty() ? ev.params().header.value(QLatin1String(HeaderKeys::ListNumber)).trimmed() : l;
}

} // namespace

namespace FisXml {

RaceId raceId(const Event& ev)
{
    RaceId id;
    const auto& g = ev.params().general;
    id.nation = g.codexNation();
    id.codex = GeneralParams::normalizedCodex(g.codex);
    id.tdNumber = ev.params().header.value(QLatin1String(HeaderKeys::TechnicalDelegate) + QStringLiteral(".code")).trimmed();
    return id;
}

QString fullCodex(const RaceId& id)
{
    if (id.codex.isEmpty())
        return QString();
    return id.nation + id.codex + (id.tdNumber.isEmpty() ? QString() : QStringLiteral(".") + id.tdNumber);
}

QString fullCodex(const Event& ev)
{
    return fullCodex(raceId(ev));
}

QString fileName(const Event& ev)
{
    const RaceId id = raceId(ev);
    return id.nation + id.codex + QStringLiteral(".xml");
}

int season(const Event& ev)
{
    const QDate d = ev.params().general.date;
    if (!d.isValid())
        return 0;
    return d.month() >= 7 ? d.year() + 1 : d.year();
}

QStringList check(const Event& ev)
{
    QStringList out;
    const auto& p = ev.params();
    const RaceId id = raceId(ev);
    if (p.type.regulation != Regulation::FIS)
        out << QCoreApplication::translate("FisXml", "The event is not a FIS race (Event type).");
    if (id.codex.isEmpty())
        out << QCoreApplication::translate("FisXml", "The codex of the race is missing (General Parameters, e.g. 6534).");
    if (id.nation.size() != 3)
        out << QCoreApplication::translate("FisXml", "The nation of the organiser is missing (General Parameters).");
    if (!p.general.date.isValid())
        out << QCoreApplication::translate("FisXml", "The date of the race is missing (General Parameters).");
    const QVector<Gender> genders = FisPenalty::genders(ev);
    if (genders.size() != 1)
        out << QCoreApplication::translate("FisXml", "A FIS race has competitors of one gender only.");
    else {
        const FisPenalty::Result r = FisPenalty::compute(ev, genders.first());
        if (!r.valid)
            out << QCoreApplication::translate("FisXml", "FIS points / Penalty: %1").arg(r.error);
    }
    if (Ranking::finalRanking(ev, 0).classified.isEmpty())
        out << QCoreApplication::translate("FisXml", "No competitor classified.");
    return out;
}

QStringList warnings(const Event& ev)
{
    QStringList out;
    const auto& p = ev.params();
    const RaceId id = raceId(ev);
    if (id.tdNumber.isEmpty())
        out << QCoreApplication::translate("FisXml", "The FIS number of the Technical Delegate is missing (Header, code of the TD).");
    if (listCode(ev).isEmpty())
        out << QCoreApplication::translate("FisXml", "The FIS points list used is not set (FIS points / Penalty).");
    for (int r = 1; r <= ev.runCount(); ++r) {
        if (Ranking::counters(ev, r).notProcessed > 0)
            out << QCoreApplication::translate("FisXml", "%1: some competitors are not processed.").arg(p.type.runName(r));
        if (p.header.runValue(r, QLatin1String(HeaderKeys::Homologation)).trimmed().isEmpty())
            out << QCoreApplication::translate("FisXml", "%1: the homologation is missing (Header).").arg(p.type.runName(r));
    }
    for (const auto& c : ev.competitors())
        if (!ev.isForerunner(c.bib) && c.code.trimmed().isEmpty()) {
            out << QCoreApplication::translate("FisXml", "Some competitors have no FIS code.");
            break;
        }
    return out;
}

QByteArray build(const Event& ev)
{
    const auto& p = ev.params();
    const auto& hp = p.header;
    const RaceId id = raceId(ev);
    const QVector<Gender> genders = FisPenalty::genders(ev);
    const Gender gender = genders.isEmpty() ? Gender::Male : genders.first();
    const FisPenalty::Result pen = FisPenalty::compute(ev, gender);
    const Precision prec = ev.precision();

    QByteArray data;
    QXmlStreamWriter w(&data);
    w.setAutoFormatting(true);
    w.setAutoFormattingIndent(4);
    w.writeStartDocument();
    w.writeStartElement(QStringLiteral("Fisresults"));
    auto text = [&](const QString& name, const QString& value) { w.writeTextElement(name, value); };
    auto person = [&](const QString& key) {
        const auto n = splitName(hp.value(key + QStringLiteral(".name")).trimmed());
        text(QStringLiteral("Lastname"), n.first);
        text(QStringLiteral("Firstname"), n.second);
        text(QStringLiteral("Nation"), hp.value(key + QStringLiteral(".nation")).trimmed().toUpper());
    };
    auto hasPerson = [&](const QString& key) { return !hp.value(key + QStringLiteral(".name")).trimmed().isEmpty(); };

    // --- race header (2.2)
    w.writeStartElement(QStringLiteral("Raceheader"));
    w.writeAttribute(QStringLiteral("Sector"), QStringLiteral("AL"));
    w.writeAttribute(QStringLiteral("Gender"), gender == Gender::Female ? QStringLiteral("W") : QStringLiteral("M"));
    text(QStringLiteral("Season"), QString::number(season(ev)));
    text(QStringLiteral("Codex"), id.codex);
    text(QStringLiteral("Nation"), id.nation);
    text(QStringLiteral("Discipline"), fisDisciplineCode(p.type.discipline));
    text(QStringLiteral("Category"), p.type.fisCategory);
    text(QStringLiteral("Type"), QStringLiteral("Official"));
    text(QStringLiteral("Eventname"), p.general.title1.section(QLatin1Char('\n'), 0, 0).trimmed());
    text(QStringLiteral("Place"), p.general.location);
    w.writeStartElement(QStringLiteral("Racedate"));
    text(QStringLiteral("Day"), QString::number(p.general.date.day()));
    text(QStringLiteral("Month"), QString::number(p.general.date.month()));
    text(QStringLiteral("Year"), QString::number(p.general.date.year()));
    w.writeEndElement();
    text(QStringLiteral("Tempunit"), QStringLiteral("C"));
    text(QStringLiteral("Longunit"), QStringLiteral("m"));
    text(QStringLiteral("Speedunit"), QStringLiteral("kmh"));
    w.writeEndElement();

    w.writeStartElement(QStringLiteral("AL_race"));
    // --- race information (2.3.1)
    w.writeStartElement(QStringLiteral("AL_raceinfo"));
    text(QStringLiteral("Usedfislist"), listNumber(listCode(ev)));
    if (pen.valid) {
        text(QStringLiteral("Appliedpenalty"), fisPoints(pen.applied));
        text(QStringLiteral("Calculatedpenalty"), fisPoints(pen.calculated));
    }
    const double f = p.fisPoints.forGender(gender).fValue;
    text(QStringLiteral("Fvalue"), f == qRound(f) ? QString::number(qRound(f)) : QString::number(f, 'f', 2));
    // the technical delegate is the <Jury>, the other officials are <Member>s
    const QString tdKey = QLatin1String(HeaderKeys::TechnicalDelegate);
    if (hasPerson(tdKey)) {
        w.writeStartElement(QStringLiteral("Jury"));
        w.writeAttribute(QStringLiteral("Function"), QStringLiteral("TechnicalDelegate"));
        if (!id.tdNumber.isEmpty())
            text(QStringLiteral("Number"), id.tdNumber);
        person(tdKey);
        w.writeEndElement();
    }
    const QList<QPair<const char*, const char*>> members = { { "ChiefRace", HeaderKeys::ChiefOfRace },
                                                             { "Referee", HeaderKeys::Referee },
                                                             { "Assistantreferee", HeaderKeys::AssistantReferee },
                                                             { "ChiefCourse", HeaderKeys::CourseChief },
                                                             { "Startreferee", HeaderKeys::StartReferee },
                                                             { "Finishreferee", HeaderKeys::FinishReferee } };
    for (const auto& m : members) {
        const QString key = QLatin1String(m.second);
        if (!hasPerson(key))
            continue;
        w.writeStartElement(QStringLiteral("Member"));
        w.writeAttribute(QStringLiteral("Function"), QLatin1String(m.first));
        person(key);
        w.writeEndElement();
    }
    for (int r = 1; r <= ev.runCount(); ++r) {
        auto run = [&](const char* key) { return hp.runValue(r, QLatin1String(key)).trimmed(); };
        const QString k = QStringLiteral("run%1.").arg(r);
        w.writeStartElement(QStringLiteral("Runinfo"));
        w.writeAttribute(QStringLiteral("no"), QString::number(r));
        w.writeStartElement(QStringLiteral("Course")); // 2.3.5
        text(QStringLiteral("Name"), run(HeaderKeys::Course));
        text(QStringLiteral("Homologation"), run(HeaderKeys::Homologation));
        const QString length = number(run(HeaderKeys::Length), 0);
        if (!length.isEmpty() && length != QLatin1String("0"))
            text(QStringLiteral("Length"), length);
        text(QStringLiteral("Gates"), hp.value(k + QStringLiteral("gates")).trimmed());
        text(QStringLiteral("Turninggates"), hp.value(k + QStringLiteral("directions")).trimmed());
        text(QStringLiteral("Startelev"), number(run(HeaderKeys::StartAltitude), 0));
        text(QStringLiteral("Finishelev"), number(run(HeaderKeys::FinishAltitude), 0));
        if (hasPerson(k + QStringLiteral("setter"))) {
            w.writeStartElement(QStringLiteral("Coursesetter"));
            person(k + QStringLiteral("setter"));
            w.writeEndElement();
        }
        int order = 0;
        for (char c = 'A'; c <= 'E'; ++c) {
            const QString fk = k + QStringLiteral("fore") + QLatin1Char(c);
            if (!hasPerson(fk))
                continue;
            w.writeStartElement(QStringLiteral("Forerunner"));
            w.writeAttribute(QStringLiteral("Order"), QString::number(++order));
            person(fk);
            w.writeEndElement();
        }
        for (int i = 1; i <= ev.interCount(); ++i) {
            w.writeEmptyElement(QStringLiteral("Intermediate"));
            w.writeAttribute(QStringLiteral("i"), QString::number(i));
            w.writeAttribute(QStringLiteral("type"), QStringLiteral("time"));
        }
        w.writeEndElement(); // Course
        // weather at the start and at the finish (2.3.9)
        const QString weather = hp.value(QLatin1String(HeaderKeys::Weather)).trimmed();
        const QString snow = hp.value(QLatin1String(HeaderKeys::Snow)).trimmed();
        const QString temps[2] = { number(hp.value(QLatin1String(HeaderKeys::TempStart)), 1),
                                   number(hp.value(QLatin1String(HeaderKeys::TempFinish)), 1) };
        const char* places[2] = { "Start", "Finish" };
        for (int i = 0; i < 2; ++i) {
            if (temps[i].isEmpty() && (i == 1 || (weather.isEmpty() && snow.isEmpty())))
                continue;
            w.writeStartElement(QStringLiteral("Weather"));
            text(QStringLiteral("Place"), QLatin1String(places[i]));
            if (!weather.isEmpty())
                text(QStringLiteral("Weather"), weather);
            if (!snow.isEmpty())
                text(QStringLiteral("Snow"), snow);
            if (!temps[i].isEmpty())
                text(QStringLiteral("Temperatureair"), temps[i]);
            w.writeEndElement();
        }
        text(QStringLiteral("Starttime"), hp.value(k + QStringLiteral("starttime")).trimmed().replace(QLatin1Char('h'), QLatin1Char(':'),
                                                                                                      Qt::CaseInsensitive));
        w.writeEndElement(); // Runinfo
    }
    text(QStringLiteral("Timingby"), p.general.timingBy);
    text(QStringLiteral("Dataprocessingby"), QStringLiteral("OpenSkiPro2 " OSP_VERSION));
    text(QStringLiteral("Softwarecompany"), QStringLiteral("OpenSkiPro2"));
    text(QStringLiteral("Softwarename"), QStringLiteral("OpenSkiPro2"));
    text(QStringLiteral("Softwareversion"), QStringLiteral(OSP_VERSION));
    w.writeEndElement(); // AL_raceinfo

    // --- competitors (3.3: gender M = male, F = female)
    QHash<int, int> startOrder; // bib -> order in the start list of run 1
    const auto& start = ev.run(1).startList;
    for (int i = 0; i < start.size(); ++i)
        startOrder.insert(start.at(i).bib, i + 1);
    auto competitor = [&](int bib) {
        const Competitor* c = ev.competitorByBib(bib);
        w.writeStartElement(QStringLiteral("Competitor"));
        text(QStringLiteral("Fiscode"), c ? c->code.trimmed() : QString());
        text(QStringLiteral("Lastname"), c ? c->lastName : QString());
        text(QStringLiteral("Firstname"), c ? c->firstName : QString());
        text(QStringLiteral("Gender"),
             (c ? FisPenalty::genderOf(c->gender) : gender) == Gender::Female ? QStringLiteral("F") : QStringLiteral("M"));
        text(QStringLiteral("Nation"), c ? c->nation.trimmed().toUpper() : QString());
        if (c && c->year > 0)
            text(QStringLiteral("Yearofbirth"), QString::number(c->year));
        if (c && !c->club.trimmed().isEmpty())
            text(QStringLiteral("Clubname"), c->club.trimmed());
        w.writeEndElement();
    };
    auto runTimes = [&](const RankedEntry& e) {
        for (int r = 1; r <= ev.runCount() && r <= 3; ++r)
            if (e.runMs[size_t(r - 1)] >= 0)
                text(QStringLiteral("Timerun%1").arg(r), fisTime(e.runMs[size_t(r - 1)], prec));
    };
    const RankingResult res = Ranking::finalRanking(ev, 0);
    w.writeStartElement(QStringLiteral("AL_classified")); // 3.1
    for (const RankedEntry& e : res.classified) {
        w.writeStartElement(QStringLiteral("AL_ranked"));
        w.writeAttribute(QStringLiteral("Status"), QStringLiteral("QLF"));
        text(QStringLiteral("Rank"), QString::number(e.rank));
        text(QStringLiteral("Order"), QString::number(startOrder.value(e.bib)));
        text(QStringLiteral("Bib"), QString::number(e.bib));
        competitor(e.bib);
        w.writeStartElement(QStringLiteral("AL_result"));
        runTimes(e);
        text(QStringLiteral("Totaltime"), fisTime(e.shownMs, prec));
        text(QStringLiteral("Diff"), fisTime(qMax<qint64>(0, e.gapMs), prec));
        if (pen.racePoints.contains(e.bib))
            text(QStringLiteral("Racepoints"), fisPoints(pen.racePoints.value(e.bib)));
        w.writeEndElement(); // AL_result
        w.writeEndElement(); // AL_ranked
    }
    w.writeEndElement(); // AL_classified
    w.writeStartElement(QStringLiteral("AL_notclassified")); // 3.8
    for (const RankingGroup& g : res.unclassified) {
        QString status;
        switch (g.status) {
        case ResultStatus::DNS: status = QStringLiteral("DNS"); break;
        case ResultStatus::NPS: status = QStringLiteral("NPS"); break; // since 2.7
        case ResultStatus::DNF: status = QStringLiteral("DNF"); break;
        case ResultStatus::DSQ: status = QStringLiteral("DSQ"); break;
        default: break;
        }
        if (status.isEmpty())
            continue; // not processed
        QVector<RankedEntry> entries = g.entries;
        std::sort(entries.begin(), entries.end(), [](const RankedEntry& a, const RankedEntry& b) { return a.bib < b.bib; });
        for (const RankedEntry& e : entries) {
            w.writeStartElement(QStringLiteral("AL_notranked"));
            w.writeAttribute(QStringLiteral("Status"), status);
            text(QStringLiteral("Run"), QString::number(g.run));
            text(QStringLiteral("Bib"), QString::number(e.bib));
            competitor(e.bib);
            // the times of the runs completed before
            bool anyTime = false;
            for (int r = 1; r <= 3; ++r)
                anyTime = anyTime || e.runMs[size_t(r - 1)] >= 0;
            if (anyTime) {
                w.writeStartElement(QStringLiteral("AL_result"));
                runTimes(e);
                w.writeEndElement();
            }
            if (g.status == ResultStatus::DSQ && !e.reason.trimmed().isEmpty()) {
                // a number is the gate, a text the reference to the rule
                bool gate = false;
                e.reason.trimmed().toInt(&gate);
                text(gate ? QStringLiteral("Gate") : QStringLiteral("Reason"), e.reason.trimmed());
            }
            w.writeEndElement();
        }
    }
    w.writeEndElement(); // AL_notclassified
    w.writeEndElement(); // AL_race
    w.writeEndElement(); // Fisresults
    w.writeEndDocument();
    data.replace("\r\n", "\n");
    data.replace("\n", "\r\n");
    return data;
}

} // namespace FisXml
