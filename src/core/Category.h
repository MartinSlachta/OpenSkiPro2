#pragma once

#include "Types.h"

#include <QList>
#include <QString>

// Category defined by gender and a range of birth years.
struct CategoryDef {
    QString name;
    Gender gender = Gender::Unknown; // Unknown = both
    int yearMin = 0;
    int yearMax = 0;

    bool matches(Gender g, int year) const;
};

struct CategoryGrid {
    QList<CategoryDef> categories;

    // First matching category name, empty if none.
    QString categoryFor(Gender g, int year) const;
    QString toString() const;
    static CategoryGrid fromString(const QString& s);
    // Reads a SkiPro *.cat file (UTF-16 INI with [Parameters] COMPETITORS_CATEGORYn_*)
    static CategoryGrid fromSkiProCatFile(const QString& path, QString* error = nullptr);
};
