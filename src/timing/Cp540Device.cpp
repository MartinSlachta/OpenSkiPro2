#include "Cp540Device.h"

#include <QCoreApplication>
#include <QSerialPort>
#include <QTcpSocket>
#include <QTimer>

Cp540Device::Cp540Device(int slot, const TimingDeviceConfig& cfg, QObject* parent)
    : TimingDevice(slot, parent)
    , m_cfg(cfg)
{
    m_recallTimer = new QTimer(this);
    m_recallTimer->setInterval(150);
    connect(m_recallTimer, &QTimer::timeout, this, &Cp540Device::sendNextRecall);
}

Cp540Device::~Cp540Device()
{
    // no signals from a half destroyed object (receivers may be destroyed too)
    const QSignalBlocker blocker(this);
    Cp540Device::close();
}

QString Cp540Device::description() const
{
    if (m_cfg.link == TimingDeviceConfig::Link::Tcp)
        return QStringLiteral("TAG Heuer CP 540 - %1:%2").arg(m_cfg.host).arg(m_cfg.tcpPort);
    return QStringLiteral("TAG Heuer CP 540 - %1 (%2 bd)").arg(m_cfg.serialPort).arg(m_cfg.baudRate);
}

bool Cp540Device::open(QString* error)
{
    close();
    m_wantOpen = true;
    m_buffer.clear();
    if (m_cfg.link == TimingDeviceConfig::Link::Serial) {
        m_serial = new QSerialPort(this);
        m_serial->setPortName(m_cfg.serialPort);
        m_serial->setBaudRate(m_cfg.baudRate);
        m_serial->setDataBits(QSerialPort::Data8);
        m_serial->setParity(QSerialPort::NoParity);
        m_serial->setStopBits(QSerialPort::OneStop);
        m_serial->setFlowControl(QSerialPort::NoFlowControl);
        if (!m_serial->open(QIODevice::ReadWrite)) {
            if (error)
                *error = m_serial->errorString();
            delete m_serial;
            m_serial = nullptr;
            m_wantOpen = false;
            emitStatus();
            return false;
        }
        connect(m_serial, &QSerialPort::readyRead, this, &Cp540Device::onReadyRead);
        connect(m_serial, &QSerialPort::errorOccurred, this, [this](QSerialPort::SerialPortError e) {
            if (e == QSerialPort::ResourceError) {
                Q_EMIT deviceMessage(tr("Serial port error: %1").arg(m_serial->errorString()));
                close();
            }
        });
        emitStatus();
        return true;
    }

    m_tcp = new QTcpSocket(this);
    connect(m_tcp, &QTcpSocket::readyRead, this, &Cp540Device::onReadyRead);
    connect(m_tcp, &QTcpSocket::connected, this, &Cp540Device::onTcpConnected);
    connect(m_tcp, &QTcpSocket::disconnected, this, &Cp540Device::onTcpDisconnected);
    connect(m_tcp, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        Q_EMIT deviceMessage(tr("Network error: %1").arg(m_tcp ? m_tcp->errorString() : QString()));
        onTcpDisconnected();
    });
    m_reconnect = new QTimer(this);
    m_reconnect->setInterval(3000);
    m_reconnect->setSingleShot(true);
    connect(m_reconnect, &QTimer::timeout, this, &Cp540Device::tryReconnect);
    m_tcp->connectToHost(m_cfg.host, quint16(m_cfg.tcpPort));
    emitStatus();
    return true; // connection is established asynchronously, with automatic retries
}

void Cp540Device::close()
{
    m_wantOpen = false;
    m_recallTimer->stop();
    m_recallQueue.clear();
    if (m_serial) {
        m_serial->disconnect(this);
        m_serial->close();
        m_serial->deleteLater();
        m_serial = nullptr;
    }
    if (m_reconnect) {
        m_reconnect->stop();
        m_reconnect->deleteLater();
        m_reconnect = nullptr;
    }
    if (m_tcp) {
        m_tcp->disconnect(this);
        m_tcp->abort();
        m_tcp->deleteLater();
        m_tcp = nullptr;
    }
    emitStatus();
}

bool Cp540Device::isOpen() const
{
    if (m_serial)
        return m_serial->isOpen();
    if (m_tcp)
        return m_tcp->state() == QAbstractSocket::ConnectedState;
    return false;
}

void Cp540Device::emitStatus()
{
    Q_EMIT statusChanged(isOpen(), isOpen() ? description() : tr("Not connected"));
}

void Cp540Device::onTcpConnected()
{
    emitStatus();
    sendCommand(Cp540Protocol::requestSerial());
}

void Cp540Device::onTcpDisconnected()
{
    emitStatus();
    if (m_wantOpen && m_reconnect && !m_reconnect->isActive())
        m_reconnect->start();
}

void Cp540Device::tryReconnect()
{
    if (!m_wantOpen || !m_tcp)
        return;
    if (m_tcp->state() == QAbstractSocket::UnconnectedState)
        m_tcp->connectToHost(m_cfg.host, quint16(m_cfg.tcpPort));
}

