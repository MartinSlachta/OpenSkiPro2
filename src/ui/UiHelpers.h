#pragma once

#include "core/Types.h"

#include <QIcon>
#include <QString>

class QDialog;
class QHBoxLayout;
class QLabel;
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

} // namespace UiHelpers
