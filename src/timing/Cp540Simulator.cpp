#include "Cp540Simulator.h"

#include <QDate>
#include <QFile>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QTime>
#include <QTimer>

Cp540Simulator::Cp540Simulator(int slot, const TimingDeviceConfig& cfg, QObject* parent)
    : Cp540Device(slot, cfg, parent)
{
    m_autoTimer = new QTimer(this);
    connect(m_autoTimer, &QTimer::timeout, this, &Cp540Simulator::autoTick);
    m_replayTimer = new QTimer(this);
    connect(m_replayTimer, &QTimer::timeout, this, &Cp540Simulator::replayTick);
    m_pendingTimer = new QTimer(this);
    m_pendingTimer->setInterval(20);
    connect(m_pendingTimer, &QTimer::timeout, this, [this] {
        const qint64 now = nowUs();
        for (int i = 0; i < m_pending.size();) {
            if (m_pending.at(i).dueUs <= now) {
                const Pending p = m_pending.takeAt(i);
                emitTime(p.channel, p.dueUs);
            } else {
                ++i;
            }
        }
        if (m_pending.isEmpty() && !m_autoTimer->isActive())
            m_pendingTimer->stop();
    });
}

QString Cp540Simulator::description() const
{
    return tr("CP540 simulator");
}

bool Cp540Simulator::open(QString*)
{
    m_open = true;
    emitStatus();
    processLine(Cp540Protocol::frame(Cp540Protocol::formatOpenRun(1, QStringLiteral("NET TIME")), true).trimmed());
    return true;
}

void Cp540Simulator::close()
{
    m_autoTimer->stop();
    m_replayTimer->stop();
    m_pendingTimer->stop();
    m_pending.clear();
    m_open = false;
    emitStatus();
}

bool Cp540Simulator::isOpen() const
{
    return m_open;
}

qint64 Cp540Simulator::nowUs() const
{
    return qint64(QTime::currentTime().msecsSinceStartOfDay()) * 1000;
}

void Cp540Simulator::emitTime(int channel, qint64 dayTimeUs)
{
    if (!m_open || channel <= 0)
        return;
    const int seq = m_seq.value(channel) + 1;
    m_seq.insert(channel, seq);
    const QByteArray data = Cp540Protocol::formatTime('N', 0, seq, channel, false, dayTimeUs,
                                                      Cp540Protocol::dayNumber(QDate::currentDate()));
    m_history.insert(qMakePair(seq, channel), data);
    if (m_dropNext) {
        m_dropNext = false;
        return;
    }
    processLine(Cp540Protocol::frame(data, true).trimmed());
}

void Cp540Simulator::trigger(int channel)
{
    emitTime(channel, nowUs());
}

void Cp540Simulator::setAutoRun(bool on)
{
    if (on) {
        m_autoTimer->start(qMax(1000, m_cfg.simStartIntervalMs));
        m_pendingTimer->start();
        autoTick();
    } else {
        m_autoTimer->stop();
    }
}

bool Cp540Simulator::autoRun() const
{
    return m_autoTimer->isActive();
}

void Cp540Simulator::autoTick()
{
    const qint64 now = nowUs();
    emitTime(m_cfg.startChannel, now);
    const int spread = qMax(0, m_cfg.simRunSpreadMs);
    const qint64 runMs = qMax(1000, m_cfg.simRunTimeMs + (spread > 0 ? int(QRandomGenerator::global()->bounded(2 * spread)) - spread : 0));
    int inters = 0;
    for (int ch : m_cfg.interChannels)
        if (ch > 0)
            ++inters;
    int k = 0;
    for (int ch : m_cfg.interChannels) {
        if (ch <= 0)
            continue;
        ++k;
        m_pending << Pending{ now + runMs * 1000 * k / (inters + 1), ch };
    }
    m_pending << Pending{ now + runMs * 1000, m_cfg.finishChannel };
    m_pendingTimer->start();
}

void Cp540Simulator::sendCommand(const QByteArray& data)
{
    // answer recall requests from the history
    static const QRegularExpression re(QStringLiteral("^#RT\\s*(\\d+)\\s+(\\d+)"));
    const auto m = re.match(QString::fromLatin1(data));
    if (!m.hasMatch())
        return;
    const auto key = qMakePair(m.captured(1).toInt(), m.captured(2).toInt());
    QByteArray rec = m_history.value(key);
    if (rec.isEmpty())
        return;
    rec[0] = 'A'; // TN -> AN
    QTimer::singleShot(50, this, [this, rec] { processLine(Cp540Protocol::frame(rec, true).trimmed()); });
}

bool Cp540Simulator::startReplay(const QString& file, int intervalMs, QString* error)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    m_replayLines.clear();
    for (const QByteArray& l : f.readAll().split('\n')) {
        const QByteArray t = l.trimmed();
        if (!t.isEmpty())
            m_replayLines << QString::fromLatin1(t);
    }
    m_replayPos = 0;
    if (!m_open)
        open(nullptr);
    m_replayTimer->start(qMax(10, intervalMs));
    return true;
}

void Cp540Simulator::stopReplay()
{
    m_replayTimer->stop();
}

void Cp540Simulator::replayTick()
{
    if (m_replayPos >= m_replayLines.size()) {
        m_replayTimer->stop();
        Q_EMIT deviceMessage(tr("Replay finished (%1 lines)").arg(m_replayLines.size()));
        return;
    }
    processLine(m_replayLines.at(m_replayPos++).toLatin1());
}
