#pragma once

#include "Cp540Protocol.h"
#include "TimingDevice.h"

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QPair>
#include <QPointer>

class QSerialPort;
class QTcpSocket;
class QTimer;

// TAG Heuer CP540 driver (THCOM08 over RS232 or Ethernet).
//
// Behaviour taken from SkiPro 2.27 (TSCChronoTAGHEUER_CP540):
//  - the number typed on the CP540 keyboard is used only on channels enabled
//    in the configuration ("use keyboard"), otherwise the software numbers
//  - a gap in the sequential numbers of a channel triggers "#RT ssss cc"
//    recall requests for the missing times
//  - answers (AN, A-, ...) are only used for times not received before
class Cp540Device : public TimingDevice {
    Q_OBJECT
public:
    Cp540Device(int slot, const TimingDeviceConfig& cfg, QObject* parent = nullptr);
    ~Cp540Device() override;

    bool open(QString* error) override;
    void close() override;
    bool isOpen() const override;
    QString description() const override;

    const TimingDeviceConfig& config() const { return m_cfg; }

    // Feed one received line (without CR/LF). Public for tests and log replay.
    void processLine(const QByteArray& line);
    // Sends a command ([Data] only), framing is added according to the link.
    virtual void sendCommand(const QByteArray& data);

protected:
    void emitStatus();

    TimingDeviceConfig m_cfg;
    bool m_downloading = false;

private:
    void onReadyRead();
    void onTcpConnected();
    void onTcpDisconnected();
    void tryReconnect();
    void handleTimeMessage(const Cp540Message& msg);
    void scheduleRecall(int fromSeq, int toSeq, int channel);
    void sendNextRecall();

    QSerialPort* m_serial = nullptr;
    QTcpSocket* m_tcp = nullptr;
    QTimer* m_reconnect = nullptr;
    QTimer* m_recallTimer = nullptr;
    bool m_wantOpen = false;
    QByteArray m_buffer;
    QHash<int, int> m_lastSeq;               // channel -> last sequential number
    QList<QPair<int, int>> m_recallQueue;    // (seq, channel)
};
