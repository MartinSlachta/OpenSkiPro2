#include "LogoPositionsDialog.h"
#include "ui/UiHelpers.h"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

LogoPageWidget::LogoPageWidget(const PrintingParams& pp, QWidget* parent)
    : QWidget(parent)
    , m_pp(pp)
{
    for (size_t i = 0; i < m_rects.size(); ++i) {
        m_rects[i] = pp.logos[i].rect;
        if (!pp.logos[i].path.isEmpty() && QFileInfo::exists(pp.logos[i].path))
            m_images[i] = QImage(pp.logos[i].path);
    }
    setMinimumSize(420, 560);
    setMouseTracking(true);
}

void LogoPageWidget::setLandscape(bool on)
{
    m_landscape = on;
    update();
}

QRectF LogoPageWidget::pageRect() const
{
    const double ratio = m_landscape ? 297.0 / 210.0 : 210.0 / 297.0;
    double h = height() - 20;
    double w = h * ratio;
    if (w > width() - 20) {
        w = width() - 20;
        h = w / ratio;
    }
    return QRectF((width() - w) / 2, (height() - h) / 2, w, h);
}

QRectF LogoPageWidget::toWidget(const QRectF& rel) const
{
    const QRectF p = pageRect();
    return QRectF(p.x() + rel.x() * p.width(), p.y() + rel.y() * p.height(), rel.width() * p.width(), rel.height() * p.height());
}

void LogoPageWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(0xbf, 0xcf, 0xdf));
    const QRectF page = pageRect();
    p.fillRect(page, Qt::white);
    p.setPen(QPen(Qt::black, 1));
    p.drawRect(page);
    // content area from the margins of the first page
    const double mmX = page.width() / (m_landscape ? 297.0 : 210.0);
    const double mmY = page.height() / (m_landscape ? 210.0 : 297.0);
    const QRectF content(page.x() + m_pp.marginsFirst[2] * mmX, page.y() + m_pp.marginsFirst[0] * mmY,
                         page.width() - (m_pp.marginsFirst[2] + m_pp.marginsFirst[3]) * mmX,
                         page.height() - (m_pp.marginsFirst[0] + m_pp.marginsFirst[1]) * mmY);
    p.setPen(QPen(Qt::gray, 1, Qt::DashLine));
    p.setBrush(QBrush(QColor(200, 200, 200), Qt::BDiagPattern));
    p.drawRect(content);
    p.setBrush(Qt::NoBrush);
    for (size_t i = 0; i < m_rects.size(); ++i) {
        if (m_images[i].isNull())
            continue;
        const QRectF box = toWidget(m_rects[i]);
        QSizeF s = m_images[i].size();
        s.scale(box.size(), Qt::KeepAspectRatio);
        p.drawImage(QRectF(box.x() + (box.width() - s.width()) / 2, box.y() + (box.height() - s.height()) / 2, s.width(), s.height()),
                    m_images[i]);
        p.setPen(QPen(QColor(0x3d, 0x6f, 0xb4), 1));
        p.drawRect(box);
        p.fillRect(QRectF(box.bottomRight() - QPointF(6, 6), QSizeF(6, 6)), QColor(0x3d, 0x6f, 0xb4));
        p.drawText(box.topLeft() + QPointF(3, 12), QString::number(int(i) + 1));
    }
}

void LogoPageWidget::mousePressEvent(QMouseEvent* e)
{
    m_drag = -1;
    for (int i = int(m_rects.size()) - 1; i >= 0; --i) {
        if (m_images[size_t(i)].isNull())
            continue;
        const QRectF box = toWidget(m_rects[size_t(i)]);
        if (box.adjusted(-3, -3, 3, 3).contains(e->position())) {
            m_drag = i;
            m_resize = QRectF(box.bottomRight() - QPointF(10, 10), QSizeF(14, 14)).contains(e->position());
            m_dragStart = e->position();
            m_rectStart = m_rects[size_t(i)];
            break;
        }
    }
}

void LogoPageWidget::mouseMoveEvent(QMouseEvent* e)
{
    if (m_drag < 0) {
        bool corner = false;
        for (size_t i = 0; i < m_rects.size(); ++i) {
            if (m_images[i].isNull())
                continue;
            const QRectF box = toWidget(m_rects[i]);
            if (QRectF(box.bottomRight() - QPointF(10, 10), QSizeF(14, 14)).contains(e->position()))
                corner = true;
        }
        setCursor(corner ? Qt::SizeFDiagCursor : Qt::ArrowCursor);
        return;
    }
    const QRectF page = pageRect();
    const QPointF d((e->position().x() - m_dragStart.x()) / page.width(), (e->position().y() - m_dragStart.y()) / page.height());
    QRectF r = m_rectStart;
    if (m_resize) {
        r.setWidth(qMax(0.02, m_rectStart.width() + d.x()));
        r.setHeight(qMax(0.01, m_rectStart.height() + d.y()));
    } else {
        r.moveTopLeft(m_rectStart.topLeft() + d);
    }
    r.moveLeft(qBound(0.0, r.left(), 1.0 - r.width()));
    r.moveTop(qBound(0.0, r.top(), 1.0 - r.height()));
    m_rects[size_t(m_drag)] = r;
    update();
}

void LogoPageWidget::mouseReleaseEvent(QMouseEvent*)
{
    m_drag = -1;
}

LogoPositionsDialog::LogoPositionsDialog(const PrintingParams& pp, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Logo Positions"));
    auto* lay = new QVBoxLayout(this);
    auto* bar = new QHBoxLayout;
    auto* portrait = UiHelpers::button(QStringLiteral("portrait"), tr("Portrait"), this);
    auto* landscape = UiHelpers::button(QStringLiteral("landscape"), tr("Landscape"), this);
    bar->addWidget(portrait);
    bar->addWidget(landscape);
    bar->addStretch();
    lay->addLayout(bar);
    m_page = new LogoPageWidget(pp, this);
    lay->addWidget(m_page, 1);
    lay->addLayout(UiHelpers::okCancelRow(this));
    connect(portrait, &QPushButton::clicked, this, [this] { m_page->setLandscape(false); });
    connect(landscape, &QPushButton::clicked, this, [this] { m_page->setLandscape(true); });
    resize(700, 900);
}

std::array<QRectF, 8> LogoPositionsDialog::rects() const
{
    return m_page->rects();
}
