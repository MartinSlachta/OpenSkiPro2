#include "ApiCheck.h"

#include <QDateTime>
#include <QFileDevice>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>
#include <QWebSocket>
#include <algorithm>
#include <climits>

namespace ApiCheck {

// ---- recorder -------------------------------------------------------------------------

Recorder::Recorder(QObject* parent)
    : QObject(parent)
    , m_ws(new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this))
    , m_retry(new QTimer(this))
{
    m_retry->setSingleShot(true);
    m_retry->setInterval(2000);
    connect(m_retry, &QTimer::timeout, this, [this] { m_ws->open(m_url); });
    connect(m_ws, &QWebSocket::connected, this, [this] {
        ++m_conn;
        m_subscribed.clear();
        Line l;
        l.insert(QStringLiteral("note"), QStringLiteral("connected"));
        record(l);
    });
    connect(m_ws, &QWebSocket::disconnected, this, [this] {
        if (m_conn > 0) {
            Line l;
            l.insert(QStringLiteral("note"), m_running ? QStringLiteral("disconnected") : QStringLiteral("stopped"));
            record(l);
        }
        if (m_running)
            m_retry->start();
    });
    connect(m_ws, &QWebSocket::textMessageReceived, this, &Recorder::onMessage);
}

void Recorder::start(const QUrl& wsUrl)
{
    m_url = wsUrl;
    m_running = true;
    m_ws->open(m_url);
}

void Recorder::stop()
{
    m_running = false;
    m_retry->stop();
    m_ws->close();
}

void Recorder::record(Line line)
{
    line.insert(QStringLiteral("rx"), QDateTime::currentMSecsSinceEpoch());
    line.insert(QStringLiteral("conn"), m_conn);
    m_lines << line;
    if (m_out) {
        m_out->write(QJsonDocument(line).toJson(QJsonDocument::Compact) + '\n');
        if (auto* f = qobject_cast<QFileDevice*>(m_out))
            f->flush(); // nothing lost when the recorder is stopped with Ctrl+C
    }
}

void Recorder::onMessage(const QString& text)
{
    const QJsonObject msg = QJsonDocument::fromJson(text.toUtf8()).object();
    Line l;
    l.insert(QStringLiteral("msg"), msg);
    record(l);
    Q_EMIT received(msg);
    if (msg.value(QStringLiteral("type")).toString() == QLatin1String("status"))
        subscribeFor(msg.value(QStringLiteral("data")).toObject());
}

void Recorder::subscribeFor(const QJsonObject& status)
{
    QStringList names;
    const QJsonObject revs = status.value(QStringLiteral("revs")).toObject();
    for (auto it = revs.begin(); it != revs.end(); ++it)
        if (it.key() != QLatin1String("status") && it.key() != QLatin1String("live"))
            names << it.key();
    const int run = status.value(QStringLiteral("timing")).toObject().value(QStringLiteral("run")).toInt();
    if (run > 0)
        names << QStringLiteral("results?upto=%1").arg(run);
    if (run > 1)
        names << QStringLiteral("results?upto=%1").arg(run - 1);
    QJsonArray add;
    for (const QString& n : names)
        if (!m_subscribed.contains(n)) {
            m_subscribed.insert(n);
            add << n;
        }
    if (add.isEmpty())
        return;
    QJsonObject data;
    data.insert(QStringLiteral("resources"), add);
    QJsonObject msg;
    msg.insert(QStringLiteral("type"), QStringLiteral("subscribe"));
    msg.insert(QStringLiteral("data"), data);
    m_ws->sendTextMessage(QString::fromUtf8(QJsonDocument(msg).toJson(QJsonDocument::Compact)));
}

QList<Line> readLines(QIODevice* in, QString* error)
{
    QList<Line> lines;
    int n = 0;
    while (!in->atEnd()) {
        const QByteArray raw = in->readLine().trimmed();
        ++n;
        if (raw.isEmpty())
            continue;
        QJsonParseError pe;
        const QJsonDocument doc = QJsonDocument::fromJson(raw, &pe);
        if (!doc.isObject()) {
            if (error)
                *error = QStringLiteral("line %1: %2").arg(n).arg(pe.errorString());
            continue; // a line cut by stopping the recorder
        }
        lines << doc.object();
    }
    return lines;
}

QString Report::text() const
{
    QStringList out;
    out << QStringLiteral("== OpenSkiPro2 API check ==");
    out << stats;
    out << QStringLiteral("checks: %1, errors: %2, warnings: %3").arg(checks).arg(errors.size()).arg(warnings.size());
    for (const QString& e : errors)
        out << QStringLiteral("ERROR   ") + e;
    for (const QString& w : warnings)
        out << QStringLiteral("WARNING ") + w;
    out << (ok() ? QStringLiteral("RESULT: OK") : QStringLiteral("RESULT: FAILED"));
    return out.join(QLatin1Char('\n'));
}

// ---- checker -------------------------------------------------------------------------

namespace {

constexpr int kMaxListed = 300; // errors / warnings listed

qint64 unitFor(int decimals)
{
    qint64 u = 1;
    for (int i = decimals; i < 3; ++i)
        u *= 10;
    return u;
}

// The time format of docs/api.md: truncated, "15.86", "1:02.34", "1:00:02.34".
QString formatMs(qint64 ms, int decimals)
{
    const qint64 unit = unitFor(decimals);
    const qint64 t = ms / unit * unit;
    const qint64 sec = t / 1000, h = sec / 3600, m = sec % 3600 / 60, s = sec % 60;
    QString out = h > 0   ? QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'))
                  : m > 0 ? QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QChar('0'))
                          : QString::number(s);
    if (decimals > 0)
        out += QLatin1Char('.') + QStringLiteral("%1").arg(t % 1000 / unit, decimals, 10, QChar('0'));
    return out;
}

