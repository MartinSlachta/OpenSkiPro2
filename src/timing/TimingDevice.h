#pragma once

#include <QMetaType>
#include <QObject>
#include <QSet>
#include <QString>
#include <array>

// A time impulse delivered by a timing device, independent of the protocol.
struct TimingImpulse {
    enum class Type {
        New,        // new time
        Cancelled,  // the device cancelled a time (same device/channel/seq)
        IdRemoved,  // the device removed the competitor number
        NewId,      // the device assigned a new competitor number
        Recalled,   // answer to a recall request (only used when missing)
        StatusDNF,  // the device gives DNF to the competitor `bib` (CP540: TC with a typed number)
    };
    Type type = Type::New;
    int device = 0;       // 1 or 2
    int channel = 0;
    bool manual = false;  // keyboard impulse on the device (M1..M4)
    int bib = 0;          // number sent by the device (0 = none)
    int seq = 0;          // device sequential number
    qint64 dayTimeUs = -1;
    int day = -1;         // device day number (days since 01.01.2000), -1 unknown
};

Q_DECLARE_METATYPE(TimingImpulse)

// Configuration of one timing device slot ("Timing Device 1/2"), stored locally.
struct TimingDeviceConfig {
    enum class Type { None = 0, CP540 = 1, Simulator = 2 };
    enum class Link { Serial = 0, Tcp = 1 };

    Type type = Type::None;
    Link link = Link::Serial;
    QString serialPort;
    int baudRate = 9600;
    bool flowControlAck = false; // "9600bds Flow Ctrl" mode: ACK every frame
    QString host = QStringLiteral("192.168.1.50");
    int tcpPort = 13500;         // 13500-13503 keep unsent data when the connection drops

    // channel mapping
    int startChannel = 1;
    int finishChannel = 4;
    std::array<int, 3> interChannels{ {2, 3, 0} };
    // channels on which the number typed on the device keyboard is used
    QSet<int> keyboardChannels;
    // sounds: (channel, wav file)
    std::array<int, 4> soundChannels{ {0, 0, 0, 0} };
    std::array<QString, 4> soundFiles;

    // simulator
    int simStartIntervalMs = 15000;
    int simRunTimeMs = 45000;
    int simRunSpreadMs = 4000;
    QString simReplayFile;

    QString soundFor(int channel) const;
};

// State of a timing device slot as shown by the timing window (and published by the API).
struct TimingDeviceState {
    int slot = 0;
    TimingDeviceConfig::Type type = TimingDeviceConfig::Type::None;
    TimingDeviceConfig::Link link = TimingDeviceConfig::Link::Serial;
    bool connected = false;
    QString text; // description of the connection (port, address)

    bool operator==(const TimingDeviceState& o) const
    {
        return slot == o.slot && type == o.type && link == o.link && connected == o.connected && text == o.text;
    }
};

// Base class of all timing device drivers.
class TimingDevice : public QObject {
    Q_OBJECT
public:
    explicit TimingDevice(int slot, QObject* parent = nullptr);
    int slot() const { return m_slot; }

    virtual bool open(QString* error) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;
    virtual QString description() const = 0;

Q_SIGNALS:
    void impulse(const TimingImpulse& imp);
    void rawLine(const QString& line, bool valid); // everything received (timing device paper)
    void statusChanged(bool connected, const QString& text);
    void deviceMessage(const QString& text);    // OP / CL / errors ...

protected:
    int m_slot;
};
