#include "SoundPlayer.h"

#include <QDir>
#include <QFileInfo>

#ifdef Q_OS_WIN
#include <windows.h>
#include <mmsystem.h>
#endif

namespace SoundPlayer {

void play(const QString& wavFile)
{
    if (wavFile.isEmpty() || !QFileInfo::exists(wavFile))
        return;
#ifdef Q_OS_WIN
    const std::wstring path = QDir::toNativeSeparators(wavFile).toStdWString();
    PlaySoundW(path.c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
#endif
}

} // namespace SoundPlayer
