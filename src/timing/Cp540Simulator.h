#pragma once

#include "Cp540Device.h"

#include <QHash>
#include <QList>
#include <QStringList>

class QTimer;

// Built-in CP540 emulator for testing without hardware. It produces genuine
// THCOM08 lines which go through the same parser as a real device, answers
// "#RT" recall commands and can replay a recorded log file.
class Cp540Simulator : public Cp540Device {
    Q_OBJECT
public:
    Cp540Simulator(int slot, const TimingDeviceConfig& cfg, QObject* parent = nullptr);

    bool open(QString* error) override;
    void close() override;
    bool isOpen() const override;
    QString description() const override;
    void sendCommand(const QByteArray& data) override;

    // manual impulse on a channel (like pressing the channel key on the CP540)
    void trigger(int channel);
    void setAutoRun(bool on);
    bool autoRun() const;
    // replay a log file (one THCOM08 line per text line)
    bool startReplay(const QString& file, int intervalMs, QString* error);
    void stopReplay();
    // drop the next generated frame (to exercise the recall mechanism)
    void dropNextFrame() { m_dropNext = true; }

private:
    void emitTime(int channel, qint64 dayTimeUs);
    void autoTick();
    void replayTick();
    qint64 nowUs() const;

    bool m_open = false;
    bool m_dropNext = false;
    QTimer* m_autoTimer = nullptr;
    QTimer* m_replayTimer = nullptr;
    QHash<int, int> m_seq;                         // channel -> last seq
    QHash<QPair<int, int>, QByteArray> m_history;  // (seq, channel) -> data, for #RT answers
    struct Pending { qint64 dueUs; int channel; };
    QList<Pending> m_pending;
    QTimer* m_pendingTimer = nullptr;
    QStringList m_replayLines;
    int m_replayPos = 0;
};
