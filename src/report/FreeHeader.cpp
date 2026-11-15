#include "FreeHeader.h"

#include <QHash>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QVector>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace FreeHeader {

QFont defaultFont()
{
    return QFont(QStringLiteral("Arial"), 10);
}

bool isEmpty(const QString& html)
{
    if (html.trimmed().isEmpty())
        return true;
    QTextDocument doc;
    doc.setHtml(html);
    return doc.toPlainText().trimmed().isEmpty();
}

namespace {

// a byte of an \'hh escape in the code page of the font (fcharset) or of the document (ansicpg)
QString decodeByte(uchar b, int codePage)
{
    if (b < 0x80)
        return QString(QChar(b));
#ifdef Q_OS_WIN
    wchar_t w[4];
    const char c = char(b);
    const int n = MultiByteToWideChar(UINT(codePage), 0, &c, 1, w, 4);
    if (n > 0)
        return QString::fromWCharArray(w, n);
#else
    Q_UNUSED(codePage)
#endif
    return QString(QChar(b));
}

int codePageOfCharset(int charset, int fallback)
{
    switch (charset) {
    case 0: return 1252;
    case 238: return 1250;  // Central European (Slovak, Czech)
    case 204: return 1251;
    case 161: return 1253;
    case 162: return 1254;
    case 186: return 1257;
    default: return fallback;
    }
}

struct State {
    bool bold = false;
    bool italic = false;
    bool underline = false;
    int halfPoints = 0;   // \fsN, 0 = the default size
    int font = -1;        // \fN
    int ucSkip = 1;       // \ucN: bytes after \uN replaced by the fallback
    bool skip = false;    // a destination that is not text (font table, pictures...)
};

struct Font {
    QString name;
    int codePage = 1252;
};

} // namespace

