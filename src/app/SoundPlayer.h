#pragma once

#include <QString>

// Plays a WAV file asynchronously (channel sounds in the timing window).
namespace SoundPlayer {
void play(const QString& wavFile);
}
