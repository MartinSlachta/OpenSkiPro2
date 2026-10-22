#pragma once

#include "core/EventParams.h"

#include <QDialog>
#include <QImage>
#include <QWidget>
#include <array>

// Page mock-up on which the logos are moved (drag) and resized (drag the
// bottom right corner). Positions are stored relative to the page.
class LogoPageWidget : public QWidget {
    Q_OBJECT
public:
    explicit LogoPageWidget(const PrintingParams& pp, QWidget* parent = nullptr);
    void setLandscape(bool on);
    std::array<QRectF, 8> rects() const { return m_rects; }

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    QRectF pageRect() const;
    QRectF toWidget(const QRectF& rel) const;

    PrintingParams m_pp;
    std::array<QRectF, 8> m_rects;
    std::array<QImage, 8> m_images;
    bool m_landscape = false;
    int m_drag = -1;
    bool m_resize = false;
    QPointF m_dragStart;
    QRectF m_rectStart;
};

class LogoPositionsDialog : public QDialog {
    Q_OBJECT
public:
    LogoPositionsDialog(const PrintingParams& pp, QWidget* parent = nullptr);
    std::array<QRectF, 8> rects() const;

private:
    LogoPageWidget* m_page;
};