QString baseOf(const QString& name)
{
    const int q = int(name.indexOf(QLatin1Char('?')));
    return q < 0 ? name : name.left(q);
}

int orderOf(const QString& state)
{
    if (state == QLatin1String("notStarted"))
        return 0;
    if (state == QLatin1String("onCourse"))
        return 1;
    return 2;
}

bool isOut(const QString& state)
{
    return state == QLatin1String("dns") || state == QLatin1String("nps") || state == QLatin1String("dnf") || state == QLatin1String("dsq");
}

// The own result of every competitor of a run ranking, without the ranks and gaps that
// move with the others: "QLF 63020 +0 [21950]", "DSQ Gate 5"; not processed = none.
QHash<int, QString> ownResults(const QJsonObject& ranking)
{
    QHash<int, QString> m;
    for (const auto& v : ranking.value(QStringLiteral("classified")).toArray()) {
        const QJsonObject o = v.toObject();
        QStringList inter;
        for (const auto& i : o.value(QStringLiteral("inter")).toArray())
            inter << QString::number(i.toObject().value(QStringLiteral("timeMs")).toInteger());
        m.insert(o.value(QStringLiteral("bib")).toInt(), QStringLiteral("QLF %1 +%2 [%3]")
                                                             .arg(o.value(QStringLiteral("timeMs")).toInteger())
                                                             .arg(o.value(QStringLiteral("penaltyMs")).toInteger())
                                                             .arg(inter.join(QLatin1Char(','))));
    }
    for (const auto& g : ranking.value(QStringLiteral("unclassified")).toArray()) {
        const QString kind = g.toObject().value(QStringLiteral("kind")).toString();
        if (kind == QLatin1String("notProcessed"))
            continue;
        for (const auto& e : g.toObject().value(QStringLiteral("entries")).toArray())
            m.insert(e.toObject().value(QStringLiteral("bib")).toInt(),
                     kind + QLatin1Char(' ') + e.toObject().value(QStringLiteral("reason")).toString());
    }
    return m;
}

QJsonObject rowOf(const QJsonObject& ranking, int bib)
{
    for (const auto& v : ranking.value(QStringLiteral("classified")).toArray())
        if (v.toObject().value(QStringLiteral("bib")).toInt() == bib)
            return v.toObject();
    return QJsonObject();
}

QString unclassifiedKind(const QJsonObject& ranking, int bib, int run)
{
    for (const auto& g : ranking.value(QStringLiteral("unclassified")).toArray()) {
        const QJsonObject go = g.toObject();
        if (run > 0 && go.value(QStringLiteral("run")).toInt() != run)
            continue;
        for (const auto& e : go.value(QStringLiteral("entries")).toArray())
            if (e.toObject().value(QStringLiteral("bib")).toInt() == bib)
                return go.value(QStringLiteral("kind")).toString();
    }
    return QString();
}

qint64 ms(const QJsonObject& o, const char* key)
{
    const QJsonValue v = o.value(QLatin1String(key));
    return v.isNull() || v.isUndefined() ? -1 : v.toInteger();
}

struct BibState {
    QString state;
    int lastPoint = 0;
    QJsonObject last; // last competitor event
};

class Checker {
public:
    Checker(Report& r, const Fetch& fetch)
        : m_rep(r)
        , m_fetch(fetch)
    {
    }

    void run(const QList<Line>& lines);

private:
    void error(const QString& where, const QString& what)
    {
        if (m_rep.errors.size() < kMaxListed)
            m_rep.errors << where + QStringLiteral(": ") + what;
        else if (m_rep.errors.size() == kMaxListed)
            m_rep.errors << QStringLiteral("... more errors not listed");
    }
    void warning(const QString& where, const QString& what)
    {
        if (m_rep.warnings.size() < kMaxListed)
            m_rep.warnings << where + QStringLiteral(": ") + what;
    }
    template <typename T>
    void expectEq(const QString& where, const QString& what, const T& actual, const T& expected)
    {
        ++m_rep.checks;
        if (!(actual == expected))
            error(where, QStringLiteral("%1 is %2, expected %3").arg(what, str(actual), str(expected)));
    }
    static QString str(qint64 v) { return v < 0 ? QStringLiteral("null") : QString::number(v); }
    static QString str(int v) { return QString::number(v); }
    static QString str(const QString& v) { return v.isNull() ? QStringLiteral("null") : QLatin1Char('"') + v + QLatin1Char('"'); }
    static QString str(bool v) { return v ? QStringLiteral("true") : QStringLiteral("false"); }

    void message(const QJsonObject& line);
    void flush();
    void formats(const QJsonValue& v, const QString& where);
    void competitor(const QJsonObject& d, const QString& where, const QSet<QString>& stale);
    void compareRow(const QString& where, const QString& what, const QJsonObject& event, const QString& resource,
                    int bib, const QSet<QString>& stale, bool withPenalty);
    void live(const QJsonObject& d, const QString& where);
    void baseline(const QJsonObject& live);
    void clock(const QJsonObject& d, qint64 rx, const QString& where);
    void finalCompare();

    Report& m_rep;
    Fetch m_fetch;
    int m_decimals = 2;
    QString m_instance;

    // connection
    int m_conn = -1;
    qint64 m_seq = 0;
    int m_snapshot = 0; // messages of the snapshot after hello still expected
    QList<QPair<QJsonObject, qint64>> m_batch; // (message, rx)
    int m_phase = 0; // 0 idle, 1 competitor, 2 live, 3 changed / resources

