#pragma once

#include "core/Types.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

class Event;
struct Competitor;

// JSON of the REST resources of the API (docs/api.md). Pure functions of the event,
// shared by the HTTP server, the WebSocket pushes and the tests.
namespace ApiJson {

// "timeMs" (truncated to the precision) + "time" (formatted); null when there is no time.
void putTime(QJsonObject& o, const QString& key, qint64 ms, Precision p);
// "gapMs" + "gap" (formatted, null for 0)
void putGap(QJsonObject& o, qint64 gapMs, Precision p);
QJsonValue dayMs(qint64 dayTimeUs); // ms of the time of day, null when there is none
QJsonValue statusCode(ResultStatus s); // "QLF", "DNS", "NPS", "DNF", "DSQ", null

// Compact competitor objects embedded in the lists, built once per resource.
class CompetitorCache {
public:
    explicit CompetitorCache(const Event& ev);
    QJsonObject get(int bib);

private:
    const Event& m_ev;
    QHash<int, QJsonObject> m_cache;
};
QJsonObject competitor(const Event& ev, const Competitor& c);
// Indices of the output groups the competitor belongs to.
QJsonArray groupsOf(const Event& ev, const Competitor& c);

QJsonObject event(const Event& ev);
QJsonObject competitors(const Event& ev);
QJsonObject runs(const Event& ev);
QJsonObject startList(const Event& ev, int run);
// group: index of the output group, -1 = everybody
QJsonObject runResults(const Event& ev, int run, int group);
// upto: last run (0 = all runs)
QJsonObject finalResults(const Event& ev, int upto, int group);

} // namespace ApiJson
