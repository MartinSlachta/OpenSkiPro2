#include "PreviewDialog.h"
#include "app/AppSettings.h"
#include "core/Event.h"
#include "report/ReportRenderer.h"
#include "ui/UiHelpers.h"

#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPrintDialog>
#include <QPrinter>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QShortcut>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

PreviewDialog::PreviewDialog(Event* ev, const ReportDocument& doc, QWidget* parent)
    : QDialog(parent)
    , m_ev(ev)
    , m_doc(doc)
{
    setWindowTitle(tr("Preview - %1").arg(doc.docTitle));
    setWindowFlags(windowFlags() | Qt::WindowMaximizeButtonHint);
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    auto* bar = new QHBoxLayout;
    bar->setContentsMargins(6, 4, 6, 4);
    auto* prev = new QToolButton(this);
    prev->setIcon(UiHelpers::icon(QStringLiteral("left")));
    prev->setIconSize(QSize(24, 24));
    prev->setAutoRaise(true);
    auto* next = new QToolButton(this);
    next->setIcon(UiHelpers::icon(QStringLiteral("right")));
    next->setIconSize(QSize(24, 24));
    next->setAutoRaise(true);
    m_pageLabel = new QLabel(this);
    QFont f = m_pageLabel->font();
    f.setBold(true);
    f.setPointSize(12);
    m_pageLabel->setFont(f);
    auto* zoom = UiHelpers::button(QStringLiteral("zoom"), tr("Zoom"), this);
    auto* portrait = UiHelpers::button(QStringLiteral("portrait"), tr("Portrait"), this);
    auto* landscape = UiHelpers::button(QStringLiteral("landscape"), tr("Landscape"), this);
    auto* print = UiHelpers::button(QStringLiteral("print"), tr("Print"), this);
    auto* pdf = UiHelpers::button(QStringLiteral("pdf"), tr("PDF"), this);
    auto* close = UiHelpers::button(QStringLiteral("close"), tr("Close"), this);
    bar->addWidget(prev);
    bar->addWidget(m_pageLabel);
    bar->addWidget(next);
    bar->addSpacing(20);
    bar->addWidget(zoom);
    bar->addWidget(portrait);
    bar->addWidget(landscape);
    bar->addStretch();
    bar->addWidget(print);
    bar->addWidget(pdf);
    bar->addWidget(close);
    lay->addLayout(bar);

    m_scroll = new QScrollArea(this);
    m_scroll->setAlignment(Qt::AlignHCenter);
    m_scroll->setStyleSheet(QStringLiteral("QScrollArea { background: #bfcfdf; }"));
    m_image = new QLabel(m_scroll);
    m_image->setAlignment(Qt::AlignCenter);
    m_image->setStyleSheet(QStringLiteral("background: white; border: 1px solid #555;"));
    m_scroll->setWidget(m_image);
    m_scroll->setWidgetResizable(false);
    // always there: a scroll bar appearing after the first page would make the next pages
    // narrower (the page is fitted to the width of the viewport)
    m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    lay->addWidget(m_scroll, 1);

    connect(prev, &QToolButton::clicked, this, [this] {
        if (m_page > 0) {
            --m_page;
            render();
        }
    });
    connect(next, &QToolButton::clicked, this, [this] {
        if (m_page + 1 < m_renderer->pageCount()) {
            ++m_page;
            render();
        }
    });
    connect(new QShortcut(QKeySequence(Qt::Key_PageDown), this), &QShortcut::activated, next, &QToolButton::click);
    connect(new QShortcut(QKeySequence(Qt::Key_PageUp), this), &QShortcut::activated, prev, &QToolButton::click);
    connect(zoom, &QPushButton::clicked, this, [this] {
        m_fitWidth = !m_fitWidth;
        AppSettings::setValue(QStringLiteral("preview/fitWidth"), m_fitWidth); // kept for the next preview
        render();
    });
    connect(portrait, &QPushButton::clicked, this, [this] {
        m_doc.format.landscape = false;
        rebuild();
    });
    connect(landscape, &QPushButton::clicked, this, [this] {
        m_doc.format.landscape = true;
        rebuild();
    });
    connect(print, &QPushButton::clicked, this, &PreviewDialog::print);
    connect(pdf, &QPushButton::clicked, this, &PreviewDialog::exportPdf);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);

    // zoomed (page fitted to the width of the window) unless the operator chose the whole page
    m_fitWidth = AppSettings::value(QStringLiteral("preview/fitWidth"), true).toBool();
    rebuild();
    resize(1100, 950);
}

PreviewDialog::~PreviewDialog() = default;

void PreviewDialog::rebuild()
{
    m_renderer = std::make_unique<ReportRenderer>(*m_ev, m_doc);
    m_page = qBound(0, m_page, m_renderer->pageCount() - 1);
    render();
}

void PreviewDialog::render()
{
    const QSizeF mm = m_renderer->pageSizeMm();
    const double ratio = devicePixelRatioF();
    double widthPx = m_fitWidth ? qMax(400, m_scroll->viewport()->width() - 30)
                                : qMax(300.0, (m_scroll->viewport()->height() - 20) * mm.width() / mm.height());
    const double dpi = widthPx / (mm.width() / 25.4) * ratio;
    QImage img = m_renderer->renderPage(m_page, dpi);
    img.setDevicePixelRatio(ratio);
    m_image->setPixmap(QPixmap::fromImage(img));
    m_image->resize(img.size() / ratio);
    m_pageLabel->setText(QStringLiteral("%1 / %2").arg(m_page + 1).arg(m_renderer->pageCount()));
}

void PreviewDialog::resizeEvent(QResizeEvent* e)
{
    QDialog::resizeEvent(e);
    // after the layout gave the scroll area its new size
    if (m_renderer)
        QTimer::singleShot(0, this, &PreviewDialog::render);
}

void PreviewDialog::showEvent(QShowEvent* e)
{
    QDialog::showEvent(e);
    // the first rendering was done before the window had its size
    if (m_renderer)
        QTimer::singleShot(0, this, &PreviewDialog::render);
}

void PreviewDialog::print()
{
    QPrinter printer(QPrinter::HighResolution);
    printer.setFullPage(true);
    printer.setDocName(m_doc.docTitle);
    QPrintDialog dlg(&printer, this);
    if (dlg.exec() == QDialog::Accepted && !m_renderer->print(&printer))
        QMessageBox::warning(this, windowTitle(), tr("Printing failed."));
}

void PreviewDialog::exportPdf()
{
    QDir().mkpath(AppSettings::exportDir());
    QString base = m_doc.docTitle;
    base.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]+")), QStringLiteral("_"));
    const QString file = QFileDialog::getSaveFileName(this, tr("PDF"), QDir(AppSettings::exportDir()).absoluteFilePath(base + QStringLiteral(".pdf")),
                                                      tr("PDF (*.pdf)"));
    if (file.isEmpty())
        return;
    QString err;
    if (!m_renderer->exportPdf(file, &err))
        QMessageBox::warning(this, windowTitle(), err);
}
