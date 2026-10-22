#pragma once

#include <QString>

// Text transformations of "Format a Field" (competitor fields).
namespace FieldFormat {

enum class Case { Lower, Capitalized, Upper };

// Capitalized: first letter of each word upper case (also after '-' and '\''), the others lower case.
QString applyCase(const QString& text, Case c);

// Every occurrence of `what` replaced by `by` (case sensitive); `what` empty: the text unchanged.
QString replace(const QString& text, const QString& what, const QString& by);

} // namespace FieldFormat
