#pragma once

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <functional>

class QIODevice;
class QWebSocket;
class QTimer;

// Black box of the OpenSkiPro2 API (docs/api.md): records every WebSocket message of a
// timed run and checks them afterwards against the REST resources. It is an outside
// client like any other: it only uses the public API.
namespace ApiCheck {

// One recorded line: {"rx": <ms since epoch>, "conn": <n>, "msg": {...}} or
// {"rx": ..., "conn": <n>, "note": "connected" / "disconnected"}.
using Line = QJsonObject;

// Connects to ws://host:port/ws, subscribes to every resource (and to the final
// ranking after the run being timed and before it), and records everything.
// Reconnects after a lost connection.
class Recorder : public QObject {
    Q_OBJECT
public:
    explicit Recorder(QObject* parent = nullptr);
    void start(const QUrl& wsUrl);
    void stop();
    void setOutput(QIODevice* out) { m_out = out; } // one JSON line per message, flushed
    const QList<Line>& lines() const { return m_lines; }

Q_SIGNALS:
    void received(const QJsonObject& message);

private:
    void record(Line line);
    void onMessage(const QString& text);
    void subscribeFor(const QJsonObject& status);

    QWebSocket* m_ws;
    QTimer* m_retry;
    QUrl m_url;
    bool m_running = false;
    int m_conn = 0;
    QSet<QString> m_subscribed;
    QIODevice* m_out = nullptr;
    QList<Line> m_lines;
};

struct Report {
    QStringList errors;
    QStringList warnings;
    QStringList stats;
    int checks = 0;

    bool ok() const { return errors.isEmpty(); }
    QString text() const;
};

// Reads a resource ("event", "run/1/results", "results?upto=1" ...) from the REST API;
// httpStatus 0 when the API cannot be reached.
using Fetch = std::function<QJsonObject(const QString& name, int* httpStatus)>;

Report check(const QList<Line>& lines, const Fetch& fetch);

QList<Line> readLines(QIODevice* in, QString* error);

} // namespace ApiCheck
