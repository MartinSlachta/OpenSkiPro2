#include "TimingDevice.h"

QString TimingDeviceConfig::soundFor(int channel) const
{
    for (size_t i = 0; i < soundChannels.size(); ++i)
        if (soundChannels[i] == channel && channel > 0 && !soundFiles[i].isEmpty())
            return soundFiles[i];
    return QString();
}

TimingDevice::TimingDevice(int slot, QObject* parent)
    : QObject(parent)
    , m_slot(slot)
{
}
