#pragma once

#include "core/FisList.h"

#include <QString>

class QWidget;

// Shared helpers of the FIS section (current list, validity check).
namespace FisUi {

// Loads the current FIS list (with its competitors when asked). Shows a message and
// returns false when there is none or it cannot be read.
bool loadList(QWidget* parent, const QString& title, FisList& list, bool withCompetitors);
// Booklet 12.4: warns when the validity of the list is over; false when the user stops.
bool confirmValidity(QWidget* parent, const QString& title, const FisListHeader& h);
QString formatDate(const QDate& d);              // 15-10-2026
QString validityText(const FisListHeader& h);    // Valid from 15-10-2026 to 21-10-2026
QString listTitle(const FisListHeader& h);       // 16th FIS points list 2026/27 (1627)

} // namespace FisUi