    // state
    QHash<QString, int> m_rev;
    QHash<QString, QJsonObject> m_content; // subscribed resources (by name), live
    QHash<QString, int> m_contentRev;
    QSet<QString> m_stale; // pushed content known to be out of date
    QHash<qint64, BibState> m_bibs; // run * 10000 + bib
    int m_timingRun = 0;
    bool m_liveKnown = false;

    // clock
    qint64 m_lastDay = -1;
    bool m_lastSynced = false;
    qint64 m_offMin = 0, m_offMax = 0;
    bool m_offSet = false;

    // statistics
    QHash<QString, int> m_types;
    QHash<QString, int> m_changes;
    QSet<qint64> m_bibsSeen;
    qint64 m_firstRx = 0, m_lastRx = 0;
    int m_connections = 0;
};

void Checker::formats(const QJsonValue& v, const QString& where)
{
    if (v.isArray()) {
        for (const auto& e : v.toArray())
            formats(e, where);
        return;
    }
    if (!v.isObject())
        return;
    const QJsonObject o = v.toObject();
    for (auto it = o.begin(); it != o.end(); ++it) {
        const QString k = it.key();
        if (k.endsWith(QLatin1String("Ms")) && !k.endsWith(QLatin1String("DayMs")) && k != QLatin1String("dayMs")
            && k != QLatin1String("penaltyMs")) {
            const QString textKey = k.left(k.size() - 2);
            if (!o.contains(textKey))
                continue;
            const qint64 value = it->isNull() ? -1 : it->toInteger();
            const QJsonValue text = o.value(textKey);
            ++m_rep.checks;
            if (value < 0) {
                if (!text.isNull())
                    error(where, QStringLiteral("%1 is null but %2 is \"%3\"").arg(k, textKey, text.toString()));
                continue;
            }
            if (value != value / unitFor(m_decimals) * unitFor(m_decimals))
                error(where, QStringLiteral("%1 = %2 is not truncated to %3 decimals").arg(k).arg(value).arg(m_decimals));
            const bool gap = textKey == QLatin1String("gap");
            const QJsonValue expected = gap && value == 0 ? QJsonValue(QJsonValue::Null) : QJsonValue(formatMs(value, m_decimals));
            if (text != expected)
                error(where, QStringLiteral("%1 is %2 for %3 = %4, expected %5")
                                 .arg(textKey, text.isNull() ? QStringLiteral("null") : QLatin1Char('"') + text.toString() + QLatin1Char('"'), k)
                                 .arg(value)
                                 .arg(expected.isNull() ? QStringLiteral("null") : QLatin1Char('"') + expected.toString() + QLatin1Char('"')));
        }
        if (it->isObject() || it->isArray())
            formats(*it, where);
    }
}

void Checker::run(const QList<Line>& lines)
{
    int st = 0;
    const QJsonObject event = m_fetch(QStringLiteral("event"), &st);
    if (st == 200)
        m_decimals = event.value(QStringLiteral("type")).toObject().value(QStringLiteral("precision")).toInt(2);
    else
        warning(QStringLiteral("REST"), QStringLiteral("GET /event gave %1, the precision is taken as 2 decimals").arg(st));

    for (const Line& l : lines) {
        const qint64 rx = l.value(QStringLiteral("rx")).toInteger();
        if (!m_firstRx)
            m_firstRx = rx;
        m_lastRx = rx;
        const int conn = l.value(QStringLiteral("conn")).toInt();
        if (conn != m_conn) {
            flush();
            m_conn = conn;
            m_seq = 0;
            m_phase = 0;
            m_snapshot = 3;
            m_lastDay = -1;
            ++m_connections;
        }
        if (l.contains(QStringLiteral("note"))) {
            if (l.value(QStringLiteral("note")).toString() == QLatin1String("disconnected"))
                warning(QStringLiteral("connection %1").arg(conn), QStringLiteral("disconnected (reconnected later: %1)")
                                                                         .arg(&l == &lines.last() ? QStringLiteral("no") : QStringLiteral("yes")));
            continue;
        }
        QJsonObject msg = l.value(QStringLiteral("msg")).toObject();
        msg.insert(QStringLiteral("_rx"), rx);
        message(msg);
    }
    flush();
    finalCompare();

    QStringList types;
    for (auto it = m_types.cbegin(); it != m_types.cend(); ++it)
        types << QStringLiteral("%1 %2").arg(it.key()).arg(it.value());
    types.sort();
    QStringList changes;
    for (auto it = m_changes.cbegin(); it != m_changes.cend(); ++it)
        changes << QStringLiteral("%1 %2").arg(it.key()).arg(it.value());
    changes.sort();
    m_rep.stats << QStringLiteral("recording: %1 lines, %2 connection(s), %3 s").arg(lines.size()).arg(m_connections).arg((m_lastRx - m_firstRx) / 1000);
    m_rep.stats << QStringLiteral("messages: ") + types.join(QStringLiteral(", "));
    m_rep.stats << QStringLiteral("competitor events: ") + changes.join(QStringLiteral(", "));
    m_rep.stats << QStringLiteral("competitors seen: %1").arg(m_bibsSeen.size());
    if (m_offSet) {
        m_rep.stats << QStringLiteral("clock: spread of (reception - device time) %1 ms").arg(m_offMax - m_offMin);
        if (m_offMax - m_offMin > 1500)
            warning(QStringLiteral("clock"), QStringLiteral("the clock messages drift against the PC by %1 ms").arg(m_offMax - m_offMin));
    }
}

