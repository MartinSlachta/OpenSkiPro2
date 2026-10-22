#pragma once

#include <QWidget>

class QLabel;

// Top banner of the main window: event title and discipline on the left,
// program name on the right (SkiPro shows the TAG Heuer logo there).
class HeaderBanner : public QWidget {
    Q_OBJECT
public:
    explicit HeaderBanner(QWidget* parent = nullptr);
    void setTitles(const QString& title, const QString& subtitle);

Q_SIGNALS:
    void openEventRequested();

private:
    QLabel* m_title;
    QLabel* m_subtitle;
};
