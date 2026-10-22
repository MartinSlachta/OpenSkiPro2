#include "FieldFormat.h"

namespace FieldFormat {

QString applyCase(const QString& text, Case c)
{
    switch (c) {
    case Case::Lower: return text.toLower();
    case Case::Upper: return text.toUpper();
    case Case::Capitalized: break;
    }
    QString out = text.toLower();
    bool wordStart = true;
    for (QChar& ch : out) {
        if (ch.isLetter()) {
            if (wordStart)
                ch = ch.toUpper();
            wordStart = false;
        } else {
            wordStart = ch.isSpace() || ch == QLatin1Char('-') || ch == QLatin1Char('\'');
        }
    }
    return out;
}

QString replace(const QString& text, const QString& what, const QString& by)
{
    if (what.isEmpty())
        return text;
    return QString(text).replace(what, by);
}

} // namespace FieldFormat
