#include "FisUi.h"
#include "app/AppSettings.h"
#include "db/FisListDb.h"

#include <QApplication>
#include <QCoreApplication>
#include <QMessageBox>

namespace FisUi {

bool loadList(QWidget* parent, const QString& title, FisList& list, bool withCompetitors)
{
    QString err;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = withCompetitors ? FisListDb::load(AppSettings::fisListPath(), list, &err)
                                    : FisListDb::loadHeader(AppSettings::fisListPath(), list, nullptr, &err);
    QApplication::restoreOverrideCursor();
    if (!ok) {
        QMessageBox::warning(parent, title, QCoreApplication::translate("FisUi", "Cannot read the FIS list:\n%1").arg(err));
        return false;
    }
    if (list.isEmpty()) {
        QMessageBox::information(parent, title,
                                 QCoreApplication::translate("FisUi", "No FIS list has been imported yet. Import it first (FIS / Import FIS list)."));
        return false;
    }
    return true;
}

bool confirmValidity(QWidget* parent, const QString& title, const FisListHeader& h)
{
    if (!h.isExpired())
        return true;
    return QMessageBox::warning(parent, title,
                                QCoreApplication::translate("FisUi", "The validity of the FIS list %1 is over (valid to %2).\n"
                                                                     "Import the current FIS list.\n\nDo you want to continue with this list?")
                                    .arg(listTitle(h), formatDate(h.validTo)),
                                QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
        == QMessageBox::Yes;
}

QString formatDate(const QDate& d)
{
    return d.isValid() ? d.toString(QStringLiteral("dd-MM-yyyy")) : QStringLiteral("-");
}

QString validityText(const FisListHeader& h)
{
    return QCoreApplication::translate("FisUi", "Valid from %1 to %2").arg(formatDate(h.validFrom), formatDate(h.validTo));
}

QString listTitle(const FisListHeader& h)
{
    return QStringLiteral("%1 (%2)").arg(h.name, h.code());
}

} // namespace FisUi
