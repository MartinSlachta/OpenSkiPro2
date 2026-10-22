#pragma once

#include "report/ReportBuilder.h"

#include <QDialog>
#include <memory>

class Event;
class QLabel;
class QScrollArea;
class ReportRenderer;

// Print preview (SkiPro: page navigation, zoom, portrait / landscape, print, PDF).
class PreviewDialog : public QDialog {
    Q_OBJECT
public:
    PreviewDialog(Event* ev, const ReportDocument& doc, QWidget* parent = nullptr);
    ~PreviewDialog() override;

protected:
    void resizeEvent(QResizeEvent* e) override;
    void showEvent(QShowEvent* e) override;

private:
    void rebuild();
    void render();
    void print();
    void exportPdf();

    Event* m_ev;
    ReportDocument m_doc;
    std::unique_ptr<ReportRenderer> m_renderer;
    int m_page = 0;
    bool m_fitWidth = false;
    QLabel* m_pageLabel;
    QLabel* m_image;
    QScrollArea* m_scroll;
};
