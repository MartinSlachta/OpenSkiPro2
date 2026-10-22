#pragma once

#include "Competitor.h"
#include "FisList.h"

#include <QStringList>

struct CategoryGrid;
class Event;

// Transfer of competitors between the FIS list and an event.
namespace FisCompetitors {

// FIS code as compared between the event and the list ("FIS 700830" -> "700830").
QString normalizedCode(const QString& code);

// New event competitor (no bib) from a competitor of the list.
Competitor fromFis(const FisCompetitor& f, const FisListHeader& h, const CategoryGrid& categories);

struct UpdateResult {
    int updated = 0;
    QStringList notUpdated; // "12 LAST First (code)"
};
// Fields (CompField ids) that "Update Competitors" can replace.
QList<int> updatableFields();
// Replaces the given fields of the event competitors found in the list by their FIS code.
UpdateResult update(Event* ev, const FisList& list, const QList<int>& fields);

} // namespace FisCompetitors
