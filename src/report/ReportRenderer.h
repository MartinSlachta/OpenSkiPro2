#pragma once

#include "ReportBuilder.h"

#include <QColor>
#include <QFont>
#include <QImage>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QVector>
#include <array>

class Event;
class QPainter;
class QPagedPaintDevice;

// Lays out a ReportDocument on A4 pages (in mm) and paints the pages on any
// device: preview images, printer or PDF. Page layout follows SkiPro:
// logos, titles, optional jury/technical header on the first page, table
// with grey rows, sections of non classified competitors, notes, signatures
// and a two line footer on every page.
class ReportRenderer {
public:
    ReportRenderer(const Event& ev, const ReportDocument& doc);

    int pageCount() const { return int(m_pages.size()); }
    QSizeF pageSizeMm() const { return m_pageMm; }

    // Paints one page; pxPerMm is the device resolution in pixels per mm.
    void paintPage(QPainter& p, int page, double pxPerMm) const;
    QImage renderPage(int page, double dpi) const;
    bool print(QPagedPaintDevice* device) const;
    bool exportPdf(const QString& path, QString* error = nullptr) const;

    static QString footerProgramText();

    // Columns of the table against the table width, as printed on the first page (dialog of the
    // document format): the width of the table and the columns at their Width (both in mm), and
    // the Width of each column that fits its longest text (title, cells, second lines).
    struct TableFit {
        double tableMm = 0;
        double columnsMm = 0;
        double unitMm = 0;       // one unit of Width
        bool autoAdjust = false;
        bool measured = false;   // there were rows to measure, not only the column titles
        QVector<int> fitWidths;
        QVector<int> cutColumns; // columns whose longest text (or title) does not fit as printed
    };
    static TableFit tableFit(const Event& ev, const ReportDocument& doc);

private:
    enum FontId { FTitle1, FTitle2, FDocTitle, FFooter, FLabels, FLines, FLinesBold, FHeader, FHeaderBold, FSection, FBar, FontCount };
    using Fonts = std::array<QFont, FontCount>;
    static Fonts fontsFor(const Event& ev);
    static double textWidth(const Fonts& fonts, int font, const QString& s);
    static double charWidth(const Fonts& fonts);              // Width unit: a digit and a bit
    static QVector<double> columnWidths(const ReportFormat& fmt, bool autoAdjust, double frameW, double charW);
    static double frameSlot(const ReportFormat& fmt, double contentW);   // width for one frame (columns of the page)
    static double frameWidth(const ReportFormat& fmt, double contentW);  // the table in it (Table width)
    struct Op {
        enum Kind { Text, Line, Fill, Image, RichText } kind = Text;
        QRectF r;           // mm
        QString text;       // text, image path or rich text (HTML)
        int font = FLines;
        int flags = 0;      // Qt::Alignment flags
        QColor color = Qt::black;
        double width = 0.2; // line width mm
    };
    using Page = QVector<Op>;

    void layout();
    void layoutRefereeReport(); // the form of the FIS "Report by the Referee"
    double textWidth(int font, const QString& s) const;
    double lineHeight(int font) const;
    void addText(Page& page, const QRectF& r, const QString& text, int font, int flags, const QColor& c = Qt::black) const;
    void addLine(Page& page, double x1, double y1, double x2, double y2, double w = 0.2) const;
    double layoutTitles(Page& page, double y, bool first) const;
    double layoutHeaderBlock(Page& page, double y) const;
    double layoutFreeHeader(Page& page, double y) const; // the free header of the event (rich text)
    void layoutFooter(Page& page, int pageNo, int pageCount) const;
    void layoutLogos(Page& page, bool first) const;
    std::array<int, 4> margins(bool first) const;

    const Event& m_ev;
    ReportDocument m_doc;
    QSizeF m_pageMm;
    Fonts m_fonts;
    QVector<Page> m_pages;
};
