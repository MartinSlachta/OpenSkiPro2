#include "AppSettings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

namespace AppSettings {

static QString s_iniOverride;

void setIniPathOverride(const QString& path)
{
    s_iniOverride = path;
}

QString iniPath()
{
    if (!s_iniOverride.isEmpty())
        return s_iniOverride;
    const QString portable = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("OpenSkiPro2.ini"));
    if (QFileInfo::exists(portable))
        return portable;
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dir.isEmpty())
        dir = QDir::homePath() + QStringLiteral("/OpenSkiPro2");
    QDir().mkpath(dir);
    return QDir(dir).absoluteFilePath(QStringLiteral("OpenSkiPro2.ini"));
}

static QSettings& settings()
{
    static QSettings s(iniPath(), QSettings::IniFormat);
    return s;
}

QVariant value(const QString& key, const QVariant& def)
{
    return settings().value(key, def);
}

void setValue(const QString& key, const QVariant& v)
{
    settings().setValue(key, v);
    settings().sync();
}

static QString documentsBase()
{
    QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (docs.isEmpty())
        docs = QDir::homePath();
    return QDir(docs).absoluteFilePath(QStringLiteral("OpenSkiPro2"));
}

QString eventsDir()
{
    const QString d = value(QStringLiteral("folders/events"), QDir(documentsBase()).absoluteFilePath(QStringLiteral("Events"))).toString();
    QDir().mkpath(d);
    return d;
}

void setEventsDir(const QString& dir)
{
    setValue(QStringLiteral("folders/events"), dir);
}

QString logosDir()
{
    return value(QStringLiteral("folders/logos"), QDir(documentsBase()).absoluteFilePath(QStringLiteral("Logos"))).toString();
}

void setLogosDir(const QString& dir)
{
    setValue(QStringLiteral("folders/logos"), dir);
}

QString exportDir()
{
    return value(QStringLiteral("folders/export"), QDir(documentsBase()).absoluteFilePath(QStringLiteral("Export"))).toString();
}

void setExportDir(const QString& dir)
{
    setValue(QStringLiteral("folders/export"), dir);
}

QString fisListPath()
{
    return QFileInfo(iniPath()).absoluteDir().absoluteFilePath(QStringLiteral("FisList.db"));
}

QString language()
{
    return value(QStringLiteral("general/language"), QStringLiteral("en")).toString();
}

void setLanguage(const QString& lang)
{
    setValue(QStringLiteral("general/language"), lang);
}

static QString intSet(const QSet<int>& s)
{
    QStringList l;
    for (int i : s)
        l << QString::number(i);
    l.sort();
    return l.join(QLatin1Char(','));
}

TimingDeviceConfig timingDevice(int slot)
{
    QSettings& s = settings();
    s.beginGroup(QStringLiteral("timingdevice%1").arg(slot));
    TimingDeviceConfig c;
    c.type = TimingDeviceConfig::Type(s.value(QStringLiteral("type"), int(c.type)).toInt());
    c.link = TimingDeviceConfig::Link(s.value(QStringLiteral("link"), int(c.link)).toInt());
    c.serialPort = s.value(QStringLiteral("serialPort"), c.serialPort).toString();
    c.baudRate = s.value(QStringLiteral("baudRate"), c.baudRate).toInt();
    c.flowControlAck = s.value(QStringLiteral("flowControlAck"), c.flowControlAck).toBool();
    c.host = s.value(QStringLiteral("host"), c.host).toString();
    c.tcpPort = s.value(QStringLiteral("tcpPort"), c.tcpPort).toInt();
    c.startChannel = s.value(QStringLiteral("startChannel"), c.startChannel).toInt();
    c.finishChannel = s.value(QStringLiteral("finishChannel"), c.finishChannel).toInt();
    for (int i = 0; i < 3; ++i)
        c.interChannels[size_t(i)] = s.value(QStringLiteral("interChannel%1").arg(i + 1), c.interChannels[size_t(i)]).toInt();
    c.keyboardChannels.clear();
    for (const QString& k : s.value(QStringLiteral("keyboardChannels")).toString().split(QLatin1Char(','), Qt::SkipEmptyParts))
        c.keyboardChannels.insert(k.toInt());
    for (int i = 0; i < 4; ++i) {
        c.soundChannels[size_t(i)] = s.value(QStringLiteral("soundChannel%1").arg(i + 1), 0).toInt();
        c.soundFiles[size_t(i)] = s.value(QStringLiteral("soundFile%1").arg(i + 1)).toString();
    }
    c.simStartIntervalMs = s.value(QStringLiteral("simStartInterval"), c.simStartIntervalMs).toInt();
    c.simRunTimeMs = s.value(QStringLiteral("simRunTime"), c.simRunTimeMs).toInt();
    c.simRunSpreadMs = s.value(QStringLiteral("simRunSpread"), c.simRunSpreadMs).toInt();
    c.simReplayFile = s.value(QStringLiteral("simReplayFile")).toString();
    s.endGroup();
    return c;
}

void setTimingDevice(int slot, const TimingDeviceConfig& c)
{
    QSettings& s = settings();
    s.beginGroup(QStringLiteral("timingdevice%1").arg(slot));
    s.setValue(QStringLiteral("type"), int(c.type));
    s.setValue(QStringLiteral("link"), int(c.link));
    s.setValue(QStringLiteral("serialPort"), c.serialPort);
    s.setValue(QStringLiteral("baudRate"), c.baudRate);
    s.setValue(QStringLiteral("flowControlAck"), c.flowControlAck);
    s.setValue(QStringLiteral("host"), c.host);
    s.setValue(QStringLiteral("tcpPort"), c.tcpPort);
    s.setValue(QStringLiteral("startChannel"), c.startChannel);
    s.setValue(QStringLiteral("finishChannel"), c.finishChannel);
    for (int i = 0; i < 3; ++i)
        s.setValue(QStringLiteral("interChannel%1").arg(i + 1), c.interChannels[size_t(i)]);
    s.setValue(QStringLiteral("keyboardChannels"), intSet(c.keyboardChannels));
    for (int i = 0; i < 4; ++i) {
        s.setValue(QStringLiteral("soundChannel%1").arg(i + 1), c.soundChannels[size_t(i)]);
        s.setValue(QStringLiteral("soundFile%1").arg(i + 1), c.soundFiles[size_t(i)]);
    }
    s.setValue(QStringLiteral("simStartInterval"), c.simStartIntervalMs);
    s.setValue(QStringLiteral("simRunTime"), c.simRunTimeMs);
    s.setValue(QStringLiteral("simRunSpread"), c.simRunSpreadMs);
    s.setValue(QStringLiteral("simReplayFile"), c.simReplayFile);
    s.endGroup();
    s.sync();
}

} // namespace AppSettings
