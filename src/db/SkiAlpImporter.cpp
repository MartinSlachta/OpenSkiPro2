#include "SkiAlpImporter.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtCore/private/qzipreader_p.h>

bool SkiAlpImporter::read(const QString& sproFile, SkiProImporter::Result& out, QString* error, const QString& logosDir)
{
    QZipReader zip(sproFile);
    if (!zip.isReadable() || zip.status() != QZipReader::NoError) {
        if (error)
            *error = QStringLiteral("The file is not a SkiAlp Pro event archive.");
        return false;
    }
    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        if (error)
            *error = QStringLiteral("Cannot create a temporary folder.");
        return false;
    }
    const QStringList names{ QStringLiteral("File1"), QStringLiteral("File2") };
    for (const QString& name : names) {
        const QByteArray data = zip.fileData(name);
        if (data.isEmpty()) {
            if (error)
                *error = QStringLiteral("The archive does not contain %1, it is not a SkiAlp Pro event.").arg(name);
            return false;
        }
        QFile f(tmp.filePath(name));
        if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size()) {
            if (error)
                *error = QStringLiteral("Cannot write %1: %2").arg(f.fileName(), f.errorString());
            return false;
        }
    }
    return SkiProImporter::readDatabases(tmp.filePath(names[0]), tmp.filePath(names[1]), SkiProImporter::Format::SkiAlp11,
                                         out, error, logosDir);
}