void Checker::message(const QJsonObject& msg)
{
    const QString type = msg.value(QStringLiteral("type")).toString();
    const qint64 seq = msg.value(QStringLiteral("seq")).toInteger();
    const QString where = QStringLiteral("conn %1 seq %2 %3").arg(m_conn).arg(seq).arg(type);
    const QJsonObject d = msg.value(QStringLiteral("data")).toObject();
    m_types[type]++;

    ++m_rep.checks;
    if (seq != m_seq + 1)
        error(where, QStringLiteral("seq after %1 (lost or doubled messages)").arg(m_seq));
    m_seq = seq;
    formats(d, where);

    // snapshot: hello, status, live
    if (m_snapshot > 0) {
        static const char* expected[] = { "hello", "status", "live" };
        const QString exp = QLatin1String(expected[3 - m_snapshot]);
        --m_snapshot;
        expectEq(where, QStringLiteral("message of the snapshot"), type, exp);
        if (type == QLatin1String("hello")) {
            expectEq(where, QStringLiteral("protocol"), d.value(QStringLiteral("protocol")).toInt(), 1);
            const QString inst = d.value(QStringLiteral("instance")).toString();
            if (!m_instance.isEmpty() && inst != m_instance)
                warning(where, QStringLiteral("OpenSkiPro2 was restarted during the recording"));
            m_instance = inst;
            return;
        }
        if (type == QLatin1String("status")) {
            const QJsonObject revs = d.value(QStringLiteral("revs")).toObject();
            for (auto it = revs.begin(); it != revs.end(); ++it)
                m_rev.insert(it.key(), it->toInt());
            m_timingRun = d.value(QStringLiteral("timing")).toObject().value(QStringLiteral("run")).toInt();
            return;
        }
        if (type == QLatin1String("live")) {
            baseline(d);
            return;
        }
    }

    if (type == QLatin1String("clock")) {
        clock(d, msg.value(QStringLiteral("_rx")).toInteger(), where);
        return;
    }
    if (type == QLatin1String("error")) {
        error(where, QStringLiteral("the server did not understand the recorder: %1").arg(d.value(QStringLiteral("message")).toString()));
        return;
    }
    if (type == QLatin1String("hello")) {
        error(where, QStringLiteral("hello in the middle of a connection"));
        return;
    }
    if (type == QLatin1String("status")) {
        if (m_phase != 0)
            flush();
        const int run = d.value(QStringLiteral("timing")).toObject().value(QStringLiteral("run")).toInt();
        if (run != m_timingRun) {
            m_timingRun = run;
            m_liveKnown = false; // another run: the next live is the new baseline
        }
        return;
    }
    if (type == QLatin1String("resource") && m_phase == 0) {
        // answer to the subscription of the recorder
        const QString name = d.value(QStringLiteral("name")).toString();
        if (d.value(QStringLiteral("status")).toInt() == 200) {
            m_content.insert(name, d.value(QStringLiteral("content")).toObject());
            m_contentRev.insert(name, d.value(QStringLiteral("rev")).toInt());
        }
        return;
    }
    // a change: competitor* live? changed resource*
    const int phase = type == QLatin1String("competitor") ? 1 : type == QLatin1String("live") ? 2 : 3;
    if ((phase < m_phase || type == QLatin1String("changed")) && m_phase == 3)
        flush(); // a new change
    else if (phase < m_phase)
        error(where, QStringLiteral("out of order: %1 after %2").arg(type, m_phase == 2 ? QStringLiteral("live") : QStringLiteral("changed")));
    m_phase = qMax(m_phase, phase);
    m_batch << qMakePair(msg, msg.value(QStringLiteral("_rx")).toInteger());
}