QString fromRtf(const QString& rtfIn)
{
    QString rtf = rtfIn;
    rtf.replace(QStringLiteral("\\CR\\"), QStringLiteral("\n"));
    if (!rtf.trimmed().startsWith(QLatin1String("{\\rtf"))) {
        // not RTF: the text as it is, line by line
        QTextDocument doc;
        doc.setPlainText(rtf);
        return rtf.trimmed().isEmpty() ? QString() : doc.toHtml();
    }

    QTextDocument doc;
    doc.setDefaultFont(defaultFont());
    QTextCursor cur(&doc);
    QHash<int, Font> fonts;
    int docCodePage = 1252;
    QVector<State> stack;
    State st;
    int align = 0;              // 0 left, 1 centre, 2 right, 3 justify
    bool firstBlock = true;
    bool inFontTable = false;
    int fontTableDepth = -1;
    int currentFontDef = -1;
    QString fontName;
    int pendingSkip = 0;        // fallback bytes still to skip after \uN

    auto blockFormat = [&] {
        QTextBlockFormat bf;
        bf.setAlignment(align == 1 ? Qt::AlignHCenter : align == 2 ? Qt::AlignRight : align == 3 ? Qt::AlignJustify : Qt::AlignLeft);
        return bf;
    };
    auto charFormat = [&] {
        QTextCharFormat cf;
        cf.setFontWeight(st.bold ? QFont::Bold : QFont::Normal);
        cf.setFontItalic(st.italic);
        cf.setFontUnderline(st.underline);
        if (st.halfPoints > 0)
            cf.setFontPointSize(st.halfPoints / 2.0);
        if (fonts.contains(st.font) && !fonts.value(st.font).name.isEmpty())
            cf.setFontFamilies({ fonts.value(st.font).name });
        return cf;
    };
    auto insertText = [&](const QString& t) {
        if (t.isEmpty() || st.skip)
            return;
        if (inFontTable) {
            fontName += t;
            return;
        }
        if (firstBlock) {
            cur.setBlockFormat(blockFormat());
            firstBlock = false;
        }
        cur.insertText(t, charFormat());
    };
    auto codePage = [&] { return fonts.contains(st.font) ? fonts.value(st.font).codePage : docCodePage; };

    const int n = int(rtf.size());
    int i = 0;
    while (i < n) {
        const QChar c = rtf.at(i);
        if (c == QLatin1Char('{')) {
            stack << st;
            ++i;
            continue;
        }
        if (c == QLatin1Char('}')) {
            const bool tableEnds = inFontTable && stack.size() - 1 <= fontTableDepth;
            if (inFontTable && currentFontDef >= 0) {
                // end of one font of the table: "Arial;" -> "Arial"
                Font f = fonts.value(currentFontDef);
                f.name = fontName.trimmed();
                if (f.name.endsWith(QLatin1Char(';')))
                    f.name.chop(1);
                fonts.insert(currentFontDef, f);
                currentFontDef = -1;
                fontName.clear();
            }
            if (tableEnds)
                inFontTable = false;
            if (!stack.isEmpty())
                st = stack.takeLast();
            ++i;
            continue;
        }
        if (c == QLatin1Char('\r') || c == QLatin1Char('\n')) {
            ++i;
            continue;
        }
        if (c != QLatin1Char('\\')) {
            if (pendingSkip > 0) {
                --pendingSkip;
                ++i;
                continue;
            }
            int j = i;
            while (j < n && rtf.at(j) != QLatin1Char('\\') && rtf.at(j) != QLatin1Char('{') && rtf.at(j) != QLatin1Char('}')
                   && rtf.at(j) != QLatin1Char('\r') && rtf.at(j) != QLatin1Char('\n'))
                ++j;
            insertText(rtf.mid(i, j - i));
            i = j;
            continue;
        }
        // control symbol or word
        ++i;
        if (i >= n)
            break;
        const QChar s = rtf.at(i);
        if (s == QLatin1Char('\\') || s == QLatin1Char('{') || s == QLatin1Char('}')) {
            insertText(QString(s));
            ++i;
            continue;
        }
        if (s == QLatin1Char('\'')) {
            const uchar b = uchar(rtf.mid(i + 1, 2).toInt(nullptr, 16));
            i += 3;
            if (pendingSkip > 0) {
                --pendingSkip;
                continue;
            }
            if (inFontTable)
                fontName += QChar(b);
            else
                insertText(decodeByte(b, codePage()));
            continue;
        }
        if (s == QLatin1Char('*')) {
            st.skip = true; // \* : an optional destination this reader does not know
            ++i;
            continue;
        }
        if (s == QLatin1Char('~')) {
            insertText(QString(QChar(0xa0)));
            ++i;
            continue;
        }
        if (!s.isLetter()) {
            ++i; // \- \_ and the like
            continue;
        }
        int j = i;
        while (j < n && rtf.at(j).isLetter())
            ++j;
        const QString word = rtf.mid(i, j - i);
        int k = j;
        if (k < n && (rtf.at(k) == QLatin1Char('-') || rtf.at(k).isDigit())) {
            ++k;
            while (k < n && rtf.at(k).isDigit())
                ++k;
        }
        const bool hasParam = k > j;
        const int param = hasParam ? rtf.mid(j, k - j).toInt() : 1;
        i = k;
        if (i < n && rtf.at(i) == QLatin1Char(' '))
            ++i; // the space ending a control word

        if (word == QLatin1String("fonttbl")) {
            inFontTable = true;
            fontTableDepth = int(stack.size()) - 1;
        } else if (word == QLatin1String("colortbl") || word == QLatin1String("stylesheet") || word == QLatin1String("info")
                   || word == QLatin1String("pict") || word == QLatin1String("header") || word == QLatin1String("footer")
                   || word == QLatin1String("object") || word == QLatin1String("generator")) {
            st.skip = true;
        } else if (word == QLatin1String("ansicpg")) {
            docCodePage = param;
        } else if (word == QLatin1String("f")) {
            if (inFontTable) {
                currentFontDef = param;
                fontName.clear();
                fonts.insert(param, Font{ QString(), docCodePage });
            } else {
                st.font = param;
            }
        } else if (word == QLatin1String("fcharset")) {
            if (inFontTable && currentFontDef >= 0) {
                Font f = fonts.value(currentFontDef);
                f.codePage = codePageOfCharset(param, docCodePage);
                fonts.insert(currentFontDef, f);
            }
        } else if (word == QLatin1String("b")) {
            st.bold = !hasParam || param != 0;
        } else if (word == QLatin1String("i")) {
            st.italic = !hasParam || param != 0;
        } else if (word == QLatin1String("ul")) {
            st.underline = !hasParam || param != 0;
        } else if (word == QLatin1String("ulnone")) {
            st.underline = false;
        } else if (word == QLatin1String("fs")) {
            st.halfPoints = param;
        } else if (word == QLatin1String("plain")) {
            st.bold = st.italic = st.underline = false;
            st.halfPoints = 0;
        } else if (word == QLatin1String("pard")) {
            align = 0;
            if (!firstBlock)
                cur.setBlockFormat(blockFormat());
        } else if (word == QLatin1String("ql") || word == QLatin1String("qc") || word == QLatin1String("qr")
                   || word == QLatin1String("qj")) {
            align = word == QLatin1String("qc") ? 1 : word == QLatin1String("qr") ? 2 : word == QLatin1String("qj") ? 3 : 0;
            if (!firstBlock)
                cur.setBlockFormat(blockFormat());
        } else if (word == QLatin1String("par")) {
            if (!st.skip && !inFontTable) {
                if (firstBlock) {
                    cur.setBlockFormat(blockFormat());
                    firstBlock = false;
                }
                cur.insertBlock(blockFormat(), charFormat());
            }
        } else if (word == QLatin1String("line")) {
            insertText(QString(QChar::LineSeparator));
        } else if (word == QLatin1String("tab")) {
            insertText(QStringLiteral("\t"));
        } else if (word == QLatin1String("cell")) {
            insertText(QStringLiteral("   "));
        } else if (word == QLatin1String("row")) {
            if (!st.skip)
                cur.insertBlock(blockFormat(), charFormat());
        } else if (word == QLatin1String("uc")) {
            st.ucSkip = param;
        } else if (word == QLatin1String("u")) {
            insertText(QString(QChar(ushort(param < 0 ? param + 65536 : param))));
            pendingSkip = st.ucSkip;
        }
    }
    // the last empty paragraph of RichEdit ("\par }") is not printed
    if (doc.blockCount() > 1 && doc.lastBlock().text().isEmpty()) {
        QTextCursor last(doc.lastBlock());
        last.movePosition(QTextCursor::PreviousCharacter, QTextCursor::KeepAnchor);
        last.removeSelectedText();
    }
    return doc.toPlainText().trimmed().isEmpty() ? QString() : doc.toHtml();
}

} // namespace FreeHeader
