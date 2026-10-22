#include "app/AppSettings.h"
#include "ui/MainWindow.h"
#include "ui/Snapshots.h"
#include "ui/UiHelpers.h"

#include <QApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("OpenSkiPro2"));
    QApplication::setOrganizationName(QStringLiteral("OpenSkiPro2"));
    QApplication::setApplicationVersion(QStringLiteral(OSP_VERSION));

    const QStringList args = QApplication::arguments();
    const int snap = int(args.indexOf(QStringLiteral("--ui-snapshots")));
    if (snap > 0 && snap + 2 < args.size()) // developer mode: never touch the user's settings
        AppSettings::setIniPathOverride(args.at(snap + 1) + QStringLiteral("/snapshots.ini"));

    // Language: English is the source language; further languages are
    // compiled translations (translations/openskipro2_<lang>.ts).
    const QString lang = AppSettings::language();
    QTranslator appTranslator;
    QTranslator qtTranslator;
    if (lang != QLatin1String("en")) {
        if (appTranslator.load(QStringLiteral(":/i18n/openskipro2_%1.qm").arg(lang)))
            QApplication::installTranslator(&appTranslator);
        if (qtTranslator.load(QStringLiteral("qtbase_%1").arg(lang), QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
            QApplication::installTranslator(&qtTranslator);
    }

    QFont f(QStringLiteral("Segoe UI"));
    f.setPointSize(9);
    QApplication::setFont(f);
    app.setStyleSheet(UiHelpers::appStyleSheet());

    if (snap > 0 && snap + 2 < args.size())
        return runSnapshots(args.at(snap + 1), args.at(snap + 2));

    MainWindow w;
    w.show();
    return QApplication::exec();
}
