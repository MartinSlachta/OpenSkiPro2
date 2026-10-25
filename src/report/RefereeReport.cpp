#include "RefereeReport.h"
#include "core/Event.h"
#include "core/FisList.h"
#include "core/FisPenalty.h"
#include "core/FisXml.h"

#include <QHash>
#include <QRegularExpression>
#include <algorithm>

namespace RefereeReport {

void splitReason(const QString& reason, QString* gate, QString* notes)
{
    // "12", "G12", "Gate 12", "brana 12 - straddle": the number is the gate, the rest a note
    static const QRegularExpression re(QStringLiteral("^\\s*(?:gate|g|br[aá]na)?\\s*\\.?\\s*(\\d+)\\b[\\s,;:.\\-]*(.*)$"),
                                       QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = re.match(reason.trimmed());
    if (m.hasMatch()) {
        *gate = m.captured(1);
        *notes = m.captured(2).trimmed();
    } else {
        gate->clear();
        *notes = reason.trimmed();
    }
}

QTime deadline(const QTime& published)
{
    return published.isValid() ? published.addSecs(15 * 60) : QTime();
}

RefereeReportData build(const Event& ev, int run, Order order, const QTime& published, const QTime& deadlineTime)
{
    RefereeReportData d;
    const auto& p = ev.params();
    d.place = p.general.location;
    d.country = FisXml::raceId(ev).nation;
    d.codex = FisXml::fullCodex(ev);
    const QString title = p.general.title1.section(QLatin1Char('\n'), 0, 0).trimmed();
    d.eventName = ev.runCount() > 1 ? QStringLiteral("%1 - %2").arg(p.type.runName(run), title) : title;
    d.date = p.general.date.isValid() ? p.general.date.toString(QStringLiteral("dd.MM.yyyy")) : QString();
    d.category = p.type.regulation == Regulation::FIS ? p.type.fisCategory : QString();
    // the form uses L (ladies) and M (men)
    QStringList genders;
    for (Gender g : FisPenalty::genders(ev))
        genders << (g == Gender::Female ? QStringLiteral("L") : QStringLiteral("M"));
    d.gender = genders.join(QStringLiteral(" / "));
    d.event = fisDisciplineCode(p.type.discipline);
    d.referee = p.header.value(QLatin1String(HeaderKeys::Referee) + QStringLiteral(".name")).trimmed();
    d.published = published.isValid() ? published.toString(QStringLiteral("HH:mm")) : QString();
    d.deadline = deadlineTime.isValid() ? deadlineTime.toString(QStringLiteral("HH:mm")) : QString();

    // order of the bibs: bib or start list of the run (then the others by bib)
    const RunData& data = ev.run(run);
    QHash<int, int> position;
    for (int i = 0; i < data.startList.size(); ++i)
        position.insert(data.startList.at(i).bib, i);
    QVector<TimeInfo> infos;
    for (const TimeInfo& info : data.infos)
        if (!ev.isForerunner(info.bib))
            infos << info;
    std::sort(infos.begin(), infos.end(), [&](const TimeInfo& a, const TimeInfo& b) {
        if (order == Order::StartList) {
            const int pa = position.value(a.bib, INT_MAX), pb = position.value(b.bib, INT_MAX);
            if (pa != pb)
                return pa < pb;
        }
        return a.bib < b.bib;
    });
    for (const TimeInfo& info : infos) {
        const QString bib = QString::number(info.bib);
        switch (info.status) {
        case ResultStatus::DNS: d.didNotStart << bib; break;
        case ResultStatus::NPS: d.notPermitted << bib; break;
        case ResultStatus::DNF: d.didNotFinish << bib; break;
        case ResultStatus::DSQ: {
            RefereeReportData::Disqualified q;
            q.bib = bib;
            if (const Competitor* c = ev.competitorByBib(info.bib)) {
                q.name = c->fullName();
                q.nation = c->nation.trimmed().toUpper();
            } else {
                q.name = ev.competitorName(info.bib);
            }
            splitReason(info.reason, &q.gate, &q.notes);
            d.disqualified << q;
            break;
        }
        default: break;
        }
    }
    return d;
}

} // namespace RefereeReport
