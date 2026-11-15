#pragma once

#include "core/EventParams.h"

#include <QDialog>

class QAction;
class QTextEdit;

// "Header" of an event with a free header (General Parameters): the rich text printed on
// the first page of the lists instead of the jury / technical data block, like the
// "Print Heading" of SkiPro (bold, italic, underline, alignment, font, text pasted from Word).
class FreeHeaderDialog : public QDialog {
    Q_OBJECT
public:
    FreeHeaderDialog(const EventParams& p, QWidget* parent = nullptr);
    void apply(EventParams& p) const;

private:
    void updateActions();
    void chooseFont();
    void loadFromFile();
    void loadFromEvent();
    void saveToFile();

    QTextEdit* m_edit;
    QAction* m_bold;
    QAction* m_italic;
    QAction* m_underline;
    QAction* m_left;
    QAction* m_center;
    QAction* m_right;
    QAction* m_font;
};
