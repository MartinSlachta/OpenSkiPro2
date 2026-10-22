#include "TimeUtil.h"

#include <QRegularExpression>
#include <QStringList>

namespace TimeUtil {

int decimalsFor(Precision p)
{
    switch (p) {
    case Precision::Second: return 0;
    case Precision::Tenth: return 1;
    case Precision::Hundredth: return 2;
    case Precision::Thousandth: return 3;
    }
    return 2;
}

static qint64 unitMs(Precision p)
{
    switch (p) {
    case Precision::Second: return 1000;
    case Precision::Tenth: return 100;
    case Precision::Hundredth: return 10;
    case Precision::Thousandth: return 1;
    }
    return 10;
}

qint64 truncateMs(qint64 ms, Precision p)
{
    if (ms < 0)
        return ms;
    const qint64 u = unitMs(p);
    return (ms / u) * u;
}

QString formatResult(qint64 ms, Precision p)
{
    if (ms < 0)
        return QString();
    const qint64 t = truncateMs(ms, p);
    const int dec = decimalsFor(p);
    qint64 totalSec = t / 1000;
    const qint64 frac = (t % 1000) / unitMs(p);
    const qint64 h = totalSec / 3600;
    const qint64 m = (totalSec % 3600) / 60;
    const qint64 s = totalSec % 60;

    QString out;
    if (h > 0)
        out = QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
    else if (m > 0)
        out = QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QChar('0'));
    else
        out = QString::number(s);
    if (dec > 0)
        out += QLatin1Char('.') + QStringLiteral("%1").arg(frac, dec, 10, QChar('0'));
    return out;
}

QString formatGap(qint64 ms, Precision p)
{
    return formatResult(ms, p);
}

qint64 parseResult(const QString& textIn)
{
    QString text = textIn.trimmed();
    if (text.isEmpty())
        return kNoTime;
    text.replace(QLatin1Char(','), QLatin1Char('.'));
    text.replace(QLatin1Char('h'), QLatin1Char(':'));
    const QStringList parts = text.split(QLatin1Char(':'));
    if (parts.size() > 3)
        return kNoTime;
    qint64 total = 0;
    for (int i = 0; i < parts.size(); ++i) {
        const QString& part = parts.at(i);
        const bool last = i == parts.size() - 1;
        if (last) {
            const QStringList sf = part.split(QLatin1Char('.'));
            if (sf.size() > 2)
                return kNoTime;
            bool ok = false;
            const qint64 sec = sf.at(0).isEmpty() ? 0 : sf.at(0).toLongLong(&ok);
            if (!sf.at(0).isEmpty() && !ok)
                return kNoTime;
            qint64 frac = 0;
            if (sf.size() == 2) {
                QString f = sf.at(1).left(3);
                while (f.size() < 3)
                    f += QLatin1Char('0');
                frac = f.toLongLong(&ok);
                if (!ok)
                    return kNoTime;
            }
            total = total * 60 + sec;
            total = total * 1000 + frac;
        } else {
            bool ok = false;
            const qint64 v = part.toLongLong(&ok);
            if (!ok)
                return kNoTime;
            total = total * 60 + v;
        }
    }
    return total;
}

QString formatDayTime(qint64 us, int decimals)
{
    if (us < 0)
        return QString();
    const qint64 sec = us / kUsPerSecond;
    const qint64 frac = us % kUsPerSecond;
    const qint64 h = sec / 3600;
    const qint64 m = (sec % 3600) / 60;
    const qint64 s = sec % 60;
    QString out = QStringLiteral("%1h%2:%3")
                      .arg(h, 2, 10, QChar('0'))
                      .arg(m, 2, 10, QChar('0'))
                      .arg(s, 2, 10, QChar('0'));
    if (decimals > 0) {
        decimals = qMin(decimals, 6);
        qint64 div = 1;
        for (int i = decimals; i < 6; ++i)
            div *= 10;
        out += QLatin1Char('.') + QStringLiteral("%1").arg(frac / div, decimals, 10, QChar('0'));
    }
    return out;
}

QString formatClock(qint64 us, int decimals)
{
    QString s = formatDayTime(us, decimals);
    s.replace(QLatin1Char('h'), QLatin1Char(':'));
    return s;
}

qint64 parseDayTime(const QString& textIn)
{
    static const QRegularExpression re(
        QStringLiteral("^\\s*(\\d{1,2})[:hH](\\d{1,2})(?::(\\d{1,2})(?:[.,](\\d{1,6}))?)?\\s*$"));
    const auto m = re.match(textIn);
    if (!m.hasMatch())
        return kNoTime;
    const qint64 h = m.captured(1).toLongLong();
    const qint64 mi = m.captured(2).toLongLong();
    const qint64 s = m.captured(3).isEmpty() ? 0 : m.captured(3).toLongLong();
    if (mi > 59 || s > 59 || h > 47)
        return kNoTime;
    QString f = m.captured(4);
    while (f.size() < 6)
        f += QLatin1Char('0');
    return ((h * 3600 + mi * 60 + s) * kUsPerSecond) + f.toLongLong();
}

qint64 netTimeMs(qint64 startUs, qint64 finishUs)
{
    if (startUs < 0 || finishUs < 0 || finishUs < startUs)
        return kNoTime;
    return (finishUs - startUs) / 1000;
}

QString formatPoints(int hundredths)
{
    if (hundredths < 0)
        return QStringLiteral("-");
    return QStringLiteral("%1,%2").arg(hundredths / 100).arg(hundredths % 100, 2, 10, QChar('0'));
}

int parsePoints(const QString& textIn)
{
    QString text = textIn.trimmed();
    if (text.isEmpty() || text == QLatin1String("-"))
        return -1;
    text.replace(QLatin1Char(','), QLatin1Char('.'));
    bool ok = false;
    const double v = text.toDouble(&ok);
    if (!ok || v < 0)
        return -1;
    return int(v * 100.0 + 0.5);
}

} // namespace TimeUtil