void Checker::flush()
{
    if (m_batch.isEmpty()) {
        m_phase = 0;
        return;
    }
    const auto batch = m_batch;
    m_batch.clear();
    m_phase = 0;

    QJsonObject changed;
    QSet<QString> pushed;
    QHash<QString, QJsonObject> before; // run rankings before this change
    for (auto it = m_content.cbegin(); it != m_content.cend(); ++it)
        if (it.key().startsWith(QLatin1String("run/")) && it.key().endsWith(QLatin1String("/results")) && !m_stale.contains(it.key()))
            before.insert(it.key(), it.value());
    for (const auto& p : batch) {
        const QJsonObject& msg = p.first;
        const QString type = msg.value(QStringLiteral("type")).toString();
        const QString where = QStringLiteral("conn %1 seq %2 %3").arg(m_conn).arg(msg.value(QStringLiteral("seq")).toInteger()).arg(type);
        const QJsonObject d = msg.value(QStringLiteral("data")).toObject();
        if (type == QLatin1String("changed")) {
            changed = d.value(QStringLiteral("resources")).toObject();
            for (auto it = changed.begin(); it != changed.end(); ++it) {
                ++m_rep.checks;
                if (m_rev.contains(it.key()) && it->toInt() <= m_rev.value(it.key()))
                    error(where, QStringLiteral("revision of %1 went from %2 to %3").arg(it.key()).arg(m_rev.value(it.key())).arg(it->toInt()));
                m_rev.insert(it.key(), it->toInt());
            }
        } else if (type == QLatin1String("resource")) {
            const QString name = d.value(QStringLiteral("name")).toString();
            pushed.insert(name);
            expectEq(where, QStringLiteral("rev of %1").arg(name), d.value(QStringLiteral("rev")).toInt(), m_rev.value(baseOf(name)));
            if (d.value(QStringLiteral("status")).toInt() == 200) {
                m_content.insert(name, d.value(QStringLiteral("content")).toObject());
                m_contentRev.insert(name, d.value(QStringLiteral("rev")).toInt());
            }
        }
    }
    // subscribed resources whose base changed without a push are not current anymore
    QSet<QString> stale;
    for (auto it = m_content.cbegin(); it != m_content.cend(); ++it)
        if (it.key() != QLatin1String("live") && changed.contains(baseOf(it.key())) && !pushed.contains(it.key()))
            stale.insert(it.key());
    for (const QString& s : stale)
        warning(QStringLiteral("conn %1").arg(m_conn), QStringLiteral("%1 changed but was not pushed").arg(s));
    m_stale += stale;
    m_stale -= pushed;

    bool sawLive = false;
    for (const auto& p : batch) {
        const QJsonObject& msg = p.first;
        const QString type = msg.value(QStringLiteral("type")).toString();
        const QString where = QStringLiteral("conn %1 seq %2 %3").arg(m_conn).arg(msg.value(QStringLiteral("seq")).toInteger()).arg(type);
        const QJsonObject d = msg.value(QStringLiteral("data")).toObject();
        if (type == QLatin1String("competitor")) {
            competitor(d, where, stale);
        } else if (type == QLatin1String("live")) {
            sawLive = true;
            if (changed.contains(QStringLiteral("live")))
                expectEq(where, QStringLiteral("rev"), d.value(QStringLiteral("rev")).toInt(), changed.value(QStringLiteral("live")).toInt());
            else
                error(where, QStringLiteral("live sent without live in changed"));
            live(d, where);
        }
    }
    // every change of the own result of a competitor (status, time, penalty, intermediates,
    // reason) comes with a competitor event - also outside the timing window
    QSet<qint64> withEvent;
    QString firstWhere;
    for (const auto& p : batch) {
        const QJsonObject& msg = p.first;
        if (firstWhere.isEmpty())
            firstWhere = QStringLiteral("conn %1 seq %2").arg(m_conn).arg(msg.value(QStringLiteral("seq")).toInteger());
        if (msg.value(QStringLiteral("type")).toString() == QLatin1String("competitor")) {
            const QJsonObject d = msg.value(QStringLiteral("data")).toObject();
            withEvent.insert(qint64(d.value(QStringLiteral("run")).toInt()) * 10000 + d.value(QStringLiteral("bib")).toInt());
        }
    }
    for (const QString& name : pushed) {
        if (!before.contains(name))
            continue;
        const int run = name.split(QLatin1Char('/')).value(1).toInt();
        const QHash<int, QString> a = ownResults(before.value(name)), b = ownResults(m_content.value(name));
        QSet<int> bibs;
        for (auto it = a.cbegin(); it != a.cend(); ++it)
            bibs.insert(it.key());
        for (auto it = b.cbegin(); it != b.cend(); ++it)
            bibs.insert(it.key());
        for (int bib : bibs) {
            if (a.value(bib) == b.value(bib))
                continue;
            ++m_rep.checks;
            if (!withEvent.contains(qint64(run) * 10000 + bib))
                error(firstWhere, QStringLiteral("result of bib %1 in %2 changed (%3 -> %4) without a competitor event")
                                      .arg(bib).arg(name, a.value(bib, QStringLiteral("none")), b.value(bib, QStringLiteral("none"))));
        }
    }

    if (changed.contains(QStringLiteral("live")) && !sawLive)
        error(QStringLiteral("conn %1").arg(m_conn), QStringLiteral("live changed (rev %1) but no live message").arg(changed.value(QStringLiteral("live")).toInt()));
}

