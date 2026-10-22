#pragma once

#include "RunData.h"

#include <QVector>

class Event;

namespace StartListBuilder {

enum class FirstRunOrder {
    ByBib,
    ByPoints,   // ascending points of the event discipline, competitors without points last
    Random,
};

// Start list for the first run built from the competitors (bib > 0, no forerunners).
QVector<StartEntry> firstRun(const Event& ev, FirstRunOrder order);

// Start list of run `run` (>= 2) from the ranking after run-1: competitors
// classified so far, the best `reverseCount` ranks (ties at the limit included)
// in reverse order, the rest in ranking order. reverseCount = 0 keeps the ranking order.
QVector<StartEntry> nextRun(const Event& ev, int run, int reverseCount);

} // namespace StartListBuilder
