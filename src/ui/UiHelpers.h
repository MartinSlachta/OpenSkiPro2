#pragma once

#include "core/Types.h"

#include <QIcon>
#include <QString>

class Event;
class QDialog;
class QHBoxLayout;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;
class QTableView;
class QWidget;

// Small helpers giving the widgets the SkiPro look (blue title bars,
// big icon buttons, yellow info boxes, Ok/Cancel rows).
namespace UiHelpers {

QIcon icon(const QString& name);
QPushButton* button(const QString& iconName, const QString& text, QWidget* parent = nullptr);
QLabel* titleBar(const QString& text, QWidget* parent = nullptr);      // blue gradient bar
QLabel* infoBox(QWidget* parent = nullptr);                            // yellow box with bold text
QLabel* counterBox(QWidget* parent = nullptr);                         // small yellow counter
// Adds an Ok / Cancel row to a dialog layout and returns it.
QHBoxLayout* okCancelRow(QDialog* dlg, QPushButton** load = nullptr, QPushButton** save = nullptr);
void styleGrid(QTableView* view, bool monospace = false);
QString statusText(ResultStatus s);
QString oneLine(const QString& text); // lines joined with " / " (multi-line titles)
QString pointName(int point);
QString appStyleSheet();
// Multi-line text field of a form: Tab moves to the next field instead of typing a tab,
// maxLines > 0 keeps at most that many lines (Enter and pasted text included).
void formTextEdit(QPlainTextEdit* edit, int maxLines = 0);
// Warns about bibs that several competitors have (after an import): the times belong to the bib.
void warnDuplicateBibs(QWidget* parent, const Event& ev);

} // namespace UiHelpers