void Checker::competitor(const QJsonObject& d, const QString& whereIn, const QSet<QString>& stale)
{
    const int run = d.value(QStringLiteral("run")).toInt();
    const int bib = d.value(QStringLiteral("bib")).toInt();
    const QString change = d.value(QStringLiteral("change")).toString();
    const QString state = d.value(QStringLiteral("state")).toString();
    const QString status = d.value(QStringLiteral("status")).toString();
    const int lastPoint = d.value(QStringLiteral("lastPoint")).toInt();
    const QJsonObject thisRun = d.value(QStringLiteral("thisRun")).toObject();
    const QString where = QStringLiteral("%1 run %2 bib %3 %4").arg(whereIn).arg(run).arg(bib).arg(change);
    const bool forerunner = d.value(QStringLiteral("competitor")).toObject().value(QStringLiteral("forerunner")).toBool();
    m_changes[change]++;
    m_bibsSeen.insert(qint64(run) * 10000 + bib);

    expectEq(where, QStringLiteral("competitor.bib"), d.value(QStringLiteral("competitor")).toObject().value(QStringLiteral("bib")).toInt(), bib);

    // the change against the new state
    ++m_rep.checks;
    bool fits = true;
    if (change == QLatin1String("started"))
        fits = state == QLatin1String("onCourse");
    else if (change == QLatin1String("intermediate"))
        fits = state == QLatin1String("onCourse") && d.value(QStringLiteral("point")).toInt() == lastPoint && lastPoint >= 1;
    else if (change == QLatin1String("finished"))
        fits = state == QLatin1String("finished");
    else if (change == QLatin1String("status"))
        fits = isOut(state);
    else if (change != QLatin1String("corrected") && change != QLatin1String("reverted"))
        warning(where, QStringLiteral("unknown change"));
    if (!fits)
        error(where, QStringLiteral("change does not fit the state %1 (lastPoint %2)").arg(state).arg(lastPoint));

    // the change against the previous state of the competitor
    const qint64 key = qint64(run) * 10000 + bib;
    const bool known = m_bibs.contains(key);
    const BibState prev = m_bibs.value(key);
    if (known) {
        const QString ps = prev.state;
        bool ok = true;
        if (change == QLatin1String("started"))
            ok = ps == QLatin1String("notStarted");
        else if (change == QLatin1String("intermediate"))
            ok = ps == QLatin1String("onCourse") && lastPoint > prev.lastPoint;
        else if (change == QLatin1String("finished"))
            ok = ps == QLatin1String("notStarted") || ps == QLatin1String("onCourse");
        else if (change == QLatin1String("status"))
            ok = ps != state;
        else if (change == QLatin1String("reverted"))
            ok = orderOf(state) < orderOf(ps) || (isOut(ps) && !isOut(state)) || (state == ps && lastPoint < prev.lastPoint);
        else if (change == QLatin1String("corrected"))
            ok = state == ps || (isOut(state) && isOut(ps));
        ++m_rep.checks;
        if (!ok)
            error(where, QStringLiteral("not possible after the state %1 (lastPoint %2) -> %3 (lastPoint %4)")
                             .arg(ps).arg(prev.lastPoint).arg(state).arg(lastPoint));
    }

    // the state itself
    ++m_rep.checks;
    if (isOut(state) && status != state.toUpper())
        error(where, QStringLiteral("state %1 with status %2").arg(state, str(status)));
    if ((state == QLatin1String("onCourse") || state == QLatin1String("notStarted")) && !d.value(QStringLiteral("status")).isNull())
        error(where, QStringLiteral("state %1 with status %2").arg(state, str(status)));
    if (state == QLatin1String("finished") && !status.isNull() && status != QLatin1String("QLF"))
        error(where, QStringLiteral("state finished with status %1").arg(status));
    const bool qlf = state == QLatin1String("finished") && status == QLatin1String("QLF");
    ++m_rep.checks;
    if (qlf != !thisRun.isEmpty())
        error(where, QStringLiteral("thisRun %1 for state %2 / status %3").arg(thisRun.isEmpty() ? QStringLiteral("missing") : QStringLiteral("present"), state, str(status)));
    if (state == QLatin1String("onCourse") && d.value(QStringLiteral("startDayMs")).isNull())
        warning(where, QStringLiteral("on course without a start time"));
    const QJsonArray inter = d.value(QStringLiteral("inter")).toArray();
    const qint64 startDay = ms(d, "startDayMs");
    for (const auto& v : inter) {
        const QJsonObject io = v.toObject();
        const int p = io.value(QStringLiteral("point")).toInt();
        ++m_rep.checks;
        if (p < 1 || p > 3)
            error(where, QStringLiteral("intermediate point %1").arg(p));
        // the intermediate time = passage - start, truncated
        const qint64 day = ms(io, "dayMs");
        if (day >= 0 && startDay >= 0) {
            const qint64 unit = unitFor(m_decimals);
            const qint64 expected = (day - startDay) / unit * unit;
            const qint64 actual = ms(io, "timeMs");
            ++m_rep.checks;
            if (actual != expected) {
                if (qAbs(actual - expected) <= unit)
                    warning(where, QStringLiteral("inter %1: %2 ms, passage - start gives %3 ms (rounding of the day times)").arg(p).arg(actual).arg(expected));
                else
                    error(where, QStringLiteral("inter %1: %2 ms, passage - start gives %3 ms").arg(p).arg(actual).arg(expected));
            }
        }
    }

    // the time = finish - start (+ penalty), truncated
    if (qlf) {
        const qint64 start = ms(d, "startDayMs"), finish = ms(d, "finishDayMs");
        if (start >= 0 && finish >= 0) {
            const qint64 unit = unitFor(m_decimals);
            const qint64 expected = (finish - start + ms(thisRun, "penaltyMs")) / unit * unit;
            const qint64 actual = ms(thisRun, "timeMs");
            ++m_rep.checks;
            if (actual != expected) {
                // the day times are cut to ms, the time is computed from microseconds
                if (qAbs(actual - expected) <= unit)
                    warning(where, QStringLiteral("time %1 ms, finish - start gives %2 ms (rounding of the day times)").arg(actual).arg(expected));
                else
                    error(where, QStringLiteral("time %1 ms, finish - start gives %2 ms").arg(actual).arg(expected));
            }
        }
        ++m_rep.checks;
        if (!forerunner && thisRun.value(QStringLiteral("rank")).isNull())
            error(where, QStringLiteral("classified without a rank"));
    }

    // against the rankings pushed in the same change
    const QString runRes = QStringLiteral("run/%1/results").arg(run);
    if (m_content.contains(runRes) && !stale.contains(runRes)) {
        const QJsonObject ranking = m_content.value(runRes);
        const QJsonObject row = rowOf(ranking, bib);
        if (qlf && !forerunner) {
            ++m_rep.checks;
            if (row.isEmpty()) {
                error(where, QStringLiteral("classified, but not in the classified of %1 (rev %2)").arg(runRes).arg(m_contentRev.value(runRes)));
            } else {
                compareRow(where, QStringLiteral("thisRun"), thisRun, runRes, bib, stale, true);
                const QJsonArray rowInter = row.value(QStringLiteral("inter")).toArray();
                for (const auto& v : inter) {
                    const QJsonObject io = v.toObject();
                    const int p = io.value(QStringLiteral("point")).toInt();
                    expectEq(where, QStringLiteral("inter %1 timeMs (vs %2)").arg(p).arg(runRes), ms(io, "timeMs"),
                             ms(rowInter.at(p - 1).toObject(), "timeMs"));
                }
            }
        } else {
            ++m_rep.checks;
            if (!row.isEmpty())
                error(where, QStringLiteral("state %1 / status %2, but classified in %3 (rank %4)")
                                 .arg(state, str(status), runRes).arg(row.value(QStringLiteral("rank")).toInt()));
            if (isOut(state))
                expectEq(where, QStringLiteral("group in %1").arg(runRes), unclassifiedKind(ranking, bib, run), state.toUpper());
        }
    }
    if (!forerunner) {
        compareRow(where, QStringLiteral("total"), d.value(QStringLiteral("total")).toObject(), QStringLiteral("results?upto=%1").arg(run), bib, stale, false);
        if (run > 1)
            compareRow(where, QStringLiteral("previous"), d.value(QStringLiteral("previous")).toObject(),
                       QStringLiteral("results?upto=%1").arg(run - 1), bib, stale, false);
    }

    BibState s;
    s.state = state;
    s.lastPoint = lastPoint;
    s.last = d;
    m_bibs.insert(key, s);
}

