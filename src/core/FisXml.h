#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

class Event;

// FIS XML result file of an alpine race (FIS Alpine Data Booklet 17): "<Nation><Codex>.xml",
// e.g. SVK6534.xml, sent to FIS after the race, according to the "Alpine Result XML
// Description" 2.12 of FIS (fis/export-xml/result-xml-alpine.pdf).
namespace FisXml {

struct RaceId {
    QString nation;   // "SVK"
    QString codex;    // "6534" (4 digits)
    QString tdNumber; // "185"
};
// From the codex and its nation (General Parameters) and the code of the TD (Header).
RaceId raceId(const Event& ev);
// "SVK6534.185" as written on the papers (without ".185" when the TD has no code).
QString fullCodex(const RaceId& id);
QString fullCodex(const Event& ev);

QString fileName(const Event& ev); // "SVK6534.xml"
int season(const Event& ev);       // FIS season of the race date (from 1st July: next year)

// Problems that make the file invalid (empty: it can be exported).
QStringList check(const Event& ev);
// Things worth checking, the file can still be exported.
QStringList warnings(const Event& ev);

// The XML document of the official results (UTF-8, CRLF line ends like the files of SkiAlp Pro).
QByteArray build(const Event& ev);

} // namespace FisXml
