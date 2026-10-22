#include "FisPenalty.h"
#include "Event.h"
#include "FisList.h"
#include "Ranking.h"

#include <QCoreApplication>
#include <algorithm>
#include <cmath>

namespace FisPenalty {

int Result::totalPoints(int bib) const
{
    const auto it = racePoints.constFind(bib);
    return it == racePoints.constEnd() ? -1 : *it + applied;
}

Gender genderOf(Gender g)
{
    return g == Gender::Female ? Gender::Female : Gender::Male;
}

QVector<Gender> genders(const Event& ev)
{
    bool men = false, women = false;
    for (const auto& c : ev.competitors())
        (genderOf(c.gender) == Gender::Female ? women : men) = true;
    QVector<Gender> out;
    if (men)
        out << Gender::Male;
    if (women)
        out << Gender::Female;
    return out;
}

int roundDiv(qint64 num, qint64 den)
{
    if (den == 0)
        return 0;
    if (den < 0) {
        num = -num;
        den = -den;
    }
    // half away from zero
    const qint64 q = (2 * std::llabs(num) + den) / (2 * den);
    return int(num < 0 ? -q : q);
}

int racePoints(qint64 timeMs, qint64 winnerMs, double fValue)
{
    if (timeMs < 0 || winnerMs <= 0)
        return -1;
    // P x 100 = (Tx - To) x F x 100 / To, exact with F in hundredths
    const qint64 f100 = qRound64(fValue * 100.0);
    return roundDiv((timeMs - winnerMs) * f100, winnerMs);
}

static int used(int points, int maxValue)
{
    return points < 0 || points > maxValue ? maxValue : points;
}

Result compute(const Event& ev, Gender g)
{
    return compute(ev, g, ev.params().fisPoints.forGender(genderOf(g)));
}

Result compute(const Event& ev, Gender gIn, const FisPointsParams& p)
{
    Result r;
    const Gender g = genderOf(gIn);
    r.gender = g;
    const Discipline disc = ev.params().type.discipline;
    if (!p.isSet() || p.maxValue <= 0) {
        r.error = QCoreApplication::translate("FisPenalty", "The F value and the maximum value are not set.");
        return r;
    }
    auto inGender = [&](int bib) {
        const Competitor* c = ev.competitorByBib(bib);
        return c && genderOf(c->gender) == g;
    };
    const RankingResult all = Ranking::finalRanking(ev, 0);
    const RankingResult res = Ranking::subRanking(all, inGender, ev.precision());
    if (res.classified.isEmpty()) {
        r.error = QCoreApplication::translate("FisPenalty", "No competitor classified.");
        return r;
    }
    auto line = [&](const RankedEntry& e, bool classified) {
        Line l;
        l.bib = e.bib;
        l.rank = classified ? e.rank : 0;
        l.status = e.status;
        l.statusRun = e.statusRun;
        const Competitor* c = ev.competitorByBib(e.bib);
        l.listPoints = c ? c->pointsFor(disc) : -1;
        l.usedPoints = used(l.listPoints, p.maxValue);
        return l;
    };

    // race points of the classified competitors
    const qint64 winner = res.classified.first().shownMs;
    QVector<Line> classified;
    int withoutPoints = 0;
    for (const auto& e : res.classified) {
        Line l = line(e, true);
        l.racePoints = racePoints(e.shownMs, winner, p.fValue);
        l.usedRacePoints = qMin(l.racePoints, p.maxValue);
        r.racePoints.insert(e.bib, l.racePoints);
        if (l.listPoints < 0)
            ++withoutPoints;
        classified << l;
    }

    // A / C: the 5 best FIS points among the 10 first (ties at the 10th rank included);
    // equal points for the 5th place: the higher race points (better penalty)
    for (const Line& l : classified)
        if (l.rank <= 10)
            r.top10 << l;
    QVector<int> order;
    for (int i = 0; i < r.top10.size(); ++i)
        order << i;
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        const Line& x = r.top10.at(a);
        const Line& y = r.top10.at(b);
        if (x.usedPoints != y.usedPoints)
            return x.usedPoints < y.usedPoints;
        return x.racePoints > y.racePoints;
    });
    int withPointsInA = 0;
    for (int k = 0; k < order.size() && k < 5; ++k) {
        Line& l = r.top10[order.at(k)];
        l.selected = true;
        r.sumA += l.usedPoints;
        r.sumC += l.usedRacePoints;
        if (l.listPoints >= 0 && l.listPoints <= p.maxValue)
            ++withPointsInA;
    }
    r.missingA = qMax(0, 5 - int(order.size()));
    r.sumA += r.missingA * p.maxValue;
    r.sumC += r.missingA * p.maxValue;

    // B: the 5 best FIS points of the competitors who started the race (not DNS1 / NPS1, art. 4.4.4)
    QVector<Line> started = classified;
    for (const auto& grp : res.unclassified) {
        if ((grp.status == ResultStatus::DNS || grp.status == ResultStatus::NPS) && grp.run <= 1)
            continue;
        if (grp.status == ResultStatus::NotProcessed)
            continue;
        for (const auto& e : grp.entries)
            started << line(e, false);
    }
    std::stable_sort(started.begin(), started.end(),
                     [](const Line& a, const Line& b) { return a.usedPoints < b.usedPoints; });
    for (int k = 0; k < started.size() && k < 5; ++k) {
        r.best5Start << started.at(k);
        r.sumB += started.at(k).usedPoints;
    }
    r.missingB = qMax(0, 5 - int(started.size()));
    r.sumB += r.missingB * p.maxValue;

    r.calculated = roundDiv(qint64(r.sumA) + r.sumB - r.sumC, 10);
    r.zValue = p.zValue;
    r.adder = p.adder;
    r.minPenalty = p.minPenalty;
    r.maxPenalty = p.maxPenalty;
    if (withPointsInA < 3 || withoutPoints >= 3) {
        r.doubledMinimum = 2 * p.maxValue > r.minPenalty;
        r.minPenalty = qMax(r.minPenalty, 2 * p.maxValue);
    }
    int applied = r.calculated + r.zValue + r.adder;
    applied = qMax(applied, r.minPenalty);
    applied = qMin(applied, r.maxPenalty);
    r.computedApplied = applied;
    r.overridden = p.appliedOverride >= 0;
    r.applied = r.overridden ? p.appliedOverride : applied;
    r.valid = true;
    return r;
}

bool loadFromList(const FisList& list, Discipline d, const QString& catcode, Gender g, FisPointsParams& out,
                  QString* error)
{
    const QString code = fisDisciplineCode(d);
    const FisDisciplineParams* dp = list.disciplineParams(code, genderOf(g));
    if (!dp) {
        if (error)
            *error = QCoreApplication::translate("FisPenalty", "No %1 constants in the FIS list.").arg(code);
        return false;
    }
    const FisRaceCategory* rc = fisRaceCategory(catcode);
    const FisCategoryPenalty* pen = list.penalty(catcode);
    if (!rc || !pen) {
        if (error)
            *error = QCoreApplication::translate("FisPenalty", "The category %1 is not in the FIS list.").arg(catcode);
        return false;
    }
    out.fValue = dp->fValue;
    out.maxValue = qRound(dp->maxPoints * 100.0);
    out.zValue = qRound(dp->zValue * 100.0);
    out.adder = qRound(dp->adders[size_t(qBound(0, rc->level, 4))] * 100.0);
    out.minPenalty = qRound(pen->minPenalty * 100.0);
    out.maxPenalty = qRound(pen->maxPenalty * 100.0);
    return true;
}

} // namespace FisPenalty