void Checker::compareRow(const QString& where, const QString& what, const QJsonObject& event, const QString& resource,
                         int bib, const QSet<QString>& stale, bool withPenalty)
{
    if (!m_content.contains(resource) || stale.contains(resource))
        return;
    const QJsonObject row = rowOf(m_content.value(resource), bib);
    ++m_rep.checks;
    if (row.isEmpty() != event.isEmpty()) {
        error(where, QStringLiteral("%1 is %2, but the competitor is %3 in %4 (rev %5)")
                         .arg(what, event.isEmpty() ? QStringLiteral("null") : QStringLiteral("set"),
                              row.isEmpty() ? QStringLiteral("not classified") : QStringLiteral("classified"), resource)
                         .arg(m_contentRev.value(resource)));
        return;
    }
    if (row.isEmpty())
        return;
    const QString w = QStringLiteral("%1 %2 vs %3").arg(where, what, resource);
    expectEq(w, QStringLiteral("timeMs"), ms(event, "timeMs"), ms(row, "timeMs"));
    expectEq(w, QStringLiteral("rank"), event.value(QStringLiteral("rank")).toInt(), row.value(QStringLiteral("rank")).toInt());
    expectEq(w, QStringLiteral("gapMs"), ms(event, "gapMs"), ms(row, "gapMs"));
    if (withPenalty)
        expectEq(w, QStringLiteral("penaltyMs"), ms(event, "penaltyMs"), ms(row, "penaltyMs"));
}

void Checker::baseline(const QJsonObject& d)
{
    const int run = d.value(QStringLiteral("run")).toInt();
    m_content.insert(QStringLiteral("live"), d);
    m_contentRev.insert(QStringLiteral("live"), d.value(QStringLiteral("rev")).toInt());
    m_liveKnown = run > 0;
    if (run <= 0)
        return;
    // what the snapshot says replaces what was known (reconnection)
    for (auto it = m_bibs.begin(); it != m_bibs.end();)
        it = it.key() / 10000 == run ? m_bibs.erase(it) : std::next(it);
    for (const auto& v : d.value(QStringLiteral("onCourse")).toArray()) {
        BibState s;
        s.state = QStringLiteral("onCourse");
        s.lastPoint = v.toObject().value(QStringLiteral("lastPoint")).toInt();
        m_bibs.insert(qint64(run) * 10000 + v.toObject().value(QStringLiteral("bib")).toInt(), s);
    }
    for (const auto& v : d.value(QStringLiteral("onStart")).toArray()) {
        BibState s;
        s.state = QStringLiteral("notStarted");
        m_bibs.insert(qint64(run) * 10000 + v.toObject().value(QStringLiteral("bib")).toInt(), s);
    }
    for (const auto& v : d.value(QStringLiteral("finished")).toArray()) {
        BibState s;
        s.state = v.toObject().value(QStringLiteral("state")).toString();
        s.lastPoint = v.toObject().value(QStringLiteral("lastPoint")).toInt();
        s.last = v.toObject();
        m_bibs.insert(qint64(run) * 10000 + v.toObject().value(QStringLiteral("bib")).toInt(), s);
    }
}

