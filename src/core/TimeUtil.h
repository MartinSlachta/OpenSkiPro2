#pragma once

#include "Types.h"

#include <QString>

// Time helpers.
//  - Time of day ("hour cell") is kept in microseconds since local midnight.
//    Values above 24h are allowed (timing over midnight).
//  - Results (net times, intermediates, penalties) are kept in milliseconds.
//  - Displayed results are always truncated, never rounded (FIS rule).
namespace TimeUtil {

constexpr qint64 kUsPerSecond = 1000000;
constexpr qint64 kUsPerDay = 86400LL * kUsPerSecond;
constexpr qint64 kNoTime = -1;

// Truncates a result in ms to the given precision.
qint64 truncateMs(qint64 ms, Precision p);

// Formats a result in ms, e.g. "15.86", "1:02.34", "1:00:02.34".
QString formatResult(qint64 ms, Precision p);

// Formats a gap ("+1.23" style is not used by SkiPro, plain "1.23").
QString formatGap(qint64 ms, Precision p);

// Parses "1:02.34", "62.34", "62,34", "1:00:02.3" into ms. Returns kNoTime on error.
qint64 parseResult(const QString& text);

// Time of day formatting as printed by SkiPro on the timing device paper:
// "13h50:12.67000" (decimals = number of fraction digits, 0..6).
QString formatDayTime(qint64 us, int decimals = 5);
// "13:50:12.670" style.
QString formatClock(qint64 us, int decimals = 3);

// Parses "13:50:12.67", "13h50:12.67000", "13:50:12" into us. Returns kNoTime on error.
qint64 parseDayTime(const QString& text);

// Net time between two day times in ms, truncated to 1/1000.
qint64 netTimeMs(qint64 startUs, qint64 finishUs);

// Points (hundredths) helpers: 5637 -> "56,37". Negative -> "-".
QString formatPoints(int hundredths);
int parsePoints(const QString& text); // returns -1 if empty/invalid

int decimalsFor(Precision p);

} // namespace TimeUtil
