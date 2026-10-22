#pragma once

#include "LiveState.h"
#include "timing/TimingDevice.h"

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QVector>
#include <array>
#include <memory>

class Event;
class QHttpServer;
class QHttpServerRequest;
class QHttpServerResponder;
class QTcpServer;
class QTimer;
class QWebSocket;
class TimingEngine;

// Read-only REST + WebSocket API of OpenSkiPro2 (docs/api.md).
// Listens to the signals of the event; the timing window attaches its engine and
// device states while it is open (live lists, clock, devices). All the work is done after the changes, coalesced
// in one pass per event loop iteration, so the timing is never delayed.
class ApiServer : public QObject {
    Q_OBJECT
public:
    static constexpr int kProtocol = 1;
    static constexpr quint16 kDefaultPort = 8080;

    explicit ApiServer(Event* ev, QObject* parent = nullptr);
    ~ApiServer() override;

    // Local configuration (AppSettings "Api/enabled", "Api/port").
    static bool settingEnabled();
    static quint16 settingPort();
    static void setSettings(bool enabled, quint16 port);
    void applySettings();

    bool start(quint16 port, QString* error = nullptr); // port 0: any free port (tests)
    void stop();
    bool isRunning() const;
    quint16 port() const;
    int clientCount() const { return int(m_clients.size()); }
    QString stateText() const; // for the status bars
    QString lastError() const { return m_error; }

    void attachTiming(TimingEngine* engine);
    void detachTiming();
    void setDevices(const QVector<TimingDeviceState>& devices);

    // A resource ("status", "run/1/results?group=0", ...) as served, with its "rev".
    // httpStatus: 200, 404 (unknown), 503 (no event).
    QJsonObject resource(const QString& name, int* httpStatus = nullptr);
    int revision(const QString& baseName) const { return m_cache.value(baseName).rev; }
    QString instance() const { return m_instance; }
    // Processes the pending changes now (normally done by a zero timer).
    void flush();

Q_SIGNALS:
    void stateChanged();
    // Every message sent to all the clients (envelope included), for the tests.
    void broadcasted(const QJsonObject& message);

private:
    struct Cached {
        int rev = 0;
        QByteArray bytes;   // compact JSON of the content, to detect changes
        QJsonObject content;
        int httpStatus = 200;
    };
    struct Client {
        QWebSocket* socket = nullptr;
        qint64 seq = 0;
        QSet<QString> subscriptions;
    };
    struct Name {
        QString base;
        int group = -1;
        int upto = 0;
        bool valid = false;
    };

    static Name parseName(const QString& name);
    QStringList baseNames() const;
    QJsonObject build(const Name& n, int* httpStatus);
    QJsonObject statusJson(bool forCompare) const;
    QJsonObject withRev(const QJsonObject& content, const QString& base) const;

    void markDirty(const QString& base);
    void markAllDirty();
    void markRunDirty(int run);
    void scheduleFlush();
    void resetLive();

    void onHttpRequest(const QHttpServerRequest& req, QHttpServerResponder& resp);
    void onNewWebSocket();
    void onTextMessage(Client* c, const QString& text);
    void dropClient(QWebSocket* s);
    QJsonObject envelope(Client& c, const QString& type, const QJsonValue& data) const;
    void send(Client& c, const QString& type, const QJsonValue& data);
    void broadcast(const QString& type, const QJsonValue& data);
    void sendResource(Client& c, const QString& name);
    void tickClock();
    QJsonObject clockJson() const;

    Event* m_ev;
    std::unique_ptr<QHttpServer> m_http;
    QTcpServer* m_tcp = nullptr; // owned by m_http
    QString m_error;
    QString m_instance;
    QHash<QString, Cached> m_cache;
    QSet<QString> m_dirty;
    bool m_flushPending = false;
    QHash<QWebSocket*, Client*> m_clients;

    QPointer<TimingEngine> m_engine;
    QVector<TimingDeviceState> m_devices;
    bool m_lastSynced = false;
    // state of every run: competitor events for all of them, at any time (also corrections
    // after the timing, e.g. a DSQ in Manage un-processed); GET /live for the timed run
    std::array<LiveState, kMaxRuns> m_runStates;
    QSet<int> m_dirtyRuns;
    QTimer* m_clock = nullptr;
    int m_clockTicks = 0;
};
