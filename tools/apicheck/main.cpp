// osp-apicheck: black box of the OpenSkiPro2 API.
//   osp-apicheck record [--host H] [--port P] [--out file.jsonl]   records until Ctrl+C
//   osp-apicheck check file.jsonl [--host H] [--port P]           checks the recording
// The check exits with 0 when everything is consistent, 1 when an error was found.

#include "ApiCheck.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTextStream>
#include <QTimer>

static QTextStream& out()
{
    static QTextStream s(stdout);
    return s;
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("osp-apicheck"));

    QCommandLineParser p;
    p.setApplicationDescription(QStringLiteral(
        "Black box of the OpenSkiPro2 API: records the WebSocket messages of a timed run\n"
        "and checks them against the REST resources (docs/api.md).\n\n"
        "  record             record until Ctrl+C (one JSON line per message)\n"
        "  check <file>       check a recording (exit code 1 when an error was found)"));
    p.addHelpOption();
    p.addPositionalArgument(QStringLiteral("command"), QStringLiteral("record or check"));
    p.addPositionalArgument(QStringLiteral("file"), QStringLiteral("recording to check"), QStringLiteral("[file]"));
    p.addOption({ QStringLiteral("host"), QStringLiteral("PC with OpenSkiPro2 (default localhost)"), QStringLiteral("host"), QStringLiteral("localhost") });
    p.addOption({ QStringLiteral("port"), QStringLiteral("port of the API (default 8080)"), QStringLiteral("port"), QStringLiteral("8080") });
    p.addOption({ QStringLiteral("out"), QStringLiteral("file of the recording (default apicheck-<date>.jsonl)"), QStringLiteral("file") });
    p.process(app);

    const QStringList args = p.positionalArguments();
    const QString cmd = args.value(0);
    const QString base = QStringLiteral("%1:%2").arg(p.value(QStringLiteral("host")), p.value(QStringLiteral("port")));

    if (cmd == QLatin1String("record")) {
        const QString file = p.isSet(QStringLiteral("out"))
            ? p.value(QStringLiteral("out"))
            : QStringLiteral("apicheck-%1.jsonl").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
        auto* f = new QFile(file, &app);
        if (!f->open(QIODevice::WriteOnly | QIODevice::Append)) {
            out() << "cannot write " << file << "\n";
            return 2;
        }
        auto* rec = new ApiCheck::Recorder(&app);
        rec->setOutput(f);
        QObject::connect(rec, &ApiCheck::Recorder::received, &app, [](const QJsonObject& m) {
            // a short line per change, the rest only counted
            const QString type = m.value(QStringLiteral("type")).toString();
            const QJsonObject d = m.value(QStringLiteral("data")).toObject();
            if (type == QLatin1String("competitor")) {
                out() << QStringLiteral("%1 run %2 bib %3 %4 %5 %6\n")
                             .arg(m.value(QStringLiteral("seq")).toInteger(), 6)
                             .arg(d.value(QStringLiteral("run")).toInt())
                             .arg(d.value(QStringLiteral("bib")).toInt(), 3)
                             .arg(d.value(QStringLiteral("change")).toString(), -12)
                             .arg(d.value(QStringLiteral("state")).toString(), -10)
                             .arg(d.value(QStringLiteral("thisRun")).toObject().value(QStringLiteral("time")).toString());
                out().flush();
            } else if (type == QLatin1String("hello") || type == QLatin1String("status")) {
                out() << QStringLiteral("%1 %2\n").arg(m.value(QStringLiteral("seq")).toInteger(), 6).arg(type);
                out().flush();
            }
        });
        out() << "recording ws://" << base << "/ws to " << file << " - stop with Ctrl+C\n";
        out().flush();
        rec->start(QUrl(QStringLiteral("ws://%1/ws").arg(base)));
        return QCoreApplication::exec();
    }

    if (cmd == QLatin1String("check")) {
        QFile f(args.value(1));
        if (!f.open(QIODevice::ReadOnly)) {
            out() << "cannot read " << args.value(1) << "\n";
            return 2;
        }
        QString err;
        const QList<ApiCheck::Line> lines = ApiCheck::readLines(&f, &err);
        if (!err.isEmpty())
            out() << "note: " << err << "\n";
        QNetworkAccessManager nam;
        auto fetch = [&](const QString& name, int* status) {
            QString path = name;
            if (path.startsWith(QLatin1String("run/")))
                path = QStringLiteral("runs/") + path.mid(4);
            QNetworkReply* r = nam.get(QNetworkRequest(QUrl(QStringLiteral("http://%1/api/v1/%2").arg(base, path))));
            QEventLoop loop;
            QObject::connect(r, &QNetworkReply::finished, &loop, &QEventLoop::quit);
            QTimer::singleShot(10000, &loop, &QEventLoop::quit);
            loop.exec();
            *status = r->isFinished() ? r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() : 0;
            const QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
            r->deleteLater();
            return o;
        };
        const ApiCheck::Report rep = ApiCheck::check(lines, fetch);
        out() << rep.text() << "\n";
        return rep.ok() ? 0 : 1;
    }

    p.showHelp(2);
}
