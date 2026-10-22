#pragma once

#include <QDialog>

struct TimeRecord;
class QPlainTextEdit;

// Comment of the operator on one impulse (passage) of the timing window.
class ImpulseCommentDialog : public QDialog {
    Q_OBJECT
public:
    ImpulseCommentDialog(const TimeRecord& rec, int seq, QWidget* parent = nullptr);

    QString comment() const;

private:
    QPlainTextEdit* m_edit;
};