void Checker::live(const QJsonObject& d, const QString& where)
{
    const int run = d.value(QStringLiteral("run")).toInt();
    m_content.insert(QStringLiteral("live"), d);
    m_contentRev.insert(QStringLiteral("live"), d.value(QStringLiteral("rev")).toInt());
    if (!m_liveKnown || run <= 0) {
        baseline(d);
        return;
    }
    if (m_timingRun > 0)
        expectEq(where, QStringLiteral("run"), run, m_timingRun);

    QSet<int> onCourse;
    qint64 lastStart = -1;
    for (const auto& v : d.value(QStringLiteral("onCourse")).toArray()) {
        const QJsonObject o = v.toObject();
        onCourse.insert(o.value(QStringLiteral("bib")).toInt());
        const qint64 s = ms(o, "startDayMs");
        ++m_rep.checks;
        if (s >= 0 && s < lastStart)
            error(where, QStringLiteral("onCourse not in start order at bib %1").arg(o.value(QStringLiteral("bib")).toInt()));
        lastStart = qMax(lastStart, s);
    }
    QSet<int> expected;
    for (auto it = m_bibs.cbegin(); it != m_bibs.cend(); ++it)
        if (it.key() / 10000 == run && it->state == QLatin1String("onCourse"))
            expected.insert(int(it.key() % 10000));
    ++m_rep.checks;
    if (onCourse != expected) {
        QList<int> a(onCourse.cbegin(), onCourse.cend()), b(expected.cbegin(), expected.cend());
        std::sort(a.begin(), a.end());
        std::sort(b.begin(), b.end());
        QStringList sa, sb;
        for (int x : a)
            sa << QString::number(x);
        for (int x : b)
            sb << QString::number(x);
        error(where, QStringLiteral("onCourse [%1], the competitor events give [%2]").arg(sa.join(QLatin1Char(',')), sb.join(QLatin1Char(','))));
    }
    const QJsonValue next = d.value(QStringLiteral("nextToFinish"));
    ++m_rep.checks;
    if (onCourse.isEmpty() != next.isNull() || (!next.isNull() && !onCourse.contains(next.toInt())))
        error(where, QStringLiteral("nextToFinish %1 with onCourse of %2").arg(next.isNull() ? QStringLiteral("null") : QString::number(next.toInt())).arg(onCourse.size()));
    for (const auto& v : d.value(QStringLiteral("onStart")).toArray()) {
        const int bib = v.toObject().value(QStringLiteral("bib")).toInt();
        const QString st = m_bibs.value(qint64(run) * 10000 + bib).state;
        ++m_rep.checks;
        if (!st.isEmpty() && st != QLatin1String("notStarted"))
            error(where, QStringLiteral("bib %1 on the start, but its state is %2").arg(bib).arg(st));
    }
    qint64 lastFinish = LLONG_MAX;
    for (const auto& v : d.value(QStringLiteral("finished")).toArray()) {
        const QJsonObject o = v.toObject();
        const int bib = o.value(QStringLiteral("bib")).toInt();
        const qint64 f = ms(o, "finishDayMs");
        ++m_rep.checks;
        if (f >= 0 && f > lastFinish)
            error(where, QStringLiteral("finished not newest first at bib %1").arg(bib));
        if (f >= 0)
            lastFinish = f;
        const BibState s = m_bibs.value(qint64(run) * 10000 + bib);
        if (s.last.isEmpty())
            continue;
        expectEq(where, QStringLiteral("finished bib %1 state").arg(bib), o.value(QStringLiteral("state")).toString(), s.state);
        expectEq(where, QStringLiteral("finished bib %1 thisRun.timeMs").arg(bib), ms(o.value(QStringLiteral("thisRun")).toObject(), "timeMs"),
                 ms(s.last.value(QStringLiteral("thisRun")).toObject(), "timeMs"));
    }
}

void Checker::clock(const QJsonObject& d, qint64 rx, const QString& where)
{
    const qint64 day = d.value(QStringLiteral("dayMs")).toInteger();
    const bool synced = d.value(QStringLiteral("synced")).toBool();
    ++m_rep.checks;
    if (!d.value(QStringLiteral("utcOffsetMin")).isDouble() || qAbs(d.value(QStringLiteral("utcOffsetMin")).toInt()) > 14 * 60)
        error(where, QStringLiteral("utcOffsetMin missing or out of range"));
    ++m_rep.checks;
    if (synced && m_lastSynced && m_lastDay >= 0 && day < m_lastDay)
        error(where, QStringLiteral("clock went back from %1 to %2").arg(m_lastDay).arg(day));
    if (synced) {
        const qint64 msOfDay = QDateTime::fromMSecsSinceEpoch(rx).time().msecsSinceStartOfDay();
        const qint64 off = msOfDay - day % 86400000;
        if (m_offSet && qAbs(off - (m_offMin + m_offMax) / 2) > 43200000)
            return; // around midnight
        if (!m_offSet) {
            m_offMin = m_offMax = off;
            m_offSet = true;
        }
        m_offMin = qMin(m_offMin, off);
        m_offMax = qMax(m_offMax, off);
    }
    m_lastDay = day;
    m_lastSynced = synced;
}

void Checker::finalCompare()
{
    // the last pushed content against REST now
    int st = 0;
    const QJsonObject status = m_fetch(QStringLiteral("status"), &st);
    if (st != 200) {
        warning(QStringLiteral("REST"), QStringLiteral("OpenSkiPro2 not reachable (%1): the final comparison is skipped").arg(st));
        return;
    }
    if (!m_instance.isEmpty() && status.value(QStringLiteral("instance")).toString() != m_instance) {
        warning(QStringLiteral("REST"), QStringLiteral("OpenSkiPro2 was restarted after the recording: the final comparison is skipped"));
        return;
    }
    int compared = 0, later = 0;
    for (auto it = m_content.cbegin(); it != m_content.cend(); ++it) {
        int s = 0;
        QJsonObject rest = m_fetch(it.key(), &s);
        if (s != 200) {
            warning(QStringLiteral("REST ") + it.key(), QStringLiteral("HTTP %1").arg(s));
            continue;
        }
        const int restRev = rest.value(QStringLiteral("rev")).toInt();
        const int rev = m_contentRev.value(it.key());
        ++m_rep.checks;
        if (restRev < rev) {
            error(QStringLiteral("REST ") + it.key(), QStringLiteral("rev %1 is lower than the pushed rev %2").arg(restRev).arg(rev));
        } else if (restRev > rev) {
            ++later;
        } else {
            ++compared;
            QJsonObject pushed = it.value();
            pushed.insert(QStringLiteral("rev"), rev);
            rest.insert(QStringLiteral("rev"), rev);
            if (pushed != rest)
                error(QStringLiteral("REST ") + it.key(), QStringLiteral("same rev %1, but the content differs from the pushed one").arg(rev));
        }
    }
    m_rep.stats << QStringLiteral("final comparison with REST: %1 resources equal, %2 changed after the recording").arg(compared).arg(later);
}

} // namespace

Report check(const QList<Line>& lines, const Fetch& fetch)
{
    Report r;
    Checker c(r, fetch);
    c.run(lines);
    return r;
}

} // namespace ApiCheck
