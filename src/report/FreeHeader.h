#pragma once

#include <QFont>
#include <QString>

// The free header of an event (SkiPro "Print Heading", USE_FREE_HEADER): a rich text
// printed on the first page of the lists instead of the jury / technical data block.
// Kept as Qt rich text (HTML) in EventParams::general.freeHeader.
namespace FreeHeader {

QFont defaultFont();                  // font of a text without its own (the editor and the print)
bool isEmpty(const QString& html);    // no visible text
// SkiPro RTF_HEADER (a Delphi RichEdit text, "\CR\" for its line ends) to Qt HTML: bold,
// italic, underline, font, size and the alignment of the paragraphs; tables and pictures
// become plain text.
QString fromRtf(const QString& rtf);

} // namespace FreeHeader
