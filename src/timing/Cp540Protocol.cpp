#include "Cp540Protocol.h"

#include <QDate>
#include <QRegularExpression>

namespace Cp540Protocol {

quint16 checksum(const QByteArray& data)
{
    quint16 sum = 0;
    for (char c : data)
        if (c != '#')
            sum = quint16(sum + quint8(c));
    return sum;
}

static bool isHex4(const QByteArray& s)
{
    if (s.size() != 4)
        return false;
    for (char c : s)
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f')))
            return false;
    return true;
}

// Field helper: 0-based substring, trimmed
static QByteArray field(const QByteArray& s, int pos, int len)
{
    if (pos >= s.size())
        return QByteArray();
    return s.mid(pos, len).trimmed();
}

static int toInt(const QByteArray& s, bool* ok = nullptr)
{
    bool o = false;
    const int v = s.trimmed().toInt(&o);
    if (ok)
        *ok = o;
    return o ? v : 0;
}

// "HH:MM:SS.FFFFF" with optional blank hours/minutes -> us
static qint64 parseTime(const QByteArray& textIn)
{
    const QByteArray text = textIn.trimmed();
    if (text.isEmpty())
        return -1;
    static const QRegularExpression re(QStringLiteral("^(?:(?:(\\d{1,2}):)?(\\d{1,2}):)?(\\d{1,2})(?:\\.(\\d{1,6}))?$"));
    const auto m = re.match(QString::fromLatin1(text));
    if (!m.hasMatch())
        return -1;
    const qint64 h = m.captured(1).toLongLong();
    const qint64 mi = m.captured(2).toLongLong();
    const qint64 s = m.captured(3).toLongLong();
    QString f = m.captured(4);
    while (f.size() < 6)
        f += QLatin1Char('0');
    return (h * 3600 + mi * 60 + s) * 1000000LL + f.toLongLong();
}

Cp540Message parse(const QByteArray& lineIn)
{
    Cp540Message msg;
    QByteArray line = lineIn;
    // strip extended frame header: STX NB PROT SRC DEST [Param] 0x04
    if (!line.isEmpty() && line.at(0) == '\x02') {
        const int sep = line.indexOf('\x04');
        if (sep >= 0)
            line = line.mid(sep + 1);
    }
    // drop leading control characters (heartbeat 0x01, ACK, NUL ...)
    int start = 0;
    while (start < line.size() && quint8(line.at(start)) < 0x20 && line.at(start) != '\t')
        ++start;
    line = line.mid(start);
    while (!line.isEmpty() && (line.endsWith('\r') || line.endsWith('\n')))
        line.chop(1);
    msg.raw = QString::fromLatin1(line);

    // checksum separation
    QByteArray data = line;
    const int tab = line.lastIndexOf('\t');
    if (tab >= 0) {
        data = line.left(tab);
        const QByteArray cs = line.mid(tab + 1).trimmed();
        if (isHex4(cs)) {
            msg.hasChecksum = true;
            msg.checksumOk = checksum(data) == quint16(cs.toUInt(nullptr, 16));
        }
    } else {
        // Logs sometimes contain the TAB converted to spaces. The trailing
        // token is taken as checksum only when it verifies.
        static const QRegularExpression trailing(QStringLiteral("^(.*?)\\s+([0-9A-Fa-f]{4})\\s*$"));
        const auto m = trailing.match(QString::fromLatin1(line));
        if (m.hasMatch()) {
            const QByteArray candidate = m.captured(1).toLatin1();
            const quint16 cs = quint16(m.captured(2).toUInt(nullptr, 16));
            if (checksum(candidate) == cs) {
                data = candidate;
                msg.hasChecksum = true;
                msg.checksumOk = true;
            }
        }
    }
    while (data.endsWith(' '))
        data.chop(1);

    if (data.size() < 2)
        return msg;
    msg.id = QString::fromLatin1(data.left(2));
    const char c0 = data.at(0);
    const char c1 = data.at(1);

    if ((c0 == 'T' || c0 == 'A' || c0 == '!') && QByteArray("N-*+=C ").contains(c1) && data.size() >= 30) {
        msg.kind = c0 == 'T' ? Cp540Message::Kind::Time
            : c0 == 'A'      ? Cp540Message::Kind::Answer
                             : Cp540Message::Kind::Download;
        switch (c1) {
        case '-': msg.timeType = Cp540Message::TimeType::IdRemoved; break;
        case '*': msg.timeType = Cp540Message::TimeType::NewId; break;
        case '+': msg.timeType = Cp540Message::TimeType::Inserted; break;
        case '=': msg.timeType = Cp540Message::TimeType::Duplicated; break;
        case 'C': msg.timeType = Cp540Message::TimeType::Cancelled; break;
        default: msg.timeType = Cp540Message::TimeType::New; break;
        }
        msg.bib = toInt(field(data, 3, 4));
        msg.seq = toInt(field(data, 8, 4));
        if (data.size() > 13 && data.at(13) == 'M') {
            msg.manual = true;
            msg.channel = toInt(field(data, 14, 1));
        } else {
            msg.channel = toInt(field(data, 13, 2));
        }
        msg.dayTimeUs = parseTime(field(data, 16, 14));
        bool ok = false;
        const int day = toInt(field(data, 31, 5), &ok);
        msg.day = ok ? day : -1;
        if (msg.dayTimeUs < 0)
            msg.kind = Cp540Message::Kind::Unknown;
        return msg;
    }

    const QByteArray id = data.left(2);
    if (id == "RR" || id == "GR") {
        msg.kind = id == "RR" ? Cp540Message::Kind::RunResult : Cp540Message::Kind::GeneralResult;
        msg.rank = toInt(field(data, 3, 4));
        msg.bib = toInt(field(data, 8, 4));
        msg.dayTimeUs = parseTime(field(data, 12, 18));
        return msg;
    }
    if (id == "IR") {
        msg.kind = Cp540Message::Kind::InterResult;
        msg.inter = toInt(field(data, 3, 1));
        msg.bib = toInt(field(data, 8, 4));
        msg.dayTimeUs = parseTime(field(data, 12, 18));
        return msg;
    }
    if (id == "VE") {
        msg.kind = Cp540Message::Kind::Speed;
        msg.inter = toInt(field(data, 3, 1));
        msg.bib = toInt(field(data, 5, 4));
        msg.text = QString::fromLatin1(data.mid(10)).trimmed();
        return msg;
    }
    if (id == "OP" || id == "DS") {
        msg.kind = id == "OP" ? Cp540Message::Kind::OpenRun : Cp540Message::Kind::DownloadStart;
        msg.run = toInt(field(data, 3, 2));
        msg.text = QString::fromLatin1(data.mid(10)).trimmed();
        return msg;
    }
    if (id == "CL" || id == "DE") {
        msg.kind = id == "CL" ? Cp540Message::Kind::CloseRun : Cp540Message::Kind::DownloadEnd;
        msg.run = toInt(field(data, 3, 2));
        return msg;
    }
    if (id == "AK") {
        msg.kind = Cp540Message::Kind::Ack;
        msg.text = QString::fromLatin1(data.mid(3)).trimmed();
        return msg;
    }
    if (id == "SN" || id == "ID") {
        msg.kind = Cp540Message::Kind::SerialNumber;
        msg.text = QString::fromLatin1(data.mid(3)).trimmed();
        return msg;
    }
    if (id == "&P") {
        msg.kind = Cp540Message::Kind::Parameter;
        msg.text = QString::fromLatin1(data.mid(3)).trimmed();
        return msg;
    }
    if (c0 == '&') {
        msg.kind = Cp540Message::Kind::Event;
        msg.text = QString::fromLatin1(data.mid(3)).trimmed();
        return msg;
    }
    return msg;
}

QByteArray frame(const QByteArray& data, bool rs232)
{
    QByteArray out = data;
    if (rs232) {
        out += '\t';
        out += QByteArray::number(checksum(data), 16).toUpper().rightJustified(4, '0');
    }
    out += "\r\n";
    return out;
}

QByteArray recallTime(int seq, int channel)
{
    // SkiPro: "#RT %4d %2d"
    return QStringLiteral("#RT %1 %2").arg(seq, 4).arg(channel, 2).toLatin1();
}

QByteArray downloadRun(int run)
{
    return QStringLiteral("#DL %1").arg(run, 2, 10, QLatin1Char('0')).toLatin1();
}

QByteArray requestSerial()
{
    return QByteArrayLiteral("#SN");
}

QByteArray printLine(const QString& text)
{
    return QByteArrayLiteral("#PL ") + text.left(24).toLatin1();
}

static QByteArray timeText(qint64 us)
{
    if (us < 0)
        us = 0;
    const qint64 sec = us / 1000000;
    const qint64 frac = (us % 1000000) / 10; // 5 digits
    return QStringLiteral("%1:%2:%3.%4")
        .arg((sec / 3600) % 24, 2, 10, QLatin1Char('0'))
        .arg((sec % 3600) / 60, 2, 10, QLatin1Char('0'))
        .arg(sec % 60, 2, 10, QLatin1Char('0'))
        .arg(frac, 5, 10, QLatin1Char('0'))
        .toLatin1();
}

QByteArray formatTime(char type, int bib, int seq, int channel, bool manual, qint64 dayTimeUs, int day)
{
    QByteArray s = "T";
    s += type;
    s += ' ';
    s += bib > 0 ? QByteArray::number(bib).rightJustified(4, ' ') : QByteArray("    ");
    s += ' ';
    s += QByteArray::number(seq).rightJustified(4, ' ');
    s += ' ';
    s += manual ? QByteArray("M") + QByteArray::number(channel) : QByteArray::number(channel).rightJustified(2, ' ');
    s += ' ';
    s += timeText(dayTimeUs);
    s += ' ';
    s += QByteArray::number(day).rightJustified(5, ' ');
    return s;
}

QByteArray formatResult(int rank, int bib, qint64 timeMs)
{
    QByteArray s = "RR ";
    s += QByteArray::number(rank).rightJustified(4, ' ');
    s += ' ';
    s += QByteArray::number(bib).rightJustified(4, ' ');
    const qint64 sec = timeMs / 1000;
    QByteArray t = QByteArray::number(sec) + '.' + QByteArray::number((timeMs % 1000) * 100).rightJustified(5, '0');
    s += t.rightJustified(18, ' ');
    return s;
}

QByteArray formatOpenRun(int run, const QString& mode)
{
    return QStringLiteral("OP %1  00  %2").arg(run, 2, 10, QLatin1Char('0')).arg(mode.left(19)).toLatin1();
}

QByteArray formatCloseRun(int run)
{
    return QStringLiteral("CL %1").arg(run, 2, 10, QLatin1Char('0')).toLatin1();
}

int dayNumber(const QDate& date)
{
    return int(QDate(2000, 1, 1).daysTo(date));
}

} // namespace Cp540Protocol