void Cp540Device::onReadyRead()
{
    QIODevice* dev = m_serial ? static_cast<QIODevice*>(m_serial) : static_cast<QIODevice*>(m_tcp);
    if (!dev)
        return;
    m_buffer += dev->readAll();
    for (;;) {
        int end = m_buffer.indexOf('\n');
        const int cr = m_buffer.indexOf('\r');
        if (end < 0 || (cr >= 0 && cr < end))
            end = cr;
        if (end < 0)
            break;
        const QByteArray line = m_buffer.left(end);
        m_buffer.remove(0, end + 1);
        if (line.trimmed().isEmpty())
            continue;
        processLine(line);
        if (m_cfg.flowControlAck && m_serial)
            m_serial->write("\x06", 1);
    }
    if (m_buffer.size() > 4096)
        m_buffer.clear(); // garbage protection
}

void Cp540Device::processLine(const QByteArray& line)
{
    const Cp540Message msg = Cp540Protocol::parse(line);
    Q_EMIT rawLine(msg.raw, !(msg.hasChecksum && !msg.checksumOk));
    if (msg.hasChecksum && !msg.checksumOk) {
        Q_EMIT deviceMessage(tr("Checksum error: %1").arg(msg.raw));
        return;
    }
    switch (msg.kind) {
    case Cp540Message::Kind::Time:
    case Cp540Message::Kind::Answer:
        handleTimeMessage(msg);
        break;
    case Cp540Message::Kind::OpenRun:
        Q_EMIT deviceMessage(tr("CP540: run %1 opened (%2)").arg(msg.run).arg(msg.text));
        m_lastSeq.clear();
        break;
    case Cp540Message::Kind::CloseRun:
        Q_EMIT deviceMessage(tr("CP540: run %1 closed").arg(msg.run));
        break;
    case Cp540Message::Kind::DownloadStart:
        m_downloading = true;
        break;
    case Cp540Message::Kind::DownloadEnd:
        m_downloading = false;
        break;
    case Cp540Message::Kind::SerialNumber:
        Q_EMIT deviceMessage(tr("CP540: %1").arg(msg.text));
        break;
    default:
        break;
    }
}

void Cp540Device::handleTimeMessage(const Cp540Message& msg)
{
    if (msg.channel < 1 || msg.channel > 99)
        return;
    const bool keyboard = m_cfg.keyboardChannels.contains(msg.channel);
    TimingImpulse imp;
    imp.device = m_slot;
    imp.channel = msg.channel;
    imp.manual = msg.manual;
    imp.seq = msg.seq;
    imp.dayTimeUs = msg.dayTimeUs;
    imp.day = msg.day;
    imp.bib = keyboard ? msg.bib : 0;

    if (msg.kind == Cp540Message::Kind::Answer) {
        if (m_downloading)
            return;
        if (msg.timeType == Cp540Message::TimeType::Cancelled)
            return;
        imp.type = TimingImpulse::Type::Recalled;
        Q_EMIT impulse(imp);
        return;
    }

    switch (msg.timeType) {
    case Cp540Message::TimeType::Cancelled:
        // SkiPro: a cancelled time carrying a number typed on the keyboard means DNF
        imp.type = (keyboard && msg.bib > 0) ? TimingImpulse::Type::StatusDNF : TimingImpulse::Type::Cancelled;
        break;
    case Cp540Message::TimeType::IdRemoved:
        if (!keyboard)
            return;
        imp.type = TimingImpulse::Type::IdRemoved;
        break;
    case Cp540Message::TimeType::NewId:
        if (!keyboard)
            return;
        imp.type = TimingImpulse::Type::NewId;
        imp.bib = msg.bib;
        break;
    case Cp540Message::TimeType::New:
    case Cp540Message::TimeType::Inserted:
    case Cp540Message::TimeType::Duplicated:
        imp.type = TimingImpulse::Type::New;
        if (!msg.manual && msg.seq > 0) {
            const auto it = m_lastSeq.constFind(msg.channel);
            if (it != m_lastSeq.constEnd() && msg.seq > *it + 1)
                scheduleRecall(*it + 1, msg.seq - 1, msg.channel);
            if (it == m_lastSeq.constEnd() || msg.seq > *it)
                m_lastSeq.insert(msg.channel, msg.seq);
        }
        break;
    }
    Q_EMIT impulse(imp);
}

void Cp540Device::scheduleRecall(int fromSeq, int toSeq, int channel)
{
    // never flood the device: at most 50 recalls for one gap
    for (int s = fromSeq; s <= toSeq && s < fromSeq + 50; ++s)
        m_recallQueue << qMakePair(s, channel);
    Q_EMIT deviceMessage(tr("Missing times %1-%2 on channel %3, recall requested").arg(fromSeq).arg(toSeq).arg(channel));
    if (!m_recallTimer->isActive())
        m_recallTimer->start();
}

void Cp540Device::sendNextRecall()
{
    if (m_recallQueue.isEmpty()) {
        m_recallTimer->stop();
        return;
    }
    const auto r = m_recallQueue.takeFirst();
    sendCommand(Cp540Protocol::recallTime(r.first, r.second));
}

void Cp540Device::sendCommand(const QByteArray& data)
{
    if (m_serial && m_serial->isOpen())
        m_serial->write(Cp540Protocol::frame(data, true));
    else if (m_tcp && m_tcp->state() == QAbstractSocket::ConnectedState)
        m_tcp->write(Cp540Protocol::frame(data, false));
}
