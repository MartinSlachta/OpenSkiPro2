#pragma once

#include <QDialog>

class QLineEdit;

class FoldersDialog : public QDialog {
    Q_OBJECT
public:
    explicit FoldersDialog(QWidget* parent = nullptr);
    void accept() override;

private:
    QLineEdit* m_events;
    QLineEdit* m_logos;
    QLineEdit* m_export;
};
