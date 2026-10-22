#include "ApiServer.h"
#include "ApiJson.h"
#include "ApiTestPage.h"
#include "app/AppSettings.h"
#include "core/Event.h"
#include "timing/TimingEngine.h"

#include <QDateTime>
#include <QFile>
#include <QHttpHeaders>
#include <QHttpServer>
#include <QHttpServerRequest>
#include <QHttpServerResponder>
#include <QHttpServerWebSocketUpgradeResponse>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTimer>
#include <QUrlQuery>
#include <QUuid>
#include <QWebSocket>
#include <algorithm>

namespace {
constexpr qint64 kMaxPendingBytes = 16 * 1024 * 1024; // a client this far behind is dropped

QByteArray compact(const QJsonObject& o)
{
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QJsonObject errorJson(const QString& code, const QString& message)
{
    QJsonObject o;
    o.insert(QStringLiteral("error"), code);
    o.insert(QStringLiteral("message"), message);
    return o;
}

QString deviceType(TimingDeviceConfig::Type t)
{
    switch (t) {
    case TimingDeviceConfig::Type::CP540: return QStringLiteral("CP540");
    case TimingDeviceConfig::Type::Simulator: return QStringLiteral("simulator");
    default: return QStringLiteral("none");
    }
}
} // namespace

ApiServer::ApiServer(Event* ev, QObject* parent)
    : QObject(parent)
    , m_ev(ev)
    , m_instance(QUuid::createUuid().toString(QUuid::WithoutBraces))
{
    connect(m_ev, &Event::opened, this, [this] {
        resetLive();
        markAllDirty();
    });
    connect(m_ev, &Event::closed, this, [this] {
        resetLive();
        markAllDirty();
    });
    connect(m_ev, &Event::paramsChanged, this, &ApiServer::markAllDirty);
    connect(m_ev, &Event::competitorsChanged, this, &ApiServer::markAllDirty);
    connect(m_ev, &Event::runChanged, this, &ApiServer::markRunDirty);
    connect(m_ev, &Event::recordChanged, this, [this](int run, int) { markRunDirty(run); });

    m_clock = new QTimer(this);
    connect(m_clock, &QTimer::timeout, this, &ApiServer::tickClock);
    m_clock->start(1000);

    markAllDirty();
    flush();
}

ApiServer::~ApiServer()
{
    blockSignals(true);
    stop();
}

// ---- settings / server ---------------------------------------------------------------

bool ApiServer::settingEnabled()
{
    return AppSettings::value(QStringLiteral("Api/enabled"), false).toBool();
}

quint16 ApiServer::settingPort()
{
    const int p = AppSettings::value(QStringLiteral("Api/port"), kDefaultPort).toInt();
    return quint16(p > 0 && p < 65536 ? p : kDefaultPort);
}

void ApiServer::setSettings(bool enabled, quint16 port)
{
    AppSettings::setValue(QStringLiteral("Api/enabled"), enabled);
    AppSettings::setValue(QStringLiteral("Api/port"), int(port));
}

void ApiServer::applySettings()
{
    if (!settingEnabled()) {
        stop();
        m_error.clear();
        Q_EMIT stateChanged();
        return;
    }
    if (isRunning() && port() == settingPort())
        return;
    start(settingPort());
}

bool ApiServer::start(quint16 port, QString* error)
{
    stop();
    m_error.clear();
    m_http = std::make_unique<QHttpServer>();
    m_http->setMissingHandler(this, [this](const QHttpServerRequest& req, QHttpServerResponder& resp) {
        onHttpRequest(req, resp);
    });
    m_http->addWebSocketUpgradeVerifier(this, [](const QHttpServerRequest& req) {
        return req.url().path() == QLatin1String("/ws") ? QHttpServerWebSocketUpgradeResponse::accept()
                                                        : QHttpServerWebSocketUpgradeResponse::passToNext();
    });
    connect(m_http.get(), &QAbstractHttpServer::newWebSocketConnection, this, &ApiServer::onNewWebSocket);

    m_tcp = new QTcpServer(m_http.get());
    if (!m_tcp->listen(QHostAddress::Any, port) || !m_http->bind(m_tcp)) {
        m_error = tr("port %1 not available").arg(port);
        if (error)
            *error = m_tcp->errorString();
        m_tcp = nullptr;
        m_http.reset();
        Q_EMIT stateChanged();
        return false;
    }
    Q_EMIT stateChanged();
    return true;
}

void ApiServer::stop()
{
    const auto sockets = m_clients.keys();
    for (QWebSocket* s : sockets) {
        s->disconnect(this);
        s->close();
        s->deleteLater();
        delete m_clients.take(s);
    }
    m_tcp = nullptr;
    m_http.reset();
    Q_EMIT stateChanged();
}

bool ApiServer::isRunning() const
{
    return m_tcp && m_tcp->isListening();
}

quint16 ApiServer::port() const
{
    return isRunning() ? m_tcp->serverPort() : 0;
}

QString ApiServer::stateText() const
{
    if (!isRunning())
        return m_error.isEmpty() ? tr("API: off") : tr("API: %1").arg(m_error);
    return tr("API :%1 - %n client(s)", nullptr, clientCount()).arg(port());
}

// ---- timing session ------------------------------------------------------------------

void ApiServer::attachTiming(TimingEngine* engine)
{
    if (m_engine)
        m_engine->disconnect(this);
    m_engine = engine;
    if (engine) {
        connect(engine, &TimingEngine::followChanged, this, [this] { markDirty(QStringLiteral("live")); });
        connect(engine, &QObject::destroyed, this, &ApiServer::detachTiming);
    }
    m_lastSynced = engine && engine->isClockSynced();
    markDirty(QStringLiteral("status"));
    markDirty(QStringLiteral("live"));
}

void ApiServer::detachTiming()
{
    if (m_engine)
        m_engine->disconnect(this);
    m_engine = nullptr;
    m_devices.clear();
    m_lastSynced = false;
    markDirty(QStringLiteral("status"));
    markDirty(QStringLiteral("live"));
}

void ApiServer::setDevices(const QVector<TimingDeviceState>& devices)
{
    if (devices == m_devices)
        return;
    m_devices = devices;
    markDirty(QStringLiteral("status"));
}

void ApiServer::resetLive()
{
    // a new event: the state of its runs without any event
    for (int r = 0; r < kMaxRuns; ++r) {
        m_runStates[size_t(r)].reset();
        if (m_ev->isOpen() && r < m_ev->runCount())
            m_runStates[size_t(r)].update(*m_ev, r + 1, true);
    }
    m_dirtyRuns.clear();
}

// ---- resources -----------------------------------------------------------------------

ApiServer::Name ApiServer::parseName(const QString& name)
{
    Name n;
    const int q = int(name.indexOf(QLatin1Char('?')));
    n.base = q < 0 ? name : name.left(q);
    static const QStringList fixed = { QStringLiteral("status"), QStringLiteral("event"), QStringLiteral("competitors"),
                                       QStringLiteral("runs"),   QStringLiteral("live"),  QStringLiteral("results") };
    static const QRegularExpression runRx(QStringLiteral("^run/([1-9])/(startlist|results)$"));
    const bool isRun = runRx.match(n.base).hasMatch();
    if (!fixed.contains(n.base) && !isRun)
        return n;
    if (q >= 0) {
        const bool groupAllowed = n.base == QLatin1String("results") || n.base.endsWith(QLatin1String("/results"));
        const QUrlQuery query(name.mid(q + 1));
        for (const auto& item : query.queryItems()) {
            bool ok = false;
            const int v = item.second.toInt(&ok);
            if (!ok || v < 0)
                return n;
            if (item.first == QLatin1String("group") && groupAllowed)
                n.group = v;
            else if (item.first == QLatin1String("upto") && n.base == QLatin1String("results"))
                n.upto = v;
            else
                return n;
        }
    }
    n.valid = true;
    return n;
}

QStringList ApiServer::baseNames() const
{
    QStringList l = { QStringLiteral("status"), QStringLiteral("event"), QStringLiteral("competitors"),
                      QStringLiteral("runs"),   QStringLiteral("live"),  QStringLiteral("results") };
    if (m_ev->isOpen())
        for (int r = 1; r <= m_ev->runCount(); ++r)
            l << QStringLiteral("run/%1/startlist").arg(r) << QStringLiteral("run/%1/results").arg(r);
    return l;
}

QJsonObject ApiServer::clockJson() const
{
    QJsonObject o;
    const qint64 us = m_engine ? m_engine->nowUs() : TimingEngine::currentDayTimeUs();
    o.insert(QStringLiteral("dayMs"), us / 1000);
    o.insert(QStringLiteral("synced"), bool(m_engine && m_engine->isClockSynced()));
    // the times of day are local: the offset gives ISO 8601 / UTC timestamps (FIS Live)
    o.insert(QStringLiteral("utcOffsetMin"), QDateTime::currentDateTime().offsetFromUtc() / 60);
    return o;
}

QJsonObject ApiServer::statusJson(bool forCompare) const
{
    QJsonObject o;
    o.insert(QStringLiteral("app"), QStringLiteral("OpenSkiPro2"));
    o.insert(QStringLiteral("version"), QStringLiteral(OSP_VERSION));
    o.insert(QStringLiteral("protocol"), kProtocol);
    o.insert(QStringLiteral("instance"), m_instance);

    QJsonObject ev;
    ev.insert(QStringLiteral("open"), m_ev->isOpen());
    ev.insert(QStringLiteral("path"), m_ev->isOpen() ? QJsonValue(m_ev->path()) : QJsonValue(QJsonValue::Null));
    o.insert(QStringLiteral("event"), ev);

    QJsonObject t;
    t.insert(QStringLiteral("active"), !m_engine.isNull());
    t.insert(QStringLiteral("run"), m_engine ? QJsonValue(m_engine->run()) : QJsonValue(QJsonValue::Null));
    QJsonObject clock = clockJson();
    if (forCompare)
        clock.remove(QStringLiteral("dayMs"));
    t.insert(QStringLiteral("clock"), clock);
    QJsonArray devices;
    for (int slot = 1; slot <= 2; ++slot) {
        TimingDeviceState st;
        st.slot = slot;
        bool found = false;
        for (const auto& d : m_devices)
            if (d.slot == slot) {
                st = d;
                found = true;
            }
        if (!found) {
            const TimingDeviceConfig cfg = AppSettings::timingDevice(slot);
            st.type = cfg.type;
            st.link = cfg.link;
        }
        QJsonObject d;
        d.insert(QStringLiteral("slot"), slot);
        d.insert(QStringLiteral("type"), deviceType(st.type));
        d.insert(QStringLiteral("link"), st.type == TimingDeviceConfig::Type::None ? QJsonValue(QJsonValue::Null)
                                         : st.link == TimingDeviceConfig::Link::Tcp ? QJsonValue(QStringLiteral("tcp"))
                                                                                     : QJsonValue(QStringLiteral("serial")));
        d.insert(QStringLiteral("connected"), st.connected);
        d.insert(QStringLiteral("text"), st.text.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(st.text));
        devices << d;
    }
    t.insert(QStringLiteral("devices"), devices);
    o.insert(QStringLiteral("timing"), t);

    if (!forCompare) {
        QJsonObject revs;
        for (const QString& n : baseNames())
            revs.insert(n, m_cache.value(n).rev);
        o.insert(QStringLiteral("revs"), revs);
    }
    return o;
}

QJsonObject ApiServer::build(const Name& n, int* httpStatus)
{
    *httpStatus = 200;
    if (n.base == QLatin1String("status"))
        return statusJson(true);
    if (n.base == QLatin1String("live"))
        return m_engine && m_ev->isOpen() && m_engine->run() >= 1 && m_engine->run() <= kMaxRuns
            ? m_runStates[size_t(m_engine->run() - 1)].live(*m_ev, m_engine)
            : LiveState().live(*m_ev, nullptr);
    if (!m_ev->isOpen()) {
        *httpStatus = 503;
        return errorJson(QStringLiteral("noEvent"), QStringLiteral("No event is open"));
    }
    auto notFound = [httpStatus](const QString& what) {
        *httpStatus = 404;
        return errorJson(QStringLiteral("notFound"), what);
    };
    if (n.group >= int(m_ev->params().outputGroups.size()))
        return notFound(QStringLiteral("No output group %1").arg(n.group));
    if (n.base == QLatin1String("event"))
        return ApiJson::event(*m_ev);
    if (n.base == QLatin1String("competitors"))
        return ApiJson::competitors(*m_ev);
    if (n.base == QLatin1String("runs"))
        return ApiJson::runs(*m_ev);
    if (n.base == QLatin1String("results")) {
        if (n.upto > m_ev->runCount())
            return notFound(QStringLiteral("No run %1").arg(n.upto));
        return ApiJson::finalResults(*m_ev, n.upto, n.group);
    }
    const QStringList parts = n.base.split(QLatin1Char('/'));
    const int run = parts.value(1).toInt();
    if (run < 1 || run > m_ev->runCount())
        return notFound(QStringLiteral("No run %1").arg(run));
    if (parts.value(2) == QLatin1String("startlist"))
        return ApiJson::startList(*m_ev, run);
    return ApiJson::runResults(*m_ev, run, n.group);
}

QJsonObject ApiServer::withRev(const QJsonObject& content, const QString& base) const
{
    QJsonObject o;
    o.insert(QStringLiteral("rev"), m_cache.value(base).rev);
    for (auto it = content.begin(); it != content.end(); ++it)
        o.insert(it.key(), it.value());
    return o;
}

QJsonObject ApiServer::resource(const QString& name, int* httpStatus)
{
    int status = 200;
    const Name n = parseName(name);
    QJsonObject out;
    if (!n.valid) {
        status = 404;
        out = errorJson(QStringLiteral("notFound"), QStringLiteral("Unknown resource %1").arg(name));
    } else if (n.base == QLatin1String("status")) {
        out = withRev(statusJson(false), n.base);
    } else {
        QJsonObject content;
        const auto it = m_cache.constFind(n.base);
        if (n.group < 0 && n.upto == 0 && it != m_cache.constEnd() && !m_dirty.contains(n.base)) {
            content = it->content;
            status = it->httpStatus;
        } else {
            content = build(n, &status);
        }
        out = status == 200 ? withRev(content, n.base) : content;
    }
    if (httpStatus)
        *httpStatus = status;
    return out;
}

void ApiServer::markDirty(const QString& base)
{
    m_dirty.insert(base);
    scheduleFlush();
}

void ApiServer::markAllDirty()
{
    if (m_ev->isOpen())
        for (int r = 1; r <= m_ev->runCount(); ++r)
            m_dirtyRuns.insert(r);
    for (const QString& n : baseNames())
        m_dirty.insert(n);
    // resources of runs which do not exist anymore
    for (auto it = m_cache.cbegin(); it != m_cache.cend(); ++it)
        m_dirty.insert(it.key());
    scheduleFlush();
}

void ApiServer::markRunDirty(int run)
{
    // the later runs too: their totals / previous rankings depend on this run
    for (int r = run; r <= kMaxRuns && (r == run || (m_ev->isOpen() && r <= m_ev->runCount())); ++r)
        m_dirtyRuns.insert(r);
    m_dirty.insert(QStringLiteral("run/%1/startlist").arg(run));
    m_dirty.insert(QStringLiteral("run/%1/results").arg(run));
    m_dirty.insert(QStringLiteral("runs"));
    m_dirty.insert(QStringLiteral("results"));
    m_dirty.insert(QStringLiteral("live"));
    scheduleFlush();
}

void ApiServer::scheduleFlush()
{
    if (m_flushPending)
        return;
    m_flushPending = true;
    QTimer::singleShot(0, this, &ApiServer::flush);
}

void ApiServer::flush()
{
    m_flushPending = false;
    if (m_dirty.isEmpty())
        return;
    const QSet<QString> dirty = m_dirty;
    m_dirty.clear();

    QVector<QPair<int, LiveState::Change>> changes; // (run, change)
    QList<int> runs(m_dirtyRuns.cbegin(), m_dirtyRuns.cend());
    std::sort(runs.begin(), runs.end());
    m_dirtyRuns.clear();
    if (m_ev->isOpen())
        for (int r : runs)
            if (r >= 1 && r <= m_ev->runCount() && r <= kMaxRuns)
                for (const auto& ch : m_runStates[size_t(r - 1)].update(*m_ev, r))
                    changes << qMakePair(r, ch);

    const QStringList names = baseNames();
    QJsonObject changed;
    for (const QString& name : dirty) {
        if (!names.contains(name)) {
            m_cache.remove(name); // run removed, event closed
            continue;
        }
        int status = 200;
        const QJsonObject content = build(parseName(name), &status);
        QByteArray bytes = compact(content);
        // the revision of the final ranking covers the rankings after every run (?upto=R):
        // a correction in the 1st run changes them even when the ranking over all runs
        // stays the same
        if (name == QLatin1String("results") && status == 200)
            for (int r = 1; r < m_ev->runCount(); ++r)
                bytes += compact(ApiJson::finalResults(*m_ev, r, -1));
        Cached& c = m_cache[name];
        if (c.rev > 0 && c.bytes == bytes && c.httpStatus == status)
            continue;
        ++c.rev;
        c.bytes = bytes;
        c.content = content;
        c.httpStatus = status;
        changed.insert(name, c.rev);
    }

    // order: status, competitor events, live, changed, subscribed resources (docs/api.md 5.4)
    if (changed.contains(QStringLiteral("status"))) {
        broadcast(QStringLiteral("status"), withRev(statusJson(false), QStringLiteral("status")));
        Q_EMIT stateChanged();
    }
    for (const auto& ch : changes)
        broadcast(QStringLiteral("competitor"), m_runStates[size_t(ch.first - 1)].competitorEvent(*m_ev, ch.second));
    if (changed.contains(QStringLiteral("live")))
        broadcast(QStringLiteral("live"), resource(QStringLiteral("live")));
    if (!changed.isEmpty()) {
        QJsonObject data;
        data.insert(QStringLiteral("resources"), changed);
        broadcast(QStringLiteral("changed"), data);
        const auto sockets = m_clients.keys();
        for (QWebSocket* s : sockets) {
            const QSet<QString> subs = m_clients.contains(s) ? m_clients.value(s)->subscriptions : QSet<QString>();
            for (const QString& name : subs)
                if (Client* c = m_clients.value(s); c && changed.contains(parseName(name).base))
                    sendResource(*c, name);
        }
    }
}

// ---- HTTP ----------------------------------------------------------------------------

void ApiServer::onHttpRequest(const QHttpServerRequest& req, QHttpServerResponder& resp)
{
    QHttpHeaders headers;
    headers.append(QHttpHeaders::WellKnownHeader::AccessControlAllowOrigin, "*");
    headers.append(QHttpHeaders::WellKnownHeader::CacheControl, "no-cache");
    const QString path = req.url().path();

    if (req.method() != QHttpServerRequest::Method::Get) {
        headers.append(QHttpHeaders::WellKnownHeader::ContentType, "application/json; charset=utf-8");
        resp.write(compact(errorJson(QStringLiteral("methodNotAllowed"), QStringLiteral("The API is read-only (GET)"))),
                   headers, QHttpServerResponder::StatusCode::MethodNotAllowed);
        return;
    }
    if (path == QLatin1String("/") || path == QLatin1String("/index.html")) {
        headers.append(QHttpHeaders::WellKnownHeader::ContentType, "text/html; charset=utf-8");
        resp.write(ApiTestPage::html(), headers);
        return;
    }

    if (path == QLatin1String("/docs") || path == QLatin1String("/docs/")) {
        headers.append(QHttpHeaders::WellKnownHeader::ContentType, "text/html; charset=utf-8");
        resp.write(ApiTestPage::swaggerHtml(), headers);
        return;
    }
    // embedded files: the OpenAPI description and Swagger UI
    static const QList<QPair<QString, QPair<QString, QByteArray>>> files = {
        { QStringLiteral("/api/v1/openapi.yaml"), { QStringLiteral(":/api/openapi.yaml"), "application/yaml; charset=utf-8" } },
        { QStringLiteral("/docs/swagger-ui-bundle.js"), { QStringLiteral(":/api/swagger/swagger-ui-bundle.js"), "text/javascript; charset=utf-8" } },
        { QStringLiteral("/docs/swagger-ui.css"), { QStringLiteral(":/api/swagger/swagger-ui.css"), "text/css; charset=utf-8" } },
        { QStringLiteral("/docs/LICENSE"), { QStringLiteral(":/api/swagger/LICENSE"), "text/plain; charset=utf-8" } },
        { QStringLiteral("/docs/NOTICE"), { QStringLiteral(":/api/swagger/NOTICE"), "text/plain; charset=utf-8" } },
    };
    for (const auto& f : files) {
        if (path != f.first)
            continue;
        QFile file(f.second.first);
        file.open(QIODevice::ReadOnly);
        headers.append(QHttpHeaders::WellKnownHeader::ContentType, f.second.second);
        resp.write(file.readAll(), headers);
        return;
    }

    QString name;
    const QString prefix = QStringLiteral("/api/v1/");
    if (path.startsWith(prefix)) {
        name = path.mid(prefix.size());
        if (name.endsWith(QLatin1Char('/')))
            name.chop(1);
        if (name.startsWith(QLatin1String("runs/")))
            name = QStringLiteral("run/") + name.mid(5);
        const QString query = req.url().query();
        if (!query.isEmpty())
            name += QLatin1Char('?') + query;
    }
    int status = 404;
    const QJsonObject body = name.isEmpty() ? errorJson(QStringLiteral("notFound"), QStringLiteral("Unknown path %1").arg(path))
                                            : resource(name, &status);
    if (status == 200) {
        const QByteArray etag = QStringLiteral("\"%1-%2\"").arg(m_instance).arg(body.value(QStringLiteral("rev")).toInt()).toUtf8();
        headers.append(QHttpHeaders::WellKnownHeader::ETag, etag);
        // the status contains the clock: always sent
        if (parseName(name).base != QLatin1String("status") && req.value("If-None-Match") == etag) {
            resp.write(headers, QHttpServerResponder::StatusCode::NotModified);
            return;
        }
    }
    headers.append(QHttpHeaders::WellKnownHeader::ContentType, "application/json; charset=utf-8");
    resp.write(compact(body), headers, QHttpServerResponder::StatusCode(status));
}

// ---- WebSocket -----------------------------------------------------------------------

void ApiServer::onNewWebSocket()
{
    while (m_http && m_http->hasPendingWebSocketConnections()) {
        QWebSocket* s = m_http->nextPendingWebSocketConnection().release();
        if (!s)
            break;
        s->setParent(this);
        auto* c = new Client;
        c->socket = s;
        m_clients.insert(s, c);
        connect(s, &QWebSocket::textMessageReceived, this, [this, s](const QString& text) {
            if (Client* cl = m_clients.value(s))
                onTextMessage(cl, text);
        });
        connect(s, &QWebSocket::disconnected, this, [this, s] { dropClient(s); });

        QJsonObject hello;
        hello.insert(QStringLiteral("protocol"), kProtocol);
        hello.insert(QStringLiteral("app"), QStringLiteral("OpenSkiPro2"));
        hello.insert(QStringLiteral("version"), QStringLiteral(OSP_VERSION));
        hello.insert(QStringLiteral("instance"), m_instance);
        send(*c, QStringLiteral("hello"), hello);
        if (m_clients.contains(s))
            send(*c, QStringLiteral("status"), resource(QStringLiteral("status")));
        if (m_clients.contains(s))
            send(*c, QStringLiteral("live"), resource(QStringLiteral("live")));
        Q_EMIT stateChanged();
    }
}

void ApiServer::dropClient(QWebSocket* s)
{
    Client* c = m_clients.take(s);
    if (!c)
        return;
    delete c;
    s->disconnect(this);
    s->deleteLater();
    Q_EMIT stateChanged();
}

void ApiServer::onTextMessage(Client* c, const QString& text)
{
    const QJsonObject msg = QJsonDocument::fromJson(text.toUtf8()).object();
    const QString type = msg.value(QStringLiteral("type")).toString();
    const QJsonArray list = msg.value(QStringLiteral("data")).toObject().value(QStringLiteral("resources")).toArray();
    if (type == QLatin1String("subscribe")) {
        for (const auto& v : list) {
            const QString name = v.toString();
            if (!parseName(name).valid) {
                QJsonObject err = errorJson(QStringLiteral("notFound"), QStringLiteral("Unknown resource %1").arg(name));
                err.insert(QStringLiteral("name"), name);
                send(*c, QStringLiteral("error"), err);
                continue;
            }
            c->subscriptions.insert(name);
            sendResource(*c, name);
        }
    } else if (type == QLatin1String("unsubscribe")) {
        for (const auto& v : list)
            c->subscriptions.remove(v.toString());
    } else {
        send(*c, QStringLiteral("error"), errorJson(QStringLiteral("unknownMessage"), QStringLiteral("Unknown message %1").arg(type)));
    }
}

void ApiServer::sendResource(Client& c, const QString& name)
{
    int status = 200;
    const QJsonObject content = resource(name, &status);
    QJsonObject data;
    data.insert(QStringLiteral("name"), name);
    data.insert(QStringLiteral("rev"), m_cache.value(parseName(name).base).rev);
    data.insert(QStringLiteral("status"), status);
    data.insert(QStringLiteral("content"), content);
    send(c, QStringLiteral("resource"), data);
}

QJsonObject ApiServer::envelope(Client& c, const QString& type, const QJsonValue& data) const
{
    QJsonObject o;
    o.insert(QStringLiteral("type"), type);
    o.insert(QStringLiteral("seq"), ++c.seq);
    o.insert(QStringLiteral("ts"), QDateTime::currentMSecsSinceEpoch());
    o.insert(QStringLiteral("data"), data);
    return o;
}

void ApiServer::send(Client& c, const QString& type, const QJsonValue& data)
{
    QWebSocket* s = c.socket;
    if (s->bytesToWrite() > kMaxPendingBytes) {
        s->abort(); // never wait for a slow client
        dropClient(s);
        return;
    }
    s->sendTextMessage(QString::fromUtf8(compact(envelope(c, type, data))));
}

void ApiServer::broadcast(const QString& type, const QJsonValue& data)
{
    QJsonObject msg;
    msg.insert(QStringLiteral("type"), type);
    msg.insert(QStringLiteral("data"), data);
    Q_EMIT broadcasted(msg);
    const auto sockets = m_clients.keys();
    for (QWebSocket* s : sockets)
        if (Client* c = m_clients.value(s)) // a slow client may have been dropped meanwhile
            send(*c, type, data);
}

void ApiServer::tickClock()
{
    ++m_clockTicks;
    const bool synced = m_engine && m_engine->isClockSynced();
    if (synced != m_lastSynced) {
        m_lastSynced = synced;
        markDirty(QStringLiteral("status"));
    }
    // every second while timing, else every 5 s
    if (!m_clients.isEmpty() && (m_engine || m_clockTicks % 5 == 0))
        broadcast(QStringLiteral("clock"), clockJson());
}
