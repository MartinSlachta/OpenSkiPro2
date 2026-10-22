#pragma once

#include "timing/TimingDevice.h"

#include <QString>
#include <QVariant>

// Local (per computer) configuration, like SkiPro's Ski.ini:
// folders, language and the two timing devices.
// Stored in OpenSkiPro2.ini next to the executable when that file exists
// (portable mode), otherwise in the user's application data folder.
namespace AppSettings {

QString iniPath();
// Uses another settings file (must be called before any other function, e.g. for tests).
void setIniPathOverride(const QString& path);

QString eventsDir();
void setEventsDir(const QString& dir);
QString logosDir();
void setLogosDir(const QString& dir);
QString exportDir();
void setExportDir(const QString& dir);
// Database of the imported FIS points list (shared by all events), next to the settings file.
QString fisListPath();

QString language();          // "en", ...
void setLanguage(const QString& lang);

TimingDeviceConfig timingDevice(int slot);
void setTimingDevice(int slot, const TimingDeviceConfig& cfg);

QVariant value(const QString& key, const QVariant& def = QVariant());
void setValue(const QString& key, const QVariant& v);

} // namespace AppSettings
