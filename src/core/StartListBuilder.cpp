#include "StartListBuilder.h"
#include "Event.h"
#include "Ranking.h"

#include <QRandomGenerator>
#include <algorithm>

namespace StartListBuilder {

QVector<StartEntry> firstRun(const Event& ev, FirstRunOrder order)
{
    QVector<const Competitor*> list;
    for (const auto& c : ev.competitors())
        if (c.bib > 0 && !ev.isForerunner(c.bib))
            list << &c;

    std::sort(list.begin(), list.end(), [](const Competitor* a, const Competitor* b) { return a->bib < b->bib; });
    if (order == FirstRunOrder::ByPoints) {
        const Discipline d = ev.params().type.discipline;
        std::stable_sort(list.begin(), list.end(), [d](const Competitor* a, const Competitor* b) {
            const int pa = a->pointsFor(d) < 0 ? INT_MAX : a->pointsFor(d);
            const int pb = b->pointsFor(d) < 0 ? INT_MAX : b->pointsFor(d);
            return pa < pb;
        });
    } else if (order == FirstRunOrder::Random) {
        std::shuffle(list.begin(), list.end(), *QRandomGenerator::global());
    }

    QVector<StartEntry> out;
    for (const Competitor* c : list) {
        StartEntry e;
        e.bib = c->bib;
        out << e;
    }
    return out;
}

QVector<StartEntry> nextRun(const Event& ev, int run, int reverseCount)
{
    QVector<StartEntry> out;
    if (run < 2)
        return out;
    const RankingResult ranking = Ranking::finalRanking(ev, run - 1);
    QVector<int> reversed, rest;
    for (const auto& e : ranking.classified) {
        if (reverseCount > 0 && e.rank <= reverseCount)
            reversed << e.bib;
        else
            rest << e.bib;
    }
    std::reverse(reversed.begin(), reversed.end());
    for (int b : reversed + rest) {
        StartEntry s;
        s.bib = b;
        out << s;
    }
    return out;
}

} // namespace StartListBuilder
