#pragma once

#include <QByteArray>
#include <QDate>
#include <QString>

// TAG Heuer THCOM08 protocol as used by the CP540 chronoprinter.
//
// Basic frame:  RS232:    [Data] TAB CS16 CR LF   (CS16 optional)
//               Ethernet: [Data] CR LF
// Time message: "Tx NNNN SSSS CC HH:MM:SS.FFFFF DDDDD"
//               x = N new, - identification removed, * new identification,
//                   + inserted, = duplicated, C cancelled
//               CC = channel, or "M1".."M4" for a manual (keyboard) impulse
//               DDDDD = days since 01.01.2000
// Field positions follow SkiPro's parser (TSCChronoTAGHEUER_CP540).
struct Cp540Message {
    enum class Kind {
        Unknown,
        Time,          // TN T- T* T+ T= TC
        Answer,        // AN A- A* A+ A= AC (answer to #RT / #DL)
        Download,      // !N ... (device to device download)
        RunResult,     // RR
        GeneralResult, // GR
        InterResult,   // IR
        Speed,         // VE
        OpenRun,       // OP
        CloseRun,      // CL
        DownloadStart, // DS
        DownloadEnd,   // DE
        Ack,           // AK
        SerialNumber,  // SN / ID
        Parameter,     // &P
        Event,         // &S ...
    };
    enum class TimeType { New, IdRemoved, NewId, Inserted, Duplicated, Cancelled };

    Kind kind = Kind::Unknown;
    TimeType timeType = TimeType::New;
    QString id;          // two letter message id, e.g. "TN"
    int bib = 0;         // candidate number (0 = none)
    int seq = 0;         // sequential number
    int channel = 0;     // 1..99
    bool manual = false; // M1..M4 keyboard impulse
    qint64 dayTimeUs = -1;
    int day = -1;        // days since 01.01.2000
    int rank = 0;
    int run = 0;         // OP/CL/DS/DE run number
    int inter = 0;       // IR intermediate number
    QString text;        // OP timing mode name, AK answer, SN data ...
    bool hasChecksum = false;
    bool checksumOk = true;
    QString raw;

    bool isValid() const { return kind != Kind::Unknown; }
    bool isTime() const { return kind == Kind::Time || kind == Kind::Answer || kind == Kind::Download; }
};

namespace Cp540Protocol {

// CS16: sum of all bytes of [Data] except '#'.
quint16 checksum(const QByteArray& data);

// Parses one received line (CR/LF already removed, may contain TAB + checksum).
Cp540Message parse(const QByteArray& line);

// Builds a frame for sending. RS232 frames carry a checksum.
QByteArray frame(const QByteArray& data, bool rs232);

// Commands
QByteArray recallTime(int seq, int channel);    // #RT
QByteArray downloadRun(int run);                // #DL
QByteArray requestSerial();                     // #SN
QByteArray printLine(const QString& text);      // #PL

// Formats a time message ([Data] only), used by the simulator and tests.
QByteArray formatTime(char type, int bib, int seq, int channel, bool manual, qint64 dayTimeUs, int day);
QByteArray formatResult(int rank, int bib, qint64 timeMs);
QByteArray formatOpenRun(int run, const QString& mode);
QByteArray formatCloseRun(int run);

// Days since 01.01.2000 for a date.
int dayNumber(const QDate& date);

} // namespace Cp540Protocol
